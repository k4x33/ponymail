#pragma once
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QStringList>

namespace ponymail {

struct EmailAddress {
    QString name;
    QString email;   // lower-cased
    QString display() const { return name.isEmpty() ? email : name; }
    QString rfc822() const;
    bool isValid() const { return email.contains('@'); }
};

namespace util {

// Parses "Name <a@b.c>, other@x.y" into a list.
QList<EmailAddress> parseAddressList(const QString& header);
EmailAddress parseAddress(const QString& header);

QString formatTimestamp(const QDateTime& dt);          // DD MMM YYYY HH:mm
QString newUuid();
QString sha1Hex(const QString& s);
QByteArray base64Url(const QByteArray& data);          // RFC 4648 §5, no padding
QByteArray fromBase64Url(const QByteArray& data);
QString htmlEscape(const QString& s);
QString stripHtml(const QString& html);
QString normalizeActionText(const QString& text);       // for action_hash stability
bool looksLikeSelf(const EmailAddress& a, const QString& userEmail, const QString& userName);
qint64 nowMs();

} // namespace util
} // namespace ponymail
