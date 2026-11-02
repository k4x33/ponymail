#include "GmailClient.h"
#include "auth/OAuthManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QTimer>
#include <QDateTime>
#include <QDebug>

namespace ponymail {

namespace {
const QString kBase = QStringLiteral("https://gmail.googleapis.com/gmail/v1/users/me/");
const QString kUpload = QStringLiteral("https://gmail.googleapis.com/upload/gmail/v1/users/me/");
constexpr int kMaxAttempts = 3;
constexpr qint64 kWindowMs = 60'000;

// Gmail API quota units per method (https://developers.google.com/gmail/api/reference/quota).
int quotaCost(const QByteArray& verb, const QString& path)
{
    const QString p = path.section('?', 0, 0);
    if (p == QLatin1String("profile")) return 1;
    if (p.startsWith(QLatin1String("labels"))) return verb == "POST" ? 5 : 1;
    if (p == QLatin1String("history")) return 2;
    if (p == QLatin1String("messages/send") || p == QLatin1String("drafts/send")) return 100;
    if (p == QLatin1String("messages/batchDelete")) return 50;
    if (p.contains(QLatin1String("/attachments/"))) return 5;
    if (p.startsWith(QLatin1String("threads"))) {
        if (p.endsWith(QLatin1String("/modify")) || p.endsWith(QLatin1String("/trash")) || p.endsWith(QLatin1String("/untrash"))) return 10;
        if (verb == "DELETE") return 20;
        return 10;   // list, get
    }
    if (p.startsWith(QLatin1String("messages"))) {
        if (verb == "DELETE") return 10;
        return 5;    // list, get, modify, trash, untrash
    }
    if (p.startsWith(QLatin1String("drafts"))) {
        if (verb == "PUT") return 15;
        if (verb == "POST") return 10;
        if (verb == "DELETE") return 10;
        return 5;    // list, get
    }
    return 10;
}

// Interactive = user-triggered mutation or the thread the user just selected; background = sync fetches.
bool isInteractive(const QByteArray& verb, const QString& path)
{
    return verb != "GET" || path == QLatin1String("profile") || path.startsWith(QLatin1String("labels"));
}
}

GmailClient::GmailClient(OAuthManager* auth, QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_auth(auth), m_nam(nam)
{
    m_pumpTimer.setSingleShot(true);
    connect(&m_pumpTimer, &QTimer::timeout, this, &GmailClient::pump);
}

void GmailClient::setQuotaBudget(int unitsPerMinute) { m_budget = qMax(100, unitsPerMinute); }

int GmailClient::unitsUsedLastMinute() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int used = 0;
    for (const auto& e : m_ledger) if (now - e.first < kWindowMs) used += e.second;
    return used;
}

void GmailClient::pump()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!m_ledger.isEmpty() && now - m_ledger.first().first >= kWindowMs) m_ledger.removeFirst();
    int used = 0;
    for (const auto& e : m_ledger) used += e.second;

    // Interactive requests first, then background. Keep a reserve so a user action
    // never waits behind a background page fetch.
    constexpr int kInteractiveReserve = 600;
    while (!m_interactive.isEmpty() || !m_background.isEmpty()) {
        const bool takeInteractive = !m_interactive.isEmpty();
        const Pending& next = takeInteractive ? m_interactive.first() : m_background.first();
        const int limit = takeInteractive ? m_budget : m_budget - kInteractiveReserve;
        if (used + next.cost > limit) break;
        Pending job = takeInteractive ? m_interactive.takeFirst() : m_background.takeFirst();
        used += job.cost;
        m_ledger.append({now, job.cost});
        job.fn();
    }
    if (!m_interactive.isEmpty() || !m_background.isEmpty()) {
        // Wake when the oldest ledger entry expires.
        const qint64 wait = m_ledger.isEmpty() ? 1000 : qMax<qint64>(250, kWindowMs - (now - m_ledger.first().first) + 50);
        m_pumpTimer.start(int(wait));
        Q_EMIT quotaThrottled(used, m_budget, m_interactive.size() + m_background.size());
    }
    Q_EMIT quotaUsage(used, m_budget);
}

void GmailClient::rawRequest(const QByteArray& verb, const QString& path, const QUrlQuery& query,
                             const QByteArray& body, const QByteArray& contentType, BytesCallback cb, int attempt)
{
    if (attempt == 0) {
        // First attempt goes through the governor; retries have already been charged.
        Pending job;
        job.cost = quotaCost(verb, path);
        job.fn = [this, verb, path, query, body, contentType, cb]() { rawRequest(verb, path, query, body, contentType, cb, 1); };
        if (isInteractive(verb, path)) m_interactive.append(job); else m_background.append(job);
        pump();
        return;
    }
    m_auth->withAccessToken([=](const QString& token, const QString& err) {
        if (token.isEmpty()) {
            if (err.contains(QLatin1String("Not signed in"))) Q_EMIT authExpired();
            cb({}, 0, err.isEmpty() ? QStringLiteral("No access token") : err);
            return;
        }
        QUrl url(path.startsWith(QLatin1String("https://")) ? path : kBase + path);
        if (!query.isEmpty()) url.setQuery(query);
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (!contentType.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(contentType));
        req.setTransferTimeout(60000);

        QNetworkReply* reply = m_nam->sendCustomRequest(req, verb, body);
        connect(reply, &QNetworkReply::finished, this, [=]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            const QString netErr = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
            reply->deleteLater();

            if (status == 401 && attempt < kMaxAttempts + 1) {
                m_auth->invalidateAccessToken();
                rawRequest(verb, path, query, body, contentType, cb, attempt + 1);
                return;
            }
            bool quota = status == 429;
            if (status == 403) {
                const auto o = QJsonDocument::fromJson(data).object();
                const QString reason = o.value(QStringLiteral("error")).toObject()
                    .value(QStringLiteral("errors")).toArray().isEmpty() ? QString()
                    : o.value(QStringLiteral("error")).toObject().value(QStringLiteral("errors")).toArray()
                      .first().toObject().value(QStringLiteral("reason")).toString();
                quota = reason.contains(QLatin1String("ateLimit"), Qt::CaseInsensitive)
                     || reason.contains(QLatin1String("quota"), Qt::CaseInsensitive);
            }
            if (quota) Q_EMIT rateLimited(status);

            if ((status >= 500 || status == 0) && attempt < kMaxAttempts + 1) {
                const int delay = 500 * (1 << attempt);
                QTimer::singleShot(delay, this, [=]() { rawRequest(verb, path, query, body, contentType, cb, attempt + 1); });
                return;
            }
            if (status >= 200 && status < 300) Q_EMIT requestSucceeded();

            QString error;
            if (status < 200 || status >= 300) {
                const auto o = QJsonDocument::fromJson(data).object();
                error = o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
                if (error.isEmpty()) error = netErr.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : netErr;
            }
            cb(data, status, error);
        });
    });
}

void GmailClient::request(const QByteArray& verb, const QString& path, const QUrlQuery& query,
                          const QByteArray& body, const QByteArray& contentType, JsonCallback cb)
{
    rawRequest(verb, path, query, body, contentType, [cb](const QByteArray& data, int status, const QString& err) {
        QJsonObject o;
        if (!data.isEmpty()) o = QJsonDocument::fromJson(data).object();
        cb(o, status, err);
    }, 0);
}

static QByteArray jsonBody(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

// --- profile / labels -------------------------------------------------------
void GmailClient::getProfile(JsonCallback cb) { request("GET", QStringLiteral("profile"), QUrlQuery(), {}, {}, cb); }
void GmailClient::listLabels(JsonCallback cb) { request("GET", QStringLiteral("labels"), QUrlQuery(), {}, {}, cb); }
void GmailClient::getLabel(const QString& id, JsonCallback cb) { request("GET", QStringLiteral("labels/") + id, QUrlQuery(), {}, {}, cb); }
void GmailClient::createLabel(const QString& name, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("name"), name},
                  {QStringLiteral("labelListVisibility"), QStringLiteral("labelShow")},
                  {QStringLiteral("messageListVisibility"), QStringLiteral("show")}};
    request("POST", QStringLiteral("labels"), QUrlQuery(), jsonBody(o), "application/json", cb);
}

// --- threads ----------------------------------------------------------------
void GmailClient::listThreads(const QString& query, const QStringList& labelIds, int maxResults,
                              const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem(QStringLiteral("q"), query);
    for (const auto& l : labelIds) q.addQueryItem(QStringLiteral("labelIds"), l);
    q.addQueryItem(QStringLiteral("maxResults"), QString::number(maxResults));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("threads"), q, {}, {}, cb);
}

void GmailClient::getThread(const QString& threadId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
    request("GET", QStringLiteral("threads/") + threadId, q, {}, {}, cb);
}

void GmailClient::getThreadMetadata(const QString& threadId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("metadata"));
    request("GET", QStringLiteral("threads/") + threadId, q, {}, {}, cb);
}

void GmailClient::modifyThread(const QString& threadId, const QStringList& add, const QStringList& remove, JsonCallback cb)
{
    QJsonObject o;
    if (!add.isEmpty()) o[QStringLiteral("addLabelIds")] = QJsonArray::fromStringList(add);
    if (!remove.isEmpty()) o[QStringLiteral("removeLabelIds")] = QJsonArray::fromStringList(remove);
    request("POST", QStringLiteral("threads/%1/modify").arg(threadId), QUrlQuery(), jsonBody(o), "application/json", cb);
}

void GmailClient::trashThread(const QString& threadId, JsonCallback cb)
{ request("POST", QStringLiteral("threads/%1/trash").arg(threadId), QUrlQuery(), {}, {}, cb); }
void GmailClient::untrashThread(const QString& threadId, JsonCallback cb)
{ request("POST", QStringLiteral("threads/%1/untrash").arg(threadId), QUrlQuery(), {}, {}, cb); }
void GmailClient::deleteThread(const QString& threadId, JsonCallback cb)
{ request("DELETE", QStringLiteral("threads/") + threadId, QUrlQuery(), {}, {}, cb); }

// --- messages ---------------------------------------------------------------
void GmailClient::listMessages(const QString& query, const QStringList& labelIds, int maxResults,
                               const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem(QStringLiteral("q"), query);
    for (const auto& l : labelIds) q.addQueryItem(QStringLiteral("labelIds"), l);
    q.addQueryItem(QStringLiteral("maxResults"), QString::number(maxResults));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("messages"), q, {}, {}, cb);
}

void GmailClient::getMessage(const QString& messageId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
    request("GET", QStringLiteral("messages/") + messageId, q, {}, {}, cb);
}

void GmailClient::modifyMessage(const QString& messageId, const QStringList& add, const QStringList& remove, JsonCallback cb)
{
    QJsonObject o;
    if (!add.isEmpty()) o[QStringLiteral("addLabelIds")] = QJsonArray::fromStringList(add);
    if (!remove.isEmpty()) o[QStringLiteral("removeLabelIds")] = QJsonArray::fromStringList(remove);
    request("POST", QStringLiteral("messages/%1/modify").arg(messageId), QUrlQuery(), jsonBody(o), "application/json", cb);
}

void GmailClient::trashMessage(const QString& messageId, JsonCallback cb)
{ request("POST", QStringLiteral("messages/%1/trash").arg(messageId), QUrlQuery(), {}, {}, cb); }
void GmailClient::deleteMessage(const QString& messageId, JsonCallback cb)
{ request("DELETE", QStringLiteral("messages/") + messageId, QUrlQuery(), {}, {}, cb); }

void GmailClient::batchDeleteMessages(const QStringList& ids, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("ids"), QJsonArray::fromStringList(ids)}};
    request("POST", QStringLiteral("messages/batchDelete"), QUrlQuery(), jsonBody(o), "application/json", cb);
}

void GmailClient::getAttachment(const QString& messageId, const QString& attachmentId, BytesCallback cb)
{
    rawRequest("GET", QStringLiteral("messages/%1/attachments/%2").arg(messageId, attachmentId), QUrlQuery(), {}, {}, cb, 0);
}

void GmailClient::sendMessage(const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("raw"), QString::fromLatin1(rawBase64Url)}};
    if (!threadId.isEmpty()) o[QStringLiteral("threadId")] = threadId;
    request("POST", QStringLiteral("messages/send"), QUrlQuery(), jsonBody(o), "application/json", cb);
}

// --- drafts -----------------------------------------------------------------
void GmailClient::listDrafts(int maxResults, const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("maxResults"), QString::number(maxResults));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("drafts"), q, {}, {}, cb);
}

void GmailClient::getDraft(const QString& draftId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
    request("GET", QStringLiteral("drafts/") + draftId, q, {}, {}, cb);
}

void GmailClient::createDraft(const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb)
{
    QJsonObject msg{{QStringLiteral("raw"), QString::fromLatin1(rawBase64Url)}};
    if (!threadId.isEmpty()) msg[QStringLiteral("threadId")] = threadId;
    QJsonObject o{{QStringLiteral("message"), msg}};
    request("POST", QStringLiteral("drafts"), QUrlQuery(), jsonBody(o), "application/json", cb);
}

void GmailClient::updateDraft(const QString& draftId, const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb)
{
    QJsonObject msg{{QStringLiteral("raw"), QString::fromLatin1(rawBase64Url)}};
    if (!threadId.isEmpty()) msg[QStringLiteral("threadId")] = threadId;
    QJsonObject o{{QStringLiteral("id"), draftId}, {QStringLiteral("message"), msg}};
    request("PUT", QStringLiteral("drafts/") + draftId, QUrlQuery(), jsonBody(o), "application/json", cb);
}

void GmailClient::deleteDraft(const QString& draftId, JsonCallback cb)
{ request("DELETE", QStringLiteral("drafts/") + draftId, QUrlQuery(), {}, {}, cb); }

void GmailClient::sendDraft(const QString& draftId, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("id"), draftId}};
    request("POST", QStringLiteral("drafts/send"), QUrlQuery(), jsonBody(o), "application/json", cb);
}

// --- history ----------------------------------------------------------------
void GmailClient::listHistory(const QString& startHistoryId, const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("startHistoryId"), startHistoryId);
    q.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("500"));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("history"), q, {}, {}, cb);
}

} // namespace ponymail
