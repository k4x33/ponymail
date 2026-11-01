#include "GmailClient.h"
#include "auth/OAuthManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QTimer>
#include <QDebug>

namespace ponymail {

namespace {
const QString kBase = QStringLiteral("https://gmail.googleapis.com/gmail/v1/users/me/");
const QString kUpload = QStringLiteral("https://gmail.googleapis.com/upload/gmail/v1/users/me/");
constexpr int kMaxAttempts = 3;
}

GmailClient::GmailClient(OAuthManager* auth, QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_auth(auth), m_nam(nam) {}

void GmailClient::rawRequest(const QByteArray& verb, const QString& path, const QUrlQuery* query,
                             const QByteArray& body, const QByteArray& contentType, BytesCallback cb, int attempt)
{
    m_auth->withAccessToken([=](const QString& token, const QString& err) {
        if (token.isEmpty()) {
            if (err.contains(QLatin1String("Not signed in"))) Q_EMIT authExpired();
            cb({}, 0, err.isEmpty() ? QStringLiteral("No access token") : err);
            return;
        }
        QUrl url(path.startsWith(QLatin1String("https://")) ? path : kBase + path);
        if (query) url.setQuery(*query);
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

            if (status == 401 && attempt < kMaxAttempts) {
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

            if ((status >= 500 || status == 0) && attempt < kMaxAttempts) {
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

void GmailClient::request(const QByteArray& verb, const QString& path, const QUrlQuery* query,
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
void GmailClient::getProfile(JsonCallback cb) { request("GET", QStringLiteral("profile"), nullptr, {}, {}, cb); }
void GmailClient::listLabels(JsonCallback cb) { request("GET", QStringLiteral("labels"), nullptr, {}, {}, cb); }
void GmailClient::getLabel(const QString& id, JsonCallback cb) { request("GET", QStringLiteral("labels/") + id, nullptr, {}, {}, cb); }
void GmailClient::createLabel(const QString& name, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("name"), name},
                  {QStringLiteral("labelListVisibility"), QStringLiteral("labelShow")},
                  {QStringLiteral("messageListVisibility"), QStringLiteral("show")}};
    request("POST", QStringLiteral("labels"), nullptr, jsonBody(o), "application/json", cb);
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
    request("GET", QStringLiteral("threads"), &q, {}, {}, cb);
}

void GmailClient::getThread(const QString& threadId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
    request("GET", QStringLiteral("threads/") + threadId, &q, {}, {}, cb);
}

void GmailClient::getThreadMetadata(const QString& threadId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("metadata"));
    request("GET", QStringLiteral("threads/") + threadId, &q, {}, {}, cb);
}

void GmailClient::modifyThread(const QString& threadId, const QStringList& add, const QStringList& remove, JsonCallback cb)
{
    QJsonObject o;
    if (!add.isEmpty()) o[QStringLiteral("addLabelIds")] = QJsonArray::fromStringList(add);
    if (!remove.isEmpty()) o[QStringLiteral("removeLabelIds")] = QJsonArray::fromStringList(remove);
    request("POST", QStringLiteral("threads/%1/modify").arg(threadId), nullptr, jsonBody(o), "application/json", cb);
}

void GmailClient::trashThread(const QString& threadId, JsonCallback cb)
{ request("POST", QStringLiteral("threads/%1/trash").arg(threadId), nullptr, {}, {}, cb); }
void GmailClient::untrashThread(const QString& threadId, JsonCallback cb)
{ request("POST", QStringLiteral("threads/%1/untrash").arg(threadId), nullptr, {}, {}, cb); }
void GmailClient::deleteThread(const QString& threadId, JsonCallback cb)
{ request("DELETE", QStringLiteral("threads/") + threadId, nullptr, {}, {}, cb); }

// --- messages ---------------------------------------------------------------
void GmailClient::listMessages(const QString& query, const QStringList& labelIds, int maxResults,
                               const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem(QStringLiteral("q"), query);
    for (const auto& l : labelIds) q.addQueryItem(QStringLiteral("labelIds"), l);
    q.addQueryItem(QStringLiteral("maxResults"), QString::number(maxResults));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("messages"), &q, {}, {}, cb);
}

void GmailClient::getMessage(const QString& messageId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
    request("GET", QStringLiteral("messages/") + messageId, &q, {}, {}, cb);
}

void GmailClient::modifyMessage(const QString& messageId, const QStringList& add, const QStringList& remove, JsonCallback cb)
{
    QJsonObject o;
    if (!add.isEmpty()) o[QStringLiteral("addLabelIds")] = QJsonArray::fromStringList(add);
    if (!remove.isEmpty()) o[QStringLiteral("removeLabelIds")] = QJsonArray::fromStringList(remove);
    request("POST", QStringLiteral("messages/%1/modify").arg(messageId), nullptr, jsonBody(o), "application/json", cb);
}

void GmailClient::trashMessage(const QString& messageId, JsonCallback cb)
{ request("POST", QStringLiteral("messages/%1/trash").arg(messageId), nullptr, {}, {}, cb); }
void GmailClient::deleteMessage(const QString& messageId, JsonCallback cb)
{ request("DELETE", QStringLiteral("messages/") + messageId, nullptr, {}, {}, cb); }

void GmailClient::batchDeleteMessages(const QStringList& ids, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("ids"), QJsonArray::fromStringList(ids)}};
    request("POST", QStringLiteral("messages/batchDelete"), nullptr, jsonBody(o), "application/json", cb);
}

void GmailClient::getAttachment(const QString& messageId, const QString& attachmentId, BytesCallback cb)
{
    rawRequest("GET", QStringLiteral("messages/%1/attachments/%2").arg(messageId, attachmentId), nullptr, {}, {}, cb, 0);
}

void GmailClient::sendMessage(const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("raw"), QString::fromLatin1(rawBase64Url)}};
    if (!threadId.isEmpty()) o[QStringLiteral("threadId")] = threadId;
    request("POST", QStringLiteral("messages/send"), nullptr, jsonBody(o), "application/json", cb);
}

// --- drafts -----------------------------------------------------------------
void GmailClient::listDrafts(int maxResults, const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("maxResults"), QString::number(maxResults));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("drafts"), &q, {}, {}, cb);
}

void GmailClient::getDraft(const QString& draftId, JsonCallback cb)
{
    QUrlQuery q; q.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
    request("GET", QStringLiteral("drafts/") + draftId, &q, {}, {}, cb);
}

void GmailClient::createDraft(const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb)
{
    QJsonObject msg{{QStringLiteral("raw"), QString::fromLatin1(rawBase64Url)}};
    if (!threadId.isEmpty()) msg[QStringLiteral("threadId")] = threadId;
    QJsonObject o{{QStringLiteral("message"), msg}};
    request("POST", QStringLiteral("drafts"), nullptr, jsonBody(o), "application/json", cb);
}

void GmailClient::updateDraft(const QString& draftId, const QByteArray& rawBase64Url, const QString& threadId, JsonCallback cb)
{
    QJsonObject msg{{QStringLiteral("raw"), QString::fromLatin1(rawBase64Url)}};
    if (!threadId.isEmpty()) msg[QStringLiteral("threadId")] = threadId;
    QJsonObject o{{QStringLiteral("id"), draftId}, {QStringLiteral("message"), msg}};
    request("PUT", QStringLiteral("drafts/") + draftId, nullptr, jsonBody(o), "application/json", cb);
}

void GmailClient::deleteDraft(const QString& draftId, JsonCallback cb)
{ request("DELETE", QStringLiteral("drafts/") + draftId, nullptr, {}, {}, cb); }

void GmailClient::sendDraft(const QString& draftId, JsonCallback cb)
{
    QJsonObject o{{QStringLiteral("id"), draftId}};
    request("POST", QStringLiteral("drafts/send"), nullptr, jsonBody(o), "application/json", cb);
}

// --- history ----------------------------------------------------------------
void GmailClient::listHistory(const QString& startHistoryId, const QString& pageToken, JsonCallback cb)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("startHistoryId"), startHistoryId);
    q.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("500"));
    if (!pageToken.isEmpty()) q.addQueryItem(QStringLiteral("pageToken"), pageToken);
    request("GET", QStringLiteral("history"), &q, {}, {}, cb);
}

} // namespace ponymail
