#pragma once
#include <QColor>
#include <QGraphicsObject>
#include <QPixmap>
#include <QString>

// Base class for every movable skin element (play/pause button, seek bar,
// cover art, track label, ...). In edit mode the item can be dragged
// anywhere on the canvas; while dragging, itemChange() compares this
// item's edges/centers against every sibling DraggableItem already placed
// and snaps to alignment within a small pixel threshold — the same "smart
// guides" behavior as PowerPoint/Figma. SkinCanvas listens to
// snapGuidesUpdated() to draw the temporary guide lines.
class DraggableItem : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 100 };
    int type() const override { return Type; }

    explicit DraggableItem(QString elementId, QGraphicsItem *parent = nullptr);

    const QString& elementId() const { return m_elementId; }
    void setElementId(const QString &id) { m_elementId = id; }
    virtual void setEditable(bool editable);
    bool isEditable() const { return m_editable; }

    // Shared accent. A custom color from edit mode is kept across launches
    // and is not replaced by the global accent until the user clears it.
    void setAccentColor(const QColor &color);
    void setSkinColor(const QColor &color);
    void clearSkinColor();
    bool hasSkinColor() const { return m_hasSkinColor; }
    QColor skinColor() const { return m_skinColor; }

    // Per-button size, 0.6–1.8, kept in the skin separately from the window fit.
    void setItemScale(qreal scale);
    qreal itemScale() const { return m_itemScale; }

    void setCustomIconPath(const QString &path);
    void clearCustomIcon();
    bool hasCustomIcon() const { return !m_customIcon.isNull(); }
    QString customIconPath() const { return m_customIconPath; }

    virtual void applyUiScale(qreal scale);
    virtual void setFontScale(qreal) {}
    virtual qreal fontScale() const { return 1.0; }
    // Shrinks the item when the canvas is too small for its preferred size.
    virtual void fitWithin(qreal maxWidth, qreal maxHeight);
    virtual bool canResize() const { return false; }
    // Corner drag scales both sides together. Wide bars opt out.
    virtual bool resizeKeepsRatio() const { return true; }
    virtual void resizeTo(qreal width, qreal height);

signals:
    // Canvas-space coordinates of the guide line to draw, and whether it's
    // currently active. Emitted continuously while dragging near a match.
    void snapGuidesUpdated(bool showHorizontal, qreal guideY, bool showVertical, qreal guideX);
    void dragFinished();              // mouse released after a move — caller should persist the layout
    void clicked();                   // released without having dragged (a real "click", not a drag)

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    virtual void applySkinColor(const QColor &color);
    const QPixmap &customIcon() const { return m_customIcon; }

private:
    QString m_elementId;
    bool m_editable = false;
    QPointF m_pressScenePos;
    bool m_moved = false;
    bool m_resizing = false;
    qreal m_resizeStartW = 0;
    qreal m_resizeStartH = 0;
    QColor m_skinColor;
    QColor m_accent{"#c9974b"};
    bool m_hasSkinColor = false;
    qreal m_itemScale = 1.0;
    QString m_customIconPath;
    QPixmap m_customIcon;

    static constexpr qreal kSnapThreshold = 6.0; // px, in scene/device units
};
