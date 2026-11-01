#pragma once
#include "ai/ClaudeClient.h"
#include <QDialog>

class QPlainTextEdit;
class QLabel;
class QPushButton;
class QWebEngineView;

namespace ponymail {

class Settings;

// "Write / Reply / Forward with AI" dialog (spec §5.2). The user types direction
// notes; the model rewrites them into prose in the configured writing style.
class AiDialog : public QDialog {
    Q_OBJECT
public:
    AiDialog(ClaudeClient* ai, Settings* settings, const QString& mode, const QString& subject,
             const QString& threadContext, QWidget* parent = nullptr);

Q_SIGNALS:
    // insertMode: reply | reply_all | forward | new
    void insertRequested(const QString& insertMode, const ClaudeClient::DraftResult& result);

private:
    void generate();
    void onGenerated(const ClaudeClient::DraftResult& r, const QString& err);
    void setBusy(bool b);

    ClaudeClient* m_ai;
    Settings* m_settings;
    QString m_mode, m_subject, m_context;
    QPlainTextEdit* m_direction;
    QWebEngineView* m_preview;
    QLabel* m_status;
    QPushButton *m_generate, *m_reply, *m_replyAll, *m_forward, *m_new;
    ClaudeClient::DraftResult m_result;
};

} // namespace ponymail
