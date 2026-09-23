#pragma once
#include "../DraggableItem.h"

class TrackLabelItem : public DraggableItem {
    Q_OBJECT
public:
    explicit TrackLabelItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setText(const QString &line1, const QString &line2, const QString &line3 = {});
    void setFontScale(qreal scale) override;
    qreal fontScale() const override { return m_fontScale; }
    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;

protected:
    void applySkinColor(const QColor &color) override;

private:
    QString m_line1, m_line2, m_line3;
    void applyFonts();

    qreal m_width = 220.0;
    qreal m_lineHeight = 20.0;
    qreal m_titlePx = 11.0;
    qreal m_metaPx = 9.0;
    qreal m_fitScale = 0;
    qreal m_fontScale = 1.0;
    QColor m_titleColor{"#ece8e0"};
};
