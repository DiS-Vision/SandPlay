#pragma once

#include "../DraggableItem.h"
#include <QColor>

class VolumeBarItem : public DraggableItem {
    Q_OBJECT
public:
    explicit VolumeBarItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setVolume(qreal fraction01);
    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;

signals:
    void volumeRequested(qreal fraction01);

protected:
    void applySkinColor(const QColor &color) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QRectF barRect() const;

    void paintSpeaker(QPainter *painter, const QRectF &box, int waves) const;
    void applyVolumeAt(const QPointF &pos);

    qreal m_volume = 0.8;
    qreal m_width = 220.0;
    qreal m_barHeight = 8.0;
    qreal m_fontPx = 13.0;
    QColor m_accent{"#c9974b"};
};
