#include "MimeBuilder.h"
#include <QDateTime>
#include <QUuid>
#include <QTextDocument>

namespace ponymail {

namespace {

QByteArray boundary(const char* tag)
{
    return QByteArray("=_PonyMail_") + tag + "_" + QUuid::createUuid().toString(QUuid::Id128).toLatin1().left(16);
}

QByteArray wrap76(const QByteArray& b64)
{
    QByteArray out;
    for (int i = 0; i < b64.size(); i += 76) out += b64.mid(i, 76) + "\r\n";
    return out;
}

QByteArray addressList(const QList<EmailAddress>& list)
{
    QStringList parts;
    for (const auto& a : list) {
        if (a.name.isEmpty()) parts << a.email;
        else parts << MimeBuilder::encodeHeaderWord(a.name) + QStringLiteral(" <") + a.email + QLatin1Char('>');
    }
    return parts.join(QStringLiteral(", ")).toUtf8();
}

bool isAscii(const QString& s)
{
    for (const QChar c : s) if (c.unicode() > 126 || c.unicode() < 32) return false;
    return true;
}

} // namespace

QString MimeBuilder::encodeHeaderWord(const QString& text)
{
    if (isAscii(text)) {
        if (text.contains(',') || text.contains('<') || text.contains('"'))
            return QStringLiteral("\"%1\"").arg(QString(text).replace('"', QStringLiteral("\\\"")));
        return text;
    }
    return QStringLiteral("=?UTF-8?B?%1?=").arg(QString::fromLatin1(text.toUtf8().toBase64()));
}

QString MimeBuilder::htmlToPlain(const QString& html)
{
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

QByteArray MimeBuilder::build(const OutgoingMessage& msg)
{
    const QByteArray CRLF = "\r\n";
    QByteArray out;

    out += "From: " + addressList({msg.from}) + CRLF;
    if (!msg.to.isEmpty()) out += "To: " + addressList(msg.to) + CRLF;
    if (!msg.cc.isEmpty()) out += "Cc: " + addressList(msg.cc) + CRLF;
    if (!msg.bcc.isEmpty()) out += "Bcc: " + addressList(msg.bcc) + CRLF;
    out += "Subject: " + encodeHeaderWord(msg.subject).toUtf8() + CRLF;
    out += "Date: " + QDateTime::currentDateTime().toString(Qt::RFC2822Date).toUtf8() + CRLF;
    out += "Message-ID: <" + QUuid::createUuid().toString(QUuid::Id128).toLatin1() + "@ponymail.local>" + CRLF;
    if (!msg.inReplyTo.isEmpty()) out += "In-Reply-To: " + msg.inReplyTo.toUtf8() + CRLF;
    if (!msg.references.isEmpty()) out += "References: " + msg.references.toUtf8() + CRLF;
    out += "MIME-Version: 1.0" + CRLF;
    out += "X-Mailer: PonyMail" + CRLF;

    // --- alternative (plain + html) ---
    const QByteArray altB = boundary("alt");
    QByteArray alt;
    alt += "--" + altB + CRLF;
    alt += "Content-Type: text/plain; charset=UTF-8" + CRLF;
    alt += "Content-Transfer-Encoding: base64" + CRLF + CRLF;
    alt += wrap76(htmlToPlain(msg.html).toUtf8().toBase64());
    alt += "--" + altB + CRLF;
    alt += "Content-Type: text/html; charset=UTF-8" + CRLF;
    alt += "Content-Transfer-Encoding: base64" + CRLF + CRLF;
    alt += wrap76(msg.html.toUtf8().toBase64());
    alt += "--" + altB + "--" + CRLF;
    const QByteArray altHeader = "Content-Type: multipart/alternative; boundary=\"" + altB + "\"" + CRLF;

    // --- related (alternative + inline cid images) ---
    QByteArray related, relatedHeader;
    if (!msg.inlineImages.isEmpty()) {
        const QByteArray relB = boundary("rel");
        relatedHeader = "Content-Type: multipart/related; boundary=\"" + relB + "\"; type=\"multipart/alternative\"" + CRLF;
        related += "--" + relB + CRLF + altHeader + CRLF + alt;
        for (const auto& img : msg.inlineImages) {
            related += "--" + relB + CRLF;
            related += "Content-Type: " + img.mimeType.toUtf8() + "; name=\"" + img.filename.toUtf8() + "\"" + CRLF;
            related += "Content-Transfer-Encoding: base64" + CRLF;
            related += "Content-ID: <" + img.contentId.toUtf8() + ">" + CRLF;
            related += "Content-Disposition: inline; filename=\"" + img.filename.toUtf8() + "\"" + CRLF + CRLF;
            related += wrap76(img.data.toBase64());
        }
        related += "--" + relB + "--" + CRLF;
    } else {
        relatedHeader = altHeader;
        related = alt;
    }

    // --- mixed (related + attachments) ---
    if (!msg.attachments.isEmpty()) {
        const QByteArray mixB = boundary("mix");
        out += "Content-Type: multipart/mixed; boundary=\"" + mixB + "\"" + CRLF + CRLF;
        out += "--" + mixB + CRLF + relatedHeader + CRLF + related;
        for (const auto& a : msg.attachments) {
            out += "--" + mixB + CRLF;
            out += "Content-Type: " + a.mimeType.toUtf8() + "; name=\"" + a.filename.toUtf8() + "\"" + CRLF;
            out += "Content-Transfer-Encoding: base64" + CRLF;
            out += "Content-Disposition: attachment; filename=\"" + a.filename.toUtf8() + "\"" + CRLF + CRLF;
            out += wrap76(a.data.toBase64());
        }
        out += "--" + mixB + "--" + CRLF;
    } else {
        out += relatedHeader + CRLF + related;
    }
    return out;
}

} // namespace ponymail
