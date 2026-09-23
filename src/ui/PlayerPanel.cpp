#include "PlayerPanel.h"

#include "TrackMime.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QIcon>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QToolButton>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kQueueGrip = 36;

class CloseCross : public QToolButton {
public:
    explicit CloseCross(QWidget *parent = nullptr) : QToolButton(parent)
    {
        setObjectName(QStringLiteral("playerClose"));
        setFixedSize(36, 36);
        setCursor(Qt::PointingHandCursor);
        setAutoRaise(true);
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QToolButton::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        QToolButton::leaveEvent(event);
        update();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 22));
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 8, 8);
        }
        painter.setPen(QPen(QColor("#ece8e0"), 1.8, Qt::SolidLine, Qt::RoundCap));
        const QPointF center = rect().center();
        constexpr qreal arm = 6.0;
        painter.drawLine(center + QPointF(-arm, -arm), center + QPointF(arm, arm));
        painter.drawLine(center + QPointF(arm, -arm), center + QPointF(-arm, arm));
    }
};

class QueueListView : public QListView {
public:
    std::function<void(int from, int to)> onReorder;
    std::function<void(int row, const QList<qint64> &ids)> onTracksDropped;
    int dragRow = -1;

    QueueListView()
    {
        setMouseTracking(true);
        setProperty("queueDragRow", -1);
    }

    QRect gripRect(const QModelIndex &index) const
    {
        const QRect row = visualRect(index);
        return QRect(row.right() - kQueueGrip, row.top(), kQueueGrip, row.height());
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        const QModelIndex index = model() ? model()->index(dragRow, 0) : QModelIndex();
        if (!index.isValid()) return;

        const QRect rect = visualRect(index).intersected(viewport()->rect());
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap(rect.size() * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setOpacity(0.72);
        viewport()->render(&painter, QPoint(), QRegion(rect));
        painter.end();

        setProperty("queueDragRow", dragRow);
        viewport()->update();

        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-sandplay-queue-row"), QByteArray::number(dragRow));
        QDrag drag(this);
        drag.setMimeData(mime);
        drag.setPixmap(pixmap);
        drag.setHotSpot(QPoint(rect.width() - kQueueGrip / 2, rect.height() / 2));
        drag.exec(Qt::MoveAction);

        dragRow = -1;
        m_gripPress = false;
        setProperty("queueDragRow", -1);
        viewport()->update();
    }
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (accepts(event)) event->acceptProposedAction();
        else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (accepts(event)) event->acceptProposedAction();
        else event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        const QPoint pos = event->position().toPoint();
        const QModelIndex at = indexAt(pos);
        if (event->source() == this) {
            const int from = dragRow >= 0 ? dragRow : currentIndex().row();
            dragRow = -1;
            const int to = at.isValid() ? at.row() : std::max(0, model()->rowCount() - 1);
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            if (onReorder && from >= 0 && to >= 0 && from != to) onReorder(from, to);
            return;
        }
        if (event->mimeData()->hasFormat(QString::fromLatin1(kTrackIdsMime))) {
            const int row = at.isValid() ? at.row() : model()->rowCount();
            event->setDropAction(Qt::CopyAction);
            event->accept();
            if (onTracksDropped) onTracksDropped(row, decodeTrackIds(event->mimeData()));
            return;
        }
        event->ignore();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex index = indexAt(event->pos());
        dragRow = index.row();
        m_gripPress = index.isValid() && gripRect(index).contains(event->pos());
        if (m_gripPress) setCursor(Qt::ClosedHandCursor);
        QListView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            const QModelIndex index = indexAt(event->pos());
            setCursor(index.isValid() && gripRect(index).contains(event->pos())
                          ? Qt::OpenHandCursor : Qt::ArrowCursor);
            QListView::mouseMoveEvent(event);
            return;
        }
        if (m_gripPress) QListView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_gripPress = false;
        if (cursor().shape() == Qt::ClosedHandCursor) unsetCursor();
        QListView::mouseReleaseEvent(event);
    }

private:
    bool m_gripPress = false;

    bool accepts(const QDropEvent *event) const
    {
        return event->source() == this
            || event->mimeData()->hasFormat(QString::fromLatin1(kTrackIdsMime));
    }
};

class QueueElideDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const int icon = opt.icon.isNull() ? 0 : opt.decorationSize.width() + 10;
        const int width = std::max(0, opt.rect.width() - icon - kQueueGrip - 8);
        opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, width);
        opt.textElideMode = Qt::ElideRight;
        const QWidget *widget = opt.widget;
        int dragRow = -1;
        for (const QWidget *owner = widget; owner; owner = owner->parentWidget()) {
            const QVariant mark = owner->property("queueDragRow");
            if (!mark.isValid()) continue;
            dragRow = mark.toInt();
            break;
        }
        const bool fading = dragRow == index.row();
        const bool selected = opt.state.testFlag(QStyle::State_Selected);
        QColor background(QStringLiteral("#181b22"));
        if (const QVariant fill = index.data(Qt::BackgroundRole); fill.canConvert<QColor>())
            background = fill.value<QColor>();
        if (selected) background = QColor(QStringLiteral("#3a3428"));
        painter->save();
        if (fading) painter->setOpacity(0.45);
        painter->fillRect(opt.rect, background);

        QRect textRect = opt.rect.adjusted(8, 0, -kQueueGrip, 0);
        if (!opt.icon.isNull()) {
            const int side = opt.decorationSize.width() > 0 ? opt.decorationSize.width() : 36;
            const QRect iconRect(textRect.left(), textRect.center().y() - side / 2, side, side);
            opt.icon.paint(painter, iconRect);
            textRect.setLeft(iconRect.right() + 10);
        }
        QColor textColor(QStringLiteral("#ece8e0"));
        if (const QVariant ink = index.data(Qt::ForegroundRole); ink.canConvert<QColor>())
            textColor = ink.value<QColor>();
        if (const QVariant fontRole = index.data(Qt::FontRole); fontRole.canConvert<QFont>())
            painter->setFont(fontRole.value<QFont>());
        else
            painter->setFont(opt.font);
        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width()));

        painter->setOpacity(fading ? 0.45 : 1.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#8b8a86"));
        const qreal barW = 16.0;
        const qreal barH = 2.4;
        const qreal x = opt.rect.right() - 26.0;
        const qreal y = opt.rect.center().y();
        painter->drawRoundedRect(QRectF(x, y - 5.0, barW, barH), 1.2, 1.2);
        painter->drawRoundedRect(QRectF(x, y + 2.6, barW, barH), 1.2, 1.2);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        const QWidget *widget = option.widget;
        auto *view = qobject_cast<const QAbstractItemView *>(widget);
        if (!view && widget) view = qobject_cast<const QAbstractItemView *>(widget->parentWidget());
        if (!view) return size;
        const int width = view->viewport()->width();
        if (width > 0) size.setWidth(width);
        return size;
    }
};

} // namespace

PlayerPanel::PlayerPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("playerPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    // Empty corner of the L-shaped margin. The logo stays on the main window.
    auto *logoSlot = new QWidget;
    logoSlot->setObjectName(QStringLiteral("playerLogo"));
    logoSlot->setFixedSize(88, 72);
    logoSlot->setAttribute(Qt::WA_StyledBackground, true);

    m_coverStatus = new QLabel;
    m_coverStatus->setStyleSheet(QStringLiteral("color: #8b8a86; background: transparent;"));
    m_editHint = new QLabel(tr("Угол кнопки и обложки меняет размер пропорционально. Правый клик: текстура, рамка, фон."));
    m_editHint->setStyleSheet(QStringLiteral("color: #c9974b; background: transparent;"));
    m_editHint->hide();

    auto *closeButton = new CloseCross;

    auto *topBar = new QWidget;
    topBar->setObjectName(QStringLiteral("playerTop"));
    topBar->setAttribute(Qt::WA_StyledBackground, true);
    topBar->setFixedHeight(72);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 12, 16, 12);
    topLayout->addWidget(m_coverStatus);
    topLayout->addWidget(m_editHint);
    topLayout->addStretch(1);
    topLayout->addWidget(closeButton);

    auto *leftGutter = new QWidget;
    leftGutter->setObjectName(QStringLiteral("playerGutter"));
    leftGutter->setAttribute(Qt::WA_StyledBackground, true);
    leftGutter->setFixedWidth(88);

    m_canvas = new SkinCanvas;
    m_canvas->setSurfaceColor(QColor(QStringLiteral("#181b22")));

    m_cover = new CoverArtItem;
    m_label = new TrackLabelItem;
    m_label->setText(tr("Ничего не играет"), tr("Выберите трек в библиотеке"), QString());
    m_seek = new SeekBarItem;
    m_seek->setShowTimes(true);
    m_play = new PlayPauseButtonItem;
    m_previous = new TextButtonItem(QStringLiteral("prev_button"), TextButtonItem::Glyph::Previous);
    m_next = new TextButtonItem(QStringLiteral("next_button"), TextButtonItem::Glyph::Next);
    m_shuffle = new TextButtonItem(QStringLiteral("shuffle_button"), TextButtonItem::Glyph::Shuffle);
    m_repeat = new TextButtonItem(QStringLiteral("repeat_button"), TextButtonItem::Glyph::RepeatOff);
    m_volumeBar = new VolumeBarItem;

    m_canvas->registerFractionalItem(m_cover, {0.03, 0.04});
    m_canvas->registerFractionalItem(m_label, {0.34, 0.05});
    m_canvas->registerFractionalItem(m_seek, {0.03, 0.58});
    m_canvas->registerFractionalItem(m_previous, {0.03, 0.78});
    m_canvas->registerFractionalItem(m_play, {0.16, 0.74});
    m_canvas->registerFractionalItem(m_next, {0.28, 0.78});
    m_canvas->registerFractionalItem(m_shuffle, {0.42, 0.78});
    m_canvas->registerFractionalItem(m_repeat, {0.60, 0.78});
    m_canvas->registerFractionalItem(m_volumeBar, {0.72, 0.80});

    m_queueTitle = new QLabel(tr("Очередь"));
    m_queueTitle->setStyleSheet(QStringLiteral("font-weight: 700; background: transparent;"));
    auto *queueView = new QueueListView;
    m_queueView = queueView;
    m_queueView->setModel(&m_queueModel);
    m_queueView->setIconSize(QSize(36, 36));
    m_queueView->setSpacing(4);
    m_queueView->setWordWrap(false);
    m_queueView->setTextElideMode(Qt::ElideRight);
    m_queueView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_queueView->setItemDelegate(new QueueElideDelegate(m_queueView));
    m_queueView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_queueView->setDragEnabled(true);
    m_queueView->setAcceptDrops(true);
    m_queueView->setDropIndicatorShown(true);
    m_queueView->setDragDropMode(QAbstractItemView::DragDrop);
    m_queueView->setDefaultDropAction(Qt::MoveAction);
    QPalette queuePalette = m_queueView->palette();
    queuePalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#3a3428")));
    queuePalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#f3efe6")));
    m_queueView->setPalette(queuePalette);
    m_queueView->viewport()->setPalette(queuePalette);
    m_queueView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_queueEmpty = new QLabel(tr("Очередь пуста. Дважды щёлкните трек в библиотеке или перетащите его сюда."));
    m_queueEmpty->setWordWrap(true);
    m_queueEmpty->setStyleSheet(QStringLiteral("color: #8b8a86; background: transparent;"));

    auto *queueLayout = new QVBoxLayout;
    queueLayout->setContentsMargins(8, 0, 16, 16);
    queueLayout->addWidget(m_queueTitle);
    queueLayout->addWidget(m_queueView, 1);
    queueLayout->addWidget(m_queueEmpty);
    m_queueView->hide();
    m_queuePane = new QWidget;
    m_queuePane->setObjectName(QStringLiteral("playerQueue"));
    m_queuePane->setAttribute(Qt::WA_StyledBackground, true);
    m_queuePane->setLayout(queueLayout);
    m_queuePane->setMinimumWidth(180);

    m_split = new QSplitter;
    m_split->addWidget(m_canvas);
    m_split->addWidget(m_queuePane);
    m_split->setStretchFactor(0, 1);
    m_split->setStretchFactor(1, 0);
    m_split->setCollapsible(0, false);
    m_split->setCollapsible(1, false);
    m_split->setHandleWidth(0);
    if (QSplitterHandle *handle = m_split->handle(1)) {
        handle->setEnabled(false);
        handle->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        handle->setCursor(Qt::ArrowCursor);
    }

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(logoSlot, 0, 0);
    grid->addWidget(topBar, 0, 1);
    grid->addWidget(leftGutter, 1, 0);
    grid->addWidget(m_split, 1, 1);

    queueView->onReorder = [this](int from, int to) { emit queueReordered(from, to); };
    queueView->onTracksDropped = [this](int row, const QList<qint64> &ids) {
        m_pendingDropIds = ids;
        emit tracksDroppedOnQueue(row);
    };

    connect(closeButton, &QToolButton::clicked, this, &PlayerPanel::closeRequested);
    connect(m_canvas, &SkinCanvas::layoutChanged, this, &PlayerPanel::skinLayoutChanged);
    connect(m_play, &DraggableItem::clicked, this, &PlayerPanel::playPauseClicked);
    connect(m_previous, &DraggableItem::clicked, this, &PlayerPanel::previousClicked);
    connect(m_next, &DraggableItem::clicked, this, &PlayerPanel::nextClicked);
    connect(m_seek, &SeekBarItem::seekRequested, this, &PlayerPanel::seekRequested);
    connect(m_shuffle, &DraggableItem::clicked, this, [this]() { emit shuffleToggled(!m_shuffleOn); });
    connect(m_repeat, &DraggableItem::clicked, this, &PlayerPanel::repeatClicked);
    connect(m_volumeBar, &VolumeBarItem::volumeRequested, this, [this](qreal fraction) {
        m_volumePercent = int(std::lround(fraction * 100.0));
        emit volumeChanged(m_volumePercent);
    });
    connect(m_split, &QSplitter::splitterMoved, this, [this]() {
        if (m_applyingRatio) return;
        applyQueueRatio();
    });
    connect(m_queueView, &QListView::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid()) emit queueActivated(index.row());
    });
    connect(m_queueView, &QListView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_queueView->indexAt(pos);
        if (!index.isValid()) return;
        QMenu menu(this);
        QAction *play = menu.addAction(tr("Играть"));
        QAction *up = menu.addAction(tr("Выше"));
        QAction *down = menu.addAction(tr("Ниже"));
        QAction *remove = menu.addAction(tr("Удалить из очереди"));
        up->setEnabled(index.row() > 0);
        down->setEnabled(index.row() + 1 < m_queueView->model()->rowCount());
        QAction *chosen = menu.exec(m_queueView->viewport()->mapToGlobal(pos));
        if (chosen == play) emit queueActivated(index.row());
        else if (chosen == up) emit queueReordered(index.row(), index.row() - 1);
        else if (chosen == down) emit queueReordered(index.row(), index.row() + 1);
        else if (chosen == remove) emit queueRemoveRequested(index.row());
    });
}

void PlayerPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int half = std::max(160, width() / 2);
    m_queuePane->setMaximumWidth(half);
    m_queuePane->setMinimumWidth(std::min(180, half));
    applyQueueRatio();
    applyQueueChrome();
}

void PlayerPanel::applyQueueRatio()
{
    const int total = m_split->width();
    const int panelWidth = std::max(width(), 1);
    if (total < 80) return;
    const int maxQueue = std::min(m_queuePane->maximumWidth(), std::max(160, panelWidth / 2));
    const int minQueue = m_queuePane->minimumWidth();
    const int queue = std::clamp(int(std::lround(panelWidth * m_queueRatio)), minQueue, std::max(minQueue, maxQueue));
    m_applyingRatio = true;
    m_split->setSizes({std::max(0, total - queue), queue});
    m_applyingRatio = false;
}

void PlayerPanel::applyQueueChrome()
{
    const int pane = std::max(180, m_queuePane->width());
    const int icon = std::clamp(pane / 8, 28, 64);
    m_queueView->setIconSize(QSize(icon, icon));
    QFont rowFont = m_queueView->font();
    rowFont.setPixelSize(std::clamp(pane / 16, 13, 22));
    m_queueView->setFont(rowFont);
    QFont titleFont = m_queueTitle->font();
    titleFont.setPixelSize(std::clamp(pane / 14, 15, 26));
    titleFont.setBold(true);
    m_queueTitle->setFont(titleFont);
    QFont emptyFont = m_queueEmpty->font();
    emptyFont.setPixelSize(std::clamp(pane / 18, 12, 18));
    m_queueEmpty->setFont(emptyFont);
}

void PlayerPanel::setTrack(const std::optional<Track> &track)
{
    if (!track) {
        m_label->setText(tr("Ничего не играет"), tr("Выберите трек в библиотеке"), QString());
        m_cover->setPixmap({});
        setPosition(0, 0);
        return;
    }
    m_label->setText(track->title.isEmpty() ? tr("Без названия") : track->title,
                     track->artist.isEmpty() ? tr("Неизвестный исполнитель") : track->artist,
                     track->album.isEmpty() ? tr("Альбом не указан") : track->album);
    m_cover->setPixmap(track->coverPath.isEmpty() ? QPixmap{} : QPixmap(track->coverPath));
}

void PlayerPanel::setCover(const QPixmap &pixmap)
{
    m_cover->setPixmap(pixmap);
}

void PlayerPanel::setPlaying(bool playing)
{
    m_play->setPlaying(playing);
    m_cover->setPlaybackActive(playing);
}

void PlayerPanel::setPosition(qint64 positionMs, qint64 durationMs)
{
    m_seek->setDurationMs(durationMs);
    m_seek->setClock(formatTime(positionMs), formatTime(durationMs));
    m_seek->setProgress(durationMs > 0 ? double(positionMs) / double(durationMs) : 0.0);
}

void PlayerPanel::setVolumePercent(int percent)
{
    m_volumePercent = std::clamp(percent, 0, 100);
    m_volumeBar->setVolume(m_volumePercent / 100.0);
}

void PlayerPanel::setShuffle(bool on)
{
    m_shuffleOn = on;
    m_shuffle->setChecked(on);
}

void PlayerPanel::setRepeatMode(PlaybackQueue::RepeatMode mode)
{
    switch (mode) {
    case PlaybackQueue::RepeatMode::RepeatOne:
        m_repeat->setGlyph(TextButtonItem::Glyph::RepeatOne);
        break;
    case PlaybackQueue::RepeatMode::RepeatAll:
        m_repeat->setGlyph(TextButtonItem::Glyph::RepeatAll);
        break;
    case PlaybackQueue::RepeatMode::Off:
        m_repeat->setGlyph(TextButtonItem::Glyph::RepeatOff);
        break;
    }
}

void PlayerPanel::setAccent(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    m_canvas->applyAccent(color);
}

void PlayerPanel::setQueue(const QVector<Track> &tracks, int currentIndex)
{
    m_queueModel.setQueue(tracks, currentIndex);
    const bool empty = tracks.isEmpty();
    m_queueView->setVisible(!empty);
    m_queueEmpty->setVisible(empty);
}

void PlayerPanel::setCoverStatus(const QString &text)
{
    m_coverStatus->setText(text);
    m_coverStatus->setVisible(!text.isEmpty() && !m_canvas->editMode());
}

void PlayerPanel::setEditMode(bool on)
{
    m_canvas->setEditMode(on);
    m_editHint->setVisible(on);
    if (on) m_coverStatus->hide();
    else m_coverStatus->setVisible(!m_coverStatus->text().isEmpty());
}

bool PlayerPanel::editMode() const
{
    return m_canvas->editMode();
}

void PlayerPanel::setCoverShapeId(const QString &id)
{
    m_cover->setShapeId(id);
    emit skinLayoutChanged();
}

QString PlayerPanel::coverShapeId() const
{
    return m_cover->shapeId();
}

void PlayerPanel::setCoverFramePath(const QString &path)
{
    m_cover->setFramePath(path);
    emit skinLayoutChanged();
}

void PlayerPanel::clearCoverFrame()
{
    m_cover->clearFrame();
    emit skinLayoutChanged();
}

bool PlayerPanel::hasCoverFrame() const
{
    return m_cover->hasFrame();
}

void PlayerPanel::setQueueRatio(qreal ratio)
{
    m_queueRatio = std::clamp(ratio, 0.16, 0.5);
    applyQueueRatio();
}

QList<qint64> PlayerPanel::takePendingDropIds()
{
    QList<qint64> ids;
    ids.swap(m_pendingDropIds);
    return ids;
}

QString PlayerPanel::formatTime(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 seconds = ms / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}
