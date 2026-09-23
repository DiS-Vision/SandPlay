#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWidget>

// Square album art with rounded corners. Empty state draws a note glyph.
class RoundedCover : public QWidget {
public:
    explicit RoundedCover(int side, QWidget *parent = nullptr)
        : QWidget(parent), m_side(side)
    {
        setFixedSize(side, side);
    }

    void setPixmap(const QPixmap &pixmap)
    {
        m_pixmap = pixmap;
        update();
    }

    void setAccent(const QColor &color)
    {
        if (!color.isValid()) return;
        m_accent = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        painter.setClipPath(clip);

        if (m_pixmap.isNull()) {
            painter.fillRect(rect(), QColor("#242833"));
            QFont font = painter.font();
            font.setPixelSize(m_side / 3);
            painter.setFont(font);
            painter.setPen(m_accent);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("♪"));
            return;
        }

        const QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint origin((scaled.width() - width()) / 2, (scaled.height() - height()) / 2);
        painter.drawPixmap(rect(), scaled, QRect(origin, size()));
    }

private:
    int m_side;
    QPixmap m_pixmap;
    QColor m_accent{"#c9974b"};
};
