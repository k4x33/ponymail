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

// Row metrics derive from the view's font so the list matches the rest of the UI on every
// platform (13px on Windows, 13pt on macOS). k = line height of the base font in px.
int MessageListDelegate::rowHeightFor(const QFont& f)
{
    const QFontMetrics fm(f);
    return qRound(fm.height() * 4.1);   // sender + subject + snippet + padding
}

QSize MessageListDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const
{
    return QSize(opt.rect.width(), rowHeightFor(opt.font));
}

QRect MessageListDelegate::actionRect(const QRect& r, HoverAction a)
{
    const int size = qBound(24, r.height() / 3, 34), gap = 4;
    const int right = r.right() - kUnreadBarWidth - 10;
    const int y = r.top() + size / 3;
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
        QFont f = opt.font; f.setBold(true); f.setPointSizeF((opt.font.pointSizeF() > 0 ? opt.font.pointSizeF() : 10.0) * (tag == TriageTag::FollowUp ? 0.8 : 1.0));
        p->setFont(f);
        const QString letter = Triage::tagLetter(tag);
        if (tag == TriageTag::Action && tagCount > 1) {
            const int th = tab.height();
            p->drawText(QRect(tab.left(), tab.top() + th / 5, tab.width(), th / 4), Qt::AlignCenter, letter);
            QFont fc = f; fc.setPointSizeF(fc.pointSizeF() * 0.8); p->setFont(fc);
            p->drawText(QRect(tab.left(), tab.top() + th / 2, tab.width(), th / 4), Qt::AlignCenter, QString::number(tagCount));
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

    const QFont base = opt.font;
    const qreal basePt = base.pointSizeF() > 0 ? base.pointSizeF() : 10.0;
    auto scaled = [&](qreal factor, bool bold = false) { QFont f = base; f.setPointSizeF(basePt * factor); f.setBold(bold); return f; };
    QFont senderFont = scaled(1.0, unread);
    QFont subjectFont = scaled(0.95, unread);
    QFont snippetFont = scaled(0.9);
    QFont timeFont = scaled(0.85);
    const QFontMetrics baseFm(base);
    const int lh = baseFm.height();                 // one text line
    const int pad = qRound(lh * 0.45);
    const int line1 = r.top() + pad;                // sender
    const int line2 = line1 + lh + qRound(lh * 0.15);   // subject
    const int line3 = line2 + lh + qRound(lh * 0.15);   // snippet
    const int iconSz = qRound(lh * 1.1);

    // Timestamp (top right, hidden under hover actions)
    const QString ts = idx.data(MessageListModel::TimestampRole).toString();
    QFontMetrics tfm(timeFont);
    const int tsWidth = tfm.horizontalAdvance(ts);
    if (!hovered) {
        p->setFont(timeFont);
        p->setPen(unread ? m_theme.accent : m_theme.textMuted);
        p->drawText(QRect(rightPad - tsWidth, line1, tsWidth, lh), Qt::AlignRight | Qt::AlignVCenter, ts);
    }

    // Sender line
    p->setFont(senderFont);
    p->setPen(m_theme.text);
    QString sender = idx.data(MessageListModel::SenderRole).toString();
    int senderRight = hovered ? rightEdge : rightPad - tsWidth - 10;
    if (msgCount > 1) {
        QFont cf = scaled(0.8);
        QFontMetrics cfm(cf);
        const QString cnt = QString::number(msgCount);
        const int cw = cfm.horizontalAdvance(cnt) + 10;
        QRect cr(senderRight - cw, line1 + (lh - cfm.height()) / 2, cw, cfm.height());
        p->setPen(Qt::NoPen); p->setBrush(m_theme.surfaceAlt.darker(104));
        p->drawRoundedRect(cr, 7, 7);
        p->setFont(cf); p->setPen(m_theme.textMuted); p->drawText(cr, Qt::AlignCenter, cnt);
        senderRight = cr.left() - 6;
        p->setFont(senderFont); p->setPen(m_theme.text);
    }
    QFontMetrics sfm(senderFont);
    sender = sfm.elidedText(sender, Qt::ElideRight, senderRight - left);
    p->drawText(QRect(left, line1, senderRight - left, lh), Qt::AlignLeft | Qt::AlignVCenter, sender);

    // Subject line with badges
    int x = left;
    const int subjY = line2;
    if (isDraft) {
        QFont bf = timeFont; bf.setBold(true); p->setFont(bf); p->setPen(m_theme.danger);
        const int dw = QFontMetrics(bf).horizontalAdvance(QStringLiteral("[Draft]")) + 4;
        p->drawText(QRect(x, subjY, dw, lh), Qt::AlignVCenter, QStringLiteral("[Draft]"));
        x += dw + 2;
    }
    p->setFont(subjectFont);
    p->setPen(unread ? m_theme.text : m_theme.textMuted);
    QString subject = idx.data(MessageListModel::SubjectRole).toString();
    int badgeSpace = 0;
    if (hasAtt) badgeSpace += iconSz + 4;
    if (calendar) badgeSpace += iconSz + 4;
    if (blocked) badgeSpace += qRound(lh * 3.6);
    QFontMetrics sbfm(subjectFont);
    subject = sbfm.elidedText(subject, Qt::ElideRight, rightEdge - x - badgeSpace);
    p->drawText(QRect(x, subjY, rightEdge - x - badgeSpace, lh), Qt::AlignLeft | Qt::AlignVCenter, subject);

    int bx = rightEdge;
    if (blocked) {
        QFont bf = scaled(0.75, true);
        const int bw = QFontMetrics(bf).horizontalAdvance(QStringLiteral("BLOCKED")) + 12;
        const int bh = qRound(lh * 0.95);
        QRect br(bx - bw, subjY + (lh - bh) / 2, bw, bh);
        p->setPen(Qt::NoPen); p->setBrush(m_theme.danger);
        p->drawRoundedRect(br, bh / 2, bh / 2);
        p->setFont(bf); p->setPen(Qt::white);
        p->drawText(br, Qt::AlignCenter, QStringLiteral("BLOCKED"));
        bx -= bw + 6;
    }
    if (calendar) { Icons::paint(*p, QStringLiteral("calendar"), QRectF(bx - iconSz, subjY, iconSz, iconSz), m_theme.textMuted); bx -= iconSz + 4; }
    if (hasAtt) { Icons::paint(*p, QStringLiteral("paperclip"), QRectF(bx - iconSz, subjY, iconSz, iconSz), m_theme.textMuted); bx -= iconSz + 4; }

    // Snippet
    p->setFont(snippetFont);
    p->setPen(m_theme.textFaint);
    QFontMetrics snfm(snippetFont);
    QString snippet = idx.data(MessageListModel::SnippetRole).toString().simplified();
    snippet = snfm.elidedText(snippet, Qt::ElideRight, rightPad - left - (starred && !hovered ? iconSz + 4 : 0));
    p->drawText(QRect(left, line3, rightPad - left, lh), Qt::AlignLeft | Qt::AlignVCenter, snippet);

    // Persistent star indicator (bottom right) when starred and not hovered
    if (starred && !hovered) Icons::paint(*p, QStringLiteral("starFilled"), QRectF(rightPad - iconSz, line3, iconSz, iconSz), m_theme.warning);

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
