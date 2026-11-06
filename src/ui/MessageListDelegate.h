#pragma once
#include "Theme.h"
#include <QStyledItemDelegate>
#include <QPersistentModelIndex>

namespace ponymail {

// Outlook-style row: left-edge triage tab, sender/subject/snippet, timestamp,
// attachment & Blocked badges, hover quick actions, far-right unread bar.
class MessageListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum HoverAction { None, Star, Delete, ReadToggle };
    static constexpr int kTabWidth = 26;
    static constexpr int kUnreadBarWidth = 3;

    explicit MessageListDelegate(QObject* parent = nullptr);
    void setTheme(const Theme& t) { m_theme = t; }
    void setHoverIndex(const QModelIndex& idx) { m_hover = idx; }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;

    // Hit-testing for hover actions (view calls this on click).
    HoverAction actionAt(const QRect& rowRect, const QPoint& pos) const;
    static QRect actionRect(const QRect& rowRect, HoverAction a);
    static int rowHeightFor(const QFont& f);

private:
    Theme m_theme = Theme::light();
    QPersistentModelIndex m_hover;
};

} // namespace ponymail
