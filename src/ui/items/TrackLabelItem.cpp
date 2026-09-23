#include "TrackLabelItem.h"
#include <QPainter>
#include <algorithm>

TrackLabelItem::TrackLabelItem(QGraphicsItem *parent) : DraggableItem("track_label", parent) {}

QRectF TrackLabelItem::boundingRect() const
{
    const int lines = m_line3.isEmpty() ? 2 : 3;
    return QRectF(0, 0, m_width, m_lineHeight * lines + 4);
}

void TrackLabelItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPixelSize(std::max(1, int(m_titlePx)));
    painter->setFont(titleFont);
    painter->setPen(m_titleColor);

    const QFontMetrics titleMetrics(titleFont);
    painter->drawText(QRectF(0, 0, m_width, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      titleMetrics.elidedText(m_line1, Qt::ElideRight, int(m_width)));

    QFont metaFont = painter->font();
    metaFont.setBold(false);
    metaFont.setPixelSize(std::max(1, int(m_metaPx)));
    painter->setFont(metaFont);
    painter->setPen(QColor("#b7b3aa"));
    const QFontMetrics metaMetrics(metaFont);
    painter->drawText(QRectF(0, m_lineHeight, m_width, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      metaMetrics.elidedText(m_line2, Qt::ElideRight, int(m_width)));
    if (!m_line3.isEmpty()) {
        painter->setPen(QColor("#8b8a86"));
        painter->drawText(QRectF(0, m_lineHeight * 2, m_width, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                          metaMetrics.elidedText(m_line3, Qt::ElideRight, int(m_width)));
    }
}

void TrackLabelItem::setText(const QString &line1, const QString &line2, const QString &line3)
{
    prepareGeometryChange();
    m_line1 = line1;
    m_line2 = line2;
    m_line3 = line3;
    update();
}

void TrackLabelItem::setFontScale(qreal scale)
{
    m_fontScale = std::clamp(scale, 0.7, 2.0);
    applyFonts();
}

void TrackLabelItem::applyFonts()
{
    prepareGeometryChange();
    if (m_fitScale < 0.05) {
        m_titlePx = 11.0 * m_fontScale;
        m_metaPx = 9.0 * m_fontScale;
    } else {
        m_titlePx = std::clamp(22.0 * m_fitScale, 15.0, 34.0) * m_fontScale;
        m_metaPx = std::clamp(15.0 * m_fitScale, 12.0, 22.0) * m_fontScale;
    }
    m_lineHeight = m_titlePx + 6;
    update();
}

void TrackLabelItem::applyUiScale(qreal scale)
{
    m_fitScale = scale;
    prepareGeometryChange();
    m_width = std::clamp(360.0 * scale, 160.0, 520.0);
    applyFonts();
}

void TrackLabelItem::fitWithin(qreal maxWidth, qreal)
{
    if (m_width <= maxWidth) return;
    prepareGeometryChange();
    m_width = std::max(120.0, maxWidth);
    update();
}

void TrackLabelItem::applySkinColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_titleColor = color;
    update();
}
