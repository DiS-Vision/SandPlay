#include "PlayPauseButtonItem.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

PlayPauseButtonItem::PlayPauseButtonItem(QGraphicsItem *parent)
    : DraggableItem("play_pause_button", parent)
{
}

QRectF PlayPauseButtonItem::boundingRect() const
{
    return QRectF(0, 0, m_diameter, m_diameter);
}

void PlayPauseButtonItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect();
    if (hasCustomIcon()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawPixmap(r.toRect(), customIcon());
        return;
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_accent);
    painter->drawEllipse(r);

    painter->setBrush(QColor("#14161c"));
    const qreal u = r.width() / 64.0;
    const QPointF c = r.center();
    if (m_playing) {
        painter->drawRoundedRect(QRectF(c.x() - 10 * u, c.y() - 12 * u, 6 * u, 24 * u), 1.5 * u, 1.5 * u);
        painter->drawRoundedRect(QRectF(c.x() + 4 * u, c.y() - 12 * u, 6 * u, 24 * u), 1.5 * u, 1.5 * u);
    } else {
        QPolygonF tri;
        tri << QPointF(c.x() - 8 * u, c.y() - 13 * u)
            << QPointF(c.x() - 8 * u, c.y() + 13 * u)
            << QPointF(c.x() + 14 * u, c.y());
        painter->drawPolygon(tri);
    }
}

void PlayPauseButtonItem::setPlaying(bool playing)
{
    m_playing = playing;
    update();
}

void PlayPauseButtonItem::setAccent(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    update();
}

void PlayPauseButtonItem::applyUiScale(qreal scale)
{
    prepareGeometryChange();
    if (m_customSize && m_fitScale >= 0.05) {
        const qreal factor = scale / m_fitScale;
        if (std::abs(factor - 1.0) > 0.001)
            m_diameter = std::max(28.0, m_diameter * factor);
    } else if (!m_customSize) {
        m_diameter = std::max(28.0, 58.0 * scale * itemScale());
    }
    m_fitScale = scale;
    update();
}

void PlayPauseButtonItem::resizeTo(qreal width, qreal height)
{
    const qreal side = std::max(28.0, std::min(width, height));
    prepareGeometryChange();
    m_diameter = side;
    m_customSize = true;
    if (m_fitScale >= 0.05)
        setItemScale(side / (58.0 * m_fitScale));
    update();
}

void PlayPauseButtonItem::fitWithin(qreal maxWidth, qreal maxHeight)
{
    const qreal cap = std::min(maxWidth, maxHeight);
    if (m_diameter <= cap) return;
    prepareGeometryChange();
    m_diameter = std::max(28.0, cap);
    update();
}

void PlayPauseButtonItem::applySkinColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    update();
}
