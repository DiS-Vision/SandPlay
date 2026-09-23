#include "SeekBarItem.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <algorithm>
#include <cmath>

SeekBarItem::SeekBarItem(QGraphicsItem *parent) : DraggableItem("seek_bar", parent) {}

QRectF SeekBarItem::barRect() const
{
    if (!m_showTimes) return QRectF(0, 0, m_width, m_barHeight);
    const qreal labelW = std::max(44.0, m_fontPx * 3.2);
    return QRectF(labelW, (m_fontPx + 6 - m_barHeight) / 2.0, std::max(20.0, m_width - labelW * 2), m_barHeight);
}

QRectF SeekBarItem::boundingRect() const
{
    if (!m_showTimes) return QRectF(0, 0, m_width, m_barHeight);
    return QRectF(0, 0, m_width, std::max(m_barHeight, m_fontPx + 6));
}

void SeekBarItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);

    const QRectF bar = barRect();
    painter->setBrush(QColor("#2a2e3a"));
    painter->drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);

    painter->setBrush(m_accent);
    QRectF fill = bar;
    fill.setWidth(std::max(bar.height(), bar.width() * m_progress));
    if (m_progress <= 0.001) fill.setWidth(0);
    painter->drawRoundedRect(fill, bar.height() / 2, bar.height() / 2);

    const qreal knobAt = m_scrubbing ? m_scrub : m_progress;
    const qreal knobR = std::max(5.0, bar.height() * 0.85);
    painter->setBrush(m_accent);
    painter->drawEllipse(QPointF(bar.left() + bar.width() * knobAt, bar.center().y()), knobR, knobR);

    if (m_showTimes) {
        QFont font = painter->font();
        font.setPixelSize(std::max(1, int(m_fontPx)));
        painter->setFont(font);
        painter->setPen(QColor("#ece8e0"));
        const qreal labelW = bar.left();
        QString elapsed = m_elapsed.isEmpty() ? QStringLiteral("0:00") : m_elapsed;
        if (m_scrubbing && m_durationMs > 0)
            elapsed = formatMs(qint64(std::llround(m_scrub * double(m_durationMs))));
        painter->drawText(QRectF(0, 0, labelW - 6, boundingRect().height()),
                          Qt::AlignRight | Qt::AlignVCenter, elapsed);
        painter->drawText(QRectF(bar.right() + 6, 0, labelW - 6, boundingRect().height()),
                          Qt::AlignLeft | Qt::AlignVCenter, m_total.isEmpty() ? QStringLiteral("0:00") : m_total);
    }
}

void SeekBarItem::setProgress(qreal fraction01)
{
    m_progress = std::clamp(fraction01, 0.0, 1.0);
    if (!m_scrubbing) m_scrub = m_progress;
    update();
}

void SeekBarItem::setWidth(qreal w)
{
    prepareGeometryChange();
    m_width = w;
}

void SeekBarItem::setAccent(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    update();
}

void SeekBarItem::setShowTimes(bool show)
{
    prepareGeometryChange();
    m_showTimes = show;
    update();
}

void SeekBarItem::setClock(const QString &elapsed, const QString &total)
{
    m_elapsed = elapsed;
    m_total = total;
    update();
}

void SeekBarItem::setDurationMs(qint64 ms)
{
    m_durationMs = std::max(qint64(0), ms);
}

void SeekBarItem::applyUiScale(qreal scale)
{
    prepareGeometryChange();
    m_fitScale = scale;
    m_width = std::clamp(520.0 * scale * itemScale(), 160.0, 980.0);
    m_barHeight = std::clamp(8.0 * scale, 6.0, 12.0);
    m_fontPx = std::clamp(13.0 * scale, 11.0, 18.0);
    update();
}

void SeekBarItem::resizeTo(qreal width, qreal)
{
    prepareGeometryChange();
    if (m_fitScale < 0.05) {
        m_width = std::clamp(width, 80.0, 980.0);
        update();
        return;
    }
    setItemScale(width / (520.0 * m_fitScale));
    m_width = std::clamp(520.0 * m_fitScale * itemScale(), 160.0, 980.0);
    update();
}

void SeekBarItem::fitWithin(qreal maxWidth, qreal)
{
    if (m_width <= maxWidth) return;
    setWidth(std::max(160.0, maxWidth));
}

void SeekBarItem::applySkinColor(const QColor &color)
{
    setAccent(color);
}

qreal SeekBarItem::fractionAt(const QPointF &pos) const
{
    const QRectF bar = barRect();
    return std::clamp((pos.x() - bar.left()) / std::max(1.0, bar.width()), 0.0, 1.0);
}

QString SeekBarItem::formatMs(qint64 ms) const
{
    if (ms < 0) ms = 0;
    const qint64 seconds = ms / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

void SeekBarItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!isEditable() && event->button() == Qt::LeftButton) {
        const QRectF hit = barRect().adjusted(-4, -10, 4, 10);
        if (!hit.contains(event->pos())) {
            event->ignore();
            return;
        }
        m_scrubbing = true;
        m_scrub = fractionAt(event->pos());
        update();
        event->accept();
        return;
    }
    DraggableItem::mousePressEvent(event);
}

void SeekBarItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_scrubbing) {
        m_scrub = fractionAt(event->pos());
        update();
        event->accept();
        return;
    }
    DraggableItem::mouseMoveEvent(event);
}

void SeekBarItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_scrubbing) {
        m_scrub = fractionAt(event->pos());
        m_progress = m_scrub;
        m_scrubbing = false;
        if (m_durationMs > 0) m_elapsed = formatMs(qint64(std::llround(m_scrub * double(m_durationMs))));
        update();
        emit seekRequested(m_scrub);
        event->accept();
        return;
    }
    DraggableItem::mouseReleaseEvent(event);
}
