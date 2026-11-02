#include "ClaudeClient.h"
#include "auth/CredentialStore.h"
#include "core/Settings.h"
#include "gmail/MimeMessage.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

namespace ponymail {

namespace {
const QString kKey = QStringLiteral("claude.api_key");
const QString kEndpoint = QStringLiteral("https://api.anthropic.com/v1/messages");
}

ClaudeClient::ClaudeClient(Settings* settings, QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_settings(settings), m_nam(nam) {}

bool ClaudeClient::hasApiKey() const { return !CredentialStore::read(kKey).isEmpty(); }
void ClaudeClient::setApiKey(const QString& key) { CredentialStore::write(kKey, key.trimmed()); }

void ClaudeClient::complete(const QString& system, const QString& user, int maxTokens, TextCallback cb)
{
    complete(system, user, maxTokens, QString(), std::move(cb));
}

void ClaudeClient::complete(const QString& system, const QString& user, int maxTokens, const QString& assistantPrefill, TextCallback cb)
{
    const QString key = CredentialStore::read(kKey);
    if (key.isEmpty()) { cb({}, QStringLiteral("Claude API key not configured (Settings → Account).")); return; }

    QJsonObject body;
    body[QStringLiteral("model")] = m_settings->aiModel();
    body[QStringLiteral("max_tokens")] = maxTokens;
    body[QStringLiteral("system")] = system;
    QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}};
    if (!assistantPrefill.isEmpty())
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), assistantPrefill}});
    body[QStringLiteral("messages")] = messages;

    QNetworkRequest req{QUrl(kEndpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key", key.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    req.setTransferTimeout(90000);

    QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb, assistantPrefill]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString netErr = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (status != 200) {
            QString err = o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            if (err.isEmpty()) err = netErr.isEmpty() ? QStringLiteral("Claude API HTTP %1").arg(status) : netErr;
            cb({}, err);
            return;
        }
        QString text = assistantPrefill;   // the model continues from the prefill
        for (const auto v : o.value(QStringLiteral("content")).toArray()) {
            const auto c = v.toObject();
            if (c.value(QStringLiteral("type")).toString() == QLatin1String("text")) text += c.value(QStringLiteral("text")).toString();
        }
        cb(text, {});
    });
}

QString ClaudeClient::extractJson(const QString& textIn)
{
    QString text = textIn;
    // Drop ```json fences if present.
    text.remove(QRegularExpression(QStringLiteral("```[a-zA-Z]*")));
    const int a = text.indexOf('{');
    const int b = text.lastIndexOf('}');
    if (a < 0 || b <= a) return {};
    text = text.mid(a, b - a + 1);
    // Strip // comments that are outside string literals, and trailing commas.
    QString out;
    bool inStr = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (inStr) {
            out += c;
            if (c == '\\' && i + 1 < text.size()) { out += text[++i]; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; out += c; continue; }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        out += c;
    }
    out.replace(QRegularExpression(QStringLiteral(",\\s*([}\\]])")), QStringLiteral("\\1"));
    return out;
}

QJsonObject ClaudeClient::parseJsonObject(const QString& text, QString* error)
{
    const QString json = extractJson(text);
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (doc.isObject()) return doc.object();
    if (error) *error = pe.error == QJsonParseError::NoError ? QStringLiteral("no JSON object found") : pe.errorString();
    qWarning().noquote() << "Claude: unparseable response:" << text.left(2000);
    return {};
}

void ClaudeClient::summarizeThread(const Thread& thread, const QString& userName, const QString& userEmail, SummaryCallback cb)
{
    QString convo;
    const int start = qMax(0, thread.messages.size() - 12);
    for (int i = start; i < thread.messages.size(); ++i) {
        const Message& m = thread.messages[i];
        convo += QStringLiteral("--- Message %1 ---\nFrom: %2\nTo: %3\nDate: %4\nSubject: %5\n\n%6\n\n")
            .arg(i + 1)
            .arg(m.from.rfc822())
            .arg([&]{ QStringList l; for (const auto& a : m.to) l << a.rfc822(); return l.join(QStringLiteral(", ")); }())
            .arg(util::formatTimestamp(m.date))
            .arg(m.subject)
            .arg(MimeMessage::plainTextForAi(m, 4000));
    }
    if (convo.size() > 24000) convo = convo.right(24000);

    QString systemPrompt = m_settings->aiSummaryPromptOverride();
    if (systemPrompt.isEmpty()) {
        systemPrompt = QStringLiteral(
            "You are an executive email triage assistant for %1 (<%2>), who is also addressed as \"Mike\" or \"Brophy\".\n"
            "Analyse the email thread and respond with ONLY a strict JSON object (no prose, no markdown fences, no comments) with exactly these keys:\n"
            "  bullets: array of 1 to 5 strings summarising the thread (never more than 5).\n"
            "  actions: array of objects, one per explicit request, question or task in the thread; each object has keys "
            "text (string), for_user (boolean) and owner (string: name or role of who must act).\n"
            "  follow_up: boolean, true when the latest message is someone answering a task or question that %1 previously asked of them.\n"
            "Example: {\"bullets\":[\"Budget approved for Q3\"],\"actions\":[{\"text\":\"Send revised deck\",\"for_user\":true,\"owner\":\"Mike\"}],\"follow_up\":false}\n"
            "Rules:\n"
            "- for_user is true only when the request is directed at %1 (by name, by 'you' in a message addressed to him, or as the clear recipient).\n"
            "- Messages written BY %1 never create actions for him; requests he makes of others are actions for those others.\n"
            "- Bullets are terse, factual, no filler, each under 140 characters.\n"
            "- Calendar invitations, newsletters, notifications and receipts have no actions.\n"
            "- Omit an action when it is already resolved later in the thread.").arg(userName, userEmail);
    }

    complete(systemPrompt, QStringLiteral("Thread subject: %1\n\n%2").arg(thread.subject(), convo), 1200, QStringLiteral("{"),
        [cb, tid = thread.id, mid = thread.latestMessageId()](const QString& text, const QString& err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            QString perr;
            const QJsonObject o = parseJsonObject(text, &perr);
            if (o.isEmpty()) { cb({}, QStringLiteral("Model returned non-JSON output (%1).").arg(perr)); return; }
            cb(ThreadSummary::fromJson(tid, mid, o), {});
        });
}

void ClaudeClient::draftEmail(const DraftRequest& req, std::function<void(const DraftResult&, const QString&)> cb)
{
    QString modeText;
    if (req.mode == QLatin1String("reply")) modeText = QStringLiteral("a reply to the most recent message");
    else if (req.mode == QLatin1String("reply_all")) modeText = QStringLiteral("a reply-all to the most recent message");
    else if (req.mode == QLatin1String("forward")) modeText = QStringLiteral("a short forwarding note that introduces the forwarded conversation below");
    else modeText = QStringLiteral("a new email");

    const QString system = QStringLiteral(
        "You write emails on behalf of %1. Write %2.\n"
        "The user's DIRECTION NOTES are raw thoughts: never copy them verbatim. Polish, rewrite and expand them into "
        "professional, natural prose in the user's voice.\n"
        "Writing style requirements from the user (follow strictly):\n%3\n\n"
        "Respond with ONLY a strict JSON object with two string keys, subject and body_html. No markdown fences, no comments.\n"
        "body_html is simple HTML using <p>, <ul>, <li>, <b> only; no <html>/<body> wrappers, no signature block, no quoted thread.\n"
        "%4")
        .arg(req.userName, modeText,
             req.writingStyle.isEmpty() ? QStringLiteral("(none provided — use a concise, direct, professional business tone)") : req.writingStyle,
             req.wantSubject ? QStringLiteral("Generate a specific subject line.") : QStringLiteral("Keep the subject as given."));

    QString user = QStringLiteral("DIRECTION NOTES:\n%1\n\n").arg(req.direction);
    if (!req.subject.isEmpty()) user += QStringLiteral("CURRENT SUBJECT: %1\n\n").arg(req.subject);
    if (!req.threadContext.isEmpty()) user += QStringLiteral("CONVERSATION CONTEXT:\n%1\n").arg(req.threadContext.right(16000));

    complete(system, user, 1500, QStringLiteral("{"), [cb](const QString& text, const QString& err) {
        if (!err.isEmpty()) { cb({}, err); return; }
        const QJsonObject o = parseJsonObject(text, nullptr);
        DraftResult r;
        if (!o.isEmpty()) {
            r.subject = o.value(QStringLiteral("subject")).toString();
            r.bodyHtml = o.value(QStringLiteral("body_html")).toString();
        }
        if (r.bodyHtml.isEmpty()) {
            // Fall back to treating the whole response as prose.
            QString t = text.toHtmlEscaped();
            t.replace(QStringLiteral("\n\n"), QStringLiteral("</p><p>"));
            t.replace('\n', QStringLiteral("<br>"));
            r.bodyHtml = QStringLiteral("<p>%1</p>").arg(t);
        }
        cb(r, {});
    });
}

} // namespace ponymail
