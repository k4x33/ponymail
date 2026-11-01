#include "Paths.h"
#include <QDir>
#include <QStandardPaths>

namespace ponymail {

QString Paths::dataDir()
{
#ifdef Q_OS_WIN
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // AppDataLocation already appends the application name when set; normalise.
    QDir d(base);
    if (d.dirName().compare(QStringLiteral("PonyMail"), Qt::CaseInsensitive) != 0)
        return d.filePath(QStringLiteral("PonyMail"));
    return base;
#elif defined(Q_OS_MAC)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    return QDir::home().filePath(QStringLiteral(".config/ponymail"));
#endif
}

QString Paths::databaseFile() { return QDir(dataDir()).filePath(QStringLiteral("ponymail.db")); }
QString Paths::settingsFile() { return QDir(dataDir()).filePath(QStringLiteral("settings.json")); }
QString Paths::logFile() { return QDir(dataDir()).filePath(QStringLiteral("ponymail.log")); }

void Paths::ensureDirs()
{
    QDir().mkpath(dataDir());
}

} // namespace ponymail
