#include "PlayPauseButtonItem.h"
#include <QPainter>
#include <algorithm>

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
    m_fitScale = scale;
    m_diameter = std::clamp(58.0 * scale * itemScale(), 28.0, 160.0);
    update();
}

void PlayPauseButtonItem::resizeTo(qreal width, qreal height)
{
    const qreal side = std::min(width, height);
    prepareGeometryChange();
    if (m_fitScale < 0.05) {
        m_diameter = std::clamp(side, 28.0, 120.0);
        update();
        return;
    }
    setItemScale(side / (58.0 * m_fitScale));
    m_diameter = std::clamp(58.0 * m_fitScale * itemScale(), 28.0, 160.0);
    update();
}

void PlayPauseButtonItem::fitWithin(qreal maxWidth, qreal maxHeight)
{
    const qreal cap = std::min(maxWidth, maxHeight);
    if (m_diameter <= cap) return;
    prepareGeometryChange();
    m_diameter = std::max(32.0, cap);
    update();
}

void PlayPauseButtonItem::applySkinColor(const QColor &color)
{
    setAccent(color);
}
