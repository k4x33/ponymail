#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QRegularExpression>

namespace ponymail {

namespace {
const char* kMigrations[] = {
    // v1 — initial schema (mirrors Supabase tables, per spec §1)
    R"SQL(
    CREATE TABLE IF NOT EXISTS action_item_statuses (
      id TEXT PRIMARY KEY,
      thread_id TEXT NOT NULL,
      action_hash TEXT NOT NULL,
      action_text TEXT NOT NULL,
      status TEXT NOT NULL,
      updated_at INTEGER NOT NULL,
      UNIQUE(thread_id, action_hash)
    );
    CREATE INDEX IF NOT EXISTS idx_action_thread ON action_item_statuses(thread_id);

    CREATE TABLE IF NOT EXISTS blocked_senders (
      id TEXT PRIMARY KEY,
      pattern TEXT NOT NULL UNIQUE,
      type TEXT NOT NULL CHECK(type IN ('email', 'domain')),
      created_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS snoozed_threads (
      thread_id TEXT PRIMARY KEY,
      snoozed_until INTEGER NOT NULL,
      original_labels TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_snooze_until ON snoozed_threads(snoozed_until);

    CREATE TABLE IF NOT EXISTS contacts (
      email TEXT PRIMARY KEY,
      name TEXT,
      frequency INTEGER DEFAULT 1,
      last_seen INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS thread_summaries (
      cache_key TEXT PRIMARY KEY,
      thread_id TEXT NOT NULL,
      summary_json TEXT NOT NULL,
      created_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_summary_thread ON thread_summaries(thread_id);
    CREATE INDEX IF NOT EXISTS idx_summary_created ON thread_summaries(created_at);

    CREATE TABLE IF NOT EXISTS kv (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
    )SQL",
    // v2 — FTS5 for contacts (external content table, kept in sync by triggers)
    R"SQL(
    CREATE VIRTUAL TABLE IF NOT EXISTS contacts_fts USING fts5(name, email, content='contacts', content_rowid='rowid');
    CREATE TRIGGER IF NOT EXISTS contacts_ai AFTER INSERT ON contacts BEGIN
      INSERT INTO contacts_fts(rowid, name, email) VALUES (new.rowid, new.name, new.email);
    END;
    CREATE TRIGGER IF NOT EXISTS contacts_ad AFTER DELETE ON contacts BEGIN
      INSERT INTO contacts_fts(contacts_fts, rowid, name, email) VALUES ('delete', old.rowid, old.name, old.email);
    END;
    CREATE TRIGGER IF NOT EXISTS contacts_au AFTER UPDATE ON contacts BEGIN
      INSERT INTO contacts_fts(contacts_fts, rowid, name, email) VALUES ('delete', old.rowid, old.name, old.email);
      INSERT INTO contacts_fts(rowid, name, email) VALUES (new.rowid, new.name, new.email);
    END;
    )SQL",
};
constexpr int kSchemaVersion = 2;
}

Database::Database(QObject* parent) : QObject(parent)
{
    m_connName = QStringLiteral("ponymail_%1").arg(reinterpret_cast<quintptr>(this));
}

Database::~Database()
{
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connName);
}

bool Database::open(const QString& path)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "SQLite open failed:" << m_db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return migrate();
}

bool Database::exec(const QString& sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        qWarning() << "SQL error:" << q.lastError().text() << "\n" << sql.left(200);
        return false;
    }
    return true;
}

int Database::schemaVersion() const
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA user_version"));
    return q.next() ? q.value(0).toInt() : 0;
}

bool Database::migrate()
{
    int v = schemaVersion();
    while (v < kSchemaVersion) {
        const QString script = QString::fromUtf8(kMigrations[v]);
        m_db.transaction();
        bool ok = true;
        // QSqlQuery executes one statement at a time; split on ';' outside triggers.
        // Triggers contain ';' inside BEGIN..END, so split on statement boundaries manually.
        QStringList statements;
        QString cur;
        int beginDepth = 0;
        const QStringList lines = script.split('\n');
        for (const QString& line : lines) {
            cur += line + '\n';
            const QString u = line.trimmed().toUpper();
            if (u.endsWith(QLatin1String("BEGIN"))) beginDepth++;
            if (u.startsWith(QLatin1String("END;"))) beginDepth--;
            if (line.trimmed().endsWith(';') && beginDepth == 0) {
                statements << cur.trimmed();
                cur.clear();
            }
        }
        if (!cur.trimmed().isEmpty()) statements << cur.trimmed();
        for (const QString& s : statements) {
            if (s.isEmpty()) continue;
            if (!exec(s)) { ok = false; break; }
        }
        if (!ok) { m_db.rollback(); return false; }
        exec(QStringLiteral("PRAGMA user_version=%1").arg(v + 1));
        m_db.commit();
        ++v;
    }
    return true;
}

// ---------------------------------------------------------------------------
// action_item_statuses
// ---------------------------------------------------------------------------
bool Database::upsertActionStatus(const QString& threadId, const ActionItem& item, ActionStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO action_item_statuses(id, thread_id, action_hash, action_text, status, updated_at) "
        "VALUES(:id, :tid, :hash, :text, :status, :ts) "
        "ON CONFLICT(thread_id, action_hash) DO UPDATE SET status=excluded.status, action_text=excluded.action_text, updated_at=excluded.updated_at"));
    q.bindValue(QStringLiteral(":id"), util::newUuid());
    q.bindValue(QStringLiteral(":tid"), threadId);
    q.bindValue(QStringLiteral(":hash"), item.hash());
    q.bindValue(QStringLiteral(":text"), item.text);
    q.bindValue(QStringLiteral(":status"), actionStatusKey(status));
    q.bindValue(QStringLiteral(":ts"), util::nowMs());
    if (!q.exec()) { qWarning() << q.lastError().text(); return false; }
    return true;
}

static ActionStatusRecord readStatusRow(const QSqlQuery& q)
{
    ActionStatusRecord r;
    r.id = q.value(0).toString();
    r.threadId = q.value(1).toString();
    r.actionHash = q.value(2).toString();
    r.actionText = q.value(3).toString();
    r.status = actionStatusFromKey(q.value(4).toString());
    r.updatedAt = q.value(5).toLongLong();
    return r;
}

QHash<QString, ActionStatusRecord> Database::actionStatusesForThread(const QString& threadId) const
{
    QHash<QString, ActionStatusRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, thread_id, action_hash, action_text, status, updated_at FROM action_item_statuses WHERE thread_id=:tid"));
    q.bindValue(QStringLiteral(":tid"), threadId);
    if (q.exec()) while (q.next()) { auto r = readStatusRow(q); out.insert(r.actionHash, r); }
    return out;
}

QHash<QString, QHash<QString, ActionStatusRecord>> Database::allActionStatuses() const
{
    QHash<QString, QHash<QString, ActionStatusRecord>> out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT id, thread_id, action_hash, action_text, status, updated_at FROM action_item_statuses")))
        while (q.next()) { auto r = readStatusRow(q); out[r.threadId].insert(r.actionHash, r); }
    return out;
}

// ---------------------------------------------------------------------------
// blocked_senders
// ---------------------------------------------------------------------------
bool Database::addBlockedSender(const QString& patternIn)
{
    QString pattern = patternIn.trimmed().toLower();
    if (pattern.isEmpty()) return false;
    const QString type = pattern.startsWith('@') ? QStringLiteral("domain") : QStringLiteral("email");
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO blocked_senders(id, pattern, type, created_at) VALUES(:id, :p, :t, :ts)"));
    q.bindValue(QStringLiteral(":id"), util::newUuid());
    q.bindValue(QStringLiteral(":p"), pattern);
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":ts"), util::nowMs());
    return q.exec();
}

bool Database::removeBlockedSender(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM blocked_senders WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), id);
    return q.exec();
}

QList<BlockedSender> Database::blockedSenders() const
{
    QList<BlockedSender> out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT id, pattern, type, created_at FROM blocked_senders ORDER BY created_at DESC")))
        while (q.next()) {
            BlockedSender b;
            b.id = q.value(0).toString(); b.pattern = q.value(1).toString();
            b.type = q.value(2).toString(); b.createdAt = q.value(3).toLongLong();
            out << b;
        }
    return out;
}

bool Database::isBlocked(const QString& emailIn) const
{
    const QString email = emailIn.trimmed().toLower();
    if (email.isEmpty()) return false;
    const int at = email.indexOf('@');
    const QString domain = at >= 0 ? email.mid(at) : QString();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM blocked_senders WHERE pattern=:e OR pattern=:d LIMIT 1"));
    q.bindValue(QStringLiteral(":e"), email);
    q.bindValue(QStringLiteral(":d"), domain);
    return q.exec() && q.next();
}

// ---------------------------------------------------------------------------
// snoozed_threads
// ---------------------------------------------------------------------------
bool Database::snoozeThread(const QString& threadId, qint64 until, const QStringList& originalLabels)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO snoozed_threads(thread_id, snoozed_until, original_labels) VALUES(:t, :u, :l)"));
    q.bindValue(QStringLiteral(":t"), threadId);
    q.bindValue(QStringLiteral(":u"), until);
    q.bindValue(QStringLiteral(":l"), QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(originalLabels)).toJson(QJsonDocument::Compact)));
    return q.exec();
}

bool Database::unsnoozeThread(const QString& threadId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM snoozed_threads WHERE thread_id=:t"));
    q.bindValue(QStringLiteral(":t"), threadId);
    return q.exec();
}

static SnoozedThread readSnooze(const QSqlQuery& q)
{
    SnoozedThread s;
    s.threadId = q.value(0).toString();
    s.snoozedUntil = q.value(1).toLongLong();
    const auto arr = QJsonDocument::fromJson(q.value(2).toString().toUtf8()).array();
    for (const auto v : arr) s.originalLabels << v.toString();
    return s;
}

QList<SnoozedThread> Database::snoozedThreads() const
{
    QList<SnoozedThread> out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT thread_id, snoozed_until, original_labels FROM snoozed_threads ORDER BY snoozed_until")))
        while (q.next()) out << readSnooze(q);
    return out;
}

QList<SnoozedThread> Database::expiredSnoozes(qint64 nowMs) const
{
    QList<SnoozedThread> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT thread_id, snoozed_until, original_labels FROM snoozed_threads WHERE snoozed_until <= :now"));
    q.bindValue(QStringLiteral(":now"), nowMs);
    if (q.exec()) while (q.next()) out << readSnooze(q);
    return out;
}

std::optional<SnoozedThread> Database::snoozeFor(const QString& threadId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT thread_id, snoozed_until, original_labels FROM snoozed_threads WHERE thread_id=:t"));
    q.bindValue(QStringLiteral(":t"), threadId);
    if (q.exec() && q.next()) return readSnooze(q);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// contacts
// ---------------------------------------------------------------------------
void Database::touchContact(const EmailAddress& a, qint64 seenAt)
{
    if (!a.isValid()) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO contacts(email, name, frequency, last_seen) VALUES(:e, :n, 1, :ts) "
        "ON CONFLICT(email) DO UPDATE SET frequency=frequency+1, last_seen=MAX(last_seen, excluded.last_seen), "
        "name=CASE WHEN excluded.name IS NOT NULL AND excluded.name<>'' THEN excluded.name ELSE name END"));
    q.bindValue(QStringLiteral(":e"), a.email);
    q.bindValue(QStringLiteral(":n"), a.name);
    q.bindValue(QStringLiteral(":ts"), seenAt);
    q.exec();
}

void Database::touchContacts(const QList<EmailAddress>& list, qint64 seenAt)
{
    m_db.transaction();
    for (const auto& a : list) touchContact(a, seenAt);
    m_db.commit();
}

QList<Contact> Database::searchContacts(const QString& prefixIn, int limit) const
{
    QList<Contact> out;
    const QString prefix = prefixIn.trimmed();
    if (prefix.isEmpty()) return out;
    QSqlQuery q(m_db);
    // FTS5 prefix match on name or email tokens; rank by frequency then recency per spec.
    QString term = prefix;
    term.remove(QRegularExpression(QStringLiteral("[\"*^:()]")));
    q.prepare(QStringLiteral(
        "SELECT c.email, c.name, c.frequency, c.last_seen FROM contacts c "
        "JOIN contacts_fts f ON f.rowid = c.rowid "
        "WHERE contacts_fts MATCH :m "
        "ORDER BY c.frequency DESC, c.last_seen DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":m"), QStringLiteral("\"%1\"*").arg(term));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (q.exec()) {
        while (q.next()) {
            Contact c; c.email = q.value(0).toString(); c.name = q.value(1).toString();
            c.frequency = q.value(2).toInt(); c.lastSeen = q.value(3).toLongLong();
            out << c;
        }
    }
    if (out.isEmpty()) {
        // Fallback LIKE search (handles punctuation FTS tokenizer splits oddly).
        QSqlQuery l(m_db);
        l.prepare(QStringLiteral("SELECT email, name, frequency, last_seen FROM contacts WHERE email LIKE :p OR name LIKE :p ORDER BY frequency DESC, last_seen DESC LIMIT :lim"));
        l.bindValue(QStringLiteral(":p"), QStringLiteral("%%1%").arg(prefix));
        l.bindValue(QStringLiteral(":lim"), limit);
        if (l.exec()) while (l.next()) {
            Contact c; c.email = l.value(0).toString(); c.name = l.value(1).toString();
            c.frequency = l.value(2).toInt(); c.lastSeen = l.value(3).toLongLong();
            out << c;
        }
    }
    return out;
}

int Database::contactCount() const
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM contacts"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ---------------------------------------------------------------------------
// thread_summaries
// ---------------------------------------------------------------------------
bool Database::putSummary(const ThreadSummary& s)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO thread_summaries(cache_key, thread_id, summary_json, created_at) VALUES(:k, :t, :j, :ts)"));
    q.bindValue(QStringLiteral(":k"), s.cacheKey());
    q.bindValue(QStringLiteral(":t"), s.threadId);
    q.bindValue(QStringLiteral(":j"), QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact)));
    q.bindValue(QStringLiteral(":ts"), util::nowMs());
    return q.exec();
}

std::optional<ThreadSummary> Database::getSummary(const QString& cacheKey) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT thread_id, summary_json FROM thread_summaries WHERE cache_key=:k"));
    q.bindValue(QStringLiteral(":k"), cacheKey);
    if (q.exec() && q.next()) {
        const int colon = cacheKey.indexOf(':');
        const QString tid = q.value(0).toString();
        const QString mid = colon >= 0 ? cacheKey.mid(colon + 1) : QString();
        return ThreadSummary::fromJson(tid, mid, QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object());
    }
    return std::nullopt;
}

std::optional<ThreadSummary> Database::latestSummaryForThread(const QString& threadId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT cache_key, summary_json FROM thread_summaries WHERE thread_id=:t ORDER BY created_at DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":t"), threadId);
    if (q.exec() && q.next()) {
        const QString key = q.value(0).toString();
        const int colon = key.indexOf(':');
        return ThreadSummary::fromJson(threadId, colon >= 0 ? key.mid(colon + 1) : QString(),
                                       QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object());
    }
    return std::nullopt;
}

void Database::pruneSummaries(int keep)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM thread_summaries WHERE cache_key NOT IN (SELECT cache_key FROM thread_summaries ORDER BY created_at DESC LIMIT :k)"));
    q.bindValue(QStringLiteral(":k"), keep);
    q.exec();
}

int Database::summaryCount() const
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM thread_summaries"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ---------------------------------------------------------------------------
// kv
// ---------------------------------------------------------------------------
QString Database::kv(const QString& key, const QString& def) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM kv WHERE key=:k"));
    q.bindValue(QStringLiteral(":k"), key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return def;
}

void Database::setKv(const QString& key, const QString& value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO kv(key, value) VALUES(:k, :v)"));
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), value);
    q.exec();
}

} // namespace ponymail
