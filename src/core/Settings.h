#pragma once
#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QString>

namespace ponymail {

// settings.json — window geometry, theme, sort, AI prompt overrides, AI writing style.
// Replaces Supabase `user_preferences` + `testudo_events` + localStorage writing style.
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject* parent = nullptr);

    void load();
    void save() const;

    // --- Appearance ---
    QString theme() const;                 // light | dark | testudo | testudo-dark
    void setTheme(const QString& t);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& g);
    QByteArray windowState() const;
    void setWindowState(const QByteArray& s);
    int splitterListWidth() const;
    void setSplitterListWidth(int px);

    // --- Behaviour ---
    QString defaultSort() const;           // date_desc | date_asc | sender
    void setDefaultSort(const QString& s);

    // --- AI ---
    QString aiWritingStyle() const;
    void setAiWritingStyle(const QString& s);
    QString aiSummaryPromptOverride() const;
    void setAiSummaryPromptOverride(const QString& s);
    QString aiModel() const;               // default claude-haiku-5-5
    void setAiModel(const QString& m);

    // --- Identity (non-secret) ---
    QString userEmail() const;
    void setUserEmail(const QString& e);
    QString userName() const;
    void setUserName(const QString& n);
    QString oauthClientId() const;         // client id is not a secret; secret lives in keyring
    void setOauthClientId(const QString& id);

    QJsonObject raw() const { return m_obj; }

Q_SIGNALS:
    void themeChanged(const QString& theme);
    void changed();

private:
    QString str(const char* key, const QString& def = QString()) const;
    void set(const char* key, const QJsonValue& v);
    QJsonObject m_obj;
};

} // namespace ponymail
