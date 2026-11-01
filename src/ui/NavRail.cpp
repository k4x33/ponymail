#include "NavRail.h"
#include "Icons.h"
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QHeaderView>

namespace ponymail {

NavRail::NavRail(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* top = new QWidget(this);
    auto* topL = new QHBoxLayout(top);
    topL->setContentsMargins(10, 10, 10, 6);
    m_compose = new QToolButton(top);
    m_compose->setText(QStringLiteral("  New mail"));
    m_compose->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_compose->setCursor(Qt::PointingHandCursor);
    m_compose->setToolTip(QStringLiteral("New mail (C)"));
    topL->addWidget(m_compose);
    topL->addStretch();
    layout->addWidget(top);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("navRail"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(10);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(m_tree, 1);

    auto* bottom = new QWidget(this);
    auto* botL = new QHBoxLayout(bottom);
    botL->setContentsMargins(10, 6, 10, 10);
    m_account = new QLabel(bottom);
    m_account->setProperty("muted", true);
    m_account->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_settings = new QToolButton(bottom);
    m_settings->setToolTip(QStringLiteral("Settings"));
    m_settings->setPopupMode(QToolButton::InstantPopup);
    m_settings->setCursor(Qt::PointingHandCursor);
    botL->addWidget(m_account, 1);
    botL->addWidget(m_settings);
    layout->addWidget(bottom);

    connect(m_compose, &QToolButton::clicked, this, &NavRail::composeRequested);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur) return;
        const QString key = cur->data(0, Qt::UserRole).toString();
        if (key.isEmpty()) return;   // section header
        if (m_folders.contains(key)) Q_EMIT folderSelected(m_folders.value(key));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* it = m_tree->itemAt(pos);
        if (!it) return;
        const QString key = it->data(0, Qt::UserRole).toString();
        if (key != QLatin1String("trash") && key != QLatin1String("spam")) return;
        QMenu menu(this);
        QAction* empty = menu.addAction(Icons::icon(QStringLiteral("trash"), m_theme.danger), QStringLiteral("Empty Folder…"));
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == empty) Q_EMIT emptyFolderRequested(m_folders.value(key));
    });
    setMinimumWidth(190);
}

void NavRail::setTheme(const Theme& t)
{
    m_theme = t;
    m_compose->setIcon(Icons::icon(QStringLiteral("compose"), t.accent));
    m_settings->setIcon(Icons::icon(QStringLiteral("gear"), t.textMuted));
    const QColor c = t.textMuted;
    static const QHash<QString, QString> icons{
        {QStringLiteral("inbox"), QStringLiteral("envelope")}, {QStringLiteral("snoozed"), QStringLiteral("snooze")},
        {QStringLiteral("sent"), QStringLiteral("send")}, {QStringLiteral("drafts"), QStringLiteral("pencil")},
        {QStringLiteral("spam"), QStringLiteral("block")}, {QStringLiteral("trash"), QStringLiteral("trash")},
        {QStringLiteral("archive"), QStringLiteral("archive")}, {QStringLiteral("blocked"), QStringLiteral("block")}};
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        const QString ic = icons.value(it.key(), QStringLiteral("chevronRight"));
        it.value()->setIcon(0, Icons::icon(ic, it.key() == QLatin1String("blocked") ? t.danger : c, 16));
    }
}

void NavRail::setFolders(const QList<Folder>& system, const QList<Folder>& labels)
{
    const QString current = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toString() : QStringLiteral("inbox");
    m_tree->blockSignals(true);
    m_tree->clear();
    m_items.clear();
    m_folders.clear();

    auto addSection = [this](const QString& title) {
        auto* h = new QTreeWidgetItem(m_tree, {title});
        h->setFlags(Qt::ItemIsEnabled);
        QFont f = h->font(0); f.setBold(true); f.setPointSizeF(9); h->setFont(0, f);
        h->setForeground(0, m_theme.textFaint);
        return h;
    };
    addSection(QStringLiteral("FOLDERS"));
    for (const auto& f : system) {
        auto* it = new QTreeWidgetItem(m_tree, {f.title});
        it->setData(0, Qt::UserRole, f.key);
        m_items[f.key] = it; m_folders[f.key] = f;
    }
    addSection(QStringLiteral("LABELS"));
    for (const auto& f : labels) {
        auto* it = new QTreeWidgetItem(m_tree, {f.title});
        it->setData(0, Qt::UserRole, f.key);
        m_items[f.key] = it; m_folders[f.key] = f;
    }
    m_tree->blockSignals(false);
    setTheme(m_theme);
    selectFolder(m_folders.contains(current) ? current : QStringLiteral("inbox"));
}

void NavRail::selectFolder(const QString& key)
{
    if (auto* it = m_items.value(key)) m_tree->setCurrentItem(it);
}

void NavRail::setUnreadCount(const QString& key, int count)
{
    auto* it = m_items.value(key);
    if (!it) return;
    const QString title = m_folders.value(key).title;
    it->setText(0, count > 0 ? QStringLiteral("%1   %2").arg(title).arg(count) : title);
}

void NavRail::setAccountLabel(const QString& text) { m_account->setText(text); }

} // namespace ponymail
