# PonyMail

Native desktop email client for Gmail (Windows 11 and macOS 12+ on Apple Silicon). C++17 / Qt 6 (Widgets + WebEngine). Talks directly to the Gmail REST API over OAuth 2.0 + PKCE and to the Claude API for thread summaries, triage and drafting. No intermediary servers, no telemetry, no read receipts.

## Install

### macOS (Apple Silicon)

Open the latest run under **Actions → Build macOS app**, download `PonyMail-macOS-arm64`, open the DMG and drag PonyMail to Applications. The app is ad-hoc signed, not notarized, so the first launch needs **right-click → Open** (or `xattr -d com.apple.quarantine /Applications/PonyMail.app`). Tokens go to the macOS Keychain; local data lives in `~/Library/Application Support/PonyMail`.

### Windows

1. Open the latest run under **Actions → Build Windows installer** (or the latest **Release** for a tagged build).
2. Download `PonyMail-Setup-win64` and run `PonyMail-Setup-<version>-win64.exe`.
3. The installer is per-user (`%LOCALAPPDATA%\Programs\PonyMail`), no admin prompt. The VC++ runtime ships app-local.

Windows SmartScreen will warn because the binary is unsigned; choose *More info → Run anyway*.

## First run

PonyMail opens **Add an account** and asks which type to add, in the style of new Outlook:

| Choice | What happens |
| --- | --- |
| **Outlook** (Microsoft 365, Outlook.com, Hotmail) | Browser opens on the Microsoft identity platform; PKCE public-client flow, no secret. Sign-in and token storage work today; **mailbox sync via Microsoft Graph is not implemented yet**, so mail features still require a Google account. |
| **Google** (Gmail, Workspace) | Browser opens on Google; PKCE flow with loopback redirect. Full mailbox sync. |
| **Other** | IMAP/POP/Exchange are not supported in this build. |

Tokens go to Windows Credential Manager; PonyMail never sees the password.

### Built-in OAuth clients (recommended)

For the one-click experience the build must carry OAuth client IDs. Add these **repository secrets** (Settings → Secrets and variables → Actions) and re-run the workflow:

| Secret | Value |
| --- | --- |
| `PONYMAIL_GOOGLE_CLIENT_ID` / `PONYMAIL_GOOGLE_CLIENT_SECRET` | Google Cloud Console OAuth client of type **Desktop app**, Gmail API enabled. A *Web application* client will reject the `http://127.0.0.1:<port>/` redirect. |
| `PONYMAIL_MS_CLIENT_ID` | Entra app registration, *Mobile and desktop applications* platform with redirect `http://localhost`, delegated Graph permissions `Mail.ReadWrite`, `Mail.Send`, `offline_access`, `openid`, `profile`, `email`; multi-tenant + personal accounts. |

When a build lacks a client, the sign-in page asks for the developer client once and stores it locally.

Settings gear → **Account & AI** holds the Claude API key, model and display name, plus *Switch account* / *Remove account*.

## Local storage

| Data | Location |
| --- | --- |
| OAuth refresh token, client secret, Claude API key | Windows Credential Manager (`PonyMail/*`) |
| `action_item_statuses`, `blocked_senders`, `snoozed_threads`, `contacts` (+FTS5), `thread_summaries`, sync cursor | `%APPDATA%\PonyMail\ponymail.db` (SQLite, WAL) |
| Theme, window geometry, sort, AI writing style, prompt override, model | `%APPDATA%\PonyMail\settings.json` |
| Diagnostic log | `%APPDATA%\PonyMail\ponymail.log` |

## Keyboard

| Key | Action |
| --- | --- |
| `C` | New mail |
| `Ctrl+Q` | Toggle read / unread |
| `Delete` | Delete (swipe-out, selects next) |
| `E` | Archive |
| `S` | Star |
| `Ctrl+R` / `Ctrl+Shift+R` / `Ctrl+F` | Reply / Reply all / Forward |
| `Ctrl+E` | Focus search |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0`, Ctrl+wheel | Reading pane text size (persisted) |
| `Ctrl+Enter` | Send (composer) |
| `F5` | Sync now |

Search box: Gmail query syntax (`from:`, `has:attachment`, `is:starred`, `to:me` chips). `testudo-on` / `testudo-off` toggle the Maryland theme.

## Build locally (Windows)

Prerequisites: Visual Studio 2022 Build Tools (C++ workload), CMake ≥ 3.21, Ninja, Qt 6.8 MSVC 2022 x64 with the *Qt WebEngine*, *Qt WebChannel* and *Qt Positioning* modules, Inno Setup 6.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\ponymail_tests.exe
mkdir dist; copy build\PonyMail.exe dist\
windeployqt --release dist\PonyMail.exe
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" /DAppVersion=1.0.0 /DSourceDir="$PWD\dist" installer\ponymail.iss
```

The same project builds and runs its tests on Linux with Qt 6.4+ (`qt6-base-dev qt6-webengine-dev libqt6sql6-sqlite`), using a file-backed credential store meant for development only.

## Layout

```
src/core      Paths, Settings (settings.json), Util (address parsing, timestamps, hashing)
src/storage   Database — SQLite schema migrations + data access for every former Supabase table
src/auth      CredentialStore (Windows Credential Manager), OAuthManager (PKCE loopback flow)
src/gmail     GmailClient (REST), MimeMessage (parse), MimeBuilder (multipart/related + cid)
src/model     Models, Triage::classifyThread, SenderResolver, MailStore
src/sync      SyncEngine — history.list polling, 30s/120s backoff, Sent sweeps, snooze wake, blocked-sender auto-trash
src/ai        ClaudeClient, SummaryQueue (2 workers, top-50 preload, scroll prefetch, 2,000 cache cap)
src/ui        MainWindow (three panes), NavRail, MessageList*, ReadingPane, ComposeWindow, AiDialog, SettingsDialogs, Theme
resources     reading.html, compose.html (WebEngine pages), icons
tests         Qt Test suite: triage state machine, sender rules, MIME, SQLite layer
installer     Inno Setup script
```
