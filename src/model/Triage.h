#pragma once
#include "Models.h"
#include <QHash>
#include <QColor>

namespace ponymail {

// classify_thread state machine (spec §4.3).
//
//   A  (amber)   open action(s) on the user; count shown when > 1
//   FU (purple)  someone replied to a task/question the user asked of them, nothing open on user
//   W  (slate)   everything outstanding is delegated / awaiting someone else
//   I  (blue)    informational, nothing for the user
//   ✓  (green)   every action item marked Completed or Ignore
//   •  (gray)    evaluation in progress
//
//   CALENDAR INVITE EXCEPTION: any thread with an .ics / text/calendar part is
//   always "I", never "A".
class Triage {
public:
    static TriageResult classifyThread(const ThreadSummary* summary,
                                       const QHash<QString, ActionStatusRecord>& statuses,
                                       bool hasCalendarInvite);

    static ActionStatus effectiveStatus(const ActionItem& item, const QHash<QString, ActionStatusRecord>& statuses);
    static bool isDoneStatus(ActionStatus s) { return s == ActionStatus::Completed || s == ActionStatus::Ignore; }
    static bool isOpenOnUser(ActionStatus s)
    { return s == ActionStatus::NotStarted || s == ActionStatus::OnHold || s == ActionStatus::InProgress; }

    static QString tagLetter(TriageTag t);
    static QColor tagColor(TriageTag t);
    static QString tagName(TriageTag t);
};

} // namespace ponymail
