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

#ifndef PONYMAIL_GOOGLE_CLIENT_ID
#  define PONYMAIL_GOOGLE_CLIENT_ID ""
#endif
#ifndef PONYMAIL_GOOGLE_CLIENT_SECRET
#  define PONYMAIL_GOOGLE_CLIENT_SECRET ""
#endif
#ifndef PONYMAIL_MS_CLIENT_ID
#  define PONYMAIL_MS_CLIENT_ID ""
#endif

namespace ponymail {

QString providerKey(Provider p)
{
    switch (p) {
    case Provider::Google: return QStringLiteral("google");
    case Provider::Microsoft: return QStringLiteral("microsoft");
    default: return QString();
    }
}

Provider providerFromKey(const QString& k)
{
    if (k == QLatin1String("google")) return Provider::Google;
    if (k == QLatin1String("microsoft")) return Provider::Microsoft;
    return Provider::None;
}

QString providerDisplayName(Provider p)
{
    switch (p) {
    case Provider::Google: return QStringLiteral("Google");
    case Provider::Microsoft: return QStringLiteral("Outlook");
    default: return QString();
    }
}

OAuthManager::Endpoints OAuthManager::endpoints(Provider p)
{
    if (p == Provider::Microsoft) {
        return {QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"),
                QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token"),
                QString(),
                QStringLiteral("offline_access openid profile email https://graph.microsoft.com/Mail.ReadWrite https://graph.microsoft.com/Mail.Send"),
                false};
    }
    // Full mailbox scope is required for permanent delete (users.messages.batchDelete)
    // and for emptying Trash/Spam, per spec §3.1.
    return {QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"),
            QStringLiteral("https://oauth2.googleapis.com/token"),
            QStringLiteral("https://oauth2.googleapis.com/revoke"),
            QStringLiteral("https://mail.google.com/ https://www.googleapis.com/auth/userinfo.email"),
            true};
}

OAuthManager::OAuthManager(Settings* settings, QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_settings(settings), m_nam(nam) {}

Provider OAuthManager::activeProvider() const { return providerFromKey(m_settings->accountProvider()); }

bool OAuthManager::hasRefreshToken(Provider p) const { return p != Provider::None && !CredentialStore::read(refreshKey(p)).isEmpty(); }

bool OAuthManager::hasBuiltInClient(Provider p) const
{
    if (p == Provider::Google) return !QString::fromLatin1(PONYMAIL_GOOGLE_CLIENT_ID).isEmpty() && !QString::fromLatin1(PONYMAIL_GOOGLE_CLIENT_SECRET).isEmpty();
    if (p == Provider::Microsoft) return !QString::fromLatin1(PONYMAIL_MS_CLIENT_ID).isEmpty();
    return false;
}

QString OAuthManager::clientId(Provider p) const
{
    const QString user = m_settings->clientId(providerKey(p));
    if (!user.isEmpty()) return user;
    if (p == Provider::Google) return QString::fromLatin1(PONYMAIL_GOOGLE_CLIENT_ID);
    if (p == Provider::Microsoft) return QString::fromLatin1(PONYMAIL_MS_CLIENT_ID);
    return {};
}

QString OAuthManager::clientSecret(Provider p) const
{
    if (p != Provider::Google) return {};
    const QString user = CredentialStore::read(secretKey(p));
    if (!user.isEmpty() && !m_settings->clientId(providerKey(p)).isEmpty()) return user;
    return QString::fromLatin1(PONYMAIL_GOOGLE_CLIENT_SECRET);
}

bool OAuthManager::hasClientConfig(Provider p) const
{
    if (clientId(p).isEmpty()) return false;
    return !endpoints(p).needsSecret || !clientSecret(p).isEmpty();
}

void OAuthManager::setUserClient(Provider p, const QString& id, const QString& secret)
{
    m_settings->setClientId(providerKey(p), id.trimmed());
    if (endpoints(p).needsSecret) CredentialStore::write(secretKey(p), secret.trimmed());
}

QString OAuthManager::userClientId(Provider p) const { return m_settings->clientId(providerKey(p)); }

void OAuthManager::clearUserClient(Provider p)
{
    m_settings->setClientId(providerKey(p), QString());
    CredentialStore::remove(secretKey(p));
}

QString OAuthManager::randomString(int len)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString s;
    s.reserve(len);
    for (int i = 0; i < len; ++i)
        s += QLatin1Char(chars[QRandomGenerator::system()->bounded(int(sizeof(chars) - 1))]);
    return s;
}

void OAuthManager::startInteractiveLogin(Provider p)
{
    cancelInteractiveLogin();
    if (!hasClientConfig(p)) { Q_EMIT authFailed(QStringLiteral("No OAuth client configured for ") + providerDisplayName(p) + QLatin1Char('.')); return; }
    m_pendingProvider = p;

    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        Q_EMIT authFailed(QStringLiteral("Could not open loopback port: ") + m_server->errorString());
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &OAuthManager::onLoopbackConnection);
    // Microsoft registers "http://localhost" (any port) for desktop apps; Google accepts 127.0.0.1 with any port.
    const QString host = p == Provider::Microsoft ? QStringLiteral("localhost") : QStringLiteral("127.0.0.1");
    m_redirectUri = QStringLiteral("http://%1:%2/").arg(host).arg(m_server->serverPort());

    m_codeVerifier = randomString(96);
    m_state = randomString(32);
    const QByteArray challenge = util::base64Url(QCryptographicHash::hash(m_codeVerifier.toLatin1(), QCryptographicHash::Sha256));

    const Endpoints ep = endpoints(p);
    QUrl url(ep.authorize);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), clientId(p));
    q.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("scope"), ep.scope);
    q.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("state"), m_state);
    if (p == Provider::Google) {
        q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
        q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    } else {
        q.addQueryItem(QStringLiteral("response_mode"), QStringLiteral("query"));
        q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("select_account"));
    }
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
                error = qq.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded);
                if (error.isEmpty()) error = qq.queryItemValue(QStringLiteral("error"));
                stateOk = qq.queryItemValue(QStringLiteral("state")) == m_state;
            }
            if (parts.size() >= 2 && parts[1].startsWith("/favicon")) { sock->disconnectFromHost(); sock->deleteLater(); return; }
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
    const Provider p = m_pendingProvider;
    const Endpoints ep = endpoints(p);
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), clientId(p));
    if (ep.needsSecret) body.addQueryItem(QStringLiteral("client_secret"), clientSecret(p));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("code_verifier"), m_codeVerifier);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    if (p == Provider::Microsoft) body.addQueryItem(QStringLiteral("scope"), ep.scope);

    QNetworkRequest req{QUrl(ep.token)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, p]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        handleTokenResponse(p, reply->readAll(), status, nullptr, false);
        reply->deleteLater();
    });
}

void OAuthManager::handleTokenResponse(Provider p, const QByteArray& body, int status, TokenCallback cb, bool fromRefresh)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (status != 200 || o.contains(QStringLiteral("error"))) {
        const QString err = o.value(QStringLiteral("error_description")).toString(
            o.value(QStringLiteral("error")).toString(QStringLiteral("HTTP %1").arg(status)));
        if (fromRefresh && o.value(QStringLiteral("error")).toString() == QLatin1String("invalid_grant")) {
            CredentialStore::remove(refreshKey(p));
            m_settings->setAccountProvider(QString());
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
    if (!refresh.isEmpty()) CredentialStore::write(refreshKey(p), refresh);
    if (!fromRefresh) {
        // Remove any other provider's token: PonyMail holds one account at a time.
        for (Provider other : {Provider::Google, Provider::Microsoft}) if (other != p) CredentialStore::remove(refreshKey(other));
        m_settings->setAccountProvider(providerKey(p));
        // Microsoft returns an id_token carrying the signed-in address.
        const QStringList idParts = o.value(QStringLiteral("id_token")).toString().split('.');
        if (idParts.size() >= 2) {
            const QJsonObject claims = QJsonDocument::fromJson(util::fromBase64Url(idParts[1].toLatin1())).object();
            const QString email = claims.value(QStringLiteral("preferred_username")).toString(claims.value(QStringLiteral("email")).toString());
            const QString name = claims.value(QStringLiteral("name")).toString();
            if (!email.isEmpty()) m_settings->setUserEmail(email);
            if (!name.isEmpty()) m_settings->setUserName(name);
        }
    }
    if (cb) cb(m_accessToken, {});
    if (!fromRefresh) Q_EMIT authenticated(p);
}

void OAuthManager::withAccessToken(TokenCallback cb)
{
    if (!m_accessToken.isEmpty() && m_expiresAt.isValid() && QDateTime::currentDateTimeUtc().secsTo(m_expiresAt) > 60) {
        cb(m_accessToken, {});
        return;
    }
    refreshAccessToken(std::move(cb));
}

void OAuthManager::refreshAccessToken(TokenCallback cb)
{
    const Provider p = activeProvider();
    const QString refresh = p == Provider::None ? QString() : CredentialStore::read(refreshKey(p));
    if (refresh.isEmpty()) { cb({}, QStringLiteral("Not signed in.")); return; }
    m_pending << std::move(cb);
    if (m_refreshing) return;
    m_refreshing = true;

    const Endpoints ep = endpoints(p);
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), clientId(p));
    if (ep.needsSecret) body.addQueryItem(QStringLiteral("client_secret"), clientSecret(p));
    body.addQueryItem(QStringLiteral("refresh_token"), refresh);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    if (p == Provider::Microsoft) body.addQueryItem(QStringLiteral("scope"), ep.scope);

    QNetworkRequest req{QUrl(ep.token)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, p]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        m_refreshing = false;
        auto waiters = m_pending;
        m_pending.clear();
        handleTokenResponse(p, data, status, [waiters](const QString& tok, const QString& err) { for (const auto& w : waiters) w(tok, err); }, true);
    });
}

void OAuthManager::signOut()
{
    const Provider p = activeProvider();
    if (p != Provider::None) {
        const QString refresh = CredentialStore::read(refreshKey(p));
        const Endpoints ep = endpoints(p);
        if (!refresh.isEmpty() && !ep.revoke.isEmpty()) {
            QNetworkRequest req{QUrl(ep.revoke)};
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
            QNetworkReply* r = m_nam->post(req, QByteArray("token=") + QUrl::toPercentEncoding(refresh));
            connect(r, &QNetworkReply::finished, r, &QNetworkReply::deleteLater);
        }
        CredentialStore::remove(refreshKey(p));
    }
    m_settings->setAccountProvider(QString());
    m_settings->setUserEmail(QString());
    invalidateAccessToken();
    Q_EMIT signedOut();
}

} // namespace ponymail
