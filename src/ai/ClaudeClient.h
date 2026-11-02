#pragma once
#include "model/Models.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <functional>

namespace ponymail {

class Settings;

// Direct client for the Claude Messages API. The API key lives in the OS
// credential store under "claude.api_key". No proxy, no telemetry.
class ClaudeClient : public QObject {
    Q_OBJECT
public:
    using TextCallback = std::function<void(const QString& text, const QString& error)>;
    using SummaryCallback = std::function<void(const ThreadSummary& summary, const QString& error)>;

    explicit ClaudeClient(Settings* settings, QNetworkAccessManager* nam, QObject* parent = nullptr);

    bool hasApiKey() const;
    void setApiKey(const QString& key);

    // Produces the 5-bullet summary + action list for a thread.
    void summarizeThread(const Thread& thread, const QString& userName, const QString& userEmail, SummaryCallback cb);

    // Drafts prose from the user's direction notes. Never copies notes verbatim.
    struct DraftRequest {
        QString mode;             // new | reply | reply_all | forward
        QString direction;        // user's notes
        QString writingStyle;     // settings.ai_writing_style
        QString userName;
        QString threadContext;    // plain text of the conversation (may be empty)
        QString subject;
        bool wantSubject = false;
    };
    struct DraftResult { QString subject; QString bodyHtml; };
    void draftEmail(const DraftRequest& req, std::function<void(const DraftResult&, const QString& error)> cb);

    void complete(const QString& system, const QString& user, int maxTokens, TextCallback cb);
    void complete(const QString& system, const QString& user, int maxTokens, const QString& assistantPrefill, TextCallback cb);

    // Lenient JSON recovery: strips fences, // comments and trailing commas.
    static QString extractJson(const QString& text);
    static QJsonObject parseJsonObject(const QString& text, QString* error);

private:
    Settings* m_settings;
    QNetworkAccessManager* m_nam;
};

} // namespace ponymail
