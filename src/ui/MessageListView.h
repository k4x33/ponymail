#pragma once
#include "MessageListDelegate.h"
#include <QListView>

namespace ponymail {

class MessageListModel;

// QListView with hover tracking, quick-action hit testing, scroll-driven
// prefetch notifications and a swipe-out delete animation.
class MessageListView : public QListView {
    Q_OBJECT
public:
    explicit MessageListView(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    MessageListDelegate* rowDelegate() const { return m_delegate; }

    QStringList selectedThreadIds() const;
    void animateRemove(const QString& threadId, std::function<void()> done);
    void selectRow(int row);
    QPair<int, int> visibleRowRange() const;

Q_SIGNALS:
    void quickStar(const QString& threadId);
    void quickDelete(const QString& threadId);
    void quickReadToggle(const QString& threadId);
    void visibleRangeChanged(int first, int last);
    void reachedBottom();
    void rowActivated(const QString& threadId);

protected:
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void emitVisible();
    MessageListDelegate* m_delegate;
};

} // namespace ponymail
