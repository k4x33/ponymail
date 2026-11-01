#include "SettingsDialogs.h"
#include "core/Settings.h"
#include "core/Paths.h"
#include "auth/OAuthManager.h"
#include "auth/CredentialStore.h"
#include "ai/ClaudeClient.h"
#include "storage/Database.h"
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QMessageBox>

namespace ponymail {

// ---------------------------------------------------------------------------
AccountDialog::AccountDialog(Settings* settings, OAuthManager* auth, ClaudeClient* ai, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_auth(auth), m_ai(ai)
{
    setWindowTitle(QStringLiteral("Account & API keys"));
    resize(560, 420);
    auto* layout = new QVBoxLayout(this);

    auto* g = new QLabel(QStringLiteral("<b>Google (Gmail API)</b><br><span style='color:#616161'>Use an OAuth client of type <i>Desktop app</i> from Google Cloud Console with the Gmail API enabled. The client secret and all tokens are kept in %1.</span>")
                             .arg(CredentialStore::isOsBacked() ? QStringLiteral("Windows Credential Manager") : QStringLiteral("the local development credential file")), this);
    g->setWordWrap(true);
    layout->addWidget(g);

    auto* form = new QFormLayout();
    m_userName = new QLineEdit(settings->userName(), this);
    m_clientId = new QLineEdit(settings->oauthClientId(), this);
    m_clientSecret = new QLineEdit(this);
    m_clientSecret->setEchoMode(QLineEdit::Password);
    m_clientSecret->setPlaceholderText(auth->hasCredentials() ? QStringLiteral("(stored — enter to replace)") : QStringLiteral("GOCSPX-…"));
    form->addRow(QStringLiteral("Display name"), m_userName);
    form->addRow(QStringLiteral("Client ID"), m_clientId);
    form->addRow(QStringLiteral("Client secret"), m_clientSecret);
    layout->addLayout(form);

    auto* row = new QHBoxLayout();
    m_state = new QLabel(this);
    m_signIn = new QPushButton(QStringLiteral("Sign in with Google"), this);
    m_signIn->setProperty("primary", true);
    m_signOut = new QPushButton(QStringLiteral("Sign out"), this);
    row->addWidget(m_state, 1); row->addWidget(m_signIn); row->addWidget(m_signOut);
    layout->addLayout(row);

    auto* c = new QLabel(QStringLiteral("<br><b>Claude (summaries, triage, drafting)</b>"), this);
    layout->addWidget(c);
    auto* form2 = new QFormLayout();
    m_apiKey = new QLineEdit(this);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(ai->hasApiKey() ? QStringLiteral("(stored — enter to replace)") : QStringLiteral("sk-ant-…"));
    m_model = new QComboBox(this);
    m_model->setEditable(true);
    m_model->addItems({QStringLiteral("claude-haiku-5-5"), QStringLiteral("claude-sonnet-5-5"), QStringLiteral("claude-opus-5-5")});
    m_model->setCurrentText(settings->aiModel());
    form2->addRow(QStringLiteral("API key"), m_apiKey);
    form2->addRow(QStringLiteral("Model"), m_model);
    layout->addLayout(form2);

    auto* path = new QLabel(QStringLiteral("<span style='color:#8A8886'>Local data: %1</span>").arg(Paths::dataDir().toHtmlEscaped()), this);
    layout->addWidget(path);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    auto save = [this]() {
        m_settings->setUserName(m_userName->text().trimmed());
        const QString secret = m_clientSecret->text().trimmed();
        if (!secret.isEmpty()) m_auth->setClientCredentials(m_clientId->text(), secret);
        else m_settings->setOauthClientId(m_clientId->text().trimmed());
        if (!m_apiKey->text().trimmed().isEmpty()) m_ai->setApiKey(m_apiKey->text().trimmed());
        m_settings->setAiModel(m_model->currentText().trimmed());
    };
    connect(buttons, &QDialogButtonBox::accepted, this, [this, save]() { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_signIn, &QPushButton::clicked, this, [this, save]() {
        save();
        if (!m_auth->hasCredentials()) { QMessageBox::warning(this, QStringLiteral("Missing credentials"), QStringLiteral("Enter the OAuth client ID and secret first.")); return; }
        Q_EMIT signInRequested();
        m_state->setText(QStringLiteral("Complete sign-in in your browser…"));
    });
    connect(m_signOut, &QPushButton::clicked, this, [this]() { Q_EMIT signOutRequested(); refreshState(); });
    connect(m_auth, &OAuthManager::authenticated, this, &AccountDialog::refreshState);
    connect(m_auth, &OAuthManager::signedOut, this, &AccountDialog::refreshState);
    connect(m_auth, &OAuthManager::authFailed, this, [this](const QString& e) { m_state->setText(QStringLiteral("<span style='color:#C4314B'>%1</span>").arg(e.toHtmlEscaped())); });
    refreshState();
}

void AccountDialog::refreshState()
{
    const bool in = m_auth->isAuthenticated();
    m_state->setText(in ? QStringLiteral("Signed in as <b>%1</b>").arg(m_settings->userEmail().toHtmlEscaped()) : QStringLiteral("Not signed in"));
    m_signOut->setEnabled(in);
}

// ---------------------------------------------------------------------------
AiWritingStyleDialog::AiWritingStyleDialog(Settings* settings, QWidget* parent) : QDialog(parent), m_settings(settings)
{
    setWindowTitle(QStringLiteral("AI Writing Style"));
    resize(560, 420);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral("Instructions the assistant follows whenever it drafts email for you. Describe tone, length, sign-off, words to avoid, formatting habits."), this);
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    layout->addWidget(hint);
    m_text = new QPlainTextEdit(settings->aiWritingStyle(), this);
    m_text->setPlaceholderText(QStringLiteral("e.g. Direct executive tone. Lead with the decision or ask. Short paragraphs, no bullet lists unless comparing options. No em dashes. Sign off with 'Thanks,\\nMike'."));
    layout->addWidget(m_text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { m_settings->setAiWritingStyle(m_text->toPlainText().trimmed()); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
BlockedSendersDialog::BlockedSendersDialog(Database* db, QWidget* parent) : QDialog(parent), m_db(db)
{
    setWindowTitle(QStringLiteral("Blocked Senders"));
    resize(600, 440);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral("Mail from these addresses or domains is moved to Deleted Items on arrival and tagged <b>Blocked</b>. Use <i>user@domain.com</i> for an address or <i>@domain.com</i> for a whole domain."), this);
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    layout->addWidget(hint);

    auto* addRow = new QHBoxLayout();
    m_add = new QLineEdit(this);
    m_add->setPlaceholderText(QStringLiteral("user@domain.com or @domain.com"));
    auto* addBtn = new QPushButton(QStringLiteral("Add"), this);
    addRow->addWidget(m_add, 1); addRow->addWidget(addBtn);
    layout->addLayout(addRow);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Pattern"), QStringLiteral("Type"), QStringLiteral("Blocked on")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    layout->addWidget(m_table, 1);

    auto* btns = new QHBoxLayout();
    auto* del = new QPushButton(QStringLiteral("Remove selected"), this);
    del->setProperty("danger", true);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    btns->addWidget(del); btns->addStretch(); btns->addWidget(close);
    layout->addLayout(btns);

    connect(addBtn, &QPushButton::clicked, this, [this]() { if (m_db->addBlockedSender(m_add->text())) { m_add->clear(); reload(); } });
    connect(m_add, &QLineEdit::returnPressed, addBtn, &QPushButton::click);
    connect(del, &QPushButton::clicked, this, [this]() {
        QSet<int> rows;
        for (auto* it : m_table->selectedItems()) rows.insert(it->row());
        for (int r : rows) m_db->removeBlockedSender(m_table->item(r, 0)->data(Qt::UserRole).toString());
        reload();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    reload();
}

void BlockedSendersDialog::reload()
{
    const auto list = m_db->blockedSenders();
    m_table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        auto* p = new QTableWidgetItem(list[i].pattern);
        p->setData(Qt::UserRole, list[i].id);
        m_table->setItem(i, 0, p);
        m_table->setItem(i, 1, new QTableWidgetItem(list[i].type));
        m_table->setItem(i, 2, new QTableWidgetItem(util::formatTimestamp(QDateTime::fromMSecsSinceEpoch(list[i].createdAt))));
    }
}

// ---------------------------------------------------------------------------
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About PonyMail"));
    auto* layout = new QVBoxLayout(this);
    auto* l = new QLabel(QStringLiteral(
        "<h2 style='margin:0'>PonyMail</h2>"
        "<p>Version %1 &nbsp;·&nbsp; native Windows x64 build (Qt %2)</p>"
        "<p style='color:#616161'>Connects directly to the Gmail REST API over OAuth 2.0 + PKCE and to the Claude API. "
        "No intermediary servers, no telemetry, no read receipts, no tracking pixels. All state is stored locally in SQLite, "
        "settings.json and the OS credential store.</p>"
        "<p style='color:#8A8886'>Data folder: %3</p>").arg(QStringLiteral(PONYMAIL_VERSION), QStringLiteral(QT_VERSION_STR), Paths::dataDir().toHtmlEscaped()), this);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(l);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace ponymail
