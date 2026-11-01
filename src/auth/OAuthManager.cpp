#include "OAuthManager.h"
#include "CredentialStore.h"
#include "core/Settings.h"
#include "core/Util.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>

namespace ponymail {

namespace {
const QString kRefreshKey = QStringLiteral("google.refresh_token");
const QString kSecretKey = QStringLiteral("google.client_secret");
const QString kAuthEndpoint = QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
const QString kTokenEndpoint = QStringLiteral("https://oauth2.googleapis.com/token");
const QString kRevokeEndpoint = QStringLiteral("https://oauth2.googleapis.com/revoke");
// Full mailbox scope is required for permanent delete (users.messages.batchDelete)
// and for emptying Trash/Spam, per spec §3.1.
const QString kScope = QStringLiteral("https://mail.google.com/ https://www.googleapis.com/auth/userinfo.email");
}

OAuthManager::OAuthManager(Settings* settings, QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_settings(settings), m_nam(nam) {}

bool OAuthManager::hasCredentials() const
{
    return !m_settings->oauthClientId().isEmpty() && !CredentialStore::read(kSecretKey).isEmpty();
}

bool OAuthManager::hasRefreshToken() const { return !CredentialStore::read(kRefreshKey).isEmpty(); }

void OAuthManager::setClientCredentials(const QString& clientId, const QString& clientSecret)
{
    m_settings->setOauthClientId(clientId.trimmed());
    CredentialStore::write(kSecretKey, clientSecret.trimmed());
}

QString OAuthManager::clientId() const { return m_settings->oauthClientId(); }

QString OAuthManager::randomString(int len)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString s;
    s.reserve(len);
    for (int i = 0; i < len; ++i)
        s += QLatin1Char(chars[QRandomGenerator::system()->bounded(int(sizeof(chars) - 1))]);
    return s;
}

void OAuthManager::startInteractiveLogin()
{
    cancelInteractiveLogin();
    if (!hasCredentials()) { Q_EMIT authFailed(QStringLiteral("OAuth client ID / secret not configured.")); return; }

    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        Q_EMIT authFailed(QStringLiteral("Could not open loopback port: ") + m_server->errorString());
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &OAuthManager::onLoopbackConnection);
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1/").arg(m_server->serverPort());

    m_codeVerifier = randomString(96);
    m_state = randomString(32);
    const QByteArray challenge = util::base64Url(
        QCryptographicHash::hash(m_codeVerifier.toLatin1(), QCryptographicHash::Sha256));

    QUrl url(kAuthEndpoint);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), clientId());
    q.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("scope"), kScope);
    q.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    q.addQueryItem(QStringLiteral("state"), m_state);
    url.setQuery(q);

    Q_EMIT loginStarted(url);
    QDesktopServices::openUrl(url);
}

void OAuthManager::cancelInteractiveLogin()
{
    if (m_server) { m_server->close(); m_server->deleteLater(); m_server = nullptr; }
}

void OAuthManager::onLoopbackConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray req = sock->readAll();
            const int eol = req.indexOf("\r\n");
            const QByteArray line = eol > 0 ? req.left(eol) : req;
            const QList<QByteArray> parts = line.split(' ');
            QString code, error;
            bool stateOk = false;
            if (parts.size() >= 2) {
                const QUrl u(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(parts[1]));
                QUrlQuery qq(u);
                code = qq.queryItemValue(QStringLiteral("code"));
                error = qq.queryItemValue(QStringLiteral("error"));
                stateOk = qq.queryItemValue(QStringLiteral("state")) == m_state;
            }
            const bool ok = !code.isEmpty() && stateOk;
            const QByteArray html = ok
                ? "<html><body style='font-family:Segoe UI,sans-serif;text-align:center;padding:48px'>"
                  "<h2>PonyMail is connected</h2><p>You can close this tab and return to PonyMail.</p></body></html>"
                : "<html><body style='font-family:Segoe UI,sans-serif;text-align:center;padding:48px'>"
                  "<h2>Sign-in failed</h2><p>Return to PonyMail and try again.</p></body></html>";
            QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                + QByteArray::number(html.size()) + "\r\n\r\n" + html;
            sock->write(resp);
            sock->flush();
            sock->disconnectFromHost();
            sock->deleteLater();
            if (ok) exchangeCode(code);
            else Q_EMIT authFailed(error.isEmpty() ? QStringLiteral("Authorization was not completed.") : error);
            cancelInteractiveLogin();
        });
    }
}

void OAuthManager::exchangeCode(const QString& code)
{
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), clientId());
    body.addQueryItem(QStringLiteral("client_secret"), CredentialStore::read(kSecretKey));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("code_verifier"), m_codeVerifier);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);

    QNetworkRequest req{QUrl(kTokenEndpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        handleTokenResponse(reply->readAll(), status, nullptr, false);
        reply->deleteLater();
    });
}

void OAuthManager::handleTokenResponse(const QByteArray& body, int status, TokenCallback cb, bool fromRefresh)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (status != 200 || o.contains(QStringLiteral("error"))) {
        const QString err = o.value(QStringLiteral("error_description")).toString(
            o.value(QStringLiteral("error")).toString(QStringLiteral("HTTP %1").arg(status)));
        if (fromRefresh && o.value(QStringLiteral("error")).toString() == QLatin1String("invalid_grant")) {
            // Refresh token revoked/expired: force a fresh login.
            CredentialStore::remove(kRefreshKey);
            Q_EMIT signedOut();
        }
        if (cb) cb({}, err);
        if (!fromRefresh) Q_EMIT authFailed(err);
        return;
    }
    m_accessToken = o.value(QStringLiteral("access_token")).toString();
    const int expiresIn = o.value(QStringLiteral("expires_in")).toInt(3600);
    m_expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
    const QString refresh = o.value(QStringLiteral("refresh_token")).toString();
    if (!refresh.isEmpty()) CredentialStore::write(kRefreshKey, refresh);
    if (cb) cb(m_accessToken, {});
    if (!fromRefresh) Q_EMIT authenticated();
}

void OAuthManager::withAccessToken(TokenCallback cb)
{
    if (!m_accessToken.isEmpty() && m_expiresAt.isValid()
        && QDateTime::currentDateTimeUtc().secsTo(m_expiresAt) > 60) {
        cb(m_accessToken, {});
        return;
    }
    refreshAccessToken(std::move(cb));
}

void OAuthManager::refreshAccessToken(TokenCallback cb)
{
    const QString refresh = CredentialStore::read(kRefreshKey);
    if (refresh.isEmpty()) { cb({}, QStringLiteral("Not signed in.")); return; }
    m_pending << std::move(cb);
    if (m_refreshing) return;
    m_refreshing = true;

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), clientId());
    body.addQueryItem(QStringLiteral("client_secret"), CredentialStore::read(kSecretKey));
    body.addQueryItem(QStringLiteral("refresh_token"), refresh);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

    QNetworkRequest req{QUrl(kTokenEndpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        m_refreshing = false;
        auto waiters = m_pending;
        m_pending.clear();
        handleTokenResponse(data, status, [waiters](const QString& tok, const QString& err) {
            for (const auto& w : waiters) w(tok, err);
        }, true);
    });
}

void OAuthManager::signOut()
{
    const QString refresh = CredentialStore::read(kRefreshKey);
    if (!refresh.isEmpty()) {
        QNetworkRequest req{QUrl(kRevokeEndpoint)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QNetworkReply* r = m_nam->post(req, QByteArray("token=") + QUrl::toPercentEncoding(refresh));
        connect(r, &QNetworkReply::finished, r, &QNetworkReply::deleteLater);
    }
    CredentialStore::remove(kRefreshKey);
    invalidateAccessToken();
    Q_EMIT signedOut();
}

} // namespace ponymail
