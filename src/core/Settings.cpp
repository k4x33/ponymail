#include "Settings.h"
#include "Paths.h"
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace ponymail {

Settings::Settings(QObject* parent) : QObject(parent) {}

void Settings::load()
{
    QFile f(Paths::settingsFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) m_obj = doc.object();
}

void Settings::save() const
{
    Paths::ensureDirs();
    QSaveFile f(Paths::settingsFile());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(m_obj).toJson(QJsonDocument::Indented));
    f.commit();
}

QString Settings::str(const char* key, const QString& def) const
{
    const auto v = m_obj.value(QLatin1String(key));
    return v.isString() ? v.toString() : def;
}

void Settings::set(const char* key, const QJsonValue& v)
{
    m_obj.insert(QLatin1String(key), v);
    save();
    Q_EMIT changed();
}

QString Settings::theme() const { return str("theme", QStringLiteral("light")); }
void Settings::setTheme(const QString& t)
{
    if (theme() == t) return;
    set("theme", t);
    Q_EMIT themeChanged(t);
}

QByteArray Settings::windowGeometry() const { return QByteArray::fromBase64(str("window_geometry").toLatin1()); }
void Settings::setWindowGeometry(const QByteArray& g) { set("window_geometry", QString::fromLatin1(g.toBase64())); }
QByteArray Settings::windowState() const { return QByteArray::fromBase64(str("window_state").toLatin1()); }
void Settings::setWindowState(const QByteArray& s) { set("window_state", QString::fromLatin1(s.toBase64())); }
qreal Settings::readingZoom() const { return m_obj.value(QStringLiteral("reading_zoom")).toDouble(1.0); }
void Settings::setReadingZoom(qreal z) { set("reading_zoom", z); }
int Settings::splitterListWidth() const { return m_obj.value(QStringLiteral("splitter_list_width")).toInt(420); }
void Settings::setSplitterListWidth(int px) { set("splitter_list_width", px); }

QString Settings::defaultSort() const { return str("default_sort", QStringLiteral("date_desc")); }
void Settings::setDefaultSort(const QString& s) { set("default_sort", s); }

QString Settings::aiWritingStyle() const { return str("ai_writing_style"); }
void Settings::setAiWritingStyle(const QString& s) { set("ai_writing_style", s); }
QString Settings::aiSummaryPromptOverride() const { return str("ai_summary_prompt_override"); }
void Settings::setAiSummaryPromptOverride(const QString& s) { set("ai_summary_prompt_override", s); }
QString Settings::aiModel() const { return str("ai_model", QStringLiteral("claude-haiku-5-5")); }
void Settings::setAiModel(const QString& m) { set("ai_model", m); }

QString Settings::userEmail() const { return str("user_email"); }
void Settings::setUserEmail(const QString& e) { set("user_email", e); }
QString Settings::userName() const { return str("user_name", QStringLiteral("Mike Brophy")); }
void Settings::setUserName(const QString& n) { set("user_name", n); }
QString Settings::accountProvider() const { return str("account_provider"); }
void Settings::setAccountProvider(const QString& p) { set("account_provider", p); }
QString Settings::clientId(const QString& provider) const
{
    if (provider == QLatin1String("google")) { const QString legacy = str("oauth_client_id"); if (!legacy.isEmpty()) return legacy; }
    return str(QByteArray("client_id_" + provider.toLatin1()).constData());
}
void Settings::setClientId(const QString& provider, const QString& id)
{
    if (provider == QLatin1String("google")) m_obj.remove(QStringLiteral("oauth_client_id"));
    set(QByteArray("client_id_" + provider.toLatin1()).constData(), id);
}

} // namespace ponymail
