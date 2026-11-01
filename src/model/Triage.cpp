#include "Triage.h"

namespace ponymail {

ActionStatus Triage::effectiveStatus(const ActionItem& item, const QHash<QString, ActionStatusRecord>& statuses)
{
    const auto it = statuses.constFind(item.hash());
    return it == statuses.constEnd() ? ActionStatus::NotStarted : it->status;
}

TriageResult Triage::classifyThread(const ThreadSummary* summary,
                                    const QHash<QString, ActionStatusRecord>& statuses,
                                    bool hasCalendarInvite)
{
    TriageResult r;

    // Calendar invitations are informational by definition and may never be "A".
    if (hasCalendarInvite) { r.tag = TriageTag::Info; return r; }

    if (!summary || !summary->valid) { r.tag = TriageTag::Evaluating; return r; }

    int openUser = 0, awaitingUser = 0, openOther = 0, total = 0, done = 0;
    for (const auto& a : summary->actions) {
        ++total;
        const ActionStatus s = effectiveStatus(a, statuses);
        if (isDoneStatus(s)) { ++done; continue; }
        if (a.forUser) {
            if (isOpenOnUser(s)) ++openUser;
            else if (s == ActionStatus::Awaiting) ++awaitingUser;
        } else {
            ++openOther;
        }
    }

    if (openUser > 0) { r.tag = TriageTag::Action; r.openUserActions = openUser; return r; }
    if (total > 0 && done == total) { r.tag = TriageTag::Done; return r; }
    if (summary->isFollowUp) { r.tag = TriageTag::FollowUp; return r; }
    if (awaitingUser > 0 || openOther > 0) { r.tag = TriageTag::Waiting; return r; }
    r.tag = TriageTag::Info;
    return r;
}

QString Triage::tagLetter(TriageTag t)
{
    switch (t) {
    case TriageTag::Action: return QStringLiteral("A");
    case TriageTag::FollowUp: return QStringLiteral("FU");
    case TriageTag::Waiting: return QStringLiteral("W");
    case TriageTag::Info: return QStringLiteral("I");
    case TriageTag::Done: return QStringLiteral("✓");
    case TriageTag::Evaluating: return QString();
    }
    return {};
}

QColor Triage::tagColor(TriageTag t)
{
    switch (t) {
    case TriageTag::Action: return QColor(0xF5, 0x9E, 0x0B);     // amber
    case TriageTag::FollowUp: return QColor(0x7C, 0x3A, 0xED);   // purple
    case TriageTag::Waiting: return QColor(0x64, 0x74, 0x8B);    // slate gray
    case TriageTag::Info: return QColor(0x0F, 0x6C, 0xBD);       // blue
    case TriageTag::Done: return QColor(0x16, 0xA3, 0x4A);       // green
    case TriageTag::Evaluating: return QColor(0xA3, 0xA3, 0xA3); // gray dot
    }
    return Qt::gray;
}

QString Triage::tagName(TriageTag t)
{
    switch (t) {
    case TriageTag::Action: return QStringLiteral("Action required");
    case TriageTag::FollowUp: return QStringLiteral("Follow-up");
    case TriageTag::Waiting: return QStringLiteral("Waiting on someone else");
    case TriageTag::Info: return QStringLiteral("Informational");
    case TriageTag::Done: return QStringLiteral("Done");
    case TriageTag::Evaluating: return QStringLiteral("Evaluating…");
    }
    return {};
}

} // namespace ponymail
