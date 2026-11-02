#pragma once
#include "model/MailStore.h"
#include "Theme.h"
#include <QWidget>
#include <QSet>

class QWebEngineView;
class QWebChannel;

namespace ponymail {

class Database;
class GmailClient;
class SummaryQueue;
class ClaudeClient;

// JS <-> C++ bridge object exposed through QWebChannel as "bridge".
class ReadingBridge : public QObject {
    Q_OBJECT
public:
    explicit ReadingBridge(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void ready() { Q_EMIT pageReady(); }
    Q_INVOKABLE void setActionStatus(const QString& threadId, const QString& hash, const QString& text, const QString& status)
    { Q_EMIT actionStatusChanged(threadId, hash, text, status); }
    Q_INVOKABLE void openAttachment(const QString& messageId, const QString& attachmentId, const QString& filename)
    { Q_EMIT attachmentRequested(messageId, attachmentId, filename); }
    Q_INVOKABLE void editDraft(const QString& messageId) { Q_EMIT editDraftRequested(messageId); }
    Q_INVOKABLE void requestCid(const QString& messageId, const QString& cid) { Q_EMIT cidRequested(messageId, cid); }
    Q_INVOKABLE void retrySummary() { Q_EMIT retrySummaryRequested(); }
Q_SIGNALS:
    void dataChanged(const QString& json);
    void cidReady(const QString& messageId, const QString& cid, const QString& dataUrl);
    void pageReady();
    void actionStatusChanged(const QString& threadId, const QString& hash, const QString& text, const QString& status);
    void attachmentRequested(const QString& messageId, const QString& attachmentId, const QString& filename);
    void editDraftRequested(const QString& messageId);
    void cidRequested(const QString& messageId, const QString& cid);
    void retrySummaryRequested();
};

class ReadingPane : public QWidget {
    Q_OBJECT
public:
    ReadingPane(MailStore* store, Database* db, GmailClient* gmail, SummaryQueue* queue, ClaudeClient* ai, QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void showThread(const QString& threadId);
    void clear();
    QString currentThreadId() const { return m_threadId; }
    void refresh();

    // Text zoom (persisted by the caller). 0.5 .. 2.5
    qreal zoom() const;
    void setZoom(qreal z);
    void zoomIn() { setZoom(zoom() + 0.1); }
    void zoomOut() { setZoom(zoom() - 0.1); }
    void zoomReset() { setZoom(1.0); }

Q_SIGNALS:
    void zoomChanged(qreal zoom);
    void editDraftRequested(const QString& threadId, const QString& messageId);
    void statusMessage(const QString& msg);

private:
    QString buildJson(bool resetExpanded) const;
    bool eventFilter(QObject* o, QEvent* e) override;
    static QString sanitizeHtml(QString html);

    MailStore* m_store;
    Database* m_db;
    GmailClient* m_gmail;
    SummaryQueue* m_queue;
    ClaudeClient* m_ai;
    QWebEngineView* m_view;
    QWebChannel* m_channel;
    ReadingBridge* m_bridge;
    Theme m_theme = Theme::light();
    QString m_threadId;
    QString m_summaryError;
    bool m_pageReady = false;
    bool m_pendingRender = false;
};

} // namespace ponymail
