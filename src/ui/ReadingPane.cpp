#include "ReadingPane.h"
#include "storage/Database.h"
#include "gmail/GmailClient.h"
#include "gmail/MimeMessage.h"
#include "ai/SummaryQueue.h"
#include "ai/ClaudeClient.h"
#include "model/Triage.h"
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineNewWindowRequest>
#include <QWebChannel>
#include <QVBoxLayout>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QFileDialog>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>

namespace ponymail {

void RemoteBlocker::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    const QString scheme = info.requestUrl().scheme();
    if (scheme == QLatin1String("qrc") || scheme == QLatin1String("data") || scheme == QLatin1String("blob")) return;
    if (!allowRemote) info.block(true);
}

namespace {
// Link clicks inside a message open in the system browser; the pane itself never navigates.
class ReadingPage : public QWebEnginePage {
public:
    ReadingPage(QWebEngineProfile* profile, QObject* parent) : QWebEnginePage(profile, parent) {}
protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override
    {
        if (type == NavigationTypeLinkClicked && isMainFrame) { QDesktopServices::openUrl(url); return false; }
        if (isMainFrame && url.scheme() != QLatin1String("qrc") && url.scheme() != QLatin1String("data") && url.scheme() != QLatin1String("about")) return false;
        return true;
    }
};
}

ReadingPane::ReadingPane(MailStore* store, Database* db, GmailClient* gmail, SummaryQueue* queue, ClaudeClient* ai, QWidget* parent)
    : QWidget(parent), m_store(store), m_db(db), m_gmail(gmail), m_queue(queue), m_ai(ai)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_view = new QWebEngineView(this);
    layout->addWidget(m_view);

    auto* profile = new QWebEngineProfile(this);   // off-the-record: nothing persisted to disk
    m_blocker = new RemoteBlocker(this);
    profile->setUrlRequestInterceptor(m_blocker);
    profile->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    profile->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    profile->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    profile->settings()->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    auto* page = new ReadingPage(profile, this);
    m_view->setPage(page);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);

    m_channel = new QWebChannel(this);
    m_bridge = new ReadingBridge(this);
    m_channel->registerObject(QStringLiteral("bridge"), m_bridge);
    page->setWebChannel(m_channel);

    // target=_blank links open in the system browser.
    connect(page, &QWebEnginePage::newWindowRequested, this, [](QWebEngineNewWindowRequest& req) { QDesktopServices::openUrl(req.requestedUrl()); });

    connect(m_bridge, &ReadingBridge::pageReady, this, [this]() { m_pageReady = true; if (m_pendingRender) { m_pendingRender = false; refresh(); } });
    connect(m_bridge, &ReadingBridge::actionStatusChanged, this, [this](const QString& threadId, const QString&, const QString& text, const QString& status) {
        ActionItem item; item.text = text;
        m_db->upsertActionStatus(threadId, item, actionStatusFromKey(status));
        m_store->setStatuses(threadId, m_db->actionStatusesForThread(threadId));   // instantly re-triages the row
    });
    connect(m_bridge, &ReadingBridge::editDraftRequested, this, [this](const QString& messageId) { Q_EMIT editDraftRequested(m_threadId, messageId); });
    connect(m_bridge, &ReadingBridge::loadRemoteRequested, this, [this]() { m_remoteAllowed.insert(m_threadId); refresh(); });
    connect(m_bridge, &ReadingBridge::retrySummaryRequested, this, [this]() { m_summaryError.clear(); m_queue->invalidate(m_threadId); m_queue->enqueueFront(m_threadId); refresh(); });
    connect(m_bridge, &ReadingBridge::attachmentRequested, this, [this](const QString& messageId, const QString& attachmentId, const QString& filename) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        const QString target = QFileDialog::getSaveFileName(this, QStringLiteral("Save attachment"), QDir(dir).filePath(filename));
        if (target.isEmpty()) return;
        m_gmail->getAttachment(messageId, attachmentId, [this, target](const QByteArray& data, int status, const QString& err) {
            if (status != 200) { Q_EMIT statusMessage(QStringLiteral("Attachment download failed: ") + err); return; }
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            QFile f(target);
            if (f.open(QIODevice::WriteOnly)) { f.write(util::fromBase64Url(o.value(QStringLiteral("data")).toString().toLatin1())); f.close(); }
            Q_EMIT statusMessage(QStringLiteral("Saved ") + target);
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        });
    });
    connect(m_bridge, &ReadingBridge::cidRequested, this, [this](const QString& messageId, const QString& cid) {
        const Thread* t = m_store->thread(m_threadId);
        if (!t) return;
        for (const auto& m : t->messages) {
            if (m.id != messageId) continue;
            for (const auto& a : m.attachments) {
                if (a.contentId != cid || a.attachmentId.isEmpty()) continue;
                const QString mime = a.mimeType;
                m_gmail->getAttachment(messageId, a.attachmentId, [this, messageId, cid, mime](const QByteArray& data, int status, const QString&) {
                    if (status != 200) return;
                    const QJsonObject o = QJsonDocument::fromJson(data).object();
                    const QByteArray raw = util::fromBase64Url(o.value(QStringLiteral("data")).toString().toLatin1());
                    Q_EMIT m_bridge->cidReady(messageId, cid, QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(raw.toBase64())));
                });
            }
        }
    });

    connect(m_store, &MailStore::triageChanged, this, [this](const QString& id) { if (id == m_threadId) refresh(); });
    connect(m_store, &MailStore::threadChanged, this, [this](const QString& id) { if (id == m_threadId) refresh(); });
    connect(m_queue, &SummaryQueue::summaryFailed, this, [this](const QString& id, const QString& err) { if (id == m_threadId) { m_summaryError = err; refresh(); } });

    setTheme(m_theme);
}

void ReadingPane::setTheme(const Theme& t)
{
    m_theme = t;
    QFile f(QStringLiteral(":/reading.html"));
    f.open(QIODevice::ReadOnly);
    QString html = QString::fromUtf8(f.readAll());
    html.replace(QStringLiteral("/*THEME*/"), t.webCss());
    m_pageReady = false;
    m_view->setHtml(html, QUrl(QStringLiteral("qrc:///reading.html")));
    m_pendingRender = true;
}

void ReadingPane::clear()
{
    m_threadId.clear();
    refresh();
}

void ReadingPane::showThread(const QString& threadId)
{
    const bool changed = threadId != m_threadId;
    m_threadId = threadId;
    m_summaryError.clear();
    m_blocker->allowRemote = m_remoteAllowed.contains(threadId);
    if (!m_pageReady) { m_pendingRender = true; return; }
    Q_EMIT m_bridge->dataChanged(buildJson(changed));
}

void ReadingPane::refresh()
{
    if (!m_pageReady) { m_pendingRender = true; return; }
    m_blocker->allowRemote = m_remoteAllowed.contains(m_threadId);
    Q_EMIT m_bridge->dataChanged(buildJson(false));
}

QString ReadingPane::sanitizeHtml(QString html)
{
    static const QRegularExpression scripts(QStringLiteral("<script\\b[^>]*>.*?</script>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression styleBlocks(QStringLiteral("<style\\b[^>]*>.*?</style>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression dangerous(QStringLiteral("<(/?)(iframe|object|embed|link|meta|base|form|input|button|frame|frameset|applet|html|head|body|title)\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression onAttrs(QStringLiteral("\\s+on[a-z]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression jsUrls(QStringLiteral("(href|src)\\s*=\\s*([\"']?)\\s*javascript:[^\"'>\\s]*"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cidSrc(QStringLiteral("<img\\b([^>]*?)\\ssrc\\s*=\\s*[\"']cid:([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    html.remove(scripts);
    html.remove(styleBlocks);   // email <style> blocks would leak into the pane's own CSS
    html.remove(dangerous);
    html.remove(onAttrs);
    html.replace(jsUrls, QStringLiteral("\\1=\\2#"));
    html.replace(cidSrc, QStringLiteral("<img\\1 data-cid=\"\\2\" src=\"data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==\""));
    return html;
}

static QJsonArray addrArray(const QList<EmailAddress>& list)
{
    QJsonArray a;
    for (const auto& e : list) a.append(QJsonObject{{QStringLiteral("name"), e.name}, {QStringLiteral("email"), e.email}});
    return a;
}

QString ReadingPane::buildJson(bool resetExpanded) const
{
    QJsonObject o;
    const Thread* t = m_threadId.isEmpty() ? nullptr : m_store->thread(m_threadId);
    if (!t) return QStringLiteral("{}");
    o[QStringLiteral("threadId")] = t->id;
    o[QStringLiteral("subject")] = t->subject();
    o[QStringLiteral("resetExpanded")] = resetExpanded;
    o[QStringLiteral("remoteBlocked")] = !m_remoteAllowed.contains(m_threadId);
    o[QStringLiteral("collapseAll")] = false;

    const TriageResult tr = m_store->triage(t->id);
    static const QHash<int, QString> tagNames{{int(TriageTag::Action), QStringLiteral("Action")}, {int(TriageTag::FollowUp), QStringLiteral("FollowUp")},
        {int(TriageTag::Waiting), QStringLiteral("Waiting")}, {int(TriageTag::Info), QStringLiteral("Info")}, {int(TriageTag::Done), QStringLiteral("Done")},
        {int(TriageTag::Evaluating), QStringLiteral("Evaluating")}};
    o[QStringLiteral("triage")] = QJsonObject{{QStringLiteral("tag"), tagNames.value(int(tr.tag))}, {QStringLiteral("count"), tr.openUserActions}};

    QJsonObject s;
    const ThreadSummary* sum = m_store->summary(t->id);
    const auto statuses = m_store->statuses(t->id);
    if (!m_ai->hasApiKey()) s[QStringLiteral("state")] = QStringLiteral("nokey");
    else if (!m_summaryError.isEmpty()) { s[QStringLiteral("state")] = QStringLiteral("error"); s[QStringLiteral("error")] = m_summaryError; }
    else if (sum && sum->valid) {
        s[QStringLiteral("state")] = QStringLiteral("ready");
        s[QStringLiteral("bullets")] = QJsonArray::fromStringList(sum->bullets);
        QJsonArray acts;
        for (const auto& a : sum->actions) {
            acts.append(QJsonObject{{QStringLiteral("text"), a.text}, {QStringLiteral("hash"), a.hash()}, {QStringLiteral("forUser"), a.forUser},
                                    {QStringLiteral("owner"), a.owner}, {QStringLiteral("status"), actionStatusKey(Triage::effectiveStatus(a, statuses))}});
        }
        s[QStringLiteral("actions")] = acts;
        s[QStringLiteral("followUp")] = sum->isFollowUp;
    } else if (m_store->isEvaluating(t->id) || m_queue->isPending(t->id)) s[QStringLiteral("state")] = QStringLiteral("evaluating");
    else s[QStringLiteral("state")] = QStringLiteral("evaluating");
    o[QStringLiteral("summary")] = s;

    QJsonArray msgs;
    for (const auto& m : t->messages) {
        QJsonObject mo;
        mo[QStringLiteral("id")] = m.id;
        mo[QStringLiteral("from")] = QJsonObject{{QStringLiteral("name"), m.from.name}, {QStringLiteral("email"), m.from.email}};
        mo[QStringLiteral("to")] = addrArray(m.to);
        mo[QStringLiteral("cc")] = addrArray(m.cc);
        mo[QStringLiteral("date")] = util::formatTimestamp(m.date);
        mo[QStringLiteral("snippet")] = m.snippet;
        mo[QStringLiteral("unread")] = m.isUnread();
        mo[QStringLiteral("isDraft")] = m.isDraft;
        mo[QStringLiteral("calendar")] = m.hasCalendarInvite();
        const bool plain = m.textHtml.isEmpty();
        mo[QStringLiteral("plain")] = plain;
        mo[QStringLiteral("html")] = plain ? util::htmlEscape(m.textPlain) : sanitizeHtml(m.textHtml);
        QJsonArray atts;
        for (const auto& a : m.attachments) {
            atts.append(QJsonObject{{QStringLiteral("attachmentId"), a.attachmentId}, {QStringLiteral("filename"), a.filename}, {QStringLiteral("mime"), a.mimeType},
                                    {QStringLiteral("size"), double(a.size)}, {QStringLiteral("inline"), a.isInline}, {QStringLiteral("cid"), a.contentId}});
        }
        mo[QStringLiteral("attachments")] = atts;
        msgs.append(mo);
    }
    o[QStringLiteral("messages")] = msgs;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace ponymail
