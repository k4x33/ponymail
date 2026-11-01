#pragma once
#include <QObject>
#include <QStringList>
#include <QSet>
#include <QHash>

namespace ponymail {

class ClaudeClient;
class MailStore;
class Database;
class Settings;

// Preemptive summarisation queue (spec §4.1):
//   * top 50 of the active sort order are queued as soon as a folder loads
//   * scrolling queues the next 25–50 rows
//   * two concurrent workers
//   * results cached in SQLite thread_summaries (pruned to 2,000)
//   * cache key = thread_id + latest_msg_id so a new reply triggers re-evaluation,
//     while per-item statuses carry over (matched by action hash)
class SummaryQueue : public QObject {
    Q_OBJECT
public:
    SummaryQueue(ClaudeClient* ai, MailStore* store, Database* db, Settings* settings, QObject* parent = nullptr);

    void setWorkerCount(int n) { m_workers = qMax(1, n); }
    void enqueue(const QStringList& threadIds, bool highPriority = false);
    void enqueueFront(const QString& threadId);   // selected thread → jump the queue
    void setVisibleRows(const QStringList& orderedThreadIds, int firstVisible, int lastVisible);
    void clear();
    bool isPending(const QString& threadId) const { return m_queue.contains(threadId) || m_inflight.contains(threadId); }
    void invalidate(const QString& threadId);     // force re-evaluation on next request

Q_SIGNALS:
    void summaryReady(const QString& threadId);
    void summaryFailed(const QString& threadId, const QString& error);

private:
    void pump();
    bool loadFromCache(const QString& threadId);

    ClaudeClient* m_ai;
    MailStore* m_store;
    Database* m_db;
    Settings* m_settings;
    QStringList m_queue;
    QSet<QString> m_inflight;
    QHash<QString, int> m_failures;
    int m_workers = 2;
};

} // namespace ponymail
