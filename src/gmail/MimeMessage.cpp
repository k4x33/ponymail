#include "MimeMessage.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QDateTime>
#include <QTimeZone>

namespace ponymail {

namespace {

QString header(const QJsonArray& headers, const QString& name)
{
    for (const auto v : headers) {
        const auto h = v.toObject();
        if (h.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0)
            return h.value(QStringLiteral("value")).toString();
    }
    return {};
}

QDateTime parseDate(const QString& internalDateMs, const QString& dateHeader)
{
    bool ok = false;
    const qint64 ms = internalDateMs.toLongLong(&ok);
    if (ok && ms > 0) return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc());
    QDateTime d = QDateTime::fromString(dateHeader, Qt::RFC2822Date);
    return d.isValid() ? d : QDateTime::currentDateTimeUtc();
}

QString decodeBody(const QJsonObject& body)
{
    const QString data = body.value(QStringLiteral("data")).toString();
    if (data.isEmpty()) return {};
    return QString::fromUtf8(util::fromBase64Url(data.toLatin1()));
}

} // namespace

void MimeMessage::walkParts(const QJsonObject& part, Message& m, int depth)
{
    if (depth > 20) return;
    const QString mime = part.value(QStringLiteral("mimeType")).toString().toLower();
    const QString filename = part.value(QStringLiteral("filename")).toString();
    const QJsonArray headers = part.value(QStringLiteral("headers")).toArray();
    const QJsonObject body = part.value(QStringLiteral("body")).toObject();
    const QJsonArray parts = part.value(QStringLiteral("parts")).toArray();

    if (mime.startsWith(QLatin1String("text/calendar")) || mime == QLatin1String("application/ics"))
        m.hasCalendarPart = true;

    const QString disposition = header(headers, QStringLiteral("Content-Disposition")).toLower();
    QString contentId = header(headers, QStringLiteral("Content-ID"));
    contentId.remove('<'); contentId.remove('>');

    const bool isAttachment = !filename.isEmpty() || disposition.startsWith(QLatin1String("attachment"))
        || !body.value(QStringLiteral("attachmentId")).toString().isEmpty();

    if (isAttachment) {
        Attachment a;
        a.messageId = m.id;
        a.attachmentId = body.value(QStringLiteral("attachmentId")).toString();
        a.filename = filename;
        a.mimeType = mime;
        a.contentId = contentId;
        a.size = body.value(QStringLiteral("size")).toInt();
        a.isInline = !contentId.isEmpty() && (disposition.startsWith(QLatin1String("inline")) || mime.startsWith(QLatin1String("image/")));
        if (a.filename.isEmpty() && a.isInline) a.filename = QStringLiteral("inline-%1").arg(m.attachments.size() + 1);
        m.attachments << a;
    } else if (mime == QLatin1String("text/plain") && m.textPlain.isEmpty()) {
        m.textPlain = decodeBody(body);
    } else if (mime == QLatin1String("text/html") && m.textHtml.isEmpty()) {
        m.textHtml = decodeBody(body);
    } else if (mime.startsWith(QLatin1String("text/calendar")) && m.textPlain.isEmpty() && parts.isEmpty()) {
        // Calendar-only bodies: keep the ICS text so the AI and preview have something to show.
        m.textPlain = decodeBody(body);
    }

    for (const auto p : parts) walkParts(p.toObject(), m, depth + 1);
}

Message MimeMessage::parseMessage(const QJsonObject& json)
{
    Message m;
    m.id = json.value(QStringLiteral("id")).toString();
    m.threadId = json.value(QStringLiteral("threadId")).toString();
    for (const auto v : json.value(QStringLiteral("labelIds")).toArray()) m.labelIds << v.toString();
    m.snippet = json.value(QStringLiteral("snippet")).toString();
    m.historyId = json.value(QStringLiteral("historyId")).toString().toLongLong();
    m.isDraft = m.labelIds.contains(QStringLiteral("DRAFT"));

    const QJsonObject payload = json.value(QStringLiteral("payload")).toObject();
    const QJsonArray headers = payload.value(QStringLiteral("headers")).toArray();
    m.date = parseDate(json.value(QStringLiteral("internalDate")).toString(), header(headers, QStringLiteral("Date")));
    m.from = util::parseAddress(header(headers, QStringLiteral("From")));
    m.to = util::parseAddressList(header(headers, QStringLiteral("To")));
    m.cc = util::parseAddressList(header(headers, QStringLiteral("Cc")));
    m.bcc = util::parseAddressList(header(headers, QStringLiteral("Bcc")));
    m.replyTo = util::parseAddressList(header(headers, QStringLiteral("Reply-To")));
    m.subject = header(headers, QStringLiteral("Subject"));
    m.messageIdHeader = header(headers, QStringLiteral("Message-ID"));
    m.references = header(headers, QStringLiteral("References"));
    m.inReplyTo = header(headers, QStringLiteral("In-Reply-To"));

    walkParts(payload, m, 0);
    return m;
}

Thread MimeMessage::parseThread(const QJsonObject& json)
{
    Thread t;
    t.id = json.value(QStringLiteral("id")).toString();
    t.historyId = json.value(QStringLiteral("historyId")).toString().toLongLong();
    t.snippet = json.value(QStringLiteral("snippet")).toString();
    for (const auto v : json.value(QStringLiteral("messages")).toArray()) t.messages << parseMessage(v.toObject());
    std::sort(t.messages.begin(), t.messages.end(), [](const Message& a, const Message& b) { return a.date < b.date; });
    return t;
}

QString MimeMessage::displayHtml(const Message& m)
{
    if (!m.textHtml.isEmpty()) return m.textHtml;
    QString t = util::htmlEscape(m.textPlain);
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace('\n', QStringLiteral("<br>"));
    return QStringLiteral("<div style=\"white-space:pre-wrap;font-family:'Segoe UI','Helvetica Neue',sans-serif;font-size:14px\">%1</div>").arg(t);
}

QString MimeMessage::stripQuotedText(const QString& plain)
{
    static const QRegularExpression onWrote(QStringLiteral("(^|\\n)\\s*On .{5,120}?wrote:\\s*\\n"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression fromBlock(QStringLiteral("\\n\\s*(-+\\s*Original Message\\s*-+|From:\\s.+\\n\\s*Sent:|From:\\s.+\\n\\s*Date:)"));
    QString out = plain;
    auto m1 = onWrote.match(out);
    if (m1.hasMatch()) out = out.left(m1.capturedStart());
    auto m2 = fromBlock.match(out);
    if (m2.hasMatch()) out = out.left(m2.capturedStart());
    // Drop trailing '>' quoted lines
    QStringList lines = out.split('\n');
    while (!lines.isEmpty() && (lines.last().trimmed().startsWith('>') || lines.last().trimmed().isEmpty())) lines.removeLast();
    return lines.join('\n').trimmed();
}

QString MimeMessage::stripSignature(const QString& plain)
{
    const int idx = plain.indexOf(QRegularExpression(QStringLiteral("(^|\\n)--\\s*\\n")));
    return idx >= 0 ? plain.left(idx).trimmed() : plain;
}

QString MimeMessage::plainTextForAi(const Message& m, int maxChars)
{
    QString t = m.textPlain.isEmpty() ? util::stripHtml(m.textHtml) : m.textPlain;
    t = stripSignature(stripQuotedText(t));
    if (t.size() > maxChars) t = t.left(maxChars) + QStringLiteral("…");
    return t;
}

} // namespace ponymail
