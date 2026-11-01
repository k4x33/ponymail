#pragma once
#include "Models.h"

namespace ponymail {

// Row sender rule (spec §3.2): never show the user as the sender. Show the most
// recent sender who is not the user; if every message is from the user, show
// "Sent by me".
class SenderResolver {
public:
    SenderResolver(const QString& userEmail, const QString& userName)
        : m_email(userEmail.toLower()), m_name(userName) {}

    bool isSelf(const EmailAddress& a) const { return util::looksLikeSelf(a, m_email, m_name); }
    QString displaySender(const Thread& t) const;
    QString displaySenderEmail(const Thread& t) const;   // empty when "Sent by me"

    // Reply-recipient rules (spec §5.1): replying to a self-sent email never puts
    // the user in To; the original Cc recipients move into To.
    void replyRecipients(const Message& original, bool replyAll,
                         QList<EmailAddress>& to, QList<EmailAddress>& cc) const;

    QString userEmail() const { return m_email; }
    QString userName() const { return m_name; }

private:
    QString m_email, m_name;
};

} // namespace ponymail
