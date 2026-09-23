#pragma once

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

// Right-pointing triangle with sand spilling off it. Color is the project terracotta.
inline QPixmap sandPlayLogoPixmap(int side)
{
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal s = side / 64.0;
    const QColor sand(QStringLiteral("#d97757"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(sand);
    QPolygonF triangle;
    triangle << QPointF(9 * s, 7 * s) << QPointF(9 * s, 39 * s) << QPointF(48 * s, 23 * s);
    painter.drawPolygon(triangle);

    struct Grain { qreal x, y, r, alpha; };
    const Grain grains[] = {
        {16, 43, 2.0, 0.95}, {26, 45, 1.6, 0.9},  {36, 44, 1.8, 0.85},
        {46, 34, 1.5, 0.9},  {50, 42, 1.3, 0.75}, {20, 50, 1.4, 0.7},
        {30, 52, 1.7, 0.65}, {40, 51, 1.2, 0.6},  {12, 54, 1.1, 0.5},
        {24, 57, 1.0, 0.45}, {44, 56, 1.3, 0.5},  {34, 59, 0.9, 0.4},
        {52, 50, 1.0, 0.45}, {18, 61, 0.8, 0.35}, {48, 60, 0.75, 0.3},
    };
    for (const Grain &grain : grains) {
        QColor color = sand;
        color.setAlphaF(grain.alpha);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(grain.x * s, grain.y * s), grain.r * s, grain.r * s);
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
