#pragma once
#include "model/Models.h"
#include <QObject>
#include <QSqlDatabase>
#include <QHash>
#include <QList>
#include <optional>

namespace ponymail {

// Embedded SQLite store replacing every Supabase table from the web prototype.
//
//   action_item_statuses  <- Supabase action_item_statuses
//   blocked_senders       <- Supabase blocked_senders
//   snoozed_threads       <- Supabase snoozed_threads
//   contacts (+fts5)      <- Supabase contacts / contact_sync_state
//   thread_summaries      <- web in-memory + localStorage summary cache
//
// All methods run on the calling thread; the app uses it from the GUI thread.
class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject* parent = nullptr);
    ~Database() override;

    bool open(const QString& path);
    bool isOpen() const { return m_db.isOpen(); }
    int schemaVersion() const;

    // --- action_item_statuses ---
    bool upsertActionStatus(const QString& threadId, const ActionItem& item, ActionStatus status);
    QHash<QString, ActionStatusRecord> actionStatusesForThread(const QString& threadId) const; // keyed by hash
    QHash<QString, QHash<QString, ActionStatusRecord>> allActionStatuses() const;            // threadId -> hash -> rec

    // --- blocked_senders ---
    bool addBlockedSender(const QString& pattern);            // auto-detects type
    bool removeBlockedSender(const QString& id);
    QList<BlockedSender> blockedSenders() const;
    bool isBlocked(const QString& email) const;

    // --- snoozed_threads ---
    bool snoozeThread(const QString& threadId, qint64 until, const QStringList& originalLabels);
    bool unsnoozeThread(const QString& threadId);
    QList<SnoozedThread> snoozedThreads() const;
    QList<SnoozedThread> expiredSnoozes(qint64 nowMs) const;
    std::optional<SnoozedThread> snoozeFor(const QString& threadId) const;

    // --- contacts ---
    void touchContact(const EmailAddress& a, qint64 seenAt);
    void touchContacts(const QList<EmailAddress>& list, qint64 seenAt);
    QList<Contact> searchContacts(const QString& prefix, int limit = 8) const;
    int contactCount() const;

    // --- thread_summaries ---
    bool putSummary(const ThreadSummary& s);
    std::optional<ThreadSummary> getSummary(const QString& cacheKey) const;
    std::optional<ThreadSummary> latestSummaryForThread(const QString& threadId) const;
    void pruneSummaries(int keep = 2000);
    int summaryCount() const;

    // --- sync state (small key/value) ---
    QString kv(const QString& key, const QString& def = QString()) const;
    void setKv(const QString& key, const QString& value);

private:
    bool migrate();
    bool exec(const QString& sql);
    QSqlDatabase m_db;
    QString m_connName;
};

} // namespace ponymail
