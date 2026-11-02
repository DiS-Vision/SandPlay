#pragma once

#include "../DraggableItem.h"
#include <QColor>

// Transport button with a drawn icon. Width and height follow the corner drag.
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
    // 0 outline, 1 fill. Skip glyphs drop the accent stroke only in outline style.
    void setButtonStyle(int style);
    void applyUiScale(qreal scale) override;
    void fitWithin(qreal maxWidth, qreal maxHeight) override;
    bool canResize() const override { return true; }
    bool resizeKeepsRatio() const override { return false; }
    void resizeTo(qreal width, qreal height) override;

protected:
    void applySkinColor(const QColor &color) override;

private:
    void paintGlyph(QPainter *painter, const QRectF &box) const;

    Glyph m_glyph = Glyph::Previous;
    bool m_checked = false;
    int m_buttonStyle = 0;
    QColor m_accent{"#c9974b"};
    qreal m_width = 46.0;
    qreal m_height = 46.0;
    qreal m_fitScale = 0;
    bool m_customSize = false;
};
