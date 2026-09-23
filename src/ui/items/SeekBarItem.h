#pragma once
#include "../DraggableItem.h"
#include <QColor>

class SeekBarItem : public DraggableItem {
    Q_OBJECT
public:
    explicit SeekBarItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setProgress(qreal fraction01); // 0.0–1.0, purely visual
    void setWidth(qreal w);
    void setAccent(const QColor &color);
    void setShowTimes(bool show);
    void setClock(const QString &elapsed, const QString &total);
    void setDurationMs(qint64 ms);
    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;
    bool canResize() const override { return true; }
    bool resizeKeepsRatio() const override { return false; }
    void resizeTo(qreal width, qreal height) override;

protected:
    void applySkinColor(const QColor &color) override;

signals:
    void seekRequested(qreal fraction01); // user clicked somewhere on the bar while NOT editing

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QRectF barRect() const;

    qreal fractionAt(const QPointF &pos) const;
    QString formatMs(qint64 ms) const;

    qreal m_progress = 0.0;
    qreal m_scrub = 0.0;
    bool m_scrubbing = false;
    qint64 m_durationMs = 0;
    qreal m_width = 400.0;
    qreal m_fitScale = 0;
    qreal m_barHeight = 6.0;
    qreal m_fontPx = 12.0;
    bool m_showTimes = false;
    QString m_elapsed;
    QString m_total;
    QColor m_accent{"#c9974b"};
};
