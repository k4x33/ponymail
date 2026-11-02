#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QUrlQuery>
#include <QTimer>
#include <QList>
#include <QPair>
#include <functional>

namespace ponymail {

class OAuthManager;

// Thin asynchronous wrapper over the Gmail REST API (v1). Every call acquires a
// fresh access token from OAuthManager, performs the request, and invokes the
// callback on the GUI thread. Emits rateLimited() on HTTP 429 / quota 403 so the
// SyncEngine can back off, and requestSucceeded() on any 2xx.
class GmailClient : public QObject {
    Q_OBJECT
public:
    using JsonCallback = std::function<void(const QJsonObject& json, int status, const QString& error)>;
    using BytesCallback = std::function<void(const QByteArray& data, int status, const QString& error)>;

    explicit GmailClient(OAuthManager* auth, QNetworkAccessManager* nam, QObject* parent = nullptr);

    // Client-side quota governor: every call is charged its Gmail quota units and
    // queued when the sliding 60 s window would exceed the budget. Default 5,400
    // (90% of the project's 6,000 units/min/user limit).
    void setQuotaBudget(int unitsPerMinute);
    int quotaBudget() const { return m_budget; }
    int unitsUsedLastMinute() const;
    int queuedRequests() const { return m_interactive.size() + m_background.size(); }

    // Profile / labels
    void getProfile(JsonCallback cb);
    void listLabels(JsonCallback cb);
    void getLabel(const QString& id, JsonCallback cb);
    void createLabel(const QString& name, JsonCallback cb);

    // Threads
    void listThreads(const QString& query, const QStringList& labelIds, int maxResults,
                     const QString& pageToken, JsonCallback cb);
    void getThread(const QString& threadId, JsonCallback cb);               // format=full
    void getThreadMetadata(const QString& threadId, JsonCallback cb);       // format=metadata
    void modifyThread(const QString& threadId, const QStringList& add, const QStringList& remove, JsonCallback cb);
    void trashThread(const QString& threadId, JsonCallback cb);
    void untrashThread(const QString& threadId, JsonCallback cb);
    void deleteThread(const QString& threadId, JsonCallback cb);            // permanent

    // Messages
    void listMessages(const QString& query, const QStringList& labelIds, int maxResults,
                      const QString& pageToken, JsonCallback cb);
    void getMessage(const QString& messageId, JsonCallback cb);
    void modifyMessage(const QString& messageId, const QStringList& add, const QStringList& remove, JsonCallback cb);
    void trashMessage(const QString& messageId, JsonCallback cb);
    void deleteMessage(const QString& messageId, JsonCallback cb);
    void batchDeleteMessages(const QStringList& ids, JsonCallback cb);
    void getAttachment(const QString& messageId, const QString& attachmentId, BytesCallback cb);
    void sendMessage(const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb);

    // Drafts
    void listDrafts(int maxResults, const QString& pageToken, JsonCallback cb);
    void getDraft(const QString& draftId, JsonCallback cb);
    void createDraft(const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb);
    void updateDraft(const QString& draftId, const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb);
    void deleteDraft(const QString& draftId, JsonCallback cb);
    void sendDraft(const QString& draftId, JsonCallback cb);

    // History
    void listHistory(const QString& startHistoryId, const QString& pageToken, JsonCallback cb);

Q_SIGNALS:
    void quotaUsage(int unitsUsed, int budget);
    void quotaThrottled(int unitsUsed, int budget, int queued);
    void rateLimited(int status);
    void requestSucceeded();
    void authExpired();

private:
    void request(const QByteArray& verb, const QString& path, const QUrlQuery& query,
                 const QByteArray& body, const QByteArray& contentType, JsonCallback cb);
    void rawRequest(const QByteArray& verb, const QString& path, const QUrlQuery& query,
                    const QByteArray& body, const QByteArray& contentType, BytesCallback cb, int attempt);

    void pump();

    struct Pending { int cost = 0; std::function<void()> fn; };
    OAuthManager* m_auth;
    QNetworkAccessManager* m_nam;
    int m_budget = 5400;
    QList<QPair<qint64, int>> m_ledger;   // (timestamp ms, units) within the last minute
    QList<Pending> m_interactive, m_background;
    QTimer m_pumpTimer;
};

} // namespace ponymail
