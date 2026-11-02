#include "SyncEngine.h"
#include <algorithm>
#include "gmail/GmailClient.h"
#include "gmail/MimeMessage.h"
#include "storage/Database.h"
#include "core/Settings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <memory>

namespace ponymail {

namespace {
constexpr int kPollNormalMs = 30'000;
constexpr int kPollBackoffMs = 120'000;
constexpr int kPageSize = 50;
constexpr int kMaxParallelThreadFetches = 4;
}

SyncEngine::SyncEngine(GmailClient* gmail, MailStore* store, Database* db, Settings* settings, QObject* parent)
    : QObject(parent), m_gmail(gmail), m_store(store), m_db(db), m_settings(settings)
{
    m_pollTimer.setInterval(kPollNormalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &SyncEngine::onTick);
    m_snoozeTimer.setInterval(30'000);
    connect(&m_snoozeTimer, &QTimer::timeout, this, &SyncEngine::checkSnoozes);

    connect(m_gmail, &GmailClient::rateLimited, this, [this](int) { setRateLimited(true); });
    connect(m_gmail, &GmailClient::requestSucceeded, this, [this]() { if (m_rateLimited) setRateLimited(false); });

    m_activeFolder = systemFolders().first();
}

QString SyncEngine::userEmail() const { return m_settings->userEmail(); }

QList<Folder> SyncEngine::systemFolders() const
{
    QList<Folder> f;
    f << Folder{QStringLiteral("inbox"), QStringLiteral("Inbox"), {QStringLiteral("INBOX")}, {}, false, true};
    f << Folder{QStringLiteral("snoozed"), QStringLiteral("Snoozed"),
                m_store->snoozedLabelId.isEmpty() ? QStringList{} : QStringList{m_store->snoozedLabelId}, {}, false, true};
    f << Folder{QStringLiteral("sent"), QStringLiteral("Sent Items"), {QStringLiteral("SENT")}, {}, false, true};
    f << Folder{QStringLiteral("drafts"), QStringLiteral("Drafts"), {QStringLiteral("DRAFT")}, {}, false, true};
    f << Folder{QStringLiteral("spam"), QStringLiteral("Junk Email"), {QStringLiteral("SPAM")}, {}, true, true};
    f << Folder{QStringLiteral("trash"), QStringLiteral("Deleted Items"), {QStringLiteral("TRASH")}, {}, true, true};
    f << Folder{QStringLiteral("archive"), QStringLiteral("Archive"), {},
                QStringLiteral("-in:inbox -in:sent -in:drafts -in:spam -in:trash -label:Snoozed -label:Blocked"), false, true};
    return f;
}

QList<Folder> SyncEngine::labelFolders() const
{
    QList<Folder> out;
    for (const auto& l : m_store->labels()) {
        if (l.type != QLatin1String("user")) continue;
        if (l.name.compare(QStringLiteral("Snoozed"), Qt::CaseInsensitive) == 0) continue;
        if (l.name.compare(QStringLiteral("Blocked"), Qt::CaseInsensitive) == 0) continue;
        out << Folder{QStringLiteral("label:") + l.id, l.name, {l.id}, {}, false, false};
    }
    std::sort(out.begin(), out.end(), [](const Folder& a, const Folder& b) { return a.title.localeAwareCompare(b.title) < 0; });
    // Dedicated "Blocked" label at the end.
    out << Folder{QStringLiteral("blocked"), QStringLiteral("Blocked"),
                  m_store->blockedLabelId.isEmpty() ? QStringList{} : QStringList{m_store->blockedLabelId}, {}, true, false};
    return out;
}

void SyncEngine::start()
{
    if (m_started) return;
    m_started = true;
    Q_EMIT statusMessage(QStringLiteral("Connecting to Gmail…"));
    m_gmail->getProfile([this](const QJsonObject& o, int status, const QString& err) {
        if (status != 200) { Q_EMIT errorOccurred(QStringLiteral("Profile: ") + err); m_started = false; return; }
        const QString email = o.value(QStringLiteral("emailAddress")).toString();
        m_settings->setUserEmail(email);
        m_historyId = m_db->kv(QStringLiteral("history_id"));
        if (m_historyId.isEmpty()) {
            m_historyId = o.value(QStringLiteral("historyId")).toVariant().toString();
            m_db->setKv(QStringLiteral("history_id"), m_historyId);
        }
        Q_EMIT profileLoaded(email);
        loadLabels([this]() {
            refreshActiveFolder(true);
            m_pollTimer.start();
            m_snoozeTimer.start();
            m_sinceLastTick.start();
            checkSnoozes();
        });
    });
}

void SyncEngine::stop()
{
    m_pollTimer.stop();
    m_snoozeTimer.stop();
    m_started = false;
}

void SyncEngine::loadLabels(std::function<void()> done)
{
    m_gmail->listLabels([this, done](const QJsonObject& o, int status, const QString& err) {
        if (status != 200) { Q_EMIT errorOccurred(QStringLiteral("Labels: ") + err); if (done) done(); return; }
        QList<Label> labels;
        for (const auto v : o.value(QStringLiteral("labels")).toArray()) {
            const auto lo = v.toObject();
            Label l;
            l.id = lo.value(QStringLiteral("id")).toString();
            l.name = lo.value(QStringLiteral("name")).toString();
            l.type = lo.value(QStringLiteral("type")).toString();
            labels << l;
        }
        m_store->setLabels(labels);
        // Ensure our two app labels exist in Gmail so other clients see consistent state.
        ensureLabel(QStringLiteral("Snoozed"), [this, done](const QString&) {
            ensureLabel(QStringLiteral("Blocked"), [this, done](const QString&) {
                if (done) done();
            });
        });
    });
}

void SyncEngine::ensureLabel(const QString& name, std::function<void(const QString&)> done)
{
    const QString existing = m_store->labelIdByName(name);
    if (!existing.isEmpty()) { done(existing); return; }
    m_gmail->createLabel(name, [this, name, done](const QJsonObject& o, int status, const QString&) {
        if (status == 200) {
            auto labels = m_store->labels();
            Label l; l.id = o.value(QStringLiteral("id")).toString(); l.name = name; l.type = QStringLiteral("user");
            labels << l;
            m_store->setLabels(labels);
            done(l.id);
        } else {
            done({});
        }
    });
}

void SyncEngine::setActiveFolder(const Folder& f)
{
    if (f.key == m_activeFolder.key && !m_store->folderThreadIds(f.key).isEmpty()) {
        m_activeFolder = f;
        return;
    }
    m_activeFolder = f;
    refreshActiveFolder(true);
}

void SyncEngine::refreshActiveFolder(bool reset)
{
    if (!m_started) return;
    fetchFolderPage(m_activeFolder, QString(), reset);
}

void SyncEngine::loadMoreActiveFolder()
{
    const QString tok = m_store->folderPageToken(m_activeFolder.key);
    if (tok.isEmpty()) return;
    fetchFolderPage(m_activeFolder, tok, false);
}

void SyncEngine::fetchFolderPage(const Folder& f, const QString& pageToken, bool reset)
{
    Folder folder = f;
    // Snoozed/Blocked depend on label ids that may have been created after construction.
    if (folder.key == QLatin1String("snoozed")) folder.labelIds = {m_store->snoozedLabelId};
    if (folder.key == QLatin1String("blocked")) folder.labelIds = {m_store->blockedLabelId};
    if (folder.labelIds.contains(QString())) folder.labelIds.removeAll(QString());
    if (folder.labelIds.isEmpty() && folder.query.isEmpty()) {
        m_store->setFolderThreadIds(folder.key, {}, false);
        return;
    }

    Q_EMIT folderLoading(folder.key, true);
    Q_EMIT syncStateChanged(true, m_rateLimited);
    QString query = folder.query;
    if (folder.includeSpamTrash) query += query.isEmpty() ? QStringLiteral("in:anywhere") : QStringLiteral(" in:anywhere");

    m_gmail->listThreads(query, folder.labelIds, kPageSize, pageToken,
        [this, folder, reset](const QJsonObject& o, int status, const QString& err) {
            if (status != 200) {
                Q_EMIT folderLoading(folder.key, false);
                Q_EMIT syncStateChanged(false, m_rateLimited);
                Q_EMIT errorOccurred(QStringLiteral("List threads: ") + err);
                return;
            }
            QStringList ids, needFetch;
            for (const auto v : o.value(QStringLiteral("threads")).toArray()) {
                const auto to = v.toObject();
                const QString id = to.value(QStringLiteral("id")).toString();
                const qint64 hid = to.value(QStringLiteral("historyId")).toString().toLongLong();
                ids << id;
                const Thread* cached = m_store->thread(id);
                if (!cached || cached->historyId < hid) needFetch << id;
            }
            m_store->setFolderPageToken(folder.key, o.value(QStringLiteral("nextPageToken")).toString());
            if (folder.key == QLatin1String("inbox")) for (const auto& id : ids) m_knownInboxThreads.insert(id);
            fetchThreads(needFetch, [this, folder, ids, reset]() {
                m_store->setFolderThreadIds(folder.key, ids, !reset);
                Q_EMIT folderLoading(folder.key, false);
                Q_EMIT syncStateChanged(false, m_rateLimited);
            });
        });
}

void SyncEngine::fetchThreads(const QStringList& ids, std::function<void()> done)
{
    if (ids.isEmpty()) { if (done) done(); return; }
    auto remaining = std::make_shared<int>(ids.size());
    auto queue = std::make_shared<QStringList>(ids);
    auto inflight = std::make_shared<int>(0);
    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, remaining, queue, inflight, pump, done]() {
        while (*inflight < kMaxParallelThreadFetches && !queue->isEmpty()) {
            const QString id = queue->takeFirst();
            ++*inflight;
            fetchThread(id, [remaining, inflight, pump, done](bool) {
                --*inflight;
                if (--*remaining == 0) { if (done) done(); return; }
                (*pump)();
            });
        }
    };
    (*pump)();
}

void SyncEngine::fetchThread(const QString& threadId, std::function<void(bool)> cb)
{
    m_gmail->getThread(threadId, [this, threadId, cb](const QJsonObject& o, int status, const QString&) {
        if (status == 404) { m_store->removeThread(threadId); if (cb) cb(false); return; }
        if (status != 200) { if (cb) cb(false); return; }
        Thread t = MimeMessage::parseThread(o);
        if (t.messages.isEmpty()) { m_store->removeThread(threadId); if (cb) cb(false); return; }
        // Carry draft ids over for messages labelled DRAFT (drafts.list is needed for mapping).
        harvestContacts(t);
        if (!m_store->blockedLabelId.isEmpty() && t.hasLabel(m_store->blockedLabelId)) m_store->setBlocked(t.id, true);
        m_store->upsertThread(t);
        reconcileThreadFolders(t);
        if (cb) cb(true);
    });
}

void SyncEngine::harvestContacts(const Thread& t)
{
    const QString self = userEmail().toLower();
    QList<EmailAddress> all;
    for (const auto& m : t.messages) {
        const qint64 ts = m.date.toMSecsSinceEpoch();
        QList<EmailAddress> addrs = m.to + m.cc + m.bcc;
        addrs << m.from;
        for (const auto& a : addrs) {
            if (!a.isValid() || a.email == self) continue;
            all << a;
            m_db->touchContact(a, ts);
        }
    }
}

bool SyncEngine::threadMatchesFolder(const Thread& t, const Folder& f) const
{
    if (f.key == QLatin1String("archive")) {
        static const QStringList excluded{QStringLiteral("INBOX"), QStringLiteral("SENT"), QStringLiteral("DRAFT"),
                                          QStringLiteral("SPAM"), QStringLiteral("TRASH")};
        for (const auto& l : excluded) if (t.hasLabel(l)) return false;
        if (!m_store->snoozedLabelId.isEmpty() && t.hasLabel(m_store->snoozedLabelId)) return false;
        if (!m_store->blockedLabelId.isEmpty() && t.hasLabel(m_store->blockedLabelId)) return false;
        return true;
    }
    for (const auto& l : f.labelIds) if (!l.isEmpty() && t.hasLabel(l)) return true;
    return false;
}

void SyncEngine::reconcileThreadFolders(const Thread& t)
{
    // Keep the active folder's id list consistent with the thread's labels.
    const Folder f = m_activeFolder;
    const bool matches = threadMatchesFolder(t, f);
    const bool listed = m_store->folderThreadIds(f.key).contains(t.id);
    if (matches && !listed) m_store->addThreadToFolder(f.key, t.id);
    else if (!matches && listed) m_store->removeThreadFromFolder(f.key, t.id);
}

// --- polling ----------------------------------------------------------------
void SyncEngine::onTick()
{
    // Detect system wake: a very long gap since the last tick means we slept.
    const qint64 gap = m_sinceLastTick.isValid() ? m_sinceLastTick.elapsed() : 0;
    m_sinceLastTick.restart();
    if (gap > m_pollTimer.interval() * 3) Q_EMIT statusMessage(QStringLiteral("Resumed — syncing…"));
    pollNow();
}

void SyncEngine::onAppFocus() { pollNow(); }

void SyncEngine::setRateLimited(bool on)
{
    if (m_rateLimited == on) return;
    m_rateLimited = on;
    m_pollTimer.setInterval(on ? kPollBackoffMs : kPollNormalMs);
    Q_EMIT syncStateChanged(m_polling, m_rateLimited);
    Q_EMIT statusMessage(on ? QStringLiteral("Gmail rate limit hit — polling every 120 s") : QStringLiteral("Polling every 30 s"));
}

void SyncEngine::pollNow()
{
    if (!m_started || m_polling) return;
    if (m_historyId.isEmpty()) { refreshActiveFolder(true); return; }
    m_polling = true;
    Q_EMIT syncStateChanged(true, m_rateLimited);
    m_gmail->listHistory(m_historyId, QString(), [this](const QJsonObject& o, int status, const QString& err) {
        m_polling = false;
        Q_EMIT syncStateChanged(false, m_rateLimited);
        if (status == 404) {
            // History expired: resync current folder and reset the cursor.
            m_gmail->getProfile([this](const QJsonObject& p, int st, const QString&) {
                if (st == 200) { m_historyId = p.value(QStringLiteral("historyId")).toVariant().toString(); m_db->setKv(QStringLiteral("history_id"), m_historyId); }
                refreshActiveFolder(true);
            });
            return;
        }
        if (status != 200) { if (!err.isEmpty()) Q_EMIT statusMessage(QStringLiteral("Sync error: ") + err); return; }
        applyHistory(o);
    });
}

void SyncEngine::applyHistory(const QJsonObject& o)
{
    QSet<QString> changed;
    QSet<QString> newInInbox;
    for (const auto hv : o.value(QStringLiteral("history")).toArray()) {
        const auto h = hv.toObject();
        auto collect = [&](const char* key, bool isAdd) {
            for (const auto mv : h.value(QLatin1String(key)).toArray()) {
                const auto msg = mv.toObject().value(QStringLiteral("message")).toObject();
                const QString tid = msg.value(QStringLiteral("threadId")).toString();
                if (tid.isEmpty()) continue;
                changed.insert(tid);
                if (isAdd) {
                    bool inbox = false;
                    for (const auto l : msg.value(QStringLiteral("labelIds")).toArray()) if (l.toString() == QLatin1String("INBOX")) inbox = true;
                    if (inbox && !m_knownInboxThreads.contains(tid)) newInInbox.insert(tid);
                }
            }
        };
        collect("messagesAdded", true);
        collect("messagesDeleted", false);
        collect("labelsAdded", false);
        collect("labelsRemoved", false);
    }
    const QString newHistoryId = o.value(QStringLiteral("historyId")).toVariant().toString();
    if (!newHistoryId.isEmpty()) { m_historyId = newHistoryId; m_db->setKv(QStringLiteral("history_id"), m_historyId); }

    if (changed.isEmpty()) return;
    const QStringList ids = changed.values();
    fetchThreads(ids, [this, newInInbox]() {
        QStringList arrived;
        for (const auto& tid : newInInbox) {
            m_knownInboxThreads.insert(tid);
            if (const Thread* t = m_store->thread(tid)) {
                afterBlockedCheck(*t);
                if (!m_store->isBlocked(tid)) arrived << tid;
            }
        }
        if (!arrived.isEmpty()) Q_EMIT newMessagesArrived(arrived);
    });
}

void SyncEngine::afterBlockedCheck(const Thread& t)
{
    const Message* latest = t.latest();
    if (!latest || !latest->labelIds.contains(QStringLiteral("INBOX"))) return;
    if (!m_db->isBlocked(latest->from.email)) return;
    m_store->setBlocked(t.id, true);
    QStringList add;
    if (!m_store->blockedLabelId.isEmpty()) add << m_store->blockedLabelId;
    m_gmail->modifyThread(t.id, add, {QStringLiteral("INBOX"), QStringLiteral("UNREAD")}, [this, tid = t.id](const QJsonObject&, int, const QString&) {
        m_gmail->trashThread(tid, [this, tid](const QJsonObject&, int, const QString&) { fetchThread(tid); });
    });
    Q_EMIT statusMessage(QStringLiteral("Blocked sender auto-trashed: ") + latest->from.email);
}

// --- snooze -----------------------------------------------------------------
void SyncEngine::checkSnoozes()
{
    const auto expired = m_db->expiredSnoozes(util::nowMs());
    for (const auto& s : expired) {
        QStringList add{QStringLiteral("INBOX")};
        for (const auto& l : s.originalLabels) if (l == QLatin1String("UNREAD") || l == QLatin1String("STARRED")) add << l;
        QStringList remove;
        if (!m_store->snoozedLabelId.isEmpty()) remove << m_store->snoozedLabelId;
        m_gmail->modifyThread(s.threadId, add, remove, [this, tid = s.threadId](const QJsonObject&, int status, const QString&) {
            if (status == 200 || status == 404) m_db->unsnoozeThread(tid);
            fetchThread(tid, [this](bool) { if (m_activeFolder.key == QLatin1String("inbox") || m_activeFolder.key == QLatin1String("snoozed")) refreshActiveFolder(true); });
        });
    }
}

void SyncEngine::snoozeThread(const QString& threadId, qint64 untilMs)
{
    const Thread* t = m_store->thread(threadId);
    const QStringList original = t ? t->allLabels() : QStringList{};
    m_db->snoozeThread(threadId, untilMs, original);
    QStringList add;
    if (!m_store->snoozedLabelId.isEmpty()) add << m_store->snoozedLabelId;
    m_store->removeThreadFromFolder(QStringLiteral("inbox"), threadId);
    m_gmail->modifyThread(threadId, add, {QStringLiteral("INBOX")}, [this, threadId](const QJsonObject&, int, const QString&) { fetchThread(threadId); });
}

void SyncEngine::unsnoozeThread(const QString& threadId)
{
    m_db->unsnoozeThread(threadId);
    QStringList remove;
    if (!m_store->snoozedLabelId.isEmpty()) remove << m_store->snoozedLabelId;
    m_store->removeThreadFromFolder(QStringLiteral("snoozed"), threadId);
    m_gmail->modifyThread(threadId, {QStringLiteral("INBOX")}, remove, [this, threadId](const QJsonObject&, int, const QString&) { fetchThread(threadId); });
}

// --- mutations --------------------------------------------------------------
void SyncEngine::modify(const QStringList& threadIds, const QStringList& add, const QStringList& remove)
{
    for (const auto& id : threadIds) {
        if (Thread* t = m_store->mutableThread(id)) {
            for (auto& m : t->messages) {
                for (const auto& l : add) if (!m.labelIds.contains(l)) m.labelIds << l;
                for (const auto& l : remove) m.labelIds.removeAll(l);
            }
            Q_EMIT m_store->threadChanged(id);
            reconcileThreadFolders(*t);
        }
        m_gmail->modifyThread(id, add, remove, [this, id](const QJsonObject&, int status, const QString& err) {
            if (status != 200) { Q_EMIT errorOccurred(QStringLiteral("Modify failed: ") + err); fetchThread(id); }
        });
    }
}

void SyncEngine::markRead(const QStringList& threadIds, bool read)
{
    if (read) modify(threadIds, {}, {QStringLiteral("UNREAD")});
    else modify(threadIds, {QStringLiteral("UNREAD")}, {});
}

void SyncEngine::setStarred(const QString& threadId, bool starred)
{
    if (starred) modify({threadId}, {QStringLiteral("STARRED")}, {});
    else modify({threadId}, {}, {QStringLiteral("STARRED")});
}

void SyncEngine::trashThreads(const QStringList& threadIds)
{
    for (const auto& id : threadIds) {
        m_store->removeThreadFromFolder(m_activeFolder.key, id);
        m_gmail->trashThread(id, [this, id](const QJsonObject&, int status, const QString& err) {
            if (status != 200) Q_EMIT errorOccurred(QStringLiteral("Delete failed: ") + err);
            fetchThread(id);
        });
    }
}

void SyncEngine::archiveThreads(const QStringList& threadIds)
{
    modify(threadIds, {}, {QStringLiteral("INBOX")});
}

void SyncEngine::deletePermanently(const QStringList& threadIds)
{
    for (const auto& id : threadIds) {
        m_store->removeThread(id);
        m_gmail->deleteThread(id, [this](const QJsonObject&, int status, const QString& err) {
            if (status != 204 && status != 200) Q_EMIT errorOccurred(QStringLiteral("Permanent delete failed: ") + err);
        });
    }
}

void SyncEngine::blockSender(const QString& threadId, const QString& pattern)
{
    m_db->addBlockedSender(pattern);
    m_store->setBlocked(threadId, true);
    QStringList add;
    if (!m_store->blockedLabelId.isEmpty()) add << m_store->blockedLabelId;
    m_store->removeThreadFromFolder(m_activeFolder.key, threadId);
    m_gmail->modifyThread(threadId, add, {QStringLiteral("INBOX"), QStringLiteral("UNREAD")}, [this, threadId](const QJsonObject&, int, const QString&) {
        m_gmail->trashThread(threadId, [this, threadId](const QJsonObject&, int, const QString&) { fetchThread(threadId); });
    });
}

void SyncEngine::countFolder(const QString& folderKey, std::function<void(int)> cb)
{
    const QString label = folderKey == QLatin1String("trash") ? QStringLiteral("TRASH") : QStringLiteral("SPAM");
    m_gmail->getLabel(label, [cb](const QJsonObject& o, int status, const QString&) {
        cb(status == 200 ? o.value(QStringLiteral("messagesTotal")).toInt() : -1);
    });
}

void SyncEngine::emptyFolder(const QString& folderKey, std::function<void(int, const QString&)> done)
{
    const QString label = folderKey == QLatin1String("trash") ? QStringLiteral("TRASH") : QStringLiteral("SPAM");
    auto total = std::make_shared<int>(0);
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, label, total, step, done, folderKey]() {
        m_gmail->listMessages(QStringLiteral("in:anywhere"), {label}, 500, QString(),
            [this, label, total, step, done, folderKey](const QJsonObject& o, int status, const QString& err) {
                if (status != 200) { done(*total, err); return; }
                QStringList ids;
                for (const auto v : o.value(QStringLiteral("messages")).toArray()) ids << v.toObject().value(QStringLiteral("id")).toString();
                if (ids.isEmpty()) {
                    for (const auto& tid : m_store->folderThreadIds(folderKey)) m_store->removeThread(tid);
                    m_store->setFolderThreadIds(folderKey, {}, false);
                    done(*total, QString());
                    return;
                }
                m_gmail->batchDeleteMessages(ids, [ids, total, step, done](const QJsonObject&, int st, const QString& e) {
                    if (st != 204 && st != 200) { done(*total, e); return; }
                    *total += ids.size();
                    (*step)();
                });
            });
    };
    (*step)();
}

void SyncEngine::afterSend()
{
    auto sweep = [this]() {
        if (m_activeFolder.key == QLatin1String("sent")) refreshActiveFolder(true);
        pollNow();
    };
    sweep();
    QTimer::singleShot(3000, this, sweep);
    QTimer::singleShot(10000, this, sweep);
}

} // namespace ponymail
