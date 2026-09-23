#include "DraggableItem.h"
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <algorithm>
#include <cmath>
#include <QCursor>

DraggableItem::DraggableItem(QString elementId, QGraphicsItem *parent)
    : QGraphicsObject(parent), m_elementId(std::move(elementId))
{
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void DraggableItem::setEditable(bool editable)
{
    m_editable = editable;
    setFlag(ItemIsMovable, editable);
    setAcceptHoverEvents(editable);
    setCursor(editable ? QCursor(Qt::SizeAllCursor) : QCursor(Qt::PointingHandCursor));
}

void DraggableItem::applyUiScale(qreal) {}

void DraggableItem::fitWithin(qreal, qreal) {}

void DraggableItem::applySkinColor(const QColor &) {}

void DraggableItem::setAccentColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    if (!m_hasSkinColor) applySkinColor(color);
}

void DraggableItem::setSkinColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_skinColor = color;
    m_hasSkinColor = true;
    applySkinColor(color);
}

void DraggableItem::clearSkinColor()
{
    m_hasSkinColor = false;
    m_skinColor = {};
    applySkinColor(m_accent);
}

void DraggableItem::setItemScale(qreal scale)
{
    m_itemScale = std::clamp(scale, 0.35, 3.0);
}

void DraggableItem::resizeTo(qreal, qreal) {}

QRectF resizeHandle(const QRectF &bounds)
{
    return QRectF(bounds.right() - 16, bounds.bottom() - 16, 16, 16);
}

qreal snapToSiblings(const DraggableItem *self, qreal value)
{
    if (!self->scene()) return value;
    for (QGraphicsItem *other : self->scene()->items()) {
        if (other == self || other->type() != DraggableItem::Type) continue;
        const auto *item = static_cast<const DraggableItem *>(other);
        if (!item->canResize()) continue;
        const qreal side = item->boundingRect().width();
        if (std::abs(item->boundingRect().height() - side) > 14.0) continue;
        if (std::abs(value - side) <= 8.0) return side;
    }
    return value;
}

void DraggableItem::setCustomIconPath(const QString &path)
{
    m_customIconPath = path;
    m_customIcon = path.isEmpty() ? QPixmap() : QPixmap(path);
    update();
}

void DraggableItem::clearCustomIcon()
{
    setCustomIconPath({});
}

void DraggableItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_pressScenePos = event->scenePos();
    m_moved = false;
    m_resizing = m_editable && canResize() && resizeHandle(boundingRect()).contains(event->pos());
    if (m_resizing) {
        m_moved = true;
        m_resizeStartW = boundingRect().width();
        m_resizeStartH = boundingRect().height();
        event->accept();
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

void DraggableItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_resizing) {
        const QPointF delta = event->scenePos() - m_pressScenePos;
        if (resizeKeepsRatio()) {
            const qreal base = std::max(m_resizeStartW, m_resizeStartH);
            const qreal side = snapToSiblings(this, std::max(28.0, base + (delta.x() + delta.y()) / 2.0));
            resizeTo(side, side);
        } else {
            resizeTo(std::max(28.0, m_resizeStartW + delta.x()), m_resizeStartH);
        }
        event->accept();
        return;
    }
    QGraphicsObject::mouseMoveEvent(event);
}

void DraggableItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    const bool wasResizing = m_resizing;
    m_resizing = false;
    if (!wasResizing) QGraphicsObject::mouseReleaseEvent(event);
    if (m_editable) {
        if (m_moved) emit dragFinished();
    } else if (!m_moved) {
        emit clicked();
    }
    emit snapGuidesUpdated(false, 0, false, 0);
}

void DraggableItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_editable && canResize() && resizeHandle(boundingRect()).contains(event->pos()))
        setCursor(Qt::SizeFDiagCursor);
    else if (m_editable)
        setCursor(Qt::SizeAllCursor);
    QGraphicsObject::hoverMoveEvent(event);
}

QVariant DraggableItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionChange && m_editable && scene()) {
        m_moved = true;
        QPointF proposed = value.toPointF();
        const QRectF myRect = boundingRect().translated(proposed);

        const qreal myLeft = myRect.left(), myRight = myRect.right(), myCx = myRect.center().x();
        const qreal myTop = myRect.top(), myBottom = myRect.bottom(), myCy = myRect.center().y();

        bool snappedX = false, snappedY = false;
        qreal guideX = 0, guideY = 0;

        for (QGraphicsItem *other : scene()->items()) {
            if (other == this || other->type() != DraggableItem::Type) continue;
            const QRectF r = other->sceneBoundingRect();
            const qreal oLeft = r.left(), oRight = r.right(), oCx = r.center().x();
            const qreal oTop = r.top(), oBottom = r.bottom(), oCy = r.center().y();

            if (!snappedX) {
                for (auto [mine, theirs] : { std::pair{myLeft, oLeft}, {myRight, oRight},
                                              {myCx, oCx}, {myLeft, oRight}, {myRight, oLeft} }) {
                    if (std::abs(mine - theirs) < kSnapThreshold) {
                        proposed.setX(proposed.x() + (theirs - mine));
                        guideX = theirs; snappedX = true; break;
                    }
                }
            }
            if (!snappedY) {
                for (auto [mine, theirs] : { std::pair{myTop, oTop}, {myBottom, oBottom},
                                              {myCy, oCy}, {myTop, oBottom}, {myBottom, oTop} }) {
                    if (std::abs(mine - theirs) < kSnapThreshold) {
                        proposed.setY(proposed.y() + (theirs - mine));
                        guideY = theirs; snappedY = true; break;
                    }
                }
            }
            if (snappedX && snappedY) break;
        }

        if (scene()) {
            const QRectF bounds = scene()->sceneRect();
            const QSizeF size = boundingRect().size();
            const qreal maxX = std::max(bounds.left(), bounds.right() - size.width());
            const qreal maxY = std::max(bounds.top(), bounds.bottom() - size.height());
            proposed.setX(std::clamp(proposed.x(), bounds.left(), maxX));
            proposed.setY(std::clamp(proposed.y(), bounds.top(), maxY));
        }

        emit snapGuidesUpdated(snappedY, guideY, snappedX, guideX);
        return proposed;
    }
    return QGraphicsObject::itemChange(change, value);
}
