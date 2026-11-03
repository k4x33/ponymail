#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <functional>

class QTcpServer;

namespace ponymail {

class Settings;

// Account providers the "Add an account" flow offers.
enum class Provider { None, Google, Microsoft };
QString providerKey(Provider p);          // "google" | "microsoft"
Provider providerFromKey(const QString& k);
QString providerDisplayName(Provider p);  // "Google" | "Outlook"

// OAuth 2.0 Authorization Code flow with PKCE and a loopback redirect
// (RFC 8252) for Google and for the Microsoft identity platform. The refresh
// token lives in the OS credential store; the access token is held in memory
// only and refreshed on demand.
//
// Client IDs are normally baked in at build time (PONYMAIL_GOOGLE_CLIENT_ID,
// PONYMAIL_GOOGLE_CLIENT_SECRET, PONYMAIL_MS_CLIENT_ID) so the user only has to
// pick a provider and sign in. When a build lacks them, the setup dialog asks
// for a developer client once and stores it locally.
class OAuthManager : public QObject {
    Q_OBJECT
public:
    using TokenCallback = std::function<void(const QString& accessToken, const QString& error)>;

    explicit OAuthManager(Settings* settings, QNetworkAccessManager* nam, QObject* parent = nullptr);

    Provider activeProvider() const;                // provider of the signed-in account (None when signed out)
    bool isAuthenticated() const { return activeProvider() != Provider::None && hasRefreshToken(activeProvider()); }
    bool hasRefreshToken(Provider p) const;

    // Client configuration
    bool hasClientConfig(Provider p) const;         // built-in or user-supplied client available
    bool hasBuiltInClient(Provider p) const;
    QString clientId(Provider p) const;
    QString clientSecret(Provider p) const;         // Google only; Microsoft public clients have none
    void setUserClient(Provider p, const QString& clientId, const QString& clientSecret);
    QString userClientId(Provider p) const;        // user-supplied id only (empty when relying on built-in)
    void clearUserClient(Provider p);

    // Opens the system browser and waits for the loopback redirect.
    void startInteractiveLogin(Provider p);
    void cancelInteractiveLogin();
    void signOut();

    // Guarantees a fresh access token for the active provider, then invokes cb.
    void withAccessToken(TokenCallback cb);
    void invalidateAccessToken() { m_accessToken.clear(); m_expiresAt = QDateTime(); }

Q_SIGNALS:
    void loginStarted(const QUrl& authUrl);
    void authenticated(Provider provider);
    void authFailed(const QString& error);
    void signedOut();

private:
    struct Endpoints { QString authorize, token, revoke, scope; bool needsSecret; };
    static Endpoints endpoints(Provider p);
    void onLoopbackConnection();
    void exchangeCode(const QString& code);
    void refreshAccessToken(TokenCallback cb);
    void handleTokenResponse(Provider p, const QByteArray& body, int status, TokenCallback cb, bool fromRefresh);
    static QString randomString(int len);
    static QString refreshKey(Provider p) { return providerKey(p) + QStringLiteral(".refresh_token"); }
    static QString secretKey(Provider p) { return providerKey(p) + QStringLiteral(".client_secret"); }

    Settings* m_settings;
    QNetworkAccessManager* m_nam;
    QTcpServer* m_server = nullptr;
    Provider m_pendingProvider = Provider::None;
    QString m_codeVerifier, m_state, m_redirectUri;
    QString m_accessToken;
    QDateTime m_expiresAt;
    QList<TokenCallback> m_pending;
    bool m_refreshing = false;
};

} // namespace ponymail
