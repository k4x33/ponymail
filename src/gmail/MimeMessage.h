#pragma once
#include "model/Models.h"
#include <QJsonObject>

namespace ponymail {

// Decodes Gmail REST `Message` / `Thread` JSON (format=full) into model structs.
class MimeMessage {
public:
    static Message parseMessage(const QJsonObject& json);
    static Thread parseThread(const QJsonObject& json);

    // Returns the best body for display: HTML if present, otherwise plain text wrapped in <pre>-like HTML.
    static QString displayHtml(const Message& m);

    // Plain-text rendering used for AI summarisation (quotes stripped, signatures trimmed).
    static QString plainTextForAi(const Message& m, int maxChars = 6000);

    // Splits body into (visible, quoted) at the first "On ... wrote:" / "From:" quote marker.
    static QString stripQuotedText(const QString& plain);
    static QString stripSignature(const QString& plain);

private:
    static void walkParts(const QJsonObject& part, Message& m, int depth);
};

} // namespace ponymail
