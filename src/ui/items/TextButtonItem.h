#pragma once

#include "../DraggableItem.h"
#include <QColor>

// Square transport button with a drawn icon (not a text label).
class TextButtonItem : public DraggableItem {
    Q_OBJECT
public:
    enum class Glyph {
        Previous,
        Next,
        Shuffle,
        RepeatOff,
        RepeatAll,
        RepeatOne,
        VolumeDown,
        VolumeUp
    };

    explicit TextButtonItem(QString elementId, Glyph glyph, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setGlyph(Glyph glyph);
    void setChecked(bool checked);
    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;
    bool canResize() const override { return true; }
    void resizeTo(qreal width, qreal height) override;

protected:
    void applySkinColor(const QColor &color) override;

private:
    void paintGlyph(QPainter *painter, const QRectF &box) const;

    Glyph m_glyph = Glyph::Previous;
    bool m_checked = false;
    QColor m_accent{"#c9974b"};
    qreal m_side = 46.0;
    qreal m_fitScale = 0;
};
