#pragma once
#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QComboBox;
class QPushButton;

namespace ponymail {

class Settings;
class OAuthManager;
class ClaudeClient;
class Database;

// Account: Google OAuth client credentials, sign-in state, Claude API key + model.
class AccountDialog : public QDialog {
    Q_OBJECT
public:
    AccountDialog(Settings* settings, OAuthManager* auth, ClaudeClient* ai, QWidget* parent = nullptr);
Q_SIGNALS:
    void signInRequested();
    void signOutRequested();
private:
    void refreshState();
    Settings* m_settings; OAuthManager* m_auth; ClaudeClient* m_ai;
    QLineEdit *m_clientId, *m_clientSecret, *m_apiKey, *m_userName;
    QComboBox* m_model;
    QLabel* m_state;
    QPushButton *m_signIn, *m_signOut;
};

class AiWritingStyleDialog : public QDialog {
    Q_OBJECT
public:
    AiWritingStyleDialog(Settings* settings, QWidget* parent = nullptr);
private:
    Settings* m_settings;
    QPlainTextEdit* m_text;
};

class BlockedSendersDialog : public QDialog {
    Q_OBJECT
public:
    BlockedSendersDialog(Database* db, QWidget* parent = nullptr);
private:
    void reload();
    Database* m_db;
    QTableWidget* m_table;
    QLineEdit* m_add;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace ponymail
