#pragma once
#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <QString>

namespace ponymail {

// Vector icons drawn with QPainter (no external icon font / SVG module).
// Names: compose, reply, replyAll, forward, trash, archive, snooze, block, star,
// starFilled, envelope, envelopeOpen, paperclip, search, close, gear, wand,
// filter, send, attach, discard, pencil, image, calendar, unreadToggle, refresh,
// chevronDown, chevronRight, check, turtle
class Icons {
public:
    static QIcon icon(const QString& name, const QColor& color, int size = 18);
    static QPixmap pixmap(const QString& name, const QColor& color, int size = 18, qreal dpr = 1.0);
    static void paint(QPainter& p, const QString& name, const QRectF& rect, const QColor& color);
};

} // namespace ponymail
