#pragma once
#include "../DraggableItem.h"
#include <QColor>

class PlayPauseButtonItem : public DraggableItem {
    Q_OBJECT
public:
    explicit PlayPauseButtonItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setPlaying(bool playing);
    void setAccent(const QColor &color);
    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;
    bool canResize() const override { return true; }
    void resizeTo(qreal width, qreal height) override;

protected:
    void applySkinColor(const QColor &color) override;

private:
    bool m_playing = false;
    QColor m_accent{"#c9974b"};
    qreal m_diameter = 40.0;
    qreal m_fitScale = 0;
};
