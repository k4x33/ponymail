#include "Util.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUuid>
#include <QTextDocument>

namespace ponymail {

QString EmailAddress::rfc822() const
{
    if (name.isEmpty()) return email;
    QString n = name;
    n.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\" <%2>").arg(n, email);
}

namespace util {

static QString cleanName(QString n)
{
    n = n.trimmed();
    if (n.size() >= 2 && n.startsWith('"') && n.endsWith('"')) n = n.mid(1, n.size() - 2);
    n.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    return n.trimmed();
}

EmailAddress parseAddress(const QString& header)
{
    EmailAddress a;
    const QString h = header.trimmed();
    const int lt = h.lastIndexOf('<');
    const int gt = h.lastIndexOf('>');
    if (lt >= 0 && gt > lt) {
        a.email = h.mid(lt + 1, gt - lt - 1).trimmed().toLower();
        a.name = cleanName(h.left(lt));
    } else {
        a.email = h.toLower();
        a.email.remove(QRegularExpression(QStringLiteral("[<>\"]")));
        a.email = a.email.trimmed();
    }
    if (a.name.compare(a.email, Qt::CaseInsensitive) == 0) a.name.clear();
    return a;
}

QList<EmailAddress> parseAddressList(const QString& header)
{
    QList<EmailAddress> out;
    // Split on commas that are not inside quotes or angle brackets.
    QString cur;
    bool inQuote = false;
    int depth = 0;
    for (const QChar c : header) {
        if (c == '"') inQuote = !inQuote;
        else if (!inQuote && c == '<') depth++;
        else if (!inQuote && c == '>') depth = qMax(0, depth - 1);
        if (c == ',' && !inQuote && depth == 0) {
            if (!cur.trimmed().isEmpty()) out << parseAddress(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.trimmed().isEmpty()) out << parseAddress(cur);
    QList<EmailAddress> valid;
    for (const auto& a : out) if (a.isValid()) valid << a;
    return valid;
}

QString formatTimestamp(const QDateTime& dtIn)
{
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    const QDateTime dt = dtIn.toLocalTime();
    const QDate d = dt.date();
    return QStringLiteral("%1 %2 %3 %4")
        .arg(d.day(), 2, 10, QLatin1Char('0'))
        .arg(QLatin1String(months[d.month() - 1]))
        .arg(d.year())
        .arg(dt.time().toString(QStringLiteral("HH:mm")));
}

QString newUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString sha1Hex(const QString& s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QByteArray base64Url(const QByteArray& data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray fromBase64Url(const QByteArray& data)
{
    return QByteArray::fromBase64(data, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString htmlEscape(const QString& s) { return s.toHtmlEscaped(); }

QString stripHtml(const QString& html)
{
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

QString normalizeActionText(const QString& text)
{
    QString t = text.toLower().trimmed();
    t.replace(QRegularExpression(QStringLiteral("[^a-z0-9 ]")), QString());
    t.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return t;
}

bool looksLikeSelf(const EmailAddress& a, const QString& userEmail, const QString& userName)
{
    if (!userEmail.isEmpty() && a.email.compare(userEmail, Qt::CaseInsensitive) == 0) return true;
    if (!userName.isEmpty() && !a.name.isEmpty() && a.name.compare(userName, Qt::CaseInsensitive) == 0) return true;
    return false;
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace util
} // namespace ponymail
