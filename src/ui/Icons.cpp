#include "Icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace ponymail {

QPixmap Icons::pixmap(const QString& name, const QColor& color, int size, qreal dpr)
{
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    paint(p, name, QRectF(0, 0, size, size), color);
    return pm;
}

QIcon Icons::icon(const QString& name, const QColor& color, int size)
{
    QIcon ic;
    ic.addPixmap(pixmap(name, color, size, 1.0));
    ic.addPixmap(pixmap(name, color, size, 2.0));
    return ic;
}

void Icons::paint(QPainter& p, const QString& name, const QRectF& r, const QColor& color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = qMin(r.width(), r.height());
    p.translate(r.center() - QPointF(s / 2, s / 2));
    p.scale(s / 24.0, s / 24.0);   // design grid 24x24
    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    auto stroke = [&](const QPainterPath& path) { p.drawPath(path); };
    auto fill = [&](const QPainterPath& path) { p.fillPath(path, color); };

    if (name == QLatin1String("compose") || name == QLatin1String("pencil")) {
        QPainterPath path;
        path.moveTo(4, 20); path.lineTo(4.5, 16); path.lineTo(16, 4.5); path.lineTo(19.5, 8); path.lineTo(8, 19.5); path.closeSubpath();
        stroke(path);
        p.drawLine(QPointF(14, 6.5), QPointF(17.5, 10));
        if (name == QLatin1String("compose")) p.drawLine(QPointF(12, 20), QPointF(20, 20));
    } else if (name == QLatin1String("reply") || name == QLatin1String("replyAll")) {
        QPainterPath path;
        path.moveTo(10, 6); path.lineTo(4, 11.5); path.lineTo(10, 17);
        path.moveTo(4.5, 11.5); path.cubicTo(13, 11.5, 18, 12.5, 20, 19);
        stroke(path);
        if (name == QLatin1String("replyAll")) { QPainterPath a; a.moveTo(7, 6.5); a.lineTo(1.5, 11.5); a.lineTo(7, 16.5); stroke(a); }
    } else if (name == QLatin1String("forward")) {
        QPainterPath path;
        path.moveTo(14, 6); path.lineTo(20, 11.5); path.lineTo(14, 17);
        path.moveTo(19.5, 11.5); path.cubicTo(11, 11.5, 6, 12.5, 4, 19);
        stroke(path);
    } else if (name == QLatin1String("trash") || name == QLatin1String("discard")) {
        p.drawLine(QPointF(4, 7), QPointF(20, 7));
        p.drawRoundedRect(QRectF(6.5, 7, 11, 13), 1.5, 1.5);
        p.drawLine(QPointF(9.5, 7), QPointF(9.5, 4.5)); p.drawLine(QPointF(9.5, 4.5), QPointF(14.5, 4.5)); p.drawLine(QPointF(14.5, 4.5), QPointF(14.5, 7));
        p.drawLine(QPointF(10, 10.5), QPointF(10, 17)); p.drawLine(QPointF(14, 10.5), QPointF(14, 17));
    } else if (name == QLatin1String("archive")) {
        p.drawRoundedRect(QRectF(3.5, 4.5, 17, 4), 1, 1);
        p.drawRect(QRectF(5, 8.5, 14, 11));
        p.drawLine(QPointF(10, 13), QPointF(14, 13));
    } else if (name == QLatin1String("snooze")) {
        p.drawEllipse(QPointF(12, 13), 7.5, 7.5);
        p.drawLine(QPointF(12, 9), QPointF(12, 13)); p.drawLine(QPointF(12, 13), QPointF(15, 15));
        p.drawLine(QPointF(4, 5), QPointF(7, 3)); p.drawLine(QPointF(20, 5), QPointF(17, 3));
    } else if (name == QLatin1String("block")) {
        p.drawEllipse(QPointF(12, 12), 8, 8);
        p.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
    } else if (name == QLatin1String("star") || name == QLatin1String("starFilled")) {
        QPainterPath path;
        for (int i = 0; i < 10; ++i) {
            const qreal ang = -M_PI / 2 + i * M_PI / 5;
            const qreal rad = (i % 2 == 0) ? 9.0 : 4.0;
            const QPointF pt(12 + rad * qCos(ang), 12.5 + rad * qSin(ang));
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        if (name == QLatin1String("starFilled")) fill(path); else stroke(path);
    } else if (name == QLatin1String("envelope") || name == QLatin1String("unreadToggle")) {
        p.drawRoundedRect(QRectF(3, 6, 18, 13), 2, 2);
        QPainterPath v; v.moveTo(3.5, 7); v.lineTo(12, 13.5); v.lineTo(20.5, 7); stroke(v);
        if (name == QLatin1String("unreadToggle")) {
            p.setBrush(QColor("#0F6CBD")); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(19, 6), 3.5, 3.5);
        }
    } else if (name == QLatin1String("envelopeOpen")) {
        QPainterPath path;
        path.moveTo(3, 10); path.lineTo(12, 4); path.lineTo(21, 10); path.lineTo(21, 19); path.lineTo(3, 19); path.closeSubpath();
        stroke(path);
        QPainterPath v; v.moveTo(3.5, 10.5); v.lineTo(12, 15.5); v.lineTo(20.5, 10.5); stroke(v);
    } else if (name == QLatin1String("paperclip") || name == QLatin1String("attach")) {
        QPainterPath path;
        path.moveTo(16.5, 7); path.lineTo(9, 14.5);
        path.cubicTo(7.5, 16, 9.5, 18.5, 11.5, 17); path.lineTo(18, 10.5);
        path.cubicTo(21, 7.5, 17, 3, 14, 6); path.lineTo(7, 13);
        path.cubicTo(3, 17, 8.5, 22, 12.5, 18.5); path.lineTo(18.5, 12.5);
        stroke(path);
    } else if (name == QLatin1String("search")) {
        p.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
        p.drawLine(QPointF(15.5, 15.5), QPointF(20.5, 20.5));
    } else if (name == QLatin1String("close")) {
        p.drawLine(QPointF(6, 6), QPointF(18, 18)); p.drawLine(QPointF(18, 6), QPointF(6, 18));
    } else if (name == QLatin1String("gear")) {
        QPainterPath path;
        for (int i = 0; i < 16; ++i) {
            const qreal ang = i * M_PI / 8;
            const qreal rad = (i % 2 == 0) ? 9.0 : 7.0;
            const QPointF pt(12 + rad * qCos(ang), 12 + rad * qSin(ang));
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        stroke(path);
        p.drawEllipse(QPointF(12, 12), 3, 3);
    } else if (name == QLatin1String("wand")) {
        p.drawLine(QPointF(4, 20), QPointF(15, 9));
        QPainterPath star;
        for (int i = 0; i < 8; ++i) {
            const qreal ang = i * M_PI / 4;
            const qreal rad = (i % 2 == 0) ? 4.5 : 1.5;
            const QPointF pt(17.5 + rad * qCos(ang), 6.5 + rad * qSin(ang));
            if (i == 0) star.moveTo(pt); else star.lineTo(pt);
        }
        star.closeSubpath();
        fill(star);
        p.drawLine(QPointF(5, 5), QPointF(5, 8)); p.drawLine(QPointF(3.5, 6.5), QPointF(6.5, 6.5));
        p.drawLine(QPointF(20, 14), QPointF(20, 17)); p.drawLine(QPointF(18.5, 15.5), QPointF(21.5, 15.5));
    } else if (name == QLatin1String("filter")) {
        QPainterPath path; path.moveTo(3, 5); path.lineTo(21, 5); path.lineTo(14, 13); path.lineTo(14, 20); path.lineTo(10, 18); path.lineTo(10, 13); path.closeSubpath();
        stroke(path);
    } else if (name == QLatin1String("send")) {
        QPainterPath path; path.moveTo(3, 12); path.lineTo(21, 4); path.lineTo(15, 21); path.lineTo(12, 13); path.closeSubpath();
        stroke(path); p.drawLine(QPointF(12, 13), QPointF(21, 4));
    } else if (name == QLatin1String("image")) {
        p.drawRoundedRect(QRectF(3.5, 5, 17, 14), 2, 2);
        p.drawEllipse(QPointF(8.5, 9.5), 1.8, 1.8);
        QPainterPath m; m.moveTo(4, 17); m.lineTo(10, 12); m.lineTo(14, 15.5); m.lineTo(16.5, 13); m.lineTo(20, 17); stroke(m);
    } else if (name == QLatin1String("calendar")) {
        p.drawRoundedRect(QRectF(3.5, 5, 17, 15), 2, 2);
        p.drawLine(QPointF(3.5, 9.5), QPointF(20.5, 9.5));
        p.drawLine(QPointF(8, 3), QPointF(8, 7)); p.drawLine(QPointF(16, 3), QPointF(16, 7));
    } else if (name == QLatin1String("refresh")) {
        QPainterPath a; a.arcMoveTo(QRectF(5, 5, 14, 14), 30); a.arcTo(QRectF(5, 5, 14, 14), 30, 300); stroke(a);
        QPainterPath h; h.moveTo(19.5, 4.5); h.lineTo(19.5, 9.5); h.lineTo(14.5, 9.5); stroke(h);
    } else if (name == QLatin1String("chevronDown")) {
        QPainterPath c; c.moveTo(6, 9.5); c.lineTo(12, 15.5); c.lineTo(18, 9.5); stroke(c);
    } else if (name == QLatin1String("chevronRight")) {
        QPainterPath c; c.moveTo(9.5, 6); c.lineTo(15.5, 12); c.lineTo(9.5, 18); stroke(c);
    } else if (name == QLatin1String("check")) {
        QPainterPath c; c.moveTo(5, 12.5); c.lineTo(10, 17.5); c.lineTo(19, 7); stroke(c);
    } else if (name == QLatin1String("turtle")) {
        // Original stylised terrapin silhouette for the Testudo easter egg (not a reproduction of any trademark).
        p.setPen(Qt::NoPen); p.setBrush(color);
        p.drawEllipse(QRectF(5, 7, 14, 9));                 // shell
        p.drawEllipse(QRectF(17.5, 10, 4.5, 3.5));          // head
        p.drawEllipse(QRectF(6, 14.5, 3, 2.2)); p.drawEllipse(QRectF(15, 14.5, 3, 2.2));
        p.drawEllipse(QRectF(2.5, 11.5, 3, 1.8));           // tail
        p.setBrush(QColor(255, 255, 255, 120));
        p.drawEllipse(QRectF(8, 8.5, 8, 5));                // shell highlight
    }
    p.restore();
}

} // namespace ponymail
