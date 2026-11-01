#pragma once
#include "core/Util.h"
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>

namespace ponymail {

// ---------------------------------------------------------------------------
// Mail entities (decoded from Gmail REST payloads)
// ---------------------------------------------------------------------------
struct Attachment {
    QString messageId;
    QString attachmentId;   // Gmail attachment id (empty when body is inline data)
    QString filename;
    QString mimeType;
    QString contentId;      // for cid: inline images
    qint64 size = 0;
    bool isInline = false;
    bool isCalendar() const
    {
        return mimeType.startsWith(QStringLiteral("text/calendar"), Qt::CaseInsensitive)
            || mimeType.startsWith(QStringLiteral("application/ics"), Qt::CaseInsensitive)
            || filename.endsWith(QStringLiteral(".ics"), Qt::CaseInsensitive);
    }
};

struct Message {
    QString id;
    QString threadId;
    QStringList labelIds;
    QDateTime date;
    EmailAddress from;
    QList<EmailAddress> to, cc, bcc, replyTo;
    QString subject;
    QString messageIdHeader;   // RFC Message-ID
    QString references;
    QString inReplyTo;
    QString snippet;
    QString textPlain;
    QString textHtml;
    QList<Attachment> attachments;
    bool hasCalendarPart = false;   // text/calendar MIME anywhere in the tree
    qint64 historyId = 0;
    bool isDraft = false;
    QString draftId;

    bool isUnread() const { return labelIds.contains(QStringLiteral("UNREAD")); }
    bool isStarred() const { return labelIds.contains(QStringLiteral("STARRED")); }
    bool hasAttachments() const
    {
        for (const auto& a : attachments) if (!a.isInline && !a.filename.isEmpty()) return true;
        return false;
    }
    int attachmentCount() const
    {
        int n = 0;
        for (const auto& a : attachments) if (!a.isInline && !a.filename.isEmpty()) ++n;
        return n;
    }
    bool hasCalendarInvite() const
    {
        if (hasCalendarPart) return true;
        for (const auto& a : attachments) if (a.isCalendar()) return true;
        return false;
    }
};

struct Thread {
    QString id;
    qint64 historyId = 0;
    QList<Message> messages;          // chronological (oldest first)
    QString snippet;

    const Message* latest() const { return messages.isEmpty() ? nullptr : &messages.last(); }
    QString latestMessageId() const { const auto* m = latest(); return m ? m->id : QString(); }
    QString subject() const
    {
        for (const auto& m : messages) if (!m.subject.isEmpty()) return m.subject;
        return QString();
    }
    QDateTime latestDate() const { const auto* m = latest(); return m ? m->date : QDateTime(); }
    bool isUnread() const { for (const auto& m : messages) if (m.isUnread()) return true; return false; }
    bool isStarred() const { for (const auto& m : messages) if (m.isStarred()) return true; return false; }
    bool hasAttachments() const { for (const auto& m : messages) if (m.hasAttachments()) return true; return false; }
    bool hasCalendarInvite() const { for (const auto& m : messages) if (m.hasCalendarInvite()) return true; return false; }
    bool hasLabel(const QString& l) const { for (const auto& m : messages) if (m.labelIds.contains(l)) return true; return false; }
    QStringList allLabels() const
    {
        QStringList out;
        for (const auto& m : messages) for (const auto& l : m.labelIds) if (!out.contains(l)) out << l;
        return out;
    }
};

struct Label {
    QString id;
    QString name;
    QString type;   // system | user
    int unread = 0;
    int total = 0;
};

// ---------------------------------------------------------------------------
// AI summary + triage
// ---------------------------------------------------------------------------
enum class ActionStatus { NotStarted, OnHold, InProgress, Awaiting, Completed, Ignore };

inline QString actionStatusKey(ActionStatus s)
{
    switch (s) {
    case ActionStatus::NotStarted: return QStringLiteral("not_started");
    case ActionStatus::OnHold: return QStringLiteral("on_hold");
    case ActionStatus::InProgress: return QStringLiteral("in_progress");
    case ActionStatus::Awaiting: return QStringLiteral("awaiting");
    case ActionStatus::Completed: return QStringLiteral("completed");
    case ActionStatus::Ignore: return QStringLiteral("ignore");
    }
    return QStringLiteral("not_started");
}

inline ActionStatus actionStatusFromKey(const QString& k)
{
    if (k == QLatin1String("on_hold")) return ActionStatus::OnHold;
    if (k == QLatin1String("in_progress")) return ActionStatus::InProgress;
    if (k == QLatin1String("awaiting")) return ActionStatus::Awaiting;
    if (k == QLatin1String("completed")) return ActionStatus::Completed;
    if (k == QLatin1String("ignore")) return ActionStatus::Ignore;
    return ActionStatus::NotStarted;
}

inline QString actionStatusLabel(ActionStatus s)
{
    switch (s) {
    case ActionStatus::NotStarted: return QStringLiteral("Not Started");
    case ActionStatus::OnHold: return QStringLiteral("On Hold");
    case ActionStatus::InProgress: return QStringLiteral("In Progress");
    case ActionStatus::Awaiting: return QStringLiteral("Awaiting Someone / Delegated");
    case ActionStatus::Completed: return QStringLiteral("Completed");
    case ActionStatus::Ignore: return QStringLiteral("Ignore");
    }
    return {};
}

struct ActionItem {
    QString text;
    bool forUser = false;      // directed at Mike Brophy
    QString owner;             // free text owner for "others"
    QString hash() const { return util::sha1Hex(util::normalizeActionText(text)); }
};

struct ThreadSummary {
    QString threadId;
    QString latestMessageId;
    QStringList bullets;              // max 5
    QList<ActionItem> actions;
    bool isFollowUp = false;          // someone replied to something the user asked of them
    bool valid = false;

    QString cacheKey() const { return threadId + QLatin1Char(':') + latestMessageId; }
    QJsonObject toJson() const;
    static ThreadSummary fromJson(const QString& threadId, const QString& latestMsgId, const QJsonObject& o);
};

inline QJsonObject ThreadSummary::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("bullets")] = QJsonArray::fromStringList(bullets);
    QJsonArray acts;
    for (const auto& a : actions) {
        QJsonObject ao;
        ao[QStringLiteral("text")] = a.text;
        ao[QStringLiteral("for_user")] = a.forUser;
        ao[QStringLiteral("owner")] = a.owner;
        acts.append(ao);
    }
    o[QStringLiteral("actions")] = acts;
    o[QStringLiteral("follow_up")] = isFollowUp;
    return o;
}

inline ThreadSummary ThreadSummary::fromJson(const QString& threadId, const QString& latestMsgId, const QJsonObject& o)
{
    ThreadSummary s;
    s.threadId = threadId;
    s.latestMessageId = latestMsgId;
    for (const auto v : o.value(QStringLiteral("bullets")).toArray()) {
        if (s.bullets.size() >= 5) break;
        const QString b = v.toString().trimmed();
        if (!b.isEmpty()) s.bullets << b;
    }
    for (const auto v : o.value(QStringLiteral("actions")).toArray()) {
        const auto ao = v.toObject();
        ActionItem a;
        a.text = ao.value(QStringLiteral("text")).toString().trimmed();
        a.forUser = ao.value(QStringLiteral("for_user")).toBool();
        a.owner = ao.value(QStringLiteral("owner")).toString();
        if (!a.text.isEmpty()) s.actions << a;
    }
    s.isFollowUp = o.value(QStringLiteral("follow_up")).toBool();
    s.valid = true;
    return s;
}

// Triage tag shown on the left edge of a message-list row.
enum class TriageTag { Evaluating, Action, FollowUp, Waiting, Info, Done };

struct TriageResult {
    TriageTag tag = TriageTag::Evaluating;
    int openUserActions = 0;   // count shown under "A" when > 1
};

// ---------------------------------------------------------------------------
// Storage records
// ---------------------------------------------------------------------------
struct ActionStatusRecord {
    QString id;
    QString threadId;
    QString actionHash;
    QString actionText;
    ActionStatus status = ActionStatus::NotStarted;
    qint64 updatedAt = 0;
};

struct BlockedSender {
    QString id;
    QString pattern;   // "user@domain.com" or "@domain.com"
    QString type;      // email | domain
    qint64 createdAt = 0;
};

struct SnoozedThread {
    QString threadId;
    qint64 snoozedUntil = 0;
    QStringList originalLabels;
};

struct Contact {
    QString email;
    QString name;
    int frequency = 1;
    qint64 lastSeen = 0;
};

} // namespace ponymail
