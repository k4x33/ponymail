#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <functional>

class QTcpServer;

namespace ponymail {

class Settings;

// Google OAuth 2.0 Authorization Code flow with PKCE and a loopback redirect
// (RFC 8252). Refresh token lives in the OS credential store; the access token
// is held in memory only and refreshed on demand.
class OAuthManager : public QObject {
    Q_OBJECT
public:
    using TokenCallback = std::function<void(const QString& accessToken, const QString& error)>;

    explicit OAuthManager(Settings* settings, QNetworkAccessManager* nam, QObject* parent = nullptr);

    bool hasCredentials() const;         // client id + secret configured
    bool hasRefreshToken() const;
    bool isAuthenticated() const { return hasRefreshToken(); }

    void setClientCredentials(const QString& clientId, const QString& clientSecret);
    QString clientId() const;

    // Opens the system browser and waits for the loopback redirect.
    void startInteractiveLogin();
    void cancelInteractiveLogin();
    void signOut();

    // Guarantees a fresh access token (refreshing if needed) then invokes cb.
    void withAccessToken(TokenCallback cb);
    void invalidateAccessToken() { m_accessToken.clear(); m_expiresAt = QDateTime(); }

Q_SIGNALS:
    void loginStarted(const QUrl& authUrl);
    void authenticated();
    void authFailed(const QString& error);
    void signedOut();

private:
    void onLoopbackConnection();
    void exchangeCode(const QString& code);
    void refreshAccessToken(TokenCallback cb);
    void handleTokenResponse(const QByteArray& body, int status, TokenCallback cb, bool fromRefresh);
    static QString randomString(int len);

    Settings* m_settings;
    QNetworkAccessManager* m_nam;
    QTcpServer* m_server = nullptr;
    QString m_codeVerifier;
    QString m_state;
    QString m_redirectUri;
    QString m_accessToken;
    QDateTime m_expiresAt;
    QList<TokenCallback> m_pending;   // callers waiting on an in-flight refresh
    bool m_refreshing = false;
};

} // namespace ponymail
