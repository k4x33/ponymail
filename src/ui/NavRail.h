#pragma once
#include "model/MailStore.h"
#include "Theme.h"
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class QLabel;

namespace ponymail {

// Left navigation rail: system folders, labels (+ "Blocked"), settings gear.
class NavRail : public QWidget {
    Q_OBJECT
public:
    explicit NavRail(QWidget* parent = nullptr);
    void setFolders(const QList<Folder>& system, const QList<Folder>& labels);
    void setTheme(const Theme& t);
    void selectFolder(const QString& key);
    void setUnreadCount(const QString& key, int count);
    void setAccountLabel(const QString& text);
    QToolButton* settingsButton() const { return m_settings; }

Q_SIGNALS:
    void folderSelected(const Folder& f);
    void emptyFolderRequested(const Folder& f);
    void composeRequested();

private:
    QTreeWidget* m_tree;
    QToolButton* m_settings;
    QToolButton* m_compose;
    QLabel* m_account;
    QHash<QString, Folder> m_folders;
    QHash<QString, QTreeWidgetItem*> m_items;
    Theme m_theme = Theme::light();
};

} // namespace ponymail
