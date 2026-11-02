#include "ComposeWindow.h"
#include <algorithm>
#include "RecipientField.h"
#include "AiDialog.h"
#include "Icons.h"
#include "storage/Database.h"
#include "gmail/GmailClient.h"
#include "gmail/MimeMessage.h"
#include "sync/SyncEngine.h"
#include "core/Settings.h"
#include "ai/ClaudeClient.h"
#include "model/MailStore.h"
#include <QToolBar>
#include <QToolButton>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebChannel>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMessageBox>
#include <QCloseEvent>
#include <QShortcut>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTime>
#include <QUuid>
#include <QStatusBar>
#include <memory>

namespace ponymail {

// ---------------------------------------------------------------------------
// Context builders
// ---------------------------------------------------------------------------
QString ComposeWindow::quoteHeader(const Message& m)
{
    return QStringLiteral("On %1, %2 wrote:").arg(util::formatTimestamp(m.date), util::htmlEscape(m.from.rfc822()));
}

QString ComposeWindow::threadPlainText(const Thread& t)
{
    QString out;
    for (const auto& m : t.messages)
        out += QStringLiteral("From: %1\nDate: %2\n\n%3\n\n----\n").arg(m.from.rfc822(), util::formatTimestamp(m.date), MimeMessage::plainTextForAi(m, 3000));
    return out;
}

static QString sanitizedBodyForQuote(const Message& m)
{
    QString html = MimeMessage::displayHtml(m);
    static const QRegularExpression scripts(QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression onAttrs(QStringLiteral("\\s+on[a-z]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"), QRegularExpression::CaseInsensitiveOption);
    html.remove(scripts); html.remove(onAttrs);
    return html;
}

ComposeContext ComposeWindow::contextForReply(const Thread& t, const Message& m, bool replyAll, const SenderResolver& who)
{
    ComposeContext c;
    c.mode = replyAll ? QStringLiteral("reply_all") : QStringLiteral("reply");
    c.threadId = t.id;
    c.originalMessageId = m.id;
    who.replyRecipients(m, replyAll, c.to, c.cc);
    QString subj = m.subject.trimmed();
    if (!subj.startsWith(QStringLiteral("Re:"), Qt::CaseInsensitive)) subj = QStringLiteral("Re: ") + subj;
    c.subject = subj;
    c.inReplyTo = m.messageIdHeader;
    c.references = (m.references + QLatin1Char(' ') + m.messageIdHeader).trimmed();
    c.quoteHtml = QStringLiteral("<div>%1</div><blockquote style=\"border-left:3px solid #ccc;margin:8px 0;padding-left:10px\">%2</blockquote>")
                      .arg(quoteHeader(m), sanitizedBodyForQuote(m));
    c.threadContextText = threadPlainText(t);
    return c;
}

ComposeContext ComposeWindow::contextForForward(const Thread& t, const Message& m, const SenderResolver&)
{
    ComposeContext c;
    c.mode = QStringLiteral("forward");
    c.threadId = t.id;
    c.originalMessageId = m.id;
    QString subj = m.subject.trimmed();
    if (!subj.startsWith(QStringLiteral("Fwd:"), Qt::CaseInsensitive) && !subj.startsWith(QStringLiteral("FW:"), Qt::CaseInsensitive)) subj = QStringLiteral("Fwd: ") + subj;
    c.subject = subj;
    QStringList to; for (const auto& a : m.to) to << util::htmlEscape(a.rfc822());
    c.quoteHtml = QStringLiteral("<div>---------- Forwarded message ---------<br>From: %1<br>Date: %2<br>Subject: %3<br>To: %4</div><br>%5")
                      .arg(util::htmlEscape(m.from.rfc822()), util::formatTimestamp(m.date), util::htmlEscape(m.subject), to.join(QStringLiteral(", ")), sanitizedBodyForQuote(m));
    for (const auto& a : m.attachments) if (!a.isInline && !a.attachmentId.isEmpty()) c.forwardAttachments << a;
    c.threadContextText = threadPlainText(t);
    return c;
}

ComposeContext ComposeWindow::contextForDraft(const Thread& t, const Message& m, const QString& draftId)
{
    ComposeContext c;
    c.mode = QStringLiteral("draft");
    c.threadId = t.messages.size() > 1 ? t.id : QString();
    c.draftId = draftId;
    c.originalMessageId = m.id;
    c.to = m.to; c.cc = m.cc; c.bcc = m.bcc;
    c.subject = m.subject;
    c.bodyHtml = MimeMessage::displayHtml(m);
    c.inReplyTo = m.inReplyTo; c.references = m.references;
    c.threadContextText = threadPlainText(t);
    return c;
}

// ---------------------------------------------------------------------------
ComposeWindow::ComposeWindow(const ComposeContext& ctx, Database* db, GmailClient* gmail, SyncEngine* sync, Settings* settings,
                             ClaudeClient* ai, MailStore* store, const Theme& theme, QWidget* parent)
    : QMainWindow(parent), m_ctx(ctx), m_db(db), m_gmail(gmail), m_sync(sync), m_settings(settings), m_ai(ai), m_store(store), m_theme(theme)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(ctx.subject.isEmpty() ? QStringLiteral("New message — PonyMail") : ctx.subject + QStringLiteral(" — PonyMail"));
    resize(860, 720);
    m_draftId = ctx.draftId;
    buildUi();
    applyTheme();
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(1500);
    connect(&m_autosave, &QTimer::timeout, this, [this]() { saveDraft(); });
}

ComposeWindow::~ComposeWindow() = default;

void ComposeWindow::buildUi()
{
    auto* tb = addToolBar(QStringLiteral("Compose"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(18, 18));
    QAction* sendA = tb->addAction(Icons::icon(QStringLiteral("send"), m_theme.accent), QStringLiteral("Send"));
    sendA->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    sendA->setToolTip(QStringLiteral("Send (Ctrl+Enter)"));
    QAction* attachA = tb->addAction(Icons::icon(QStringLiteral("attach"), m_theme.textMuted), QStringLiteral("Attach"));
    QAction* discardA = tb->addAction(Icons::icon(QStringLiteral("discard"), m_theme.danger), QStringLiteral("Discard"));
    tb->addSeparator();
    QAction* aiA = tb->addAction(Icons::icon(QStringLiteral("wand"), QColor(0x7C, 0x3A, 0xED)), QStringLiteral("AI Assist"));
    connect(sendA, &QAction::triggered, this, &ComposeWindow::send);
    connect(attachA, &QAction::triggered, this, &ComposeWindow::attachFiles);
    connect(discardA, &QAction::triggered, this, &ComposeWindow::discard);
    connect(aiA, &QAction::triggered, this, &ComposeWindow::openAiAssist);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(central);
    auto* hl = new QVBoxLayout(header);
    hl->setContentsMargins(16, 10, 16, 6);
    hl->setSpacing(2);

    auto* fromRow = new QHBoxLayout();
    m_from = new QLabel(QStringLiteral("From   %1 <%2>").arg(m_settings->userName(), m_settings->userEmail()), header);
    m_from->setProperty("muted", true);
    fromRow->addWidget(m_from, 1);
    m_ccBtn = new QToolButton(header); m_ccBtn->setText(QStringLiteral("Cc")); m_ccBtn->setAutoRaise(true);
    m_bccBtn = new QToolButton(header); m_bccBtn->setText(QStringLiteral("Bcc")); m_bccBtn->setAutoRaise(true);
    fromRow->addWidget(m_ccBtn); fromRow->addWidget(m_bccBtn);
    hl->addLayout(fromRow);

    m_to = new RecipientField(QStringLiteral("To"), m_db, header);
    m_cc = new RecipientField(QStringLiteral("Cc"), m_db, header);
    m_bcc = new RecipientField(QStringLiteral("Bcc"), m_db, header);
    hl->addWidget(m_to); hl->addWidget(m_cc); hl->addWidget(m_bcc);
    m_cc->setVisible(!m_ctx.cc.isEmpty());
    m_bcc->setVisible(!m_ctx.bcc.isEmpty());
    connect(m_ccBtn, &QToolButton::clicked, this, [this]() { m_cc->setVisible(!m_cc->isVisible()); if (m_cc->isVisible()) m_cc->editor()->setFocus(); });
    connect(m_bccBtn, &QToolButton::clicked, this, [this]() { m_bcc->setVisible(!m_bcc->isVisible()); if (m_bcc->isVisible()) m_bcc->editor()->setFocus(); });

    m_subject = new QLineEdit(header);
    m_subject->setPlaceholderText(QStringLiteral("Add a subject"));
    m_subject->setStyleSheet(QStringLiteral("QLineEdit{border:none;border-bottom:1px solid %1;border-radius:0;font-size:14px;padding:8px 2px;}").arg(m_theme.border.name()));
    hl->addWidget(m_subject);

    m_attachList = new QListWidget(header);
    m_attachList->setObjectName(QStringLiteral("attachList"));
    m_attachList->setFlow(QListView::LeftToRight);
    m_attachList->setWrapping(true);
    m_attachList->setFixedHeight(0);
    m_attachList->setVisible(false);
    m_attachList->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* rm = new QAction(QStringLiteral("Remove attachment"), m_attachList);
    connect(rm, &QAction::triggered, this, [this]() {
        for (auto* it : m_attachList->selectedItems()) {
            const QString name = it->data(Qt::UserRole).toString();
            m_localAttachments.erase(std::remove_if(m_localAttachments.begin(), m_localAttachments.end(), [&](const OutgoingPart& p) { return p.filename == name; }), m_localAttachments.end());
            m_ctx.forwardAttachments.erase(std::remove_if(m_ctx.forwardAttachments.begin(), m_ctx.forwardAttachments.end(), [&](const Attachment& a) { return a.filename == name; }), m_ctx.forwardAttachments.end());
            delete it;
        }
        m_attachList->setVisible(m_attachList->count() > 0);
        m_attachList->setFixedHeight(m_attachList->count() > 0 ? 44 : 0);
        scheduleAutosave();
    });
    m_attachList->addAction(rm);
    hl->addWidget(m_attachList);
    layout->addWidget(header);

    m_editor = new QWebEngineView(central);
    auto* profile = new QWebEngineProfile(this);
    auto* page = new QWebEnginePage(profile, this);
    m_editor->setPage(page);
    m_editor->setContextMenuPolicy(Qt::NoContextMenu);
    m_channel = new QWebChannel(this);
    m_bridge = new ComposeBridge(this);
    m_channel->registerObject(QStringLiteral("bridge"), m_bridge);
    page->setWebChannel(m_channel);
    layout->addWidget(m_editor, 1);
    setCentralWidget(central);
    statusBar()->showMessage(m_draftId.isEmpty() ? QStringLiteral("Not saved") : QStringLiteral("Draft loaded"));

    // Seed fields
    m_to->setAddresses(m_ctx.to);
    m_cc->setAddresses(m_ctx.cc);
    m_bcc->setAddresses(m_ctx.bcc);
    m_subject->setText(m_ctx.subject);
    for (const auto& a : m_ctx.forwardAttachments) {
        auto* it = new QListWidgetItem(Icons::icon(QStringLiteral("paperclip"), m_theme.textMuted, 14), a.filename, m_attachList);
        it->setData(Qt::UserRole, a.filename);
    }
    if (m_attachList->count() > 0) { m_attachList->setVisible(true); m_attachList->setFixedHeight(44); }

    connect(m_bridge, &ComposeBridge::pageReady, this, [this]() {
        m_pageReady = true;
        const QString body = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("b"), m_ctx.bodyHtml}, {QStringLiteral("q"), m_ctx.quoteHtml}}).toJson(QJsonDocument::Compact));
        m_editor->page()->runJavaScript(QStringLiteral("(function(d){setContent(d.b, d.q);})(%1)").arg(body));
        if (m_ctx.mode == QLatin1String("new")) m_to->editor()->setFocus();
    });
    connect(m_bridge, &ComposeBridge::edited, this, [this]() { scheduleAutosave(); });
    for (auto* f : {m_to, m_cc, m_bcc}) connect(f, &RecipientField::changed, this, [this]() { scheduleAutosave(); });
    connect(m_subject, &QLineEdit::textEdited, this, [this]() { scheduleAutosave(); });

    QFile f(QStringLiteral(":/compose.html"));
    f.open(QIODevice::ReadOnly);
    QString html = QString::fromUtf8(f.readAll());
    html.replace(QStringLiteral("/*THEME*/"), m_theme.webCss());
    m_editor->setHtml(html, QUrl(QStringLiteral("qrc:///compose.html")));
}

void ComposeWindow::applyTheme() { setStyleSheet(m_theme.stylesheet()); }

void ComposeWindow::scheduleAutosave()
{
    m_dirty = true;                     // first character edited → draft autosave armed
    if (!m_sending && !m_discarded) m_autosave.start();
    statusBar()->showMessage(QStringLiteral("Unsaved changes…"));
}

void ComposeWindow::withBodyHtml(std::function<void(const QString&)> cb)
{
    if (!m_pageReady) { cb(m_ctx.bodyHtml); return; }
    m_editor->page()->runJavaScript(QStringLiteral("getHtml()"), [cb](const QVariant& v) { cb(v.toString()); });
}

void ComposeWindow::extractInlineImages(QString& html, QList<OutgoingPart>& images)
{
    static const QRegularExpression dataImg(QStringLiteral("<img\\b([^>]*?)\\ssrc\\s*=\\s*\"data:(image/[a-zA-Z0-9.+-]+);base64,([^\"]+)\""), QRegularExpression::CaseInsensitiveOption);
    int offset = 0, n = 0;
    QString out;
    auto it = dataImg.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        ++n;
        OutgoingPart p;
        p.mimeType = m.captured(2).toLower();
        p.data = QByteArray::fromBase64(m.captured(3).toLatin1());
        p.contentId = QStringLiteral("img%1-%2@ponymail").arg(n).arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
        p.filename = QStringLiteral("image%1.%2").arg(n).arg(p.mimeType.section('/', 1).replace(QStringLiteral("jpeg"), QStringLiteral("jpg")));
        images << p;
        out += html.mid(offset, m.capturedStart() - offset);
        out += QStringLiteral("<img%1 src=\"cid:%2\"").arg(m.captured(1), p.contentId);
        offset = m.capturedEnd();
    }
    out += html.mid(offset);
    html = out;
}

void ComposeWindow::fetchForwardAttachments(std::function<void(QList<OutgoingPart>)> cb)
{
    if (m_ctx.forwardAttachments.isEmpty()) { cb({}); return; }
    auto parts = std::make_shared<QList<OutgoingPart>>();
    auto remaining = std::make_shared<int>(m_ctx.forwardAttachments.size());
    for (const auto& a : m_ctx.forwardAttachments) {
        m_gmail->getAttachment(m_ctx.originalMessageId, a.attachmentId, [parts, remaining, a, cb](const QByteArray& data, int status, const QString&) {
            if (status == 200) {
                OutgoingPart p; p.filename = a.filename; p.mimeType = a.mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : a.mimeType;
                p.data = util::fromBase64Url(QJsonDocument::fromJson(data).object().value(QStringLiteral("data")).toString().toLatin1());
                parts->append(p);
            }
            if (--*remaining == 0) cb(*parts);
        });
    }
}

void ComposeWindow::collectOutgoing(std::function<void(OutgoingMessage, bool)> cb)
{
    m_to->commitPendingText(); m_cc->commitPendingText(); m_bcc->commitPendingText();
    withBodyHtml([this, cb](const QString& htmlIn) {
        OutgoingMessage msg;
        msg.from = EmailAddress{m_settings->userName(), m_settings->userEmail()};
        msg.to = m_to->addresses(); msg.cc = m_cc->addresses(); msg.bcc = m_bcc->addresses();
        msg.subject = m_subject->text();
        msg.inReplyTo = m_ctx.inReplyTo; msg.references = m_ctx.references;
        QString html = htmlIn;
        extractInlineImages(html, msg.inlineImages);
        msg.html = QStringLiteral("<div style=\"font-family:'Helvetica Neue',Helvetica,Arial,sans-serif;font-size:11pt;max-width:600px\">%1</div>").arg(html);
        msg.attachments = m_localAttachments;
        fetchForwardAttachments([msg, cb](QList<OutgoingPart> fwd) mutable { msg.attachments += fwd; cb(msg, true); });
    });
}

void ComposeWindow::saveDraft(std::function<void(bool)> done)
{
    if (m_saving || m_sending || m_discarded || !m_dirty) { if (done) done(false); return; }
    m_saving = true;
    collectOutgoing([this, done](OutgoingMessage msg, bool) {
        const QByteArray raw = MimeBuilder::buildBase64Url(msg);
        auto finish = [this, done](const QJsonObject& o, int status, const QString& err) {
            m_saving = false;
            if (status == 200) {
                m_draftId = o.value(QStringLiteral("id")).toString();
                m_dirty = false;
                statusBar()->showMessage(QStringLiteral("Draft saved ") + QTime::currentTime().toString(QStringLiteral("HH:mm")));
                if (done) done(true);
            } else {
                statusBar()->showMessage(QStringLiteral("Draft save failed: ") + err);
                if (done) done(false);
            }
            if (m_closeAfterSave && !done) { m_dirty = false; close(); return; }
            if (m_dirty) m_autosave.start();
        };
        if (m_draftId.isEmpty()) m_gmail->createDraft(raw, m_ctx.threadId, finish);
        else m_gmail->updateDraft(m_draftId, raw, m_ctx.threadId, finish);
    });
}

void ComposeWindow::send()
{
    if (m_sending) return;
    m_to->commitPendingText(); m_cc->commitPendingText(); m_bcc->commitPendingText();
    if (m_to->isEmpty() && m_cc->isEmpty() && m_bcc->isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No recipients"), QStringLiteral("Add at least one recipient before sending."));
        return;
    }
    if (m_subject->text().trimmed().isEmpty()) {
        if (QMessageBox::question(this, QStringLiteral("Send without subject?"), QStringLiteral("This message has no subject. Send anyway?")) != QMessageBox::Yes) return;
    }
    m_sending = true;
    m_autosave.stop();
    statusBar()->showMessage(QStringLiteral("Sending…"));
    collectOutgoing([this](OutgoingMessage msg, bool) {
        const QByteArray raw = MimeBuilder::buildBase64Url(msg);
        m_gmail->sendMessage(raw, m_ctx.threadId, [this](const QJsonObject& o, int status, const QString& err) {
            if (status != 200) {
                m_sending = false;
                statusBar()->showMessage(QStringLiteral("Send failed: ") + err);
                QMessageBox::critical(this, QStringLiteral("Send failed"), err);
                return;
            }
            const QString sentThreadId = o.value(QStringLiteral("threadId")).toString();
            if (!m_draftId.isEmpty()) m_gmail->deleteDraft(m_draftId, [](const QJsonObject&, int, const QString&) {});
            if (!m_ctx.threadId.isEmpty() && (m_ctx.mode == QLatin1String("reply") || m_ctx.mode == QLatin1String("reply_all")))
                m_sync->markRead({m_ctx.threadId}, true);                 // replying marks the thread read
            m_sync->afterSend();                                           // Sent sweep now, +3 s, +10 s
            Q_EMIT sent(sentThreadId);
            Q_EMIT statusMessage(QStringLiteral("Message sent."));
            m_discarded = true;   // prevent close-time autosave
            close();
        });
    });
}

void ComposeWindow::discard()
{
    if (QMessageBox::question(this, QStringLiteral("Discard draft?"), QStringLiteral("Discard this message and delete the draft?")) != QMessageBox::Yes) return;
    m_discarded = true;
    m_autosave.stop();
    if (!m_draftId.isEmpty()) {
        SyncEngine* sync = m_sync;
        const bool wasDraft = m_ctx.mode == QLatin1String("draft");
        m_gmail->deleteDraft(m_draftId, [sync, wasDraft](const QJsonObject&, int, const QString&) { if (wasDraft) sync->refreshActiveFolder(true); });
    }
    close();
}

void ComposeWindow::attachFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Attach files"));
    QMimeDatabase mdb;
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        OutgoingPart p;
        p.filename = QFileInfo(path).fileName();
        p.mimeType = mdb.mimeTypeForFile(path).name();
        p.data = f.readAll();
        if (p.data.size() > 24 * 1024 * 1024) { QMessageBox::warning(this, QStringLiteral("Too large"), p.filename + QStringLiteral(" exceeds Gmail's 25 MB limit.")); continue; }
        m_localAttachments << p;
        auto* it = new QListWidgetItem(Icons::icon(QStringLiteral("paperclip"), m_theme.textMuted, 14), p.filename, m_attachList);
        it->setData(Qt::UserRole, p.filename);
    }
    if (m_attachList->count() > 0) { m_attachList->setVisible(true); m_attachList->setFixedHeight(44); }
    scheduleAutosave();
}

void ComposeWindow::openAiAssist()
{
    QString mode = m_ctx.mode;
    if (mode == QLatin1String("draft")) mode = m_ctx.threadId.isEmpty() ? QStringLiteral("new") : QStringLiteral("reply");
    auto* dlg = new AiDialog(m_ai, m_settings, mode, m_subject->text(), m_ctx.threadContextText, this);
    connect(dlg, &AiDialog::insertRequested, this, [this](const QString& insertMode, const ClaudeClient::DraftResult& r) {
        if (m_subject->text().trimmed().isEmpty() || insertMode == QLatin1String("new")) if (!r.subject.isEmpty()) m_subject->setText(r.subject);
        const QString js = QStringLiteral("insertAiBody(%1)").arg(QString::fromUtf8(QJsonDocument(QJsonArray{r.bodyHtml}).toJson(QJsonDocument::Compact)).chopped(1).mid(1));
        m_editor->page()->runJavaScript(js);
        scheduleAutosave();
    });
    dlg->open();
}

void ComposeWindow::closeEvent(QCloseEvent* e)
{
    if (!m_discarded && !m_sending && (m_dirty || m_saving)) {
        // Flush a final autosave so nothing typed is lost; close once the save completes.
        hide();
        m_closeAfterSave = true;
        if (!m_saving) saveDraft([this](bool) { m_dirty = false; m_saving = false; close(); });
        e->ignore();
        return;
    }
    e->accept();
}

} // namespace ponymail
