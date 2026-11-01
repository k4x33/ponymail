#pragma once
#include <QString>

namespace ponymail {

// Resolves platform-specific storage locations.
//   Windows: %APPDATA%\PonyMail
//   Linux:   ~/.config/ponymail
//   macOS:   ~/Library/Application Support/PonyMail
class Paths {
public:
    static QString dataDir();
    static QString databaseFile();   // ponymail.db
    static QString settingsFile();   // settings.json
    static QString logFile();        // ponymail.log
    static void ensureDirs();
};

} // namespace ponymail
