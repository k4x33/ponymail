#pragma once
#include "auth/OAuthManager.h"
#include "Theme.h"
#include <QDialog>
#include <QUrl>

class QStackedWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace ponymail {

// "Add an account" flow in the style of new Outlook:
//   1. pick a provider card (Outlook, Google, Other)
//   2. the system browser opens for OAuth; the dialog waits for the loopback redirect
// When the build has no baked-in client for the provider, step 2 first asks
// for a developer OAuth client (once; stored locally).
class AccountSetupDialog : public QDialog {
    Q_OBJECT
public:
    AccountSetupDialog(OAuthManager* auth, const Theme& theme, QWidget* parent = nullptr);

Q_SIGNALS:
    void accountAdded(Provider provider);

private:
    QWidget* buildPicker();
    QWidget* buildSignIn();
    QWidget* buildOther();
    QWidget* providerCard(const QString& glyph, const QColor& glyphBg, const QString& title, const QString& subtitle, std::function<void()> onClick);
    void choose(Provider p);
    void launch();

    OAuthManager* m_auth;
    Theme m_theme;
    Provider m_provider = Provider::None;
    QStackedWidget* m_stack;
    QLabel *m_signInTitle, *m_signInStatus;
    QWidget* m_devBox;
    QLineEdit *m_devClientId, *m_devClientSecret;
    QLabel* m_devSecretLabel;
    QPushButton *m_launchBtn, *m_reopenBtn, *m_copyBtn, *m_clearClientBtn;
    QUrl m_lastUrl;
};

} // namespace ponymail
