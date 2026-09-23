#pragma once
#include "../DraggableItem.h"
#include <QPixmap>

class CoverArtItem : public DraggableItem {
    Q_OBJECT
public:
    enum class Shape { Rounded, Square, Vinyl };

    explicit CoverArtItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setPixmap(const QPixmap &pixmap);
    void setSize(qreal size);
    void setShape(Shape shape);
    Shape coverShape() const { return m_shape; }
    void setShapeId(const QString &id);
    QString shapeId() const;
    void setPlaybackActive(bool playing);
    void setEditable(bool editable) override;

    void setFramePath(const QString &path);
    void clearFrame();
    QString framePath() const { return m_framePath; }
    bool hasFrame() const { return !m_frame.isNull(); }

    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;
    bool canResize() const override { return true; }
    void resizeTo(qreal width, qreal height) override;

protected:
    void applySkinColor(const QColor &color) override;

private:
    void drawArt(QPainter *painter, const QRectF &target) const;
    void syncSpin();

    QPixmap m_pixmap;
    QPixmap m_frame;
    QString m_framePath;
    qreal m_size = 56.0;
    qreal m_fitScale = 0;
    Shape m_shape = Shape::Rounded;
    bool m_playing = false;
    qreal m_angle = 0;
    class QVariantAnimation *m_spin = nullptr;
    mutable QPixmap m_artCache;
    mutable qint64 m_artCacheKey = 0;
};
