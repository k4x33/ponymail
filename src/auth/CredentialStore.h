#pragma once
#include <QString>

namespace ponymail {

// OS secure credential store. Replaces Supabase `app_user_connections` /
// `app_user_connector_slots`.
//
//   Windows : Windows Credential Manager (CredWriteW / CredReadW, generic
//             credentials, persisted LOCAL_MACHINE scope encrypted via DPAPI
//             for the current user).
//   Other   : development fallback — a 0600 file under the data dir. The
//             shipped product targets Windows 11, so this path is never used
//             by the installer build; it exists so the Linux CI/compile loop
//             and unit tests can run.
//
// Keys used by the app:
//   google.refresh_token, google.access_token, google.client_secret, claude.api_key
class CredentialStore {
public:
    static bool write(const QString& key, const QString& secret);
    static QString read(const QString& key);
    static bool remove(const QString& key);
    static bool isOsBacked();
};

} // namespace ponymail
