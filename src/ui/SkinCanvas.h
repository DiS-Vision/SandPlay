#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsLineItem>
#include <QMap>
#include <QJsonObject>
#include <QPixmap>
#include "DraggableItem.h"

class QResizeEvent;
class QMouseEvent;
class QContextMenuEvent;
class QPainter;

// Hosts every movable player element (play button, seek bar, cover, label,
// ...) on an absolute-position canvas. Outside edit mode it's just the
// player's normal transport bar; toggling edit mode makes every element
// draggable with live alignment guides (requirement #5).
class SkinCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit SkinCanvas(QWidget *parent = nullptr);

    void registerItem(DraggableItem *item, const QPointF &defaultPos);
    // Positions are fractions of the canvas (0–1) and scale with it.
    void registerFractionalItem(DraggableItem *item, const QPointF &fraction);
    void setEditMode(bool editing);
    // Positions follow the canvas, but control sizes stay as the user set them.
    void setCompact(bool compact);
    bool editMode() const { return m_editing; }
    bool fractional() const { return m_fractional; }

    void applyAccent(const QColor &color);
    void setSurfaceColor(const QColor &color);
    void setBackgroundPath(const QString &path);
    void clearBackground();
    bool hasBackground() const { return !m_background.isNull(); }
    QString backgroundPath() const { return m_backgroundPath; }
    QVector<DraggableItem *> items() const;
    QMap<QString, QPointF> positions() const;
    bool samePositions(const QMap<QString, QPointF> &saved) const;
    void restorePositions(const QMap<QString, QPointF> &saved);
    // Extra multiplier for button and art size, on top of the canvas fit.
    void setUserScale(qreal scale);
    void relayout();

    // version 3: {elementId: {x, y, color?}} with x/y in 0–1.
    QJsonObject serializeLayout() const;
    void applyLayout(const QJsonObject &layout); // missing ids keep their default position

signals:
    void layoutChanged(); // an item was dropped in a new spot — caller may want to auto-save/debounce
    void backgroundClicked(); // click on empty canvas, outside edit mode
    void itemMenuRequested(DraggableItem *item);
    void canvasMenuRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    QGraphicsScene m_scene;
    QGraphicsLineItem *m_hGuide;
    QGraphicsLineItem *m_vGuide;
    QMap<QString, DraggableItem*> m_items;
    QMap<QString, QPointF> m_fractions;
    bool m_editing = false;
    bool m_fractional = false;
    bool m_compact = false;
    bool m_placing = false;
    qreal m_userScale = 1.0;
    QPixmap m_background;
    QString m_backgroundPath;

    void showGuides(bool showH, qreal y, bool showV, qreal x);
    void rememberFraction(DraggableItem *item);
    void applyFractionalPositions();
    void separateItems();
};
