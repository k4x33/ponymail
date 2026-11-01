#include "MessageListView.h"
#include "MessageListModel.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QTimer>

namespace ponymail {

MessageListView::MessageListView(QWidget* parent) : QListView(parent), m_delegate(new MessageListDelegate(this))
{
    setObjectName(QStringLiteral("messageList"));
    setItemDelegate(m_delegate);
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);   // Ctrl/Shift multi-select
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFrameShape(QFrame::NoFrame);
    verticalScrollBar()->setSingleStep(24);
}

void MessageListView::setTheme(const Theme& t) { m_delegate->setTheme(t); viewport()->update(); }

QStringList MessageListView::selectedThreadIds() const
{
    QStringList ids;
    for (const auto& idx : selectionModel()->selectedIndexes()) ids << idx.data(MessageListModel::ThreadIdRole).toString();
    return ids;
}

void MessageListView::mouseMoveEvent(QMouseEvent* e)
{
    const QModelIndex idx = indexAt(e->pos());
    m_delegate->setHoverIndex(idx);
    viewport()->update();
    QListView::mouseMoveEvent(e);
}

void MessageListView::leaveEvent(QEvent* e)
{
    m_delegate->setHoverIndex(QModelIndex());
    viewport()->update();
    QListView::leaveEvent(e);
}

void MessageListView::mousePressEvent(QMouseEvent* e)
{
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid() && e->button() == Qt::LeftButton) {
        const QRect rr = visualRect(idx);
        const auto action = m_delegate->actionAt(rr, e->pos());
        const QString id = idx.data(MessageListModel::ThreadIdRole).toString();
        if (action == MessageListDelegate::Star) { Q_EMIT quickStar(id); return; }
        if (action == MessageListDelegate::Delete) { Q_EMIT quickDelete(id); return; }
        if (action == MessageListDelegate::ReadToggle) { Q_EMIT quickReadToggle(id); return; }
    }
    QListView::mousePressEvent(e);
}

void MessageListView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid()) Q_EMIT rowActivated(idx.data(MessageListModel::ThreadIdRole).toString());
    QListView::mouseDoubleClickEvent(e);
}

void MessageListView::scrollContentsBy(int dx, int dy)
{
    QListView::scrollContentsBy(dx, dy);
    emitVisible();
    if (verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 40) Q_EMIT reachedBottom();
}

void MessageListView::resizeEvent(QResizeEvent* e)
{
    QListView::resizeEvent(e);
    emitVisible();
}

QPair<int, int> MessageListView::visibleRowRange() const
{
    if (!model() || model()->rowCount() == 0) return {0, -1};
    const int first = qMax(0, indexAt(QPoint(5, 1)).row());
    QModelIndex lastIdx = indexAt(QPoint(5, viewport()->height() - 2));
    const int last = lastIdx.isValid() ? lastIdx.row() : model()->rowCount() - 1;
    return {first, last};
}

void MessageListView::emitVisible()
{
    const auto range = visibleRowRange();
    Q_EMIT visibleRangeChanged(range.first, range.second);
}

void MessageListView::selectRow(int row)
{
    if (!model() || row < 0 || row >= model()->rowCount()) return;
    const QModelIndex idx = model()->index(row, 0);
    selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    scrollTo(idx);
}

void MessageListView::animateRemove(const QString& threadId, std::function<void()> done)
{
    auto* m = qobject_cast<MessageListModel*>(model());
    if (!m) { if (done) done(); return; }
    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(double(viewport()->width()));
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [m, threadId](const QVariant& v) { m->setSwipeOffset(threadId, v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, this, [m, threadId, done]() { m->setSwipeOffset(threadId, 0.0); if (done) done(); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace ponymail
