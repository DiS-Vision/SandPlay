#pragma once

#include <QAbstractItemView>
#include <QEasingCurve>
#include <QEvent>
#include <QObject>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>

// Glides the vertical scrollbar instead of jumping a whole row per wheel notch.
// Parent it to the view so it follows the view's lifetime.
class SmoothScroller : public QObject {
public:
    explicit SmoothScroller(QAbstractItemView *view)
        : QObject(view), m_view(view)
    {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->verticalScrollBar()->setSingleStep(28);
        view->viewport()->installEventFilter(this);
        m_anim = new QPropertyAnimation(view->verticalScrollBar(), "value", this);
        m_anim->setEasingCurve(QEasingCurve::OutQuad);
        m_anim->setDuration(80);
        connect(view->verticalScrollBar(), &QScrollBar::sliderPressed, this, [this]() {
            m_anim->stop();
            m_target = m_view->verticalScrollBar()->value();
        });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_view->viewport() || event->type() != QEvent::Wheel)
            return false;
        const auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->buttons() != Qt::NoButton)
            return false;
        if (wheel->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
            return false;

        int delta = 0;
        if (!wheel->pixelDelta().isNull())
            delta = -wheel->pixelDelta().y();
        else if (wheel->angleDelta().y() != 0) {
            const int row = std::max(32, m_view->sizeHintForRow(0));
            delta = -wheel->angleDelta().y() * row / 40;
        }
        if (delta == 0)
            return false;

        QScrollBar *bar = m_view->verticalScrollBar();
        if (m_anim->state() != QAbstractAnimation::Running)
            m_target = bar->value();
        m_target = std::clamp(m_target + delta, bar->minimum(), bar->maximum());
        m_anim->stop();
        m_anim->setStartValue(bar->value());
        m_anim->setEndValue(m_target);
        m_anim->setDuration(80);
        m_anim->start();
        return true;
    }

private:
    QAbstractItemView *m_view = nullptr;
    QPropertyAnimation *m_anim = nullptr;
    int m_target = 0;
};
