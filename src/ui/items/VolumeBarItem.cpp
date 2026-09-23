#include "VolumeBarItem.h"
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

VolumeBarItem::VolumeBarItem(QGraphicsItem *parent) : DraggableItem("volume_bar", parent) {}

QRectF VolumeBarItem::barRect() const
{
    const qreal icon = std::max(18.0, m_fontPx * 1.7);
    const qreal percent = std::max(36.0, m_fontPx * 2.4);
    const qreal gap = 8.0;
    const qreal x = icon + gap;
    const qreal w = std::max(24.0, m_width - icon - percent - gap * 3 - icon);
    return QRectF(x, (boundingRect().height() - m_barHeight) / 2.0, w, m_barHeight);
}

QRectF VolumeBarItem::boundingRect() const
{
    return QRectF(0, 0, m_width, std::max(28.0, m_fontPx + 14));
}

void VolumeBarItem::paintSpeaker(QPainter *painter, const QRectF &box, int waves) const
{
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor("#ece8e0"));
    const qreal x = box.left();
    const qreal y = box.top();
    const qreal w = box.width();
    const qreal h = box.height();
    painter->drawRoundedRect(QRectF(x, y + h * 0.34, w * 0.16, h * 0.32), 1, 1);
    QPolygonF horn;
    horn << QPointF(x + w * 0.14, y + h * 0.34)
         << QPointF(x + w * 0.40, y + h * 0.14)
         << QPointF(x + w * 0.40, y + h * 0.86)
         << QPointF(x + w * 0.14, y + h * 0.66);
    painter->drawPolygon(horn);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor("#ece8e0"), std::max(1.2, w * 0.07), Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < waves; ++i) {
        const qreal inset = w * (0.48 + i * 0.2);
        painter->drawArc(QRectF(x + inset - w * 0.22, y + h * 0.22, w * 0.5, h * 0.56), -50 * 16, 100 * 16);
    }
    painter->restore();
}

void VolumeBarItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal icon = std::max(18.0, m_fontPx * 1.7);
    const qreal h = boundingRect().height();
    paintSpeaker(painter, QRectF(0, (h - icon) / 2.0, icon, icon), 1);

    painter->setPen(Qt::NoPen);
    const QRectF bar = barRect();
    painter->setBrush(QColor("#2a2e3a"));
    painter->drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);

    painter->setBrush(m_accent);
    QRectF fill = bar;
    fill.setWidth(m_volume <= 0.001 ? 0 : std::max(bar.height(), bar.width() * m_volume));
    painter->drawRoundedRect(fill, bar.height() / 2, bar.height() / 2);

    paintSpeaker(painter, QRectF(bar.right() + 8, (h - icon) / 2.0, icon, icon), 2);

    QFont font = painter->font();
    font.setPixelSize(std::max(1, int(m_fontPx)));
    painter->setFont(font);
    painter->setPen(QColor("#ece8e0"));
    const qreal percentX = bar.right() + 8 + icon + 4;
    painter->drawText(QRectF(percentX, 0, m_width - percentX, h),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QString::number(int(std::lround(m_volume * 100))) + QLatin1Char('%'));
}

void VolumeBarItem::setVolume(qreal fraction01)
{
    m_volume = std::clamp(fraction01, 0.0, 1.0);
    update();
}

void VolumeBarItem::applyUiScale(qreal scale)
{
    prepareGeometryChange();
    m_width = std::clamp(240.0 * scale, 180.0, 360.0);
    m_barHeight = std::clamp(8.0 * scale, 6.0, 12.0);
    m_fontPx = std::clamp(13.0 * scale, 11.0, 18.0);
    update();
}

void VolumeBarItem::fitWithin(qreal maxWidth, qreal)
{
    if (m_width <= maxWidth) return;
    prepareGeometryChange();
    m_width = std::max(72.0, maxWidth);
    update();
}

void VolumeBarItem::applySkinColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    update();
}

void VolumeBarItem::applyVolumeAt(const QPointF &pos)
{
    const QRectF bar = barRect();
    const qreal fraction = std::clamp((pos.x() - bar.left()) / std::max(1.0, bar.width()), 0.0, 1.0);
    setVolume(fraction);
    emit volumeRequested(fraction);
}

void VolumeBarItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!isEditable()) {
        applyVolumeAt(event->pos());
        event->accept();
        return;
    }
    DraggableItem::mousePressEvent(event);
}

void VolumeBarItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!isEditable() && (event->buttons() & Qt::LeftButton)) {
        applyVolumeAt(event->pos());
        event->accept();
        return;
    }
    DraggableItem::mouseMoveEvent(event);
}

void VolumeBarItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (!isEditable()) {
        event->accept();
        return;
    }
    DraggableItem::mouseReleaseEvent(event);
}
