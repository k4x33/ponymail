#include "SenderResolver.h"

namespace ponymail {

QString SenderResolver::displaySender(const Thread& t) const
{
    for (int i = t.messages.size() - 1; i >= 0; --i) {
        const auto& m = t.messages[i];
        if (!m.from.isValid()) continue;
        if (!isSelf(m.from)) return m.from.display();
    }
    return QStringLiteral("Sent by me");
}

QString SenderResolver::displaySenderEmail(const Thread& t) const
{
    for (int i = t.messages.size() - 1; i >= 0; --i) {
        const auto& m = t.messages[i];
        if (m.from.isValid() && !isSelf(m.from)) return m.from.email;
    }
    return {};
}

static void addUnique(QList<EmailAddress>& list, const EmailAddress& a)
{
    for (const auto& e : list) if (e.email == a.email) return;
    list << a;
}

void SenderResolver::replyRecipients(const Message& original, bool replyAll,
                                     QList<EmailAddress>& to, QList<EmailAddress>& cc) const
{
    to.clear(); cc.clear();
    const bool selfSent = isSelf(original.from);
    const QList<EmailAddress> replyTarget = original.replyTo.isEmpty() ? QList<EmailAddress>{original.from} : original.replyTo;

    if (selfSent) {
        // Reply to something I sent: address the people I sent it to.
        for (const auto& a : original.to) if (!isSelf(a)) addUnique(to, a);
        if (to.isEmpty()) for (const auto& a : original.cc) if (!isSelf(a)) addUnique(to, a);   // Cc -> To
        else if (replyAll) for (const auto& a : original.cc) if (!isSelf(a)) addUnique(cc, a);
        if (to.isEmpty() && !cc.isEmpty()) { to = cc; cc.clear(); }
        return;
    }

    for (const auto& a : replyTarget) if (!isSelf(a)) addUnique(to, a);
    if (replyAll) {
        for (const auto& a : original.to) if (!isSelf(a)) addUnique(cc, a);
        for (const auto& a : original.cc) if (!isSelf(a)) addUnique(cc, a);
        // Remove anyone already in To from Cc.
        for (const auto& t : to) cc.erase(std::remove_if(cc.begin(), cc.end(), [&](const EmailAddress& c) { return c.email == t.email; }), cc.end());
    }
}

} // namespace ponymail
