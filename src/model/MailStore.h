#pragma once
#include "Models.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>

namespace ponymail {

// Describes a navigable folder in the left rail.
struct Folder {
    QString key;              // inbox | snoozed | sent | drafts | spam | trash | archive | blocked | label:<id>
    QString title;
    QStringList labelIds;     // Gmail labelIds filter (may be empty)
    QString query;            // Gmail q filter (may be empty)
    bool includeSpamTrash = false;
    bool isSystem = true;

    bool operator==(const Folder& o) const { return key == o.key; }
};

// In-memory cache of threads keyed by id, plus which folders each thread was
// listed in. The SyncEngine writes; views read.
class MailStore : public QObject {
    Q_OBJECT
public:
    explicit MailStore(QObject* parent = nullptr);

    // Threads
    const Thread* thread(const QString& id) const;
    Thread* mutableThread(const QString& id);
    void upsertThread(const Thread& t);
    void removeThread(const QString& id);
    QList<const Thread*> threadsInFolder(const QString& folderKey) const;
    void setFolderThreadIds(const QString& folderKey, const QStringList& ids, bool append);
    void addThreadToFolder(const QString& folderKey, const QString& threadId);
    void removeThreadFromFolder(const QString& folderKey, const QString& threadId);
    QStringList folderThreadIds(const QString& folderKey) const { return m_folderIds.value(folderKey); }
    QString folderPageToken(const QString& folderKey) const { return m_pageTokens.value(folderKey); }
    void setFolderPageToken(const QString& folderKey, const QString& tok) { m_pageTokens[folderKey] = tok; }

    // Labels
    QList<Label> labels() const { return m_labels; }
    void setLabels(const QList<Label>& l);
    QString labelIdByName(const QString& name) const;
    QString labelName(const QString& id) const;

    // Local triage state
    void setSummary(const QString& threadId, const ThreadSummary& s);
    const ThreadSummary* summary(const QString& threadId) const;
    void setStatuses(const QString& threadId, const QHash<QString, ActionStatusRecord>& st);
    QHash<QString, ActionStatusRecord> statuses(const QString& threadId) const { return m_statuses.value(threadId); }
    void markEvaluating(const QString& threadId, bool on);
    bool isEvaluating(const QString& threadId) const { return m_evaluating.contains(threadId); }
    TriageResult triage(const QString& threadId) const;

    // Blocked tag (local) — mirrors Gmail "Blocked" label but also kept locally
    void setBlocked(const QString& threadId, bool b) { if (b) m_blocked.insert(threadId); else m_blocked.remove(threadId); }
    bool isBlocked(const QString& threadId) const { return m_blocked.contains(threadId); }

    QString snoozedLabelId, blockedLabelId;

Q_SIGNALS:
    void threadChanged(const QString& threadId);
    void threadRemoved(const QString& threadId);
    void folderChanged(const QString& folderKey);
    void labelsChanged();
    void triageChanged(const QString& threadId);

private:
    QHash<QString, Thread> m_threads;
    QHash<QString, QStringList> m_folderIds;
    QHash<QString, QString> m_pageTokens;
    QList<Label> m_labels;
    QHash<QString, ThreadSummary> m_summaries;
    QHash<QString, QHash<QString, ActionStatusRecord>> m_statuses;
    QSet<QString> m_evaluating;
    QSet<QString> m_blocked;
};

} // namespace ponymail
