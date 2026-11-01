#include "MessageListModel.h"
#include "storage/Database.h"
#include "model/Triage.h"

namespace ponymail {

MessageListModel::MessageListModel(MailStore* store, Database* db, QObject* parent)
    : QAbstractListModel(parent), m_store(store), m_db(db)
{
    connect(m_store, &MailStore::folderChanged, this, [this](const QString& key) { if (key == m_folderKey) rebuild(); });
    connect(m_store, &MailStore::threadChanged, this, [this](const QString& id) {
        const int row = m_rows.indexOf(id);
        if (row >= 0) {
            // Thread may no longer pass filters (e.g. became read while unread-only is on).
            const Thread* t = m_store->thread(id);
            if (!t || !passesFilter(*t)) { rebuild(); return; }
            Q_EMIT dataChanged(index(row), index(row));
        } else if (m_store->folderThreadIds(m_folderKey).contains(id)) {
            rebuild();
        }
    });
    connect(m_store, &MailStore::threadRemoved, this, [this](const QString& id) { if (m_rows.contains(id)) rebuild(); });
    connect(m_store, &MailStore::triageChanged, this, [this](const QString& id) {
        const int row = m_rows.indexOf(id);
        if (row >= 0) {
            if (m_filter == ListFilter::ActionOnly || m_filter == ListFilter::FollowUpOnly || m_filter == ListFilter::InfoOnly) {
                const Thread* t = m_store->thread(id);
                if (!t || !passesFilter(*t)) { rebuild(); return; }
            }
            Q_EMIT dataChanged(index(row), index(row));
        } else if (m_store->folderThreadIds(m_folderKey).contains(id)) {
            rebuild();
        }
    });
}

void MessageListModel::setIdentity(const QString& email, const QString& name)
{
    m_resolver = SenderResolver(email, name.isEmpty() ? QStringLiteral("Mike Brophy") : name);
    rebuild();
}

void MessageListModel::setFolder(const QString& folderKey) { m_folderKey = folderKey; rebuild(); }
void MessageListModel::setFilter(ListFilter f) { m_filter = f; rebuild(); }
void MessageListModel::setUnreadOnly(bool on) { m_unreadOnly = on; rebuild(); }
void MessageListModel::setLocalSearch(const QString& text) { m_search = text.trimmed(); rebuild(); }
void MessageListModel::setSort(const QString& sortKey) { m_sort = sortKey; rebuild(); }

bool MessageListModel::passesFilter(const Thread& t) const
{
    if (m_unreadOnly && !t.isUnread()) return false;
    const TriageResult tr = m_store->triage(t.id);
    switch (m_filter) {
    case ListFilter::All: break;
    case ListFilter::ActionOnly: if (tr.tag != TriageTag::Action) return false; break;
    case ListFilter::FollowUpOnly: if (tr.tag != TriageTag::FollowUp) return false; break;
    case ListFilter::InfoOnly: if (tr.tag != TriageTag::Info) return false; break;
    case ListFilter::Important: if (!t.hasLabel(QStringLiteral("IMPORTANT"))) return false; break;
    case ListFilter::Starred: if (!t.isStarred()) return false; break;
    case ListFilter::ToMe: {
        bool toMe = false;
        for (const auto& m : t.messages) for (const auto& a : m.to) if (m_resolver.isSelf(a)) toMe = true;
        if (!toMe) return false;
        break;
    }
    case ListFilter::HasFiles: if (!t.hasAttachments()) return false; break;
    case ListFilter::CalendarInvites: if (!t.hasCalendarInvite()) return false; break;
    }
    if (!m_search.isEmpty()) {
        const QString hay = m_resolver.displaySender(t) + ' ' + t.subject() + ' ' + t.snippet
            + ' ' + m_resolver.displaySenderEmail(t);
        if (!hay.contains(m_search, Qt::CaseInsensitive)) return false;
    }
    return true;
}

void MessageListModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    QList<const Thread*> threads = m_store->threadsInFolder(m_folderKey);
    QList<const Thread*> kept;
    for (const Thread* t : threads) if (passesFilter(*t)) kept << t;
    if (m_sort == QLatin1String("date_asc"))
        std::stable_sort(kept.begin(), kept.end(), [](const Thread* a, const Thread* b) { return a->latestDate() < b->latestDate(); });
    else if (m_sort == QLatin1String("sender"))
        std::stable_sort(kept.begin(), kept.end(), [this](const Thread* a, const Thread* b) {
            return m_resolver.displaySender(*a).localeAwareCompare(m_resolver.displaySender(*b)) < 0; });
    else
        std::stable_sort(kept.begin(), kept.end(), [](const Thread* a, const Thread* b) { return a->latestDate() > b->latestDate(); });
    for (const Thread* t : kept) m_rows << t->id;
    endResetModel();
    Q_EMIT rebuilt();
}

int MessageListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant MessageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const QString id = m_rows[index.row()];
    const Thread* t = m_store->thread(id);
    if (!t) return {};
    switch (role) {
    case ThreadIdRole: return id;
    case Qt::DisplayRole:
    case SubjectRole: return t->subject().isEmpty() ? QStringLiteral("(no subject)") : t->subject();
    case SenderRole: return m_resolver.displaySender(*t);
    case SenderEmailRole: return m_resolver.displaySenderEmail(*t);
    case SnippetRole: { QString s = t->latest() ? t->latest()->snippet : t->snippet; return s.replace(QStringLiteral("&#39;"), QStringLiteral("'")).replace(QStringLiteral("&amp;"), QStringLiteral("&")).replace(QStringLiteral("&quot;"), QStringLiteral("\"")); }
    case TimestampRole: return util::formatTimestamp(t->latestDate());
    case UnreadRole: return t->isUnread();
    case StarredRole: return t->isStarred();
    case HasAttachmentRole: return t->hasAttachments();
    case BlockedRole: return m_store->isBlocked(id);
    case TriageTagRole: return static_cast<int>(m_store->triage(id).tag);
    case TriageCountRole: return m_store->triage(id).openUserActions;
    case IsDraftRole: return t->hasLabel(QStringLiteral("DRAFT"));
    case SwipeOffsetRole: return m_swipe.value(id, 0.0);
    case CalendarRole: return t->hasCalendarInvite();
    case MessageCountRole: return t->messages.size();
    case Qt::ToolTipRole: return QStringLiteral("%1\n%2").arg(m_resolver.displaySender(*t), t->subject());
    }
    return {};
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {{ThreadIdRole, "threadId"}, {SenderRole, "sender"}, {SubjectRole, "subject"}, {SnippetRole, "snippet"},
            {TimestampRole, "timestamp"}, {UnreadRole, "unread"}, {StarredRole, "starred"}, {HasAttachmentRole, "hasAttachment"},
            {BlockedRole, "blocked"}, {TriageTagRole, "triageTag"}, {TriageCountRole, "triageCount"}};
}

void MessageListModel::setSwipeOffset(const QString& threadId, qreal px)
{
    if (px == 0.0) m_swipe.remove(threadId); else m_swipe[threadId] = px;
    const int row = m_rows.indexOf(threadId);
    if (row >= 0) Q_EMIT dataChanged(index(row), index(row), {SwipeOffsetRole});
}

} // namespace ponymail
