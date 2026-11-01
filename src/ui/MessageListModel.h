#pragma once
#include "model/MailStore.h"
#include "model/SenderResolver.h"
#include <QAbstractListModel>
#include <QHash>

namespace ponymail {

class Database;

// Client-side filter applied on top of the raw Gmail folder listing (spec §4.4).
enum class ListFilter { All, ActionOnly, FollowUpOnly, InfoOnly, Important, Starred, ToMe, HasFiles, CalendarInvites };

class MessageListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ThreadIdRole = Qt::UserRole + 1, SenderRole, SenderEmailRole, SubjectRole, SnippetRole, TimestampRole,
        UnreadRole, StarredRole, HasAttachmentRole, BlockedRole, TriageTagRole, TriageCountRole,
        IsDraftRole, SwipeOffsetRole, CalendarRole, MessageCountRole
    };

    MessageListModel(MailStore* store, Database* db, QObject* parent = nullptr);

    void setFolder(const QString& folderKey);
    QString folderKey() const { return m_folderKey; }
    void setFilter(ListFilter f);
    ListFilter filter() const { return m_filter; }
    void setUnreadOnly(bool on);
    bool unreadOnly() const { return m_unreadOnly; }
    void setLocalSearch(const QString& text);   // substring match on sender/subject/snippet (search chips go to Gmail)
    void setSort(const QString& sortKey);        // date_desc | date_asc | sender
    void setIdentity(const QString& email, const QString& name);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString threadIdAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows[row] : QString(); }
    int rowForThread(const QString& threadId) const { return m_rows.indexOf(threadId); }
    QStringList orderedThreadIds() const { return m_rows; }
    void setSwipeOffset(const QString& threadId, qreal px);
    void rebuild();

Q_SIGNALS:
    void rebuilt();

private:
    bool passesFilter(const Thread& t) const;

    MailStore* m_store;
    Database* m_db;
    QString m_folderKey;
    ListFilter m_filter = ListFilter::All;
    bool m_unreadOnly = false;
    QString m_search;
    QString m_sort = QStringLiteral("date_desc");
    QStringList m_rows;
    QHash<QString, qreal> m_swipe;
    SenderResolver m_resolver{QString(), QStringLiteral("Mike Brophy")};
};

} // namespace ponymail
