#pragma once
#include "model/Models.h"
#include "model/SenderResolver.h"
#include "gmail/MimeBuilder.h"
#include "Theme.h"
#include <QMainWindow>
#include <QTimer>

class QLineEdit;
class QLabel;
class QListWidget;
class QWebEngineView;
class QWebChannel;
class QToolButton;

namespace ponymail {

class Database;
class GmailClient;
class SyncEngine;
class Settings;
class ClaudeClient;
class RecipientField;
class MailStore;

// Everything needed to open a composer.
struct ComposeContext {
    QString mode;            // new | reply | reply_all | forward | draft
    QString threadId;
    QString originalMessageId;
    QString draftId;         // when editing an existing Gmail draft
    QList<EmailAddress> to, cc, bcc;
    QString subject;
    QString bodyHtml;        // editable content
    QString quoteHtml;       // quoted conversation shown below the editable content
    QString inReplyTo, references;
    QList<Attachment> forwardAttachments;   // fetched from Gmail at send time
    QString threadContextText;              // plain text for the AI
};

class ComposeBridge : public QObject {
    Q_OBJECT
public:
    explicit ComposeBridge(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void ready() { Q_EMIT pageReady(); }
    Q_INVOKABLE void contentChanged() { Q_EMIT edited(); }
Q_SIGNALS:
    void pageReady();
    void edited();
};

class ComposeWindow : public QMainWindow {
    Q_OBJECT
public:
    ComposeWindow(const ComposeContext& ctx, Database* db, GmailClient* gmail, SyncEngine* sync, Settings* settings,
                  ClaudeClient* ai, MailStore* store, const Theme& theme, QWidget* parent = nullptr);
    ~ComposeWindow() override;

    static ComposeContext contextForReply(const Thread& t, const Message& m, bool replyAll, const SenderResolver& who);
    static ComposeContext contextForForward(const Thread& t, const Message& m, const SenderResolver& who);
    static ComposeContext contextForDraft(const Thread& t, const Message& m, const QString& draftId);
    static QString quoteHeader(const Message& m);
    static QString threadPlainText(const Thread& t);

Q_SIGNALS:
    void sent(const QString& threadId);
    void statusMessage(const QString& msg);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi();
    void applyTheme();
    void scheduleAutosave();
    void saveDraft(std::function<void(bool)> done = nullptr);
    void send();
    void discard();
    void attachFiles();
    void openAiAssist();
    void collectOutgoing(std::function<void(OutgoingMessage, bool ok)> cb);
    void withBodyHtml(std::function<void(const QString&)> cb);
    void fetchForwardAttachments(std::function<void(QList<OutgoingPart>)> cb);
    static void extractInlineImages(QString& html, QList<OutgoingPart>& images);

    ComposeContext m_ctx;
    Database* m_db; GmailClient* m_gmail; SyncEngine* m_sync; Settings* m_settings; ClaudeClient* m_ai; MailStore* m_store;
    Theme m_theme;
    RecipientField *m_to, *m_cc, *m_bcc;
    QLineEdit* m_subject;
    QLabel* m_from;
    QListWidget* m_attachList;
    QWebEngineView* m_editor;
    QWebChannel* m_channel;
    ComposeBridge* m_bridge;
    QToolButton *m_ccBtn, *m_bccBtn;
    QTimer m_autosave;
    QList<OutgoingPart> m_localAttachments;
    bool m_pageReady = false;
    bool m_dirty = false;
    bool m_saving = false;
    bool m_sending = false;
    bool m_discarded = false;
    bool m_closeAfterSave = false;
    QString m_draftId;
};

} // namespace ponymail
