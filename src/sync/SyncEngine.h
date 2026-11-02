#pragma once
#include "model/MailStore.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <functional>

namespace ponymail {

class GmailClient;
class Database;
class Settings;

// Background sync (spec §2.2):
//   * users.history.list every 30 s; 120 s after a 429/quota hit, 30 s again on success
//   * Sent sweep immediately after send, plus +3 s and +10 s
//   * immediate sweep on window focus / system wake
//   * blocked-sender auto-trash + "Blocked" label on arrival
//   * snooze wake timer re-applies INBOX and removes "Snoozed"
//   * harvests contacts from every message header seen
class SyncEngine : public QObject {
    Q_OBJECT
public:
    SyncEngine(GmailClient* gmail, MailStore* store, Database* db, Settings* settings, QObject* parent = nullptr);

    void start();                      // profile → labels → active folder → polling
    void stop();
    void setActiveFolder(const Folder& f);
    Folder activeFolder() const { return m_activeFolder; }
    QList<Folder> systemFolders() const;
    QList<Folder> labelFolders() const;

    void refreshActiveFolder(bool reset = true);
    void loadMoreActiveFolder();
    void pollNow();                    // history sweep
    void afterSend();                  // Sent refresh now, +3 s, +10 s
    void onAppFocus();

    // Mutations (all optimistic: local state first, then Gmail)
    void markRead(const QStringList& threadIds, bool read);
    void setStarred(const QString& threadId, bool starred);
    void trashThreads(const QStringList& threadIds);
    void archiveThreads(const QStringList& threadIds);
    void deletePermanently(const QStringList& threadIds);
    void snoozeThread(const QString& threadId, qint64 untilMs);
    void unsnoozeThread(const QString& threadId);
    void blockSender(const QString& threadId, const QString& pattern);
    void emptyFolder(const QString& folderKey, std::function<void(int, const QString&)> done);   // trash | spam
    void countFolder(const QString& folderKey, std::function<void(int)> cb);
    void fetchThread(const QString& threadId, std::function<void(bool)> cb = nullptr);

    int pollIntervalMs() const { return m_pollTimer.interval(); }
    bool isRateLimited() const { return m_rateLimited; }
    QString userEmail() const;

Q_SIGNALS:
    void statusMessage(const QString& msg);
    void syncStateChanged(bool syncing, bool rateLimited);
    void folderLoading(const QString& folderKey, bool loading);
    void newMessagesArrived(const QStringList& threadIds);
    void errorOccurred(const QString& msg);
    void profileLoaded(const QString& email);

private:
    void loadLabels(std::function<void()> done);
    void ensureLabel(const QString& name, std::function<void(const QString& id)> done);
    void fetchFolderPage(const Folder& f, const QString& pageToken, bool reset);
    void fetchThreads(const QStringList& ids, std::function<void()> done);
    void applyHistory(const QJsonObject& json);
    void checkSnoozes();
    void onTick();
    void setRateLimited(bool on);
    void harvestContacts(const Thread& t);
    bool threadMatchesFolder(const Thread& t, const Folder& f) const;
    void reconcileThreadFolders(const Thread& t);
    void afterBlockedCheck(const Thread& t);
    void modify(const QStringList& threadIds, const QStringList& add, const QStringList& remove);

    GmailClient* m_gmail;
    MailStore* m_store;
    Database* m_db;
    Settings* m_settings;
    Folder m_activeFolder;
    QTimer m_pollTimer;
    QTimer m_snoozeTimer;
    QElapsedTimer m_sinceLastTick;
    QString m_historyId;
    bool m_rateLimited = false;
    bool m_polling = false;
    bool m_started = false;
    int m_inflightThreadFetches = 0;
    QStringList m_fetchQueue;
    QSet<QString> m_knownInboxThreads;
    QSet<QString> m_loadingPages;   // folder keys with a page request in flight
};

} // namespace ponymail
