#include "AiDialog.h"
#include "Icons.h"
#include "core/Settings.h"
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QKeyEvent>
#include <QPointer>

namespace ponymail {

AiDialog::AiDialog(ClaudeClient* ai, Settings* settings, const QString& mode, const QString& subject,
                   const QString& threadContext, QWidget* parent)
    : QDialog(parent), m_ai(ai), m_settings(settings), m_mode(mode), m_subject(subject), m_context(threadContext)
{
    setAttribute(Qt::WA_DeleteOnClose);
    const QString title = mode == QLatin1String("new") ? QStringLiteral("Write with AI")
                        : mode == QLatin1String("forward") ? QStringLiteral("Forward with AI") : QStringLiteral("Reply with AI");
    setWindowTitle(title);
    setWindowIcon(Icons::icon(QStringLiteral("wand"), QColor(0x7C, 0x3A, 0xED)));
    resize(720, 620);

    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("<span style='color:#7C3AED;font-size:16px'>&#10022;</span> <b>%1</b>").arg(title), this);
    layout->addWidget(heading);

    auto* hint = new QLabel(QStringLiteral("Direction: tell the assistant what to say. Your notes are rewritten into polished prose in your writing style — they are never pasted verbatim."), this);
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    layout->addWidget(hint);

    m_direction = new QPlainTextEdit(this);
    m_direction->setPlaceholderText(QStringLiteral("e.g. agree to Thursday, ask for the revised numbers by Wednesday, keep it short"));
    m_direction->setFixedHeight(96);
    layout->addWidget(m_direction);

    auto* genRow = new QHBoxLayout();
    m_generate = new QPushButton(Icons::icon(QStringLiteral("wand"), Qt::white), QStringLiteral("Generate"), this);
    m_generate->setProperty("primary", true);
    m_generate->setDefault(true);
    m_status = new QLabel(this);
    m_status->setProperty("muted", true);
    genRow->addWidget(m_generate);
    genRow->addWidget(m_status, 1);
    layout->addLayout(genRow);

    m_preview = new QWebEngineView(this);
    m_preview->setContextMenuPolicy(Qt::NoContextMenu);
    m_preview->setHtml(QStringLiteral("<html><body style='font-family:Segoe UI,sans-serif;color:#8A8886;padding:16px'>Generated draft appears here.</body></html>"));
    layout->addWidget(m_preview, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->addWidget(new QLabel(QStringLiteral("Insert as:"), this));
    m_reply = new QPushButton(QStringLiteral("Reply"), this);
    m_replyAll = new QPushButton(QStringLiteral("Reply All"), this);
    m_forward = new QPushButton(QStringLiteral("Forward"), this);
    m_new = new QPushButton(QStringLiteral("New email"), this);
    for (auto* b : {m_reply, m_replyAll, m_forward, m_new}) { b->setEnabled(false); btnRow->addWidget(b); }
    btnRow->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    btnRow->addWidget(cancel);
    layout->addLayout(btnRow);

    if (mode == QLatin1String("new")) { m_reply->hide(); m_replyAll->hide(); m_forward->hide(); }
    else if (mode == QLatin1String("forward")) { m_reply->hide(); m_replyAll->hide(); }

    connect(m_generate, &QPushButton::clicked, this, &AiDialog::generate);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto emitInsert = [this](const QString& m) { Q_EMIT insertRequested(m, m_result); accept(); };
    connect(m_reply, &QPushButton::clicked, this, [emitInsert]() { emitInsert(QStringLiteral("reply")); });
    connect(m_replyAll, &QPushButton::clicked, this, [emitInsert]() { emitInsert(QStringLiteral("reply_all")); });
    connect(m_forward, &QPushButton::clicked, this, [emitInsert]() { emitInsert(QStringLiteral("forward")); });
    connect(m_new, &QPushButton::clicked, this, [emitInsert]() { emitInsert(QStringLiteral("new")); });
    m_direction->setFocus();
}

void AiDialog::setBusy(bool b)
{
    m_generate->setEnabled(!b);
    m_direction->setEnabled(!b);
    m_status->setText(b ? QStringLiteral("Drafting…") : QString());
}

void AiDialog::generate()
{
    const QString direction = m_direction->toPlainText().trimmed();
    if (direction.isEmpty()) { m_status->setText(QStringLiteral("Add a direction first.")); return; }
    setBusy(true);
    ClaudeClient::DraftRequest req;
    req.mode = m_mode;
    req.direction = direction;
    req.writingStyle = m_settings->aiWritingStyle();
    req.userName = m_settings->userName();
    req.threadContext = m_context;
    req.subject = m_subject;
    req.wantSubject = m_subject.trimmed().isEmpty();
    QPointer<AiDialog> self(this);
    m_ai->draftEmail(req, [self](const ClaudeClient::DraftResult& r, const QString& err) {
        if (!self) return;
        AiDialog* const d = self.data();
        d->onGenerated(r, err);
    });
}

void AiDialog::onGenerated(const ClaudeClient::DraftResult& r, const QString& err)
{
    {
        setBusy(false);
        if (!err.isEmpty()) { m_status->setText(QStringLiteral("Error: ") + err); return; }
        m_result = r;
        if (m_result.subject.isEmpty()) m_result.subject = m_subject;
        m_preview->setHtml(QStringLiteral("<html><body style=\"font-family:'Helvetica Neue',Helvetica,Arial,sans-serif;font-size:11pt;color:#242424;padding:16px 24px;max-width:600px\">%1%2</body></html>")
                               .arg(m_subject.trimmed().isEmpty() ? QStringLiteral("<p style='color:#616161'><b>Subject:</b> %1</p>").arg(m_result.subject.toHtmlEscaped()) : QString(), m_result.bodyHtml));
        for (auto* b : {m_reply, m_replyAll, m_forward, m_new}) b->setEnabled(true);
        m_status->setText(QStringLiteral("Draft ready. Edit the direction and regenerate, or insert."));
    }
}

} // namespace ponymail
