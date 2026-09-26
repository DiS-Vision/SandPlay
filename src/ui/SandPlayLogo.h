#pragma once

#include <QIcon>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <cmath>

// Pastel orange triangle pointing right, with a soft white edge. Three lighter
// grains sit on a straight line under the lower edge.
inline QPixmap sandPlayLogoPixmap(int side)
{
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal s = side / 64.0;

    const QPointF corners[] = {
        QPointF(11 * s, 9 * s),
        QPointF(11 * s, 44 * s),
        QPointF(48 * s, 26.5 * s),
    };
    const qreal radius = 5.5 * s;
    QPainterPath triangle;
    for (int i = 0; i < 3; ++i) {
        const QPointF curr = corners[i];
        const QPointF prev = corners[(i + 2) % 3];
        const QPointF next = corners[(i + 1) % 3];
        QLineF inbound(curr, prev);
        QLineF outbound(curr, next);
        const qreal r = std::min(radius, std::min(inbound.length(), outbound.length()) * 0.42);
        inbound.setLength(r);
        outbound.setLength(r);
        if (i == 0) triangle.moveTo(inbound.p2());
        else triangle.lineTo(inbound.p2());
        triangle.quadTo(curr, outbound.p2());
    }
    triangle.closeSubpath();

    const QPen round(Qt::white, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen glow = round;
    glow.setWidthF(6.2 * s);
    glow.setColor(QColor(255, 255, 255, 80));
    painter.setPen(glow);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(triangle);

    QPen edge = round;
    edge.setWidthF(2.2 * s);
    edge.setColor(Qt::white);
    painter.setPen(edge);
    painter.setBrush(QColor(QStringLiteral("#f6b48a")));
    painter.drawPath(triangle);

    // Parallel to the lower edge, dropped clear of it.
    const QPointF edgeStart = corners[1];
    const QPointF along = corners[2] - edgeStart;
    const qreal alongLen = std::hypot(along.x(), along.y());
    const QPointF dir = along / alongLen;
    const QPointF down(dir.y(), -dir.x());
    const QPointF outward = down.y() > 0 ? down : QPointF(-down.x(), -down.y());
    const qreal grain = 3.3 * s;
    const qreal gap = 8.2 * s;
    const qreal steps[] = {0.31, 0.58, 0.85};

    QPen grainEdge = round;
    grainEdge.setWidthF(1.7 * s);
    grainEdge.setColor(QColor(QStringLiteral("#e39a78")));
    painter.setPen(grainEdge);
    painter.setBrush(QColor(QStringLiteral("#f8c7a6")));
    for (qreal t : steps) {
        const QPointF center = edgeStart + along * t + outward * gap;
        painter.drawEllipse(center, grain, grain);
    }
    return pm;
}

inline QIcon sandPlayLogoIcon()
{
    QIcon icon;
    for (int side : {16, 24, 32, 48, 64, 256})
        icon.addPixmap(sandPlayLogoPixmap(side));
    return icon;
}
