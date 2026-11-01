#include "MessageListDelegate.h"
#include "MessageListModel.h"
#include "Icons.h"
#include "model/Triage.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QApplication>

namespace ponymail {

MessageListDelegate::MessageListDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize MessageListDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const
{
    return QSize(opt.rect.width(), kRowHeight);
}

QRect MessageListDelegate::actionRect(const QRect& r, HoverAction a)
{
    const int size = 26, gap = 4;
    const int right = r.right() - kUnreadBarWidth - 10;
    const int y = r.top() + 8;
    switch (a) {
    case ReadToggle: return QRect(right - size, y, size, size);
    case Delete: return QRect(right - 2 * size - gap, y, size, size);
    case Star: return QRect(right - 3 * size - 2 * gap, y, size, size);
    default: return {};
    }
}

MessageListDelegate::HoverAction MessageListDelegate::actionAt(const QRect& rowRect, const QPoint& pos) const
{
    for (HoverAction a : {Star, Delete, ReadToggle}) if (actionRect(rowRect, a).contains(pos)) return a;
    return None;
}

void MessageListDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);

    QRect r = opt.rect;
    const qreal swipe = idx.data(MessageListModel::SwipeOffsetRole).toDouble();
    if (swipe != 0.0) { p->setOpacity(qMax(0.0, 1.0 - qAbs(swipe) / qMax(1, r.width()))); r.translate(int(swipe), 0); }

    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = m_hover.isValid() && m_hover == idx;
    const bool unread = idx.data(MessageListModel::UnreadRole).toBool();
    const bool starred = idx.data(MessageListModel::StarredRole).toBool();
    const bool blocked = idx.data(MessageListModel::BlockedRole).toBool();
    const bool hasAtt = idx.data(MessageListModel::HasAttachmentRole).toBool();
    const bool isDraft = idx.data(MessageListModel::IsDraftRole).toBool();
    const bool calendar = idx.data(MessageListModel::CalendarRole).toBool();
    const int msgCount = idx.data(MessageListModel::MessageCountRole).toInt();
    const auto tag = static_cast<TriageTag>(idx.data(MessageListModel::TriageTagRole).toInt());
    const int tagCount = idx.data(MessageListModel::TriageCountRole).toInt();

    // Background
    QColor bg = m_theme.surface;
    if (selected) bg = m_theme.selection;
    else if (hovered) bg = m_theme.hover;
    p->fillRect(r, bg);
    p->setPen(QPen(m_theme.border, 1));
    p->drawLine(r.bottomLeft() + QPoint(kTabWidth, 0), r.bottomRight());

    // Left-edge triage tab
    QRect tab(r.left(), r.top(), kTabWidth, r.height());
    const QColor tagColor = Triage::tagColor(tag);
    if (tag == TriageTag::Evaluating) {
        p->fillRect(tab, bg.darker(103));
        p->setBrush(tagColor); p->setPen(Qt::NoPen);
        p->drawEllipse(tab.center() + QPoint(1, 0), 4, 4);
    } else {
        p->fillRect(tab, tagColor);
        p->setPen(Qt::white);
        QFont f = opt.font; f.setBold(true); f.setPointSizeF(tag == TriageTag::FollowUp ? 8.5 : 10.5);
        p->setFont(f);
        const QString letter = Triage::tagLetter(tag);
        if (tag == TriageTag::Action && tagCount > 1) {
            p->drawText(QRect(tab.left(), tab.top() + 14, tab.width(), 18), Qt::AlignCenter, letter);
            QFont fc = f; fc.setPointSizeF(8.5); p->setFont(fc);
            p->drawText(QRect(tab.left(), tab.top() + 34, tab.width(), 16), Qt::AlignCenter, QString::number(tagCount));
        } else {
            p->drawText(tab, Qt::AlignCenter, letter);
        }
    }

    // Right-edge unread bar (always far right, never left)
    if (unread) p->fillRect(QRect(r.right() - kUnreadBarWidth + 1, r.top(), kUnreadBarWidth, r.height()), m_theme.unreadBar);

    // Text columns
    const int left = r.left() + kTabWidth + 12;
    const int rightPad = r.right() - kUnreadBarWidth - 12;
    int rightEdge = rightPad;
    if (hovered) rightEdge = actionRect(r, Star).left() - 8;

    QFont senderFont = opt.font; senderFont.setPointSizeF(10.5); senderFont.setBold(unread);
    QFont subjectFont = opt.font; subjectFont.setPointSizeF(10); subjectFont.setBold(unread);
    QFont snippetFont = opt.font; snippetFont.setPointSizeF(9.5);
    QFont timeFont = opt.font; timeFont.setPointSizeF(9);

    // Timestamp (top right, hidden under hover actions)
    const QString ts = idx.data(MessageListModel::TimestampRole).toString();
    QFontMetrics tfm(timeFont);
    const int tsWidth = tfm.horizontalAdvance(ts);
    if (!hovered) {
        p->setFont(timeFont);
        p->setPen(unread ? m_theme.accent : m_theme.textMuted);
        p->drawText(QRect(rightPad - tsWidth, r.top() + 10, tsWidth, 16), Qt::AlignRight | Qt::AlignVCenter, ts);
    }

    // Sender line
    p->setFont(senderFont);
    p->setPen(m_theme.text);
    QString sender = idx.data(MessageListModel::SenderRole).toString();
    int senderRight = hovered ? rightEdge : rightPad - tsWidth - 10;
    if (msgCount > 1) {
        QFont cf = timeFont; cf.setPointSizeF(8.5);
        QFontMetrics cfm(cf);
        const QString cnt = QString::number(msgCount);
        const int cw = cfm.horizontalAdvance(cnt) + 10;
        QRect cr(senderRight - cw, r.top() + 12, cw, 15);
        p->setPen(Qt::NoPen); p->setBrush(m_theme.surfaceAlt.darker(104));
        p->drawRoundedRect(cr, 7, 7);
        p->setFont(cf); p->setPen(m_theme.textMuted); p->drawText(cr, Qt::AlignCenter, cnt);
        senderRight = cr.left() - 6;
        p->setFont(senderFont); p->setPen(m_theme.text);
    }
    QFontMetrics sfm(senderFont);
    sender = sfm.elidedText(sender, Qt::ElideRight, senderRight - left);
    p->drawText(QRect(left, r.top() + 8, senderRight - left, 20), Qt::AlignLeft | Qt::AlignVCenter, sender);

    // Subject line with badges
    int x = left;
    const int subjY = r.top() + 30;
    if (isDraft) {
        QFont bf = timeFont; bf.setBold(true); p->setFont(bf); p->setPen(m_theme.danger);
        p->drawText(QRect(x, subjY, 46, 18), Qt::AlignVCenter, QStringLiteral("[Draft]"));
        x += 48;
    }
    p->setFont(subjectFont);
    p->setPen(unread ? m_theme.text : m_theme.textMuted);
    QString subject = idx.data(MessageListModel::SubjectRole).toString();
    int badgeSpace = 0;
    if (hasAtt) badgeSpace += 22;
    if (calendar) badgeSpace += 22;
    if (blocked) badgeSpace += 62;
    QFontMetrics sbfm(subjectFont);
    subject = sbfm.elidedText(subject, Qt::ElideRight, rightEdge - x - badgeSpace);
    p->drawText(QRect(x, subjY, rightEdge - x - badgeSpace, 18), Qt::AlignLeft | Qt::AlignVCenter, subject);

    int bx = rightEdge;
    if (blocked) {
        QRect br(bx - 58, subjY + 1, 58, 16);
        p->setPen(Qt::NoPen); p->setBrush(m_theme.danger);
        p->drawRoundedRect(br, 8, 8);
        QFont bf = timeFont; bf.setBold(true); bf.setPointSizeF(8); p->setFont(bf); p->setPen(Qt::white);
        p->drawText(br, Qt::AlignCenter, QStringLiteral("BLOCKED"));
        bx -= 64;
    }
    if (calendar) { Icons::paint(*p, QStringLiteral("calendar"), QRectF(bx - 18, subjY, 18, 18), m_theme.textMuted); bx -= 22; }
    if (hasAtt) { Icons::paint(*p, QStringLiteral("paperclip"), QRectF(bx - 18, subjY, 18, 18), m_theme.textMuted); bx -= 22; }

    // Snippet
    p->setFont(snippetFont);
    p->setPen(m_theme.textFaint);
    QFontMetrics snfm(snippetFont);
    QString snippet = idx.data(MessageListModel::SnippetRole).toString().simplified();
    snippet = snfm.elidedText(snippet, Qt::ElideRight, rightPad - left - (starred && !hovered ? 22 : 0));
    p->drawText(QRect(left, r.top() + 50, rightPad - left, 18), Qt::AlignLeft | Qt::AlignVCenter, snippet);

    // Persistent star indicator (bottom right) when starred and not hovered
    if (starred && !hovered) Icons::paint(*p, QStringLiteral("starFilled"), QRectF(rightPad - 16, r.top() + 50, 16, 16), m_theme.warning);

    // Hover quick actions
    if (hovered) {
        auto drawBtn = [&](HoverAction a, const QString& icon, const QColor& c) {
            const QRect br = actionRect(r, a);
            p->setPen(QPen(m_theme.border, 1)); p->setBrush(m_theme.surface);
            p->drawRoundedRect(br, 4, 4);
            Icons::paint(*p, icon, QRectF(br).adjusted(4, 4, -4, -4), c);
        };
        drawBtn(Star, starred ? QStringLiteral("starFilled") : QStringLiteral("star"), starred ? m_theme.warning : m_theme.textMuted);
        drawBtn(Delete, QStringLiteral("trash"), m_theme.textMuted);
        drawBtn(ReadToggle, unread ? QStringLiteral("envelopeOpen") : QStringLiteral("envelope"), m_theme.textMuted);
    }

    p->restore();
}

} // namespace ponymail
