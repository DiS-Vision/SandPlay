#include "SkinCanvas.h"
#include "items/CoverArtItem.h"
#include "items/TrackLabelItem.h"
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QVector>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

SkinCanvas::SkinCanvas(QWidget *parent) : QGraphicsView(parent)
{
    setScene(&m_scene);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setBackgroundBrush(QColor("#1b1e27"));
    m_scene.setBackgroundBrush(QColor("#1b1e27"));

    QPen guidePen(QColor("#c9974b")); // matches the default accent — visible on dark or light skins
    guidePen.setStyle(Qt::DashLine);
    guidePen.setWidthF(1.0);

    m_hGuide = m_scene.addLine(0, 0, 0, 0, guidePen);
    m_vGuide = m_scene.addLine(0, 0, 0, 0, guidePen);
    m_hGuide->setZValue(1000);
    m_vGuide->setZValue(1000);
    m_hGuide->hide();
    m_vGuide->hide();
}

void SkinCanvas::registerItem(DraggableItem *item, const QPointF &defaultPos)
{
    item->setPos(defaultPos);
    m_scene.addItem(item);
    m_items.insert(item->elementId(), item);

    connect(item, &DraggableItem::snapGuidesUpdated, this, &SkinCanvas::showGuides);
    connect(item, &DraggableItem::dragFinished, this, [this, item]() {
        if (m_fractional) rememberFraction(item);
        emit layoutChanged();
    });
}

void SkinCanvas::registerFractionalItem(DraggableItem *item, const QPointF &fraction)
{
    m_fractional = true;
    m_fractions.insert(item->elementId(), fraction);
    registerItem(item, fraction);
}

void SkinCanvas::setSurfaceColor(const QColor &color)
{
    setBackgroundBrush(color);
    m_scene.setBackgroundBrush(color);
}

void SkinCanvas::setBackgroundPath(const QString &path)
{
    m_backgroundPath = path;
    m_background = path.isEmpty() ? QPixmap() : QPixmap(path);
    viewport()->update();
}

void SkinCanvas::clearBackground()
{
    setBackgroundPath({});
}

QVector<DraggableItem *> SkinCanvas::items() const
{
    QVector<DraggableItem *> out;
    out.reserve(m_items.size());
    for (auto *item : std::as_const(m_items)) out.push_back(item);
    return out;
}

QMap<QString, QPointF> SkinCanvas::positions() const
{
    QMap<QString, QPointF> out;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        out.insert(it.key(), it.value()->pos());
    return out;
}

bool SkinCanvas::samePositions(const QMap<QString, QPointF> &saved) const
{
    if (saved.size() != m_items.size()) return false;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        if (!saved.contains(it.key())) return false;
        if ((it.value()->pos() - saved.value(it.key())).manhattanLength() > 0.5) return false;
    }
    return true;
}

void SkinCanvas::restorePositions(const QMap<QString, QPointF> &saved)
{
    m_placing = true;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        if (!saved.contains(it.key())) continue;
        it.value()->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);
        it.value()->setPos(saved.value(it.key()));
        it.value()->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    }
    m_placing = false;
}

void SkinCanvas::setUserScale(qreal scale)
{
    m_userScale = std::clamp(scale, 0.7, 1.6);
    applyFractionalPositions();
}

void SkinCanvas::relayout()
{
    applyFractionalPositions();
}

void SkinCanvas::applyAccent(const QColor &color)
{
    for (auto *item : std::as_const(m_items)) item->setAccentColor(color);
}

void SkinCanvas::rememberFraction(DraggableItem *item)
{
    const QRectF bounds = m_scene.sceneRect();
    if (bounds.width() < 1.0 || bounds.height() < 1.0) return;
    const qreal x = std::clamp(item->pos().x() / bounds.width(), 0.0, 1.0);
    const qreal y = std::clamp(item->pos().y() / bounds.height(), 0.0, 1.0);
    m_fractions[item->elementId()] = QPointF(x, y);
}

void SkinCanvas::applyFractionalPositions()
{
    const QRectF bounds = m_scene.sceneRect();
    if (!m_fractional || bounds.width() < 1.0 || bounds.height() < 1.0) return;

    if (m_compact) {
        m_placing = true;
        for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
            DraggableItem *item = it.value();
            item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);
            const QPointF fraction = m_fractions.value(it.key(), QPointF(0.05, 0.05));
            const QSizeF size = item->boundingRect().size();
            const qreal maxX = std::max(0.0, bounds.width() - size.width());
            const qreal maxY = std::max(0.0, bounds.height() - size.height());
            item->setPos(std::clamp(fraction.x() * bounds.width(), 0.0, maxX),
                         std::clamp(fraction.y() * bounds.height(), 0.0, maxY));
            item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        }
        m_placing = false;
        return;
    }

    // Size stays at the pixels chosen in the editor. The editor canvas is
    // larger than the live one, and scaling by that difference made the
    // cover come back much smaller than the user had just set.
    const qreal scale = m_userScale;
    m_placing = true;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        DraggableItem *item = it.value();
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);
        item->applyUiScale(scale);
        const QPointF fraction = m_fractions.value(it.key(), QPointF(0.05, 0.05));
        const QSizeF size = item->boundingRect().size();
        const qreal maxX = std::max(0.0, bounds.width() - size.width());
        const qreal maxY = std::max(0.0, bounds.height() - size.height());
        item->setPos(std::clamp(fraction.x() * bounds.width(), 0.0, maxX),
                     std::clamp(fraction.y() * bounds.height(), 0.0, maxY));
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    }
    m_placing = false;
}

void SkinCanvas::separateItems()
{
    QVector<DraggableItem *> items;
    items.reserve(m_items.size());
    for (auto *item : std::as_const(m_items)) items.push_back(item);
    std::sort(items.begin(), items.end(), [](const DraggableItem *a, const DraggableItem *b) {
        if (std::abs(a->pos().y() - b->pos().y()) > 6.0) return a->pos().y() < b->pos().y();
        return a->pos().x() < b->pos().x();
    });

    const QRectF bounds = m_scene.sceneRect();
    constexpr qreal gap = 10.0;
    for (int pass = 0; pass < 8; ++pass) {
        bool moved = false;
        for (int i = 0; i < items.size(); ++i) {
            for (int j = 0; j < i; ++j) {
                const QRectF a = items[j]->sceneBoundingRect().adjusted(-gap, -gap, gap, gap);
                const QRectF b = items[i]->sceneBoundingRect();
                if (!a.intersects(b)) continue;
                const qreal overlapX = std::min(a.right(), b.right()) - std::max(a.left(), b.left());
                const qreal overlapY = std::min(a.bottom(), b.bottom()) - std::max(a.top(), b.top());
                if (overlapX <= 0.0 || overlapY <= 0.0) continue;

                const QSizeF size = items[i]->boundingRect().size();
                const qreal maxX = std::max(bounds.left(), bounds.right() - size.width());
                const qreal maxY = std::max(bounds.top(), bounds.bottom() - size.height());
                const auto fits = [&](const QPointF &p) {
                    return p.x() >= bounds.left() - 0.5 && p.y() >= bounds.top() - 0.5
                        && p.x() <= maxX + 0.5 && p.y() <= maxY + 0.5;
                };
                const QPointF right(a.right(), items[i]->pos().y());
                const QPointF down(items[i]->pos().x(), a.bottom());
                QPointF pos = items[i]->pos();
                if (overlapX < overlapY && fits(right)) pos = right;
                else if (fits(down)) pos = down;
                else if (fits(right)) pos = right;
                else pos = QPointF(std::clamp(right.x(), bounds.left(), maxX),
                                    std::clamp(down.y(), bounds.top(), maxY));
                pos.setX(std::clamp(pos.x(), bounds.left(), maxX));
                pos.setY(std::clamp(pos.y(), bounds.top(), maxY));
                if (pos == items[i]->pos()) continue;
                items[i]->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);
                items[i]->setPos(pos);
                items[i]->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
                moved = true;
            }
        }
        if (!moved) break;
    }
}

void SkinCanvas::setCompact(bool compact)
{
    m_compact = compact;
    if (compact) m_fractional = true;
}

void SkinCanvas::setEditMode(bool editing)
{
    m_editing = editing;
    for (auto *item : std::as_const(m_items)) item->setEditable(editing);
    if (!editing) {
        m_hGuide->hide();
        m_vGuide->hide();
        applyFractionalPositions();
    }
}

void SkinCanvas::showGuides(bool showH, qreal y, bool showV, qreal x)
{
    const QRectF r = m_scene.sceneRect();
    if (showH) { m_hGuide->setLine(r.left(), y, r.right(), y); m_hGuide->show(); }
    else m_hGuide->hide();

    if (showV) { m_vGuide->setLine(x, r.top(), x, r.bottom()); m_vGuide->show(); }
    else m_vGuide->hide();
}

void SkinCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    const QSize size = viewport()->size();
    if (size.width() > 0 && size.height() > 0)
        m_scene.setSceneRect(0, 0, size.width(), size.height());
    applyFractionalPositions();
}

void SkinCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_editing) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }
    auto *item = dynamic_cast<DraggableItem *>(itemAt(event->pos()));
    if (!item) {
        emit canvasMenuRequested();
        event->accept();
        return;
    }
    emit itemMenuRequested(item);
    event->accept();
}

void SkinCanvas::drawBackground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawBackground(painter, rect);
    if (m_background.isNull()) return;
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawPixmap(m_scene.sceneRect(), m_background, QRectF(m_background.rect()));
    painter->restore();
}

void SkinCanvas::drawForeground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);
    if (!m_editing) return;
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#c9974b"));
    pen.setStyle(Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (auto *item : std::as_const(m_items)) {
        if (!item->isVisible()) continue;
        const QRectF bounds = item->sceneBoundingRect();
        painter->drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        if (!item->canResize()) continue;
        painter->setBrush(QColor("#c9974b"));
        painter->drawRect(QRectF(bounds.right() - 12, bounds.bottom() - 12, 10, 10));
        painter->setBrush(Qt::NoBrush);
    }
}

void SkinCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    const QGraphicsItem *item = itemAt(event->pos());
    QGraphicsView::mouseReleaseEvent(event);
    if (m_editing || event->button() != Qt::LeftButton) return;
    if (item && item->type() == DraggableItem::Type) return;
    emit backgroundClicked();
}

QJsonObject SkinCanvas::serializeLayout() const
{
    QJsonObject out;
    out["version"] = m_fractional ? 3 : 2;
    if (!m_backgroundPath.isEmpty())
        out[QStringLiteral("background")] = m_backgroundPath;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        QJsonObject pos;
        if (m_fractional) {
            const QPointF fraction = m_fractions.value(it.key(), QPointF(0, 0));
            pos["x"] = fraction.x();
            pos["y"] = fraction.y();
        } else {
            pos["x"] = it.value()->pos().x();
            pos["y"] = it.value()->pos().y();
        }
        if (it.value()->canResize()) {
            pos["w"] = it.value()->boundingRect().width();
            pos["h"] = it.value()->boundingRect().height();
        }
        if (it.value()->hasSkinColor())
            pos["color"] = it.value()->skinColor().name(QColor::HexRgb);
        if (std::abs(it.value()->itemScale() - 1.0) > 0.01)
            pos["scale"] = it.value()->itemScale();
        if (!it.value()->isVisible())
            pos["hidden"] = true;
        if (it.value()->hasCustomIcon())
            pos["icon"] = it.value()->customIconPath();
        if (auto *cover = dynamic_cast<CoverArtItem *>(it.value())) {
            pos["shape"] = cover->shapeId();
            if (cover->hasFrame()) pos["frame"] = cover->framePath();
        }
        if (std::abs(it.value()->fontScale() - 1.0) > 0.01)
            pos["font"] = it.value()->fontScale();
        out[it.key()] = pos;
    }
    return out;
}

void SkinCanvas::applyLayout(const QJsonObject &layout)
{
    const int version = layout.value(QStringLiteral("version")).toInt();
    if (m_fractional && version < 3) return;
    if (!m_fractional && version < 2) return;
    if (layout.contains(QStringLiteral("background")))
        setBackgroundPath(layout.value(QStringLiteral("background")).toString());
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        if (!layout.contains(it.key())) continue;
        const QJsonObject pos = layout.value(it.key()).toObject();
        const double x = pos.value(QStringLiteral("x")).toDouble(-1);
        const double y = pos.value(QStringLiteral("y")).toDouble(-1);
        if (m_fractional) {
            if (x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)
                m_fractions[it.key()] = QPointF(x, y);
        } else if (x >= 0.0 && y >= 0.0) {
            it.value()->setPos(x, y);
        }
        const QColor color(pos.value(QStringLiteral("color")).toString());
        if (color.isValid()) it.value()->setSkinColor(color);
        const double itemScale = pos.value(QStringLiteral("scale")).toDouble(1.0);
        if (itemScale >= 0.35 && itemScale <= 3.0) it.value()->setItemScale(itemScale);
        it.value()->setCustomIconPath(pos.value(QStringLiteral("icon")).toString());
        it.value()->setVisible(!pos.value(QStringLiteral("hidden")).toBool(false));
        if (auto *cover = dynamic_cast<CoverArtItem *>(it.value())) {
            if (pos.contains(QStringLiteral("shape")))
                cover->setShapeId(pos.value(QStringLiteral("shape")).toString());
            cover->setFramePath(pos.value(QStringLiteral("frame")).toString());
        }
        if (auto *label = dynamic_cast<TrackLabelItem *>(it.value())) {
            double font = pos.value(QStringLiteral("font")).toDouble(0);
            if (font < 0.7)
                font = layout.value(QStringLiteral("font")).toDouble(0);
            if (font >= 0.7 && font <= 2.0) label->setFontScale(font);
        }
        const double w = pos.value(QStringLiteral("w")).toDouble(0);
        const double h = pos.value(QStringLiteral("h")).toDouble(0);
        if (m_compact && w > 1.0 && h > 1.0) it.value()->resizeTo(w, h);
    }
    applyFractionalPositions();
}
