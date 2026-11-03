#include "AccountSetupDialog.h"
#include "auth/CredentialStore.h"
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPainter>
#include <QPixmap>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QCloseEvent>
#include <functional>

namespace ponymail {

namespace {
QPixmap glyphPixmap(const QString& glyph, const QColor& bg, int size, qreal dpr)
{
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22);
    QFont f(QStringLiteral("Segoe UI"));
    f.setBold(true);
    f.setPixelSize(int(size * 0.55));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, glyph);
    return pm;
}
}

AccountSetupDialog::AccountSetupDialog(OAuthManager* auth, const Theme& theme, QWidget* parent)
    : QDialog(parent), m_auth(auth), m_theme(theme)
{
    setWindowTitle(QStringLiteral("Add an account"));
    setModal(true);
    resize(560, 520);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildPicker());
    m_stack->addWidget(buildSignIn());
    m_stack->addWidget(buildOther());
    layout->addWidget(m_stack);

    connect(m_auth, &OAuthManager::loginStarted, this, [this](const QUrl& u) {
        m_lastUrl = u;
        m_signInStatus->setText(QStringLiteral("Your browser has opened. Sign in to %1 and approve PonyMail's access, then return here.").arg(providerDisplayName(m_provider)));
        m_reopenBtn->setEnabled(true);
        m_copyBtn->setEnabled(true);
    });
    connect(m_auth, &OAuthManager::authenticated, this, [this](Provider p) {
        Q_EMIT accountAdded(p);
        accept();
    });
    connect(m_auth, &OAuthManager::authFailed, this, [this](const QString& err) {
        QString msg = QStringLiteral("<span style='color:%1'>Sign-in failed: %2</span>").arg(m_theme.danger.name(), err.toHtmlEscaped());
        if (m_devBox->isVisible()) msg += QStringLiteral("<br><span style='color:%1'>Check the client ID and secret above, then click Try again.</span>").arg(m_theme.textMuted.name());
        m_signInStatus->setText(msg);
        m_launchBtn->setEnabled(true);
        m_launchBtn->setText(QStringLiteral("Try again"));
        m_devClientSecret->setFocus();
    });
}

QWidget* AccountSetupDialog::providerCard(const QString& glyph, const QColor& glyphBg, const QString& title, const QString& subtitle, std::function<void()> onClick)
{
    auto* btn = new QToolButton(this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn->setFixedHeight(76);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton{background:%1;border:1px solid %2;border-radius:8px;text-align:left;padding:0;}"
        "QToolButton:hover{border-color:%3;background:%4;}").arg(m_theme.surface.name(), m_theme.border.name(), m_theme.accent.name(), m_theme.hover.name()));
    auto* l = new QHBoxLayout(btn);
    l->setContentsMargins(16, 10, 16, 10);
    l->setSpacing(14);
    auto* icon = new QLabel(btn);
    icon->setPixmap(glyphPixmap(glyph, glyphBg, 44, devicePixelRatioF()));
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* text = new QLabel(QStringLiteral("<div style='font-size:15px;font-weight:600'>%1</div><div style='color:%3;font-size:12px'>%2</div>").arg(title, subtitle, m_theme.textMuted.name()), btn);
    text->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* chev = new QLabel(QStringLiteral("<span style='color:%1;font-size:18px'>&#8250;</span>").arg(m_theme.textFaint.name()), btn);
    chev->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->addWidget(icon);
    l->addWidget(text, 1);
    l->addWidget(chev);
    connect(btn, &QToolButton::clicked, this, onClick);
    return btn;
}

QWidget* AccountSetupDialog::buildPicker()
{
    auto* page = new QWidget(this);
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(36, 32, 36, 28);
    l->setSpacing(12);
    auto* h = new QLabel(QStringLiteral("<div style='font-size:22px;font-weight:600'>Add an account</div>"
                                        "<div style='color:%1;margin-top:4px'>Choose the type of account you want to add to PonyMail.</div>").arg(m_theme.textMuted.name()), page);
    h->setWordWrap(true);
    l->addWidget(h);
    l->addSpacing(8);
    l->addWidget(providerCard(QStringLiteral("O"), QColor("#0F6CBD"), QStringLiteral("Outlook"),
                              QStringLiteral("Microsoft 365, Outlook.com, Hotmail, Live"), [this]() { choose(Provider::Microsoft); }));
    l->addWidget(providerCard(QStringLiteral("G"), QColor("#EA4335"), QStringLiteral("Google"),
                              QStringLiteral("Gmail and Google Workspace"), [this]() { choose(Provider::Google); }));
    l->addWidget(providerCard(QStringLiteral("@"), QColor("#64748B"), QStringLiteral("Other"),
                              QStringLiteral("IMAP, POP or Exchange account"), [this]() { m_stack->setCurrentIndex(2); }));
    l->addStretch();
    auto* foot = new QLabel(QStringLiteral("<span style='color:%1;font-size:12px'>Sign-in happens in your browser. PonyMail never sees your password; tokens are kept in %2.</span>").arg(m_theme.textFaint.name(), CredentialStore::storeDisplayName()), page);
    foot->setWordWrap(true);
    l->addWidget(foot);
    auto* row = new QHBoxLayout();
    row->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), page);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    row->addWidget(cancel);
    l->addLayout(row);
    return page;
}

QWidget* AccountSetupDialog::buildSignIn()
{
    auto* page = new QWidget(this);
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(36, 32, 36, 28);
    l->setSpacing(12);
    m_signInTitle = new QLabel(page);
    m_signInTitle->setWordWrap(true);
    l->addWidget(m_signInTitle);

    m_devBox = new QWidget(page);
    auto* dl = new QVBoxLayout(m_devBox);
    dl->setContentsMargins(0, 0, 0, 0);
    auto* devHint = new QLabel(m_devBox);
    devHint->setWordWrap(true);
    devHint->setObjectName(QStringLiteral("devHint"));
    dl->addWidget(devHint);
    auto* form = new QFormLayout();
    m_devClientId = new QLineEdit(m_devBox);
    m_devClientSecret = new QLineEdit(m_devBox);
    m_devClientSecret->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Client ID"), m_devClientId);
    m_devSecretLabel = new QLabel(QStringLiteral("Client secret"), m_devBox);
    form->addRow(m_devSecretLabel, m_devClientSecret);
    dl->addLayout(form);
    l->addWidget(m_devBox);

    m_signInStatus = new QLabel(page);
    m_signInStatus->setWordWrap(true);
    m_signInStatus->setProperty("muted", true);
    l->addWidget(m_signInStatus);
    l->addStretch();

    auto* row = new QHBoxLayout();
    auto* back = new QPushButton(QStringLiteral("Back"), page);
    connect(back, &QPushButton::clicked, this, [this]() { m_auth->cancelInteractiveLogin(); m_stack->setCurrentIndex(0); });
    m_clearClientBtn = new QPushButton(QStringLiteral("Clear saved client"), page);
    m_clearClientBtn->setProperty("danger", true);
    connect(m_clearClientBtn, &QPushButton::clicked, this, [this]() {
        m_auth->cancelInteractiveLogin();
        m_auth->clearUserClient(m_provider);
        m_devClientId->clear(); m_devClientSecret->clear();
        m_signInStatus->setText(QStringLiteral("Saved client removed. Enter the new client ID and secret."));
        m_launchBtn->setText(QStringLiteral("Continue")); m_launchBtn->setEnabled(true);
        choose(m_provider);
    });
    m_copyBtn = new QPushButton(QStringLiteral("Copy sign-in link"), page);
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(m_lastUrl.toString()); m_signInStatus->setText(QStringLiteral("Sign-in link copied. Paste it into any browser on this PC.")); });
    m_reopenBtn = new QPushButton(QStringLiteral("Open browser again"), page);
    m_reopenBtn->setEnabled(false);
    connect(m_reopenBtn, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(m_lastUrl); });
    m_launchBtn = new QPushButton(page);
    m_launchBtn->setProperty("primary", true);
    m_launchBtn->setDefault(true);
    connect(m_launchBtn, &QPushButton::clicked, this, &AccountSetupDialog::launch);
    row->addWidget(back);
    row->addWidget(m_clearClientBtn);
    row->addStretch();
    row->addWidget(m_copyBtn);
    row->addWidget(m_reopenBtn);
    row->addWidget(m_launchBtn);
    l->addLayout(row);
    return page;
}

QWidget* AccountSetupDialog::buildOther()
{
    auto* page = new QWidget(this);
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(36, 32, 36, 28);
    auto* h = new QLabel(QStringLiteral("<div style='font-size:22px;font-weight:600'>Other account</div>"
                                        "<div style='color:%1;margin-top:8px;line-height:1.5'>IMAP, POP and on-premises Exchange accounts are not supported in this build. "
                                        "PonyMail currently connects to Google and Outlook accounts through their OAuth sign-in.</div>").arg(m_theme.textMuted.name()), page);
    h->setWordWrap(true);
    l->addWidget(h);
    l->addStretch();
    auto* row = new QHBoxLayout();
    auto* back = new QPushButton(QStringLiteral("Back"), page);
    connect(back, &QPushButton::clicked, this, [this]() { m_stack->setCurrentIndex(0); });
    row->addWidget(back);
    row->addStretch();
    l->addLayout(row);
    return page;
}

void AccountSetupDialog::choose(Provider p)
{
    m_provider = p;
    const QString name = providerDisplayName(p);
    m_signInTitle->setText(QStringLiteral("<div style='font-size:22px;font-weight:600'>Sign in to %1</div>"
                                          "<div style='color:%2;margin-top:4px'>PonyMail will open your default browser to complete sign-in securely.</div>").arg(name, m_theme.textMuted.name()));
    // Built-in client: nothing to ask. User-supplied client: always show the box so a
    // wrong ID/secret can be corrected (prefilled ID, secret left blank = keep stored).
    const bool builtIn = m_auth->hasBuiltInClient(p) && m_auth->userClientId(p).isEmpty();
    const bool haveStored = m_auth->hasClientConfig(p);
    const bool needDev = !builtIn;
    m_devBox->setVisible(needDev);
    if (needDev) {
        auto* hint = m_devBox->findChild<QLabel*>(QStringLiteral("devHint"));
        const QString lead = haveStored ? QStringLiteral("<b>OAuth client.</b> Using your saved client. Edit either field to replace it; leave the secret blank to keep the stored one.")
                                        : QStringLiteral("<b>One-time setup.</b> This build has no built-in %1 client.").arg(providerDisplayName(p));
        if (p == Provider::Google)
            hint->setText(lead + QStringLiteral(" Paste the Client ID and secret of an OAuth client of type <i>Desktop app</i> from Google Cloud Console (Gmail API enabled). Stored locally only."));
        else
            hint->setText(lead + QStringLiteral(" Paste the Application (client) ID of an Entra app registration with a <i>Mobile and desktop</i> redirect of <code>http://localhost</code> and Microsoft Graph <i>Mail.ReadWrite</i>, <i>Mail.Send</i>, <i>offline_access</i> permissions."));
        m_devClientId->setText(m_auth->clientId(p));
        m_devClientSecret->clear();
        m_devClientSecret->setPlaceholderText(haveStored && p == Provider::Google ? QStringLiteral("(stored — enter to replace)") : QStringLiteral("GOCSPX-…"));
        m_devSecretLabel->setVisible(p == Provider::Google);
        m_devClientSecret->setVisible(p == Provider::Google);
    }
    m_signInStatus->setText(QStringLiteral("Click Continue to open the browser."));
    m_launchBtn->setText(QStringLiteral("Continue"));
    m_launchBtn->setEnabled(true);
    m_reopenBtn->setEnabled(false);
    m_copyBtn->setEnabled(false);
    m_clearClientBtn->setVisible(!m_auth->userClientId(p).isEmpty());
    m_stack->setCurrentIndex(1);
    if (builtIn) launch();
}

void AccountSetupDialog::launch()
{
    if (m_devBox->isVisible()) {
        if (m_devClientId->text().trimmed().isEmpty() || (m_provider == Provider::Google && m_devClientSecret->text().trimmed().isEmpty() && m_auth->clientSecret(m_provider).isEmpty())) {
            m_signInStatus->setText(QStringLiteral("<span style='color:%1'>Enter the client details first.</span>").arg(m_theme.danger.name()));
            return;
        }
        m_auth->setUserClient(m_provider, m_devClientId->text(), m_devClientSecret->text().trimmed().isEmpty() ? m_auth->clientSecret(m_provider) : m_devClientSecret->text());
    }
    m_launchBtn->setEnabled(false);
    m_launchBtn->setText(QStringLiteral("Waiting for sign-in…"));
    m_signInStatus->setText(QStringLiteral("Opening your browser…"));
    m_auth->startInteractiveLogin(m_provider);
}

} // namespace ponymail
