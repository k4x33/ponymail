#include "CredentialStore.h"
#include "core/Paths.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wincred.h>
#  include <vector>
#else
#  include <QFile>
#  include <QDir>
#  include <QJsonDocument>
#  include <QJsonObject>
#  include <QSaveFile>
#endif

namespace ponymail {

#ifdef Q_OS_WIN

static std::wstring targetName(const QString& key)
{
    return (QStringLiteral("PonyMail/") + key).toStdWString();
}

bool CredentialStore::write(const QString& key, const QString& secret)
{
    const std::wstring target = targetName(key);
    const QByteArray blob = secret.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    std::wstring user = L"ponymail";
    cred.UserName = const_cast<LPWSTR>(user.c_str());
    return CredWriteW(&cred, 0) == TRUE;
}

QString CredentialStore::read(const QString& key)
{
    const std::wstring target = targetName(key);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) return {};
    QString out = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob),
                                    static_cast<int>(cred->CredentialBlobSize));
    CredFree(cred);
    return out;
}

bool CredentialStore::remove(const QString& key)
{
    const std::wstring target = targetName(key);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
}

bool CredentialStore::isOsBacked() { return true; }

#else // ------------------------------------------------------------------ dev fallback

static QString storeFile() { return QDir(Paths::dataDir()).filePath(QStringLiteral("credentials.dev.json")); }

static QJsonObject loadStore()
{
    QFile f(storeFile());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

static bool saveStore(const QJsonObject& o)
{
    Paths::ensureDirs();
    QSaveFile f(storeFile());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool CredentialStore::write(const QString& key, const QString& secret)
{
    auto o = loadStore();
    o[key] = secret;
    return saveStore(o);
}

QString CredentialStore::read(const QString& key) { return loadStore().value(key).toString(); }

bool CredentialStore::remove(const QString& key)
{
    auto o = loadStore();
    o.remove(key);
    return saveStore(o);
}

bool CredentialStore::isOsBacked() { return false; }

#endif

} // namespace ponymail
