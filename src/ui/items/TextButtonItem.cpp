#include "TextButtonItem.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <cmath>

TextButtonItem::TextButtonItem(QString elementId, Glyph glyph, QGraphicsItem *parent)
    : DraggableItem(std::move(elementId), parent), m_glyph(glyph)
{
}

QRectF TextButtonItem::boundingRect() const
{
    return QRectF(0, 0, m_width, m_height);
}

void TextButtonItem::paintGlyph(QPainter *painter, const QRectF &box) const
{
    const qreal x = box.left();
    const qreal y = box.top();
    const qreal w = box.width();
    const qreal h = box.height();
    const bool engaged = m_checked || m_glyph == Glyph::RepeatAll || m_glyph == Glyph::RepeatOne;
    painter->setBrush(engaged ? QColor("#14161c") : QColor("#ece8e0"));
    painter->setPen(Qt::NoPen);

    auto triangle = [&](const QPointF &a, const QPointF &b, const QPointF &c) {
        QPolygonF poly;
        poly << a << b << c;
        painter->drawPolygon(poly);
    };

    switch (m_glyph) {
    case Glyph::Previous:
        painter->drawRoundedRect(QRectF(x, y + h * 0.08, w * 0.16, h * 0.84), 1, 1);
        triangle(QPointF(x + w * 0.92, y), QPointF(x + w * 0.28, y + h * 0.5), QPointF(x + w * 0.92, y + h));
        break;
    case Glyph::Next:
        painter->drawRoundedRect(QRectF(x + w * 0.84, y + h * 0.08, w * 0.16, h * 0.84), 1, 1);
        triangle(QPointF(x + w * 0.08, y), QPointF(x + w * 0.72, y + h * 0.5), QPointF(x + w * 0.08, y + h));
        break;
    case Glyph::Shuffle: {
        painter->setPen(QPen(painter->brush().color(), std::max(1.6, w * 0.12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        QPainterPath top;
        top.moveTo(x, y + h * 0.22);
        top.cubicTo(x + w * 0.35, y + h * 0.22, x + w * 0.45, y + h * 0.78, x + w * 0.72, y + h * 0.78);
        painter->drawPath(top);
        QPainterPath bottom;
        bottom.moveTo(x, y + h * 0.78);
        bottom.cubicTo(x + w * 0.35, y + h * 0.78, x + w * 0.45, y + h * 0.22, x + w * 0.72, y + h * 0.22);
        painter->drawPath(bottom);
        painter->setPen(Qt::NoPen);
        painter->setBrush(engaged ? QColor("#14161c") : QColor("#ece8e0"));
        triangle(QPointF(x + w * 0.62, y + h * 0.08), QPointF(x + w, y + h * 0.22), QPointF(x + w * 0.62, y + h * 0.40));
        triangle(QPointF(x + w * 0.62, y + h * 0.60), QPointF(x + w, y + h * 0.78), QPointF(x + w * 0.62, y + h * 0.96));
        break;
    }
    case Glyph::RepeatOff:
    case Glyph::RepeatAll:
    case Glyph::RepeatOne: {
        const QColor ink = (m_glyph == Glyph::RepeatOff) ? QColor("#8b8a86") : painter->brush().color();
        painter->setPen(QPen(ink, std::max(1.6, w * 0.11), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(QRectF(x + w * 0.12, y + h * 0.16, w * 0.70, h * 0.70), 40 * 16, 280 * 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(ink);
        triangle(QPointF(x + w * 0.62, y + h * 0.02), QPointF(x + w * 0.98, y + h * 0.18), QPointF(x + w * 0.62, y + h * 0.36));
        if (m_glyph == Glyph::RepeatOne) {
            QFont font = painter->font();
            font.setBold(true);
            font.setPixelSize(std::max(8, int(h * 0.42)));
            painter->setFont(font);
            painter->setPen(ink);
            painter->drawText(box, Qt::AlignCenter, QStringLiteral("1"));
        }
        break;
    }
    case Glyph::VolumeDown:
    case Glyph::VolumeUp: {
        painter->drawRoundedRect(QRectF(x, y + h * 0.34, w * 0.16, h * 0.32), 1, 1);
        QPolygonF horn;
        horn << QPointF(x + w * 0.14, y + h * 0.34)
             << QPointF(x + w * 0.42, y + h * 0.12)
             << QPointF(x + w * 0.42, y + h * 0.88)
             << QPointF(x + w * 0.14, y + h * 0.66);
        painter->drawPolygon(horn);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(engaged ? QColor("#14161c") : QColor("#ece8e0"), std::max(1.4, w * 0.09), Qt::SolidLine, Qt::RoundCap));
        const int waves = m_glyph == Glyph::VolumeUp ? 2 : 1;
        for (int i = 0; i < waves; ++i) {
            const qreal inset = w * (0.52 + i * 0.18);
            painter->drawArc(QRectF(x + inset - w * 0.28, y + h * 0.22, w * 0.56, h * 0.56), -50 * 16, 100 * 16);
        }
        break;
    }
    }
}

void TextButtonItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const bool engaged = m_checked || m_glyph == Glyph::RepeatAll || m_glyph == Glyph::RepeatOne;
    if (hasCustomIcon()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawPixmap(boundingRect().toRect(), customIcon());
        return;
    }
    const QRectF r = boundingRect().adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal unit = std::max(28.0, std::min(m_width, m_height));
    painter->setPen(Qt::NoPen);
    painter->setBrush(engaged ? m_accent : QColor("#242833"));
    painter->drawRoundedRect(r, unit * 0.22, unit * 0.22);
    const bool skipGlyph = m_glyph == Glyph::Previous || m_glyph == Glyph::Next;
    if (!engaged && !(m_buttonStyle == 0 && skipGlyph)) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(m_accent, std::max(1.2, unit * 0.035)));
        painter->drawRoundedRect(r.adjusted(1.2, 1.2, -1.2, -1.2), unit * 0.2, unit * 0.2);
    }
    const qreal pad = unit * 0.22;
    paintGlyph(painter, boundingRect().adjusted(pad, pad, -pad, -pad));
}

void TextButtonItem::setGlyph(Glyph glyph)
{
    m_glyph = glyph;
    update();
}

void TextButtonItem::setChecked(bool checked)
{
    m_checked = checked;
    update();
}

void TextButtonItem::setButtonStyle(int style)
{
    const int next = style == 1 ? 1 : 0;
    if (m_buttonStyle == next) return;
    m_buttonStyle = next;
    update();
}

void TextButtonItem::applyUiScale(qreal scale)
{
    prepareGeometryChange();
    if (m_customSize && m_fitScale >= 0.05) {
        const qreal factor = scale / m_fitScale;
        if (std::abs(factor - 1.0) > 0.001) {
            m_width = std::max(28.0, m_width * factor);
            m_height = std::max(28.0, m_height * factor);
        }
    } else if (!m_customSize) {
        const qreal side = std::max(28.0, 48.0 * scale * itemScale());
        m_width = side;
        m_height = side;
    }
    m_fitScale = scale;
    update();
}

void TextButtonItem::resizeTo(qreal width, qreal height)
{
    prepareGeometryChange();
    m_width = std::max(28.0, width);
    m_height = std::max(28.0, height);
    m_customSize = true;
    if (m_fitScale >= 0.05)
        setItemScale(std::min(m_width, m_height) / (48.0 * m_fitScale));
    update();
}

void TextButtonItem::fitWithin(qreal maxWidth, qreal maxHeight)
{
    if (m_width <= maxWidth && m_height <= maxHeight) return;
    const qreal factor = std::min(maxWidth / std::max(1.0, m_width), maxHeight / std::max(1.0, m_height));
    prepareGeometryChange();
    m_width = std::max(28.0, m_width * factor);
    m_height = std::max(28.0, m_height * factor);
    update();
}

void TextButtonItem::applySkinColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    update();
}
