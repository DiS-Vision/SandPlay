#include "CoverArtItem.h"
#include <QEasingCurve>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

CoverArtItem::CoverArtItem(QGraphicsItem *parent) : DraggableItem("cover_art", parent)
{
    m_spin = new QVariantAnimation(this);
    m_spin->setDuration(6000);
    m_spin->setLoopCount(-1);
    m_spin->setEasingCurve(QEasingCurve::Linear);
    connect(m_spin, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_angle = std::fmod(value.toReal(), 360.0);
        if (m_angle < 0) m_angle += 360.0;
        update();
    });
}

QRectF CoverArtItem::boundingRect() const
{
    return QRectF(0, 0, m_size, m_size);
}

void CoverArtItem::drawArt(QPainter *painter, const QRectF &target) const
{
    if (m_pixmap.isNull()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#242833"));
        painter->drawRect(target);
        return;
    }
    const qreal dpr = painter->device() ? std::max(qreal(2), painter->device()->devicePixelRatioF()) : 2;
    const int px = std::max(1, int(std::ceil(target.width() * dpr)));
    const int py = std::max(1, int(std::ceil(target.height() * dpr)));
    if (m_artCache.isNull() || m_artCacheKey != m_pixmap.cacheKey()
        || m_artCache.width() != px || m_artCache.height() != py) {
        QImage scaled = m_pixmap.toImage().scaled(QSize(px, py), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = std::max(0, (scaled.width() - px) / 2);
        const int y = std::max(0, (scaled.height() - py) / 2);
        scaled = scaled.copy(x, y, std::min(px, scaled.width()), std::min(py, scaled.height()));
        m_artCache = QPixmap::fromImage(scaled);
        m_artCacheKey = m_pixmap.cacheKey();
    }
    painter->drawPixmap(target, m_artCache, QRectF(m_artCache.rect()));
}

void CoverArtItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = boundingRect();

    if (m_shape == Shape::Vinyl) {
        const QRectF disc = r.adjusted(0.5, 0.5, -0.5, -0.5);
        const QPointF center = disc.center();

        painter->save();
        QPainterPath clip;
        clip.addEllipse(disc);
        painter->setClipPath(clip);
        painter->translate(center);
        painter->rotate(m_angle);
        painter->translate(-center);
        drawArt(painter, disc);
        painter->restore();

        const qreal ring = std::max(1.6, disc.width() * 0.034);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(18, 20, 26, 210), std::max(1.0, disc.width() * 0.016)));
        painter->drawEllipse(disc.adjusted(0.4, 0.4, -0.4, -0.4));
        painter->setPen(QPen(QColor(226, 230, 236, 150), ring));
        const QRectF rim = disc.adjusted(ring * 0.45, ring * 0.45, -ring * 0.45, -ring * 0.45);
        painter->drawEllipse(rim);

        painter->save();
        painter->setClipPath(clip);
        painter->setPen(QPen(QColor(255, 255, 255, 200), std::max(1.5, ring * 0.7), Qt::SolidLine, Qt::RoundCap));
        painter->drawArc(rim, 108 * 16, 42 * 16);
        QRadialGradient glare(center.x() - disc.width() * 0.18, center.y() - disc.height() * 0.30, disc.width() * 0.22);
        glare.setColorAt(0.0, QColor(255, 255, 255, 78));
        glare.setColorAt(0.55, QColor(255, 255, 255, 18));
        glare.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter->setPen(Qt::NoPen);
        painter->setBrush(glare);
        painter->drawEllipse(disc);
        painter->restore();

        const qreal hole = std::max(3.0, disc.width() * 0.085);
        painter->setPen(QPen(QColor(220, 224, 230, 120), std::max(1.0, disc.width() * 0.012)));
        painter->setBrush(QColor("#101218"));
        painter->drawEllipse(QRectF(center.x() - hole / 2.0, center.y() - hole / 2.0, hole, hole));
    } else {
        const qreal radius = m_shape == Shape::Square ? 0.0 : r.width() * 0.08;
        painter->save();
        QPainterPath clip;
        clip.addRoundedRect(r, radius, radius);
        painter->setClipPath(clip);
        drawArt(painter, r);
        painter->restore();
    }

    if (!m_frame.isNull())
        painter->drawPixmap(r, m_frame, QRectF(m_frame.rect()));
}

void CoverArtItem::setPixmap(const QPixmap &pixmap)
{
    m_pixmap = pixmap;
    m_artCache = {};
    m_artCacheKey = 0;
    update();
}

void CoverArtItem::setPlaybackActive(bool playing)
{
    m_playing = playing;
    syncSpin();
}

void CoverArtItem::setEditable(bool editable)
{
    DraggableItem::setEditable(editable);
    syncSpin();
}

void CoverArtItem::syncSpin()
{
    const bool spin = m_playing && m_shape == Shape::Vinyl && !isEditable();
    if (!spin) {
        if (m_spin->state() == QAbstractAnimation::Running)
            m_spin->stop();
        if (m_shape != Shape::Vinyl && m_angle != 0) {
            m_angle = 0;
            update();
        }
        return;
    }
    if (m_spin->state() == QAbstractAnimation::Running) return;
    m_spin->setStartValue(m_angle);
    m_spin->setEndValue(m_angle + 360.0);
    m_spin->start();
}

void CoverArtItem::setSize(qreal size)
{
    prepareGeometryChange();
    m_size = size;
    update();
}

void CoverArtItem::setShape(Shape shape)
{
    m_shape = shape;
    syncSpin();
    update();
}

void CoverArtItem::setShapeId(const QString &id)
{
    if (id == QLatin1String("square")) m_shape = Shape::Square;
    else if (id == QLatin1String("vinyl")) m_shape = Shape::Vinyl;
    else m_shape = Shape::Rounded;
    syncSpin();
    update();
}

QString CoverArtItem::shapeId() const
{
    switch (m_shape) {
    case Shape::Square: return QStringLiteral("square");
    case Shape::Vinyl: return QStringLiteral("vinyl");
    case Shape::Rounded: return QStringLiteral("rounded");
    }
    return QStringLiteral("rounded");
}

void CoverArtItem::setFramePath(const QString &path)
{
    m_framePath = path;
    m_frame = path.isEmpty() ? QPixmap() : QPixmap(path);
    update();
}

void CoverArtItem::clearFrame()
{
    setFramePath({});
}

void CoverArtItem::applyUiScale(qreal scale)
{
    m_fitScale = scale;
    setSize(std::clamp(200.0 * scale * itemScale(), 64.0, 460.0));
}

void CoverArtItem::resizeTo(qreal width, qreal height)
{
    const qreal side = std::min(width, height);
    if (m_fitScale < 0.05) {
        setSize(std::clamp(side, 28.0, 280.0));
        return;
    }
    setItemScale(side / (200.0 * m_fitScale));
    setSize(std::clamp(200.0 * m_fitScale * itemScale(), 64.0, 460.0));
}

void CoverArtItem::fitWithin(qreal maxWidth, qreal maxHeight)
{
    const qreal cap = std::min(maxWidth, maxHeight);
    if (m_size > cap) setSize(std::max(72.0, cap));
}

void CoverArtItem::applySkinColor(const QColor &)
{
    // The accent used to stroke a frame around the art. The shape itself
    // is the frame now; a custom picture is stored separately.
}
