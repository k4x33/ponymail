#pragma once
#include "core/Util.h"
#include <QByteArray>
#include <QList>
#include <QString>

namespace ponymail {

struct OutgoingPart {
    QString filename;
    QString mimeType;
    QString contentId;   // set for inline (cid:) images
    QByteArray data;
};

struct OutgoingMessage {
    EmailAddress from;
    QList<EmailAddress> to, cc, bcc;
    QString subject;
    QString html;              // body as HTML (may contain <img src="cid:...">)
    QString inReplyTo;         // RFC Message-ID of the message being replied to
    QString references;
    QList<OutgoingPart> inlineImages;
    QList<OutgoingPart> attachments;
};

// Builds an RFC 5322 message: multipart/mixed > multipart/related > multipart/alternative.
class MimeBuilder {
public:
    static QByteArray build(const OutgoingMessage& msg);
    static QByteArray buildBase64Url(const OutgoingMessage& msg) { return util::base64Url(build(msg)); }
    static QString htmlToPlain(const QString& html);
    static QString encodeHeaderWord(const QString& text);   // RFC 2047 when non-ASCII
};

} // namespace ponymail
