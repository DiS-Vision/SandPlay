#include "MainWindow.h"
#include "SandPlayLogo.h"
#include "items/TextButtonItem.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTextEdit>

#include "TrackMime.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QColorDialog>
#include <QImageReader>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QMap>
#include <QCursor>
#include <QDialog>
#include <QFormLayout>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QWidgetAction>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <QToolButton>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QStatusBar>
#include <QItemSelectionModel>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QRegion>
#include <QCloseEvent>
#include <QEvent>
#include <QResizeEvent>
#include <algorithm>
#include <functional>

namespace {

class PixmapSlide : public QWidget {
public:
    explicit PixmapSlide(const QPixmap &frame, QWidget *parent) : QWidget(parent), m_frame(frame)
    {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_frame);
    }

private:
    QPixmap m_frame;
};

QPixmap renderPanelFrame(PlayerPanel *panel, const QRect &target, bool offscreen)
{
    if (offscreen) {
        // Keep the live panel outside the window while the bitmap is taken,
        // otherwise it flashes at full size for a frame before the slide.
        panel->setAttribute(Qt::WA_DontShowOnScreen, true);
        panel->setGeometry(target.translated(0, target.height() + 8));
        panel->show();
    } else {
        panel->setGeometry(target);
    }
    if (panel->layout()) panel->layout()->activate();
    panel->repaint();

    const qreal ratio = std::max(1.0, panel->devicePixelRatioF());
    QPixmap shot(target.size() * ratio);
    shot.setDevicePixelRatio(ratio);
    shot.fill(QColor(QStringLiteral("#181b22")));
    panel->render(&shot, QPoint(), QRegion(QRect(QPoint(0, 0), target.size())),
                  QWidget::DrawWindowBackground | QWidget::DrawChildren);
    if (offscreen) {
        panel->hide();
        panel->setAttribute(Qt::WA_DontShowOnScreen, false);
        panel->move(target.x(), target.bottom() + 8);
    }
    return shot;
}

class TrackCoverDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->fillRect(option.rect, QColor(selected ? QStringLiteral("#3a3428")
                                                       : QStringLiteral("#1b1e27")));
        if (index.column() == TrackTableModel::ColCover) {
            const int side = std::min(option.rect.width(), option.rect.height()) - 16;
            if (side < 8) return;
            const QRect box(option.rect.center().x() - side / 2, option.rect.center().y() - side / 2, side, side);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            QPainterPath clip;
            clip.addRoundedRect(box, 8, 8);
            painter->setClipPath(clip);
            const QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();
            if (thumb.isNull()) {
                painter->fillRect(box, QColor(QStringLiteral("#242833")));
                painter->setClipping(false);
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(QStringLiteral("#d97757")));
                QPolygonF mark;
                const qreal s = side / 64.0;
                const QPointF o = box.topLeft();
                mark << o + QPointF(22 * s, 20 * s) << o + QPointF(22 * s, 44 * s) << o + QPointF(46 * s, 32 * s);
                painter->drawPolygon(mark);
            } else {
                painter->drawPixmap(box, thumb);
            }
            painter->restore();
            return;
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        int align = index.data(Qt::TextAlignmentRole).toInt();
        if (align == 0) align = Qt::AlignLeft | Qt::AlignVCenter;
        painter->save();
        painter->setPen(QColor(QStringLiteral("#f3efe6")));
        painter->setFont(option.font);
        const QRect textRect = option.rect.adjusted(12, 0, -12, 0);
        painter->drawText(textRect, align,
                          option.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};

class PlaylistRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->fillRect(option.rect, QColor(selected ? QStringLiteral("#3a3428") : QStringLiteral("#1b1e27")));
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        QRect textRect = opt.rect.adjusted(10, 0, -10, 0);
        if (!opt.icon.isNull()) {
            const int side = 28;
            const QRect iconRect(textRect.left(), textRect.center().y() - side / 2, side, side);
            QPainterPath clip;
            clip.addRoundedRect(QRectF(iconRect), 6, 6);
            painter->setClipPath(clip);
            painter->drawPixmap(iconRect, opt.icon.pixmap(side, side));
            painter->setClipping(false);
            textRect.setLeft(iconRect.right() + 8);
        }
        painter->setPen(QColor(QStringLiteral("#f3efe6")));
        painter->setFont(opt.font);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};

class LibraryTableView : public QTableView {
public:
    int dragRow = -1;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex index = indexAt(event->pos());
        dragRow = index.row();
        QTableView::mousePressEvent(event);
        // A click past the rows clears the row, then Qt selects the previous
        // cell again. Drop that leftover highlight.
        if (!index.isValid() && selectionModel())
            selectionModel()->clear();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool empty = !indexAt(event->pos()).isValid();
        QTableView::mouseReleaseEvent(event);
        if (empty && selectionModel())
            selectionModel()->clear();
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (property("reorderable").toBool() && event->source() == this) event->acceptProposedAction();
        else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (property("reorderable").toBool() && event->source() == this) event->acceptProposedAction();
        else event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        auto *tracks = qobject_cast<TrackTableModel *>(model());
        if (!property("reorderable").toBool() || event->source() != this || !tracks) {
            event->ignore();
            return;
        }
        const int from = dragRow >= 0 ? dragRow : currentIndex().row();
        dragRow = -1;
        const QModelIndex at = indexAt(event->position().toPoint());
        const int to = at.isValid() ? at.row() : tracks->rowCount() - 1;
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (from >= 0 && to >= 0 && from != to)
            QTimer::singleShot(0, tracks, [tracks, from, to]() { tracks->reorderRows(from, to); });
    }
};

class PlaylistDropView : public QListView {
public:
    std::function<void(int row, const QList<qint64> &ids)> onTracksDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(QString::fromLatin1(kTrackIdsMime))) event->acceptProposedAction();
        else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->mimeData()->hasFormat(QString::fromLatin1(kTrackIdsMime))) event->acceptProposedAction();
        else event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasFormat(QString::fromLatin1(kTrackIdsMime))) {
            event->ignore();
            return;
        }
        int row = indexAt(event->position().toPoint()).row();
        if (row < 0) row = currentIndex().row();
        event->setDropAction(Qt::CopyAction);
        event->accept();
        if (row >= 0 && onTracksDropped) onTracksDropped(row, decodeTrackIds(event->mimeData()));
    }
};

QString formatClock(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 seconds = ms / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

bool loadStaticPng(QWidget *parent, const QString &path, QImage *image)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (reader.format().toLower() != "png") {
        QMessageBox::warning(parent, QObject::tr("Изображение"),
                             QObject::tr("Нужен файл в формате PNG."));
        return false;
    }
    if (reader.imageCount() > 1) {
        QMessageBox::warning(parent, QObject::tr("Изображение"),
                             QObject::tr("Изображение должно быть статичным: без анимации и движения."));
        return false;
    }
    *image = reader.read();
    if (image->isNull()) {
        QMessageBox::warning(parent, QObject::tr("Изображение"),
                             QObject::tr("Не удалось прочитать PNG."));
        return false;
    }
    return true;
}

bool keepElementsStill(QWidget *parent, SkinCanvas *canvas, const QMap<QString, QPointF> &before)
{
    if (canvas->samePositions(before)) return true;
    canvas->restorePositions(before);
    QMessageBox::warning(parent, QObject::tr("Скин"),
                         QObject::tr("Скин меняет только внешний вид. Элементы должны оставаться на месте."));
    return false;
}

bool chooseFontScale(QWidget *parent, qreal current, qreal *chosen)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Размер шрифта"));
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(70, 200);
    slider->setValue(int(std::lround(std::clamp(current, 0.7, 2.0) * 100.0)));
    auto *value = new QLabel(QString::number(slider->value()) + QLatin1Char('%'));
    value->setMinimumWidth(48);
    QObject::connect(slider, &QSlider::valueChanged, value, [value](int percent) {
        value->setText(QString::number(percent) + QLatin1Char('%'));
    });
    auto *row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(value);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QObject::tr("Название, исполнитель и альбом")));
    layout->addLayout(row);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    *chosen = slider->value() / 100.0;
    return true;
}

bool canHideControl(const QString &id)
{
    return id == QLatin1String("prev_button") || id == QLatin1String("next_button")
        || id == QLatin1String("shuffle_button") || id == QLatin1String("repeat_button")
        || id == QLatin1String("play_pause_button") || id == QLatin1String("mini_play")
        || id == QLatin1String("mini_prev") || id == QLatin1String("mini_next")
        || id == QLatin1String("volume_bar");
}

QString controlTitle(const QString &id)
{
    if (id == QLatin1String("prev_button") || id == QLatin1String("mini_prev")) return QObject::tr("Назад");
    if (id == QLatin1String("next_button") || id == QLatin1String("mini_next")) return QObject::tr("Дальше");
    if (id == QLatin1String("shuffle_button")) return QObject::tr("Перемешать");
    if (id == QLatin1String("repeat_button")) return QObject::tr("Повтор");
    if (id == QLatin1String("play_pause_button") || id == QLatin1String("mini_play"))
        return QObject::tr("Воспроизведение");
    if (id == QLatin1String("volume_bar")) return QObject::tr("Громкость");
    return id;
}

} // namespace

MainWindow::MainWindow(Database &db, AudioEngine &audio, LibraryManager &library,
                       CoverCache &covers, QWidget *parent)
    : QMainWindow(parent)
    , m_db(db), m_audio(audio), m_library(library), m_covers(covers)
    , m_fetcher(db, covers)
{
    setWindowTitle(QStringLiteral("SandPlay"));
    resize(1100, 760);
    setStyleSheet(QStringLiteral(R"(
        QWidget { background: #14161c; color: #ece8e0; }
        QTableView, QListView, QLineEdit {
            background: #1b1e27; color: #ece8e0;
            border: 1px solid #2a2e3a; border-radius: 6px;
            selection-background-color: #3a3428; selection-color: #f3efe6;
        }
        QHeaderView::section {
            background: #1b1e27; color: #8b8a86; border: none;
            border-right: 1px solid #2a2e3a; border-bottom: 1px solid #2a2e3a;
            padding: 8px 12px;
        }
        QTableView#libraryTable {
            border: none; border-radius: 0px; background: #1b1e27;
            selection-background-color: #3a3428; selection-color: #f3efe6;
        }
        QListView::item:selected {
            background-color: #3a3428; color: #f3efe6;
        }
        QPushButton {
            background: #242833; color: #ece8e0;
            border: 1px solid #2a2e3a; border-radius: 6px; padding: 6px 10px;
        }
        QPushButton:hover { border-color: #c9974b; }
        QPushButton:checked { background: #c9974b; color: #14161c; }
        QSplitter::handle { background: #2a2e3a; }
        QGraphicsView { background: #1b1e27; border: none; }
        QStatusBar { color: #8b8a86; }
        #playerPanel, #playerLogo, #playerGutter, #playerTop, #playerQueue { background: #181b22; border: none; }
        #logoStrip { background: #14161c; border: none; }
        #appLogo { background: transparent; border: none; border-radius: 12px; }
        #appLogo:hover { background: #242833; }
        #miniBar { background: #1b1e27; border-top: 1px solid #2a2e3a; }
    )"));

    auto *addButton = new QPushButton(tr("+ Добавить трек"));
    auto *importButton = new QPushButton(tr("+ Импорт папки"));
    auto *libraryButton = new QPushButton(tr("Вся библиотека"));
    auto *newPlaylistButton = new QPushButton(tr("+ Новый плейлист"));

    auto *playlistView = new PlaylistDropView;
    m_playlistView = playlistView;
    m_playlistView->setModel(&m_playlistModel);
    m_playlistView->setIconSize(QSize(28, 28));
    m_playlistView->setSpacing(2);
    m_playlistView->setAcceptDrops(true);
    m_playlistView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_playlistView->setItemDelegate(new PlaylistRowDelegate(m_playlistView));

    auto *railLayout = new QVBoxLayout;
    railLayout->setContentsMargins(8, 8, 8, 8);
    railLayout->addWidget(addButton);
    railLayout->addWidget(importButton);
    railLayout->addWidget(libraryButton);
    railLayout->addWidget(new QLabel(tr("Плейлисты")));
    railLayout->addWidget(m_playlistView, 1);
    railLayout->addWidget(newPlaylistButton);
    auto *rail = new QWidget;
    rail->setLayout(railLayout);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Поиск по названию, исполнителю, альбому…"));
    auto *trackView = new LibraryTableView;
    m_trackView = trackView;
    m_trackView->setModel(&m_trackModel);
    m_trackView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trackView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_trackView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trackView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_trackView->setObjectName(QStringLiteral("libraryTable"));
    m_trackView->setShowGrid(false);
    m_trackView->setWordWrap(false);
    m_trackView->setTextElideMode(Qt::ElideRight);
    m_trackView->setAlternatingRowColors(false);
    m_trackView->setIconSize(QSize(56, 56));
    m_trackView->setItemDelegate(new TrackCoverDelegate(m_trackView));
    m_trackView->setDragEnabled(true);
    m_trackView->setAcceptDrops(true);
    m_trackView->setDropIndicatorShown(true);
    m_trackView->setDragDropMode(QAbstractItemView::DragDrop);
    m_trackView->setDefaultDropAction(Qt::CopyAction);
    m_trackView->verticalHeader()->setDefaultSectionSize(72);
    m_trackView->verticalHeader()->setMinimumWidth(0);
    m_trackView->verticalHeader()->setMaximumWidth(0);
    m_trackView->verticalHeader()->setFixedWidth(0);
    m_trackView->verticalHeader()->hide();
    m_trackView->horizontalHeader()->setHighlightSections(false);
    m_trackView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_trackView->horizontalHeader()->setStretchLastSection(false);
    m_trackView->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColCover, QHeaderView::Fixed);
    m_trackView->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColTitle, QHeaderView::Interactive);
    m_trackView->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColArtistAlbum, QHeaderView::Stretch);
    m_trackView->horizontalHeader()->setSectionResizeMode(TrackTableModel::ColDuration, QHeaderView::Fixed);
    m_trackView->setColumnWidth(TrackTableModel::ColCover, 76);
    m_trackView->setColumnWidth(TrackTableModel::ColTitle, 280);
    m_trackView->setColumnWidth(TrackTableModel::ColDuration, 84);
    QPalette selection = m_trackView->palette();
    selection.setColor(QPalette::Highlight, QColor(QStringLiteral("#3a3428")));
    selection.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#f3efe6")));
    m_trackView->setPalette(selection);
    m_trackView->viewport()->setPalette(selection);
    m_playlistView->setPalette(selection);
    m_playlistView->viewport()->setPalette(selection);

    m_showMore = new QPushButton(tr("Показать ещё"));
    m_showMore->hide();

    auto *centerLayout = new QVBoxLayout;
    centerLayout->setContentsMargins(8, 8, 8, 8);
    centerLayout->addWidget(m_searchEdit);
    centerLayout->addWidget(m_trackView, 1);
    centerLayout->addWidget(m_showMore);
    auto *center = new QWidget;
    center->setLayout(centerLayout);

    m_librarySplit = new QSplitter;
    m_librarySplit->addWidget(rail);
    m_librarySplit->addWidget(center);
    m_librarySplit->setStretchFactor(1, 1);
    m_librarySplit->setSizes({240, 860});
    m_librarySplit->handle(1)->installEventFilter(this);

    m_panel = new PlayerPanel(this);
    m_panel->hide();

    m_canvas = new SkinCanvas;
    m_canvas->setFixedHeight(72);
    m_canvas->setCompact(true);

    m_coverItem = new CoverArtItem;
    m_coverItem->setElementId(QStringLiteral("mini_cover"));
    m_coverItem->setSize(64);
    m_labelItem = new TrackLabelItem;
    m_labelItem->setElementId(QStringLiteral("mini_label"));
    m_labelItem->setText(tr("Ничего не играет"), tr("Нажмите, чтобы открыть плеер"));
    m_playPauseItem = new PlayPauseButtonItem;
    m_playPauseItem->setElementId(QStringLiteral("mini_play"));
    auto *miniPrev = new TextButtonItem(QStringLiteral("mini_prev"), TextButtonItem::Glyph::Previous);
    auto *miniNext = new TextButtonItem(QStringLiteral("mini_next"), TextButtonItem::Glyph::Next);
    miniPrev->resizeTo(36, 36);
    miniNext->resizeTo(36, 36);
    m_seekItem = new SeekBarItem;
    m_seekItem->setElementId(QStringLiteral("mini_seek"));
    m_seekItem->setShowTimes(true);
    m_seekItem->setWidth(420);

    m_canvas->registerFractionalItem(m_coverItem, {0.01, 0.08});
    m_canvas->registerFractionalItem(m_labelItem, {0.07, 0.10});
    m_canvas->registerFractionalItem(miniPrev, {0.405, 0.14});
    m_canvas->registerFractionalItem(m_playPauseItem, {0.46, 0.10});
    m_canvas->registerFractionalItem(miniNext, {0.515, 0.14});
    m_canvas->registerFractionalItem(m_seekItem, {0.01, 0.62});

    auto *miniLayout = new QHBoxLayout;
    miniLayout->setContentsMargins(8, 4, 8, 4);
    miniLayout->addWidget(m_canvas, 1);
    auto *miniBar = new QWidget;
    miniBar->setObjectName(QStringLiteral("miniBar"));
    miniBar->setLayout(miniLayout);

    auto *logoButton = new QToolButton;
    logoButton->setObjectName(QStringLiteral("appLogo"));
    logoButton->setFixedSize(44, 44);
    logoButton->setIconSize(QSize(36, 36));
    logoButton->setAutoRaise(true);
    logoButton->setCursor(Qt::PointingHandCursor);
    logoButton->setIcon(sandPlayLogoIcon());
    auto *logoStrip = new QWidget;
    logoStrip->setObjectName(QStringLiteral("logoStrip"));
    logoStrip->setFixedHeight(64);
    auto *logoRow = new QHBoxLayout(logoStrip);
    logoRow->setContentsMargins(14, 8, 14, 8);
    logoRow->addWidget(logoButton);
    logoRow->addStretch(1);

    auto *rootLayout = new QVBoxLayout;
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(logoStrip);
    rootLayout->addWidget(m_librarySplit, 1);
    rootLayout->addWidget(miniBar);
    auto *root = new QWidget;
    root->setLayout(rootLayout);
    setCentralWidget(root);
    m_panel->setParent(root);
    m_panel->hide();

    playlistView->onTracksDropped = [this](int row, const QList<qint64> &ids) {
        const qint64 playlistId = m_playlistModel.idAt(row);
        if (playlistId >= 0) addTracksToPlaylist(playlistId, ids);
    };

    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddTrackClicked);
    connect(importButton, &QPushButton::clicked, this, &MainWindow::onImportFolderClicked);
    connect(libraryButton, &QPushButton::clicked, this, &MainWindow::showLibrary);
    connect(newPlaylistButton, &QPushButton::clicked, this, &MainWindow::onNewPlaylistClicked);
    connect(m_playlistView, &QListView::clicked, this, &MainWindow::onPlaylistSelected);
    connect(m_trackView, &QTableView::doubleClicked, this, &MainWindow::onTrackDoubleClicked);
    connect(m_trackView, &QTableView::customContextMenuRequested, this, &MainWindow::onTrackContextMenuRequested);
    connect(m_playlistView, &QListView::customContextMenuRequested, this, &MainWindow::onPlaylistContextMenuRequested);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(m_showMore, &QPushButton::clicked, this, [this]() {
        m_libraryShown += kLibraryPage;
        refreshTracks(m_searchEdit->text(), true);
    });
    qApp->installEventFilter(this);
    connect(&m_trackModel, &TrackTableModel::orderChanged, this, [this]() {
        if (m_activePlaylistId < 0) return;
        QVector<qint64> ids;
        for (int row = 0; row < m_trackModel.rowCount(); ++row)
            ids.push_back(m_trackModel.trackAt(row).id);
        m_db.setPlaylistTrackOrder(m_activePlaylistId, ids);
    });

    connect(m_playPauseItem, &DraggableItem::clicked, this, &MainWindow::onPlayPauseClicked);
    connect(miniPrev, &DraggableItem::clicked, this, [this]() { m_queue.previous(); });
    connect(miniNext, &DraggableItem::clicked, this, [this]() { m_queue.skipNext(); });
    connect(m_coverItem, &DraggableItem::clicked, this, &MainWindow::togglePlayer);
    connect(m_labelItem, &DraggableItem::clicked, this, &MainWindow::togglePlayer);
    connect(m_canvas, &SkinCanvas::backgroundClicked, this, &MainWindow::togglePlayer);
    connect(m_canvas, &SkinCanvas::layoutChanged, this, &MainWindow::onSkinLayoutChanged);
    connect(m_canvas, &SkinCanvas::itemMenuRequested, this, &MainWindow::configureItem);
    connect(m_panel->canvas(), &SkinCanvas::itemMenuRequested, this, &MainWindow::configureItem);
    connect(m_canvas, &SkinCanvas::canvasMenuRequested, this, [this]() { configureCanvas(m_canvas); });
    connect(m_panel->canvas(), &SkinCanvas::canvasMenuRequested, this, [this]() { configureCanvas(m_panel->canvas()); });
    connect(m_seekItem, &SeekBarItem::seekRequested, this, &MainWindow::onSeekRequested);
    connect(m_librarySplit, &QSplitter::splitterMoved, this, [this]() {
        if (!m_librarySplit->sizes().isEmpty())
            m_db.setSetting(QStringLiteral("library_split"), QString::number(m_librarySplit->sizes().value(0)));
    });

    connect(m_panel, &PlayerPanel::closeRequested, this, [this]() { setPlayerOpen(false); });
    connect(m_panel, &PlayerPanel::playPauseClicked, this, &MainWindow::onPlayPauseClicked);
    connect(m_panel, &PlayerPanel::previousClicked, this, [this]() { m_queue.previous(); });
    connect(m_panel, &PlayerPanel::nextClicked, this, [this]() { m_queue.skipNext(); });
    connect(m_panel, &PlayerPanel::seekRequested, this, &MainWindow::onSeekRequested);
    connect(m_panel, &PlayerPanel::volumeChanged, this, [this](int percent) {
        m_audio.setVolume(percent / 100.0f);
        m_db.setSetting(QStringLiteral("volume"), QString::number(percent));
    });
    connect(m_panel, &PlayerPanel::shuffleToggled, this, [this](bool on) {
        m_queue.setShuffle(on);
        m_db.setSetting(QStringLiteral("shuffle"), on ? QStringLiteral("1") : QStringLiteral("0"));
    });
    connect(m_panel, &PlayerPanel::repeatClicked, this, &MainWindow::cycleRepeatMode);
    connect(m_panel, &PlayerPanel::queueActivated, this, [this](int row) {
        if (row == m_queue.currentIndex() && m_queue.current()) {
            if (m_audio.isPlaying()) m_audio.pause();
            else m_audio.resume();
            return;
        }
        m_queue.jumpTo(row);
    });
    connect(m_panel, &PlayerPanel::queueRemoveRequested, this, [this](int row) { m_queue.removeAt(row); });
    connect(m_panel, &PlayerPanel::queueReordered, this, [this](int from, int to) { m_queue.moveTrack(from, to); });
    connect(m_panel, &PlayerPanel::tracksDroppedOnQueue, this, [this](int row) {
        QVector<Track> tracks;
        for (qint64 id : m_panel->takePendingDropIds()) {
            if (auto track = m_db.trackById(id)) tracks.push_back(*track);
        }
        m_queue.insertTracks(row, tracks);
    });
    connect(m_panel, &PlayerPanel::skinLayoutChanged, this, &MainWindow::onSkinLayoutChanged);
    connect(m_panel, &PlayerPanel::queueRatioChanged, this, [this](qreal ratio) {
        m_db.setSetting(QStringLiteral("queue_ratio"), QString::number(ratio, 'f', 3));
    });
    connect(logoButton, &QToolButton::clicked, this, &MainWindow::openLogoMenu);

    connect(&m_queue, &PlaybackQueue::currentChanged, this, &MainWindow::onQueueCurrentChanged);
    connect(&m_queue, &PlaybackQueue::queueChanged, this, [this]() {
        m_panel->setQueue(m_queue.tracks(), m_queue.currentIndex());
    });
    connect(&m_audio, &AudioEngine::positionChanged, this, &MainWindow::onAudioPositionChanged);
    connect(&m_audio, &AudioEngine::trackFinished, this, &MainWindow::onAudioTrackFinished);
    connect(&m_audio, &AudioEngine::playbackStateChanged, this, [this](bool playing) {
        m_playPauseItem->setPlaying(playing);
        m_panel->setPlaying(playing);
        m_coverItem->setPlaybackActive(playing);
    });
    connect(&m_fetcher, &CoverFetcher::coverReady, this, &MainWindow::onCoverReady);
    connect(&m_fetcher, &CoverFetcher::lookupStatus, this, [this](const QString &text) {
        m_panel->setCoverStatus(text);
        if (text.isEmpty()) statusBar()->clearMessage();
        else statusBar()->showMessage(text);
    });

    const QStringList eq = m_db.getSetting(QStringLiteral("eq")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (int band = 0; band < eq.size() && band < AudioEngine::kEqBands; ++band)
        m_audio.setEqualizerGain(band, eq.at(band).toFloat());

    const int volume = std::clamp(m_db.getSetting(QStringLiteral("volume"), QStringLiteral("80")).toInt(), 0, 100);
    m_audio.setVolume(volume / 100.0f);
    m_panel->setVolumePercent(volume);

    const bool shuffle = m_db.getSetting(QStringLiteral("shuffle")) == QLatin1String("1");
    m_queue.setShuffle(shuffle);
    m_panel->setShuffle(shuffle);

    int repeat = m_db.getSetting(QStringLiteral("repeat"), QStringLiteral("0")).toInt();
    if (repeat < 0 || repeat > 2) repeat = 0;
    const auto repeatMode = static_cast<PlaybackQueue::RepeatMode>(repeat);
    m_queue.setRepeatMode(repeatMode);
    m_panel->setRepeatMode(repeatMode);

    QColor accent(m_db.getSetting(QStringLiteral("accent"), QStringLiteral("#c9974b")));
    if (!accent.isValid()) accent = QColor(QStringLiteral("#c9974b"));
    applyAccent(accent);

    loadOrCreateDefaultSkin();
    refreshTracks();
    refreshPlaylists();
    adoptFirstTrackCovers();
    if (m_db.getSetting(QStringLiteral("cover_lookup_v2")).isEmpty()) {
        m_db.forgetMissedCovers();
        m_db.setSetting(QStringLiteral("cover_lookup_v2"), QStringLiteral("1"));
    }
    m_fetcher.enqueueMissing();
    connect(&m_library, &LibraryManager::libraryFoldersChanged, this, &MainWindow::onLibraryFoldersChanged);
    m_library.startWatching();
}

void MainWindow::presentTracks(const QVector<Track> &tracks, int libraryTotal, bool keepScroll)
{
    const int scroll = keepScroll && m_trackView ? m_trackView->verticalScrollBar()->value() : 0;
    m_trackModel.setTracks(tracks);
    const bool more = libraryTotal >= 0 && tracks.size() < libraryTotal;
    if (m_showMore) {
        m_showMore->setVisible(more);
        if (more) m_showMore->setText(tr("Показать ещё"));
    }
    if (keepScroll && m_trackView)
        m_trackView->verticalScrollBar()->setValue(scroll);
}

void MainWindow::onLibraryFoldersChanged(int added, const QList<qint64> &removedIds,
                                         const QList<qint64> &renamedIds, const QStringList &renamedPaths)
{
    if (!removedIds.isEmpty()) m_queue.removeIds(removedIds);
    for (int i = 0; i < renamedIds.size() && i < renamedPaths.size(); ++i)
        m_queue.updatePath(renamedIds.at(i), renamedPaths.at(i));

    refreshTracks(m_searchEdit->text(), true);
    refreshPlaylists();
    if (added > 0) {
        adoptFirstTrackCovers();
        m_fetcher.enqueueMissing();
    }

    QStringList parts;
    if (added > 0) parts << tr("добавлено %1").arg(added);
    if (!removedIds.isEmpty()) parts << tr("убрано %1").arg(removedIds.size());
    if (!renamedIds.isEmpty()) parts << tr("переименовано %1").arg(renamedIds.size());
    if (!parts.isEmpty())
        statusBar()->showMessage(tr("Папки на диске: %1").arg(parts.join(QStringLiteral(", "))), 5000);
}

void MainWindow::refreshTracks(const QString &search, bool keepScroll)
{
    if (m_activePlaylistId >= 0) {
        presentTracks(m_db.playlistTracks(m_activePlaylistId), -1, keepScroll);
        return;
    }
    const int total = m_db.countTracks(search);
    presentTracks(m_db.queryTracks(search, m_libraryShown, 0), total, keepScroll);
}

void MainWindow::refreshPlaylists()
{
    m_playlistModel.setPlaylists(m_db.allPlaylists());
}

void MainWindow::showLibrary()
{
    m_activePlaylistId = -1;
    setReorderable(false);
    m_playlistView->clearSelection();
    refreshTracks(m_searchEdit->text());
}

void MainWindow::setReorderable(bool reorderable)
{
    m_trackView->setProperty("reorderable", reorderable);
    m_trackModel.setReorderable(reorderable);
}

QRect MainWindow::playerTarget() const
{
    QWidget *host = m_panel ? m_panel->parentWidget() : centralWidget();
    if (!host) return rect();
    return QRect(QPoint(0, 0), host->size());
}

void MainWindow::togglePlayer()
{
    setPlayerOpen(!m_panelOpen);
}

void MainWindow::setPlayerOpen(bool open)
{
    if (m_panelAnim) {
        auto *old = m_panelAnim;
        m_panelAnim = nullptr;
        old->stop();
    }

    m_panelOpen = open;
    const QRect target = playerTarget();
    if (target.width() < 8 || target.height() < 8) {
        if (m_slideGhost) {
            m_slideGhost->hide();
            m_slideGhost->deleteLater();
            m_slideGhost = nullptr;
        }
        m_panel->setUpdatesEnabled(true);
        m_panel->setGeometry(target);
        m_panel->setVisible(open);
        return;
    }

    if (!open) {
        // The panel is already on screen. Re-rendering it into a bitmap
        // before the slide is what made the close feel like it stalled.
        QWidget *slider = m_slideGhost ? static_cast<QWidget *>(m_slideGhost) : m_panel;
        if (slider == m_panel) m_panel->raise();
        auto *anim = new QPropertyAnimation(slider, "pos", this);
        anim->setDuration(280);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(slider->pos());
        anim->setEndValue(QPoint(target.x(), target.bottom()));
        m_panelAnim = anim;
        connect(anim, &QPropertyAnimation::finished, this, [this, anim]() {
            if (m_panelAnim != anim) return;
            m_panelAnim = nullptr;
            if (m_slideGhost) {
                m_slideGhost->hide();
                m_slideGhost->deleteLater();
                m_slideGhost = nullptr;
            }
            m_panel->hide();
            const QRect now = playerTarget();
            m_panel->move(now.x(), now.bottom() + 8);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }

    if (m_slideGhost) {
        m_slideGhost->hide();
        m_slideGhost->deleteLater();
        m_slideGhost = nullptr;
    }

    // Slide a single bitmap. Moving the live panel relayouts the scene on
    // every frame, which is what made the open look like it stuttered.
    m_capturing = true;
    const QPixmap frame = renderPanelFrame(m_panel, target, true);
    m_capturing = false;
    m_panel->hide();
    auto *ghost = new PixmapSlide(frame, m_panel->parentWidget());
    ghost->setGeometry(QRect(QPoint(target.x(), target.bottom()), target.size()));
    ghost->show();
    ghost->raise();
    m_slideGhost = ghost;

    auto *anim = new QPropertyAnimation(ghost, "pos", this);
    anim->setDuration(280);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(ghost->pos());
    anim->setEndValue(target.topLeft());
    m_panelAnim = anim;
    connect(anim, &QPropertyAnimation::finished, this, [this, anim]() {
        if (m_panelAnim != anim) return;
        m_panelAnim = nullptr;
        if (m_slideGhost) {
            m_slideGhost->hide();
            m_slideGhost->deleteLater();
            m_slideGhost = nullptr;
        }
        m_panel->setGeometry(playerTarget());
        m_panel->show();
        m_panel->raise();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!m_splitRestored && m_librarySplit && m_librarySplit->width() > 420) {
        m_splitRestored = true;
        const int total = m_librarySplit->width();
        const int saved = m_db.getSetting(QStringLiteral("library_split"), QStringLiteral("240")).toInt();
        if (saved >= 160 && saved <= total - 280)
            m_librarySplit->setSizes({saved, total - saved});
        if (m_panel)
            m_panel->setQueueRatio(m_db.getSetting(QStringLiteral("queue_ratio"), QStringLiteral("0.30")).toDouble());
    }
    if (!m_panel || !m_panelOpen || m_capturing) return;
    if (m_panelAnim && m_panelAnim->state() == QAbstractAnimation::Running) return;
    m_panel->setGeometry(playerTarget());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    onSkinLayoutChanged();
    if (m_librarySplit && !m_librarySplit->sizes().isEmpty())
        m_db.setSetting(QStringLiteral("library_split"), QString::number(m_librarySplit->sizes().value(0)));
    if (m_panel)
        m_db.setSetting(QStringLiteral("queue_ratio"), QString::number(m_panel->queueRatio(), 'f', 3));
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_librarySplit && watched == m_librarySplit->handle(1)
        && event->type() == QEvent::MouseButtonDblClick) {
        const int total = std::max(m_librarySplit->width(), 480);
        const int left = std::min(240, std::max(160, total - 280));
        m_librarySplit->setSizes({left, std::max(200, total - left)});
        m_db.setSetting(QStringLiteral("library_split"), QString::number(m_librarySplit->sizes().value(0)));
        return true;
    }
    if (event->type() == QEvent::KeyPress && handlePlayerKey(static_cast<QKeyEvent *>(event)))
        return true;
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::handlePlayerKey(QKeyEvent *event)
{
    if (!event || QApplication::activeModalWidget()) return false;
    if (QApplication::activeWindow() != this) return false;

    QWidget *focus = QApplication::focusWidget();
    const bool typing = qobject_cast<QLineEdit *>(focus)
        || qobject_cast<QTextEdit *>(focus)
        || qobject_cast<QPlainTextEdit *>(focus)
        || qobject_cast<QAbstractSpinBox *>(focus);
    const bool onButton = qobject_cast<QAbstractButton *>(focus);
    const int key = event->key();
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);

    auto volumeStep = [this](int delta) {
        const int percent = std::clamp(int(std::lround(m_audio.volume() * 100.0)) + delta, 0, 100);
        m_audio.setVolume(percent / 100.0f);
        m_panel->setVolumePercent(percent);
        m_db.setSetting(QStringLiteral("volume"), QString::number(percent));
    };

    if (key == Qt::Key_MediaPlay || key == Qt::Key_MediaTogglePlayPause || key == Qt::Key_MediaPause) {
        if (!event->isAutoRepeat()) onPlayPauseClicked();
        return true;
    }
    if (key == Qt::Key_MediaNext) {
        if (!event->isAutoRepeat()) m_queue.skipNext();
        return true;
    }
    if (key == Qt::Key_MediaPrevious) {
        if (!event->isAutoRepeat()) m_queue.previous();
        return true;
    }
    if (key == Qt::Key_VolumeUp) {
        volumeStep(5);
        return true;
    }
    if (key == Qt::Key_VolumeDown) {
        volumeStep(-5);
        return true;
    }
    if (typing || onButton) return false;

    if (key == Qt::Key_Space) {
        if (!event->isAutoRepeat()) onPlayPauseClicked();
        return true;
    }
    if (ctrl && key == Qt::Key_Right) {
        if (!event->isAutoRepeat()) m_queue.skipNext();
        return true;
    }
    if (ctrl && key == Qt::Key_Left) {
        if (!event->isAutoRepeat()) m_queue.previous();
        return true;
    }
    if (ctrl && key == Qt::Key_Up) {
        volumeStep(5);
        return true;
    }
    if (ctrl && key == Qt::Key_Down) {
        volumeStep(-5);
        return true;
    }
    return false;
}

void MainWindow::applyAccent(const QColor &color)
{
    m_accent = color;
    m_playPauseItem->setAccent(color);
    m_seekItem->setAccent(color);
    m_panel->setAccent(color);
}

void MainWindow::openLogoMenu()
{
    QMenu menu(this);
    auto *editButton = new QPushButton(tr("Редактировать плеер"), &menu);
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setStyleSheet(QStringLiteral("text-align: left; padding: 8px 16px;"));
    auto *edit = new QWidgetAction(&menu);
    edit->setDefaultWidget(editButton);
    menu.addAction(edit);
    connect(editButton, &QPushButton::clicked, edit, &QAction::trigger);

    QAction *color = menu.addAction(tr("Цвет кнопок и полос…"));
    menu.addSeparator();
    QAction *eq = menu.addAction(tr("Эквалайзер…"));

    QAction *chosen = menu.exec(QCursor::pos());
    if (chosen == edit) openPlayerEditor();
    else if (chosen == color) {
        const QColor picked = QColorDialog::getColor(m_accent, this, tr("Цвет плеера"));
        if (!picked.isValid()) return;
        applyAccent(picked);
        m_db.setSetting(QStringLiteral("accent"), picked.name(QColor::HexRgb));
    } else if (chosen == eq) {
        openEqualizer();
    }
}

void MainWindow::openPlayerEditor()
{
    SkinCanvas *tab = m_panel->canvas();
    auto *split = qobject_cast<QSplitter *>(tab->parentWidget());
    const int splitIndex = split ? split->indexOf(tab) : 0;
    auto *miniLayout = qobject_cast<QHBoxLayout *>(m_canvas->parentWidget() ? m_canvas->parentWidget()->layout() : nullptr);
    const int miniIndex = miniLayout ? miniLayout->indexOf(m_canvas) : 0;

    // Measure the canvases where the user actually sees them. A stretched
    // dialog would turn the same fractions into a different layout.
    if (!m_panel->isVisible()) {
        m_panel->setGeometry(playerTarget());
        if (m_panel->layout()) m_panel->layout()->activate();
    }
    QSize tabSize = tab->size();
    if (tabSize.width() < 80 || tabSize.height() < 80) {
        const QRect target = playerTarget();
        const int queue = int(target.width() * m_panel->queueRatio());
        tabSize = QSize(std::max(240, target.width() - 88 - queue), std::max(240, target.height() - 72));
    }
    QSize miniSize = m_canvas->size();
    if (miniSize.width() < 80) miniSize.setWidth(std::max(240, width() - 220));
    if (miniSize.height() < 40) miniSize.setHeight(72);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Редактировать плеер"));
    auto *root = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(tr(
        "Холст того же размера, что и в плеере. Потяните угол кнопки или обложки — размер меняется пропорционально. "
        "Правый клик по кнопке ставит текстуру PNG, по обложке — форму и рамку, по пустому месту — фон."));
    hint->setWordWrap(true);
    hint->setMaximumWidth(std::max(tabSize.width(), miniSize.width()));
    root->addWidget(hint);
    root->addWidget(new QLabel(tr("Вкладка")));
    tab->setParent(nullptr);
    tab->setFixedSize(tabSize);
    root->addWidget(tab, 0, Qt::AlignLeft);
    root->addWidget(new QLabel(tr("Нижняя панель")));
    m_canvas->setParent(nullptr);
    m_canvas->setFixedSize(miniSize);
    root->addWidget(m_canvas, 0, Qt::AlignLeft);
    auto *done = new QPushButton(tr("Готово"));
    connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
    root->addWidget(done, 0, Qt::AlignRight);
    dialog.adjustSize();

    m_panel->setEditMode(true);
    m_canvas->setEditMode(true);
    dialog.exec();

    tab->setMinimumSize(0, 0);
    tab->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMinimumSize(0, 0);
    m_canvas->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    m_canvas->setFixedHeight(72);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    if (split) {
        split->insertWidget(splitIndex, tab);
        split->setStretchFactor(splitIndex, 1);
    }
    if (miniLayout) miniLayout->insertWidget(miniIndex, m_canvas, 1);
    m_panel->setEditMode(false);
    m_canvas->setEditMode(false);
    onSkinLayoutChanged();
}

void MainWindow::configureItem(DraggableItem *item)
{
    if (!item) return;
    auto *cover = dynamic_cast<CoverArtItem *>(item);
    const bool button = dynamic_cast<TextButtonItem *>(item) || dynamic_cast<PlayPauseButtonItem *>(item);

    QMenu menu(this);
    auto *label = dynamic_cast<TrackLabelItem *>(item);
    QAction *font = nullptr;
    if (label)
        font = menu.addAction(tr("Размер шрифта…"));
    QAction *recolor = menu.addAction(tr("Цвет элемента…"));
    QAction *reset = menu.addAction(tr("Сбросить цвет"));
    reset->setEnabled(item->hasSkinColor());
    QAction *icon = nullptr;
    QAction *clearIcon = nullptr;
    QAction *rounded = nullptr;
    QAction *square = nullptr;
    QAction *vinyl = nullptr;
    QAction *frame = nullptr;
    QAction *clearFrame = nullptr;
    if (button) {
        icon = menu.addAction(tr("Текстура PNG…"));
        clearIcon = menu.addAction(tr("Сбросить иконку"));
        clearIcon->setEnabled(item->hasCustomIcon());
    }
    if (cover) {
        menu.addSeparator();
        rounded = menu.addAction(tr("Скруглённый квадрат"));
        square = menu.addAction(tr("Квадрат"));
        vinyl = menu.addAction(tr("CD"));
        for (QAction *action : {rounded, square, vinyl}) action->setCheckable(true);
        rounded->setChecked(cover->shapeId() == QLatin1String("rounded"));
        square->setChecked(cover->shapeId() == QLatin1String("square"));
        vinyl->setChecked(cover->shapeId() == QLatin1String("vinyl"));
        frame = menu.addAction(tr("Рамка PNG…"));
        clearFrame = menu.addAction(tr("Убрать рамку"));
        clearFrame->setEnabled(cover->hasFrame());
    }
    QAction *hide = nullptr;
    if (canHideControl(item->elementId())) {
        menu.addSeparator();
        hide = menu.addAction(tr("Убрать"));
    }

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;
    if (chosen == font) {
        qreal scale = label->fontScale();
        if (!chooseFontScale(this, scale, &scale)) return;
        label->setFontScale(scale);
    } else if (chosen == recolor) {
        const QColor picked = QColorDialog::getColor(item->hasSkinColor() ? item->skinColor() : m_accent,
                                                     this, tr("Цвет элемента"));
        if (!picked.isValid()) return;
        item->setSkinColor(picked);
    } else if (chosen == reset) {
        item->clearSkinColor();
    } else if (chosen == icon) {
        const QString picked = QFileDialog::getOpenFileName(
            this, tr("Текстура кнопки"), {}, tr("PNG (*.png)"));
        if (picked.isEmpty()) return;
        QImage image;
        if (!loadStaticPng(this, picked, &image)) return;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/icons");
        QDir().mkpath(dir);
        const QString dest = dir + QLatin1Char('/') + item->elementId() + QStringLiteral(".png");
        image.save(dest, "PNG");
        SkinCanvas *canvas = item->scene() == m_canvas->scene() ? m_canvas : m_panel->canvas();
        const auto before = canvas->positions();
        item->setCustomIconPath(dest);
        keepElementsStill(this, canvas, before);
    } else if (chosen == clearIcon) {
        item->clearCustomIcon();
    } else if (chosen == rounded) {
        cover->setShapeId(QStringLiteral("rounded"));
    } else if (chosen == square) {
        cover->setShapeId(QStringLiteral("square"));
    } else if (chosen == vinyl) {
        cover->setShapeId(QStringLiteral("vinyl"));
    } else if (chosen == frame) {
        const QString picked = QFileDialog::getOpenFileName(
            this, tr("Рамка обложки"), {}, tr("PNG (*.png)"));
        if (picked.isEmpty()) return;
        QImage image;
        if (!loadStaticPng(this, picked, &image)) return;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/icons");
        QDir().mkpath(dir);
        const QString dest = dir + QLatin1Char('/') + item->elementId() + QStringLiteral("_frame.png");
        image.save(dest, "PNG");
        SkinCanvas *canvas = item->scene() == m_canvas->scene() ? m_canvas : m_panel->canvas();
        const auto before = canvas->positions();
        cover->setFramePath(dest);
        keepElementsStill(this, canvas, before);
    } else if (chosen == clearFrame) {
        cover->clearFrame();
    } else if (chosen == hide) {
        item->setVisible(false);
    } else {
        return;
    }
    if (item->scene() == m_panel->canvas()->scene()) mirrorTabLook();
    onSkinLayoutChanged();
}

void MainWindow::configureCanvas(SkinCanvas *canvas)
{
    if (!canvas) return;
    QMenu menu(this);
    QAction *background = menu.addAction(tr("Фон из PNG…"));
    QAction *clear = menu.addAction(tr("Убрать фон"));
    clear->setEnabled(canvas->hasBackground());
    QAction *skips = nullptr;
    if (canvas == m_canvas) {
        menu.addSeparator();
        skips = menu.addAction(tr("Кнопки перемотки"));
        skips->setCheckable(true);
        bool shown = false;
        for (DraggableItem *item : canvas->items()) {
            if (item->elementId() == QLatin1String("mini_prev") || item->elementId() == QLatin1String("mini_next"))
                shown = shown || item->isVisible();
        }
        skips->setChecked(shown);
    }
    for (DraggableItem *item : canvas->items()) {
        if (item->isVisible() || !canHideControl(item->elementId())) continue;
        menu.addSeparator();
        break;
    }
    for (DraggableItem *item : canvas->items()) {
        if (item->isVisible() || !canHideControl(item->elementId())) continue;
        QAction *restore = menu.addAction(tr("Вернуть «%1»").arg(controlTitle(item->elementId())));
        restore->setData(item->elementId());
    }
    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;
    const auto before = canvas->positions();
    if (chosen == background) {
        const QString picked = QFileDialog::getOpenFileName(
            this, tr("Фон плеера"), {}, tr("PNG (*.png)"));
        if (picked.isEmpty()) return;
        QImage image;
        if (!loadStaticPng(this, picked, &image)) return;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/icons");
        QDir().mkpath(dir);
        const QString dest = dir + QLatin1Char('/')
            + (canvas == m_canvas ? QStringLiteral("mini_background.png") : QStringLiteral("player_background.png"));
        image.save(dest, "PNG");
        canvas->setBackgroundPath(dest);
    } else if (chosen == clear) {
        canvas->clearBackground();
    } else if (chosen == skips) {
        for (DraggableItem *item : canvas->items()) {
            if (item->elementId() == QLatin1String("mini_prev") || item->elementId() == QLatin1String("mini_next"))
                item->setVisible(skips->isChecked());
        }
    } else if (!chosen->data().toString().isEmpty()) {
        const QString id = chosen->data().toString();
        for (DraggableItem *item : canvas->items()) {
            if (item->elementId() == id) item->setVisible(true);
        }
    } else {
        return;
    }
    keepElementsStill(this, canvas, before);
    if (canvas == m_panel->canvas()) mirrorTabLook();
    onSkinLayoutChanged();
}

void MainWindow::openEqualizer()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Эквалайзер"));
    dialog.resize(560, 280);
    auto *columns = new QHBoxLayout;
    QVector<QSlider *> sliders;
    for (int band = 0; band < AudioEngine::kEqBands; ++band) {
        auto *slider = new QSlider(Qt::Vertical);
        slider->setRange(-12, 12);
        slider->setValue(int(std::lround(m_audio.equalizerGain(band))));
        slider->setTickPosition(QSlider::TicksBothSides);
        slider->setTickInterval(6);
        sliders.push_back(slider);
        connect(slider, &QSlider::valueChanged, this, [this, band](int value) {
            m_audio.setEqualizerGain(band, float(value));
            QStringList parts;
            for (int i = 0; i < AudioEngine::kEqBands; ++i)
                parts << QString::number(m_audio.equalizerGain(i), 'f', 0);
            m_db.setSetting(QStringLiteral("eq"), parts.join(QLatin1Char(',')));
        });
        auto *col = new QVBoxLayout;
        col->addWidget(slider, 1, Qt::AlignHCenter);
        auto *label = new QLabel(m_audio.equalizerBandLabel(band));
        label->setAlignment(Qt::AlignHCenter);
        col->addWidget(label);
        columns->addLayout(col);
    }
    auto *reset = new QPushButton(tr("Сбросить"));
    connect(reset, &QPushButton::clicked, &dialog, [sliders]() {
        for (QSlider *slider : sliders) slider->setValue(0);
    });
    auto *root = new QVBoxLayout(&dialog);
    root->addLayout(columns);
    root->addWidget(reset, 0, Qt::AlignRight);
    dialog.exec();
}

void MainWindow::cycleRepeatMode()
{
    PlaybackQueue::RepeatMode next = PlaybackQueue::RepeatMode::Off;
    switch (m_queue.repeatMode()) {
    case PlaybackQueue::RepeatMode::Off: next = PlaybackQueue::RepeatMode::RepeatAll; break;
    case PlaybackQueue::RepeatMode::RepeatAll: next = PlaybackQueue::RepeatMode::RepeatOne; break;
    case PlaybackQueue::RepeatMode::RepeatOne: next = PlaybackQueue::RepeatMode::Off; break;
    }
    m_queue.setRepeatMode(next);
    m_panel->setRepeatMode(next);
    m_db.setSetting(QStringLiteral("repeat"), QString::number(int(next)));
}

QVector<Track> MainWindow::selectedTracks() const
{
    QList<int> rows;
    if (m_trackView->selectionModel()) {
        for (const QModelIndex &index : m_trackView->selectionModel()->selectedRows())
            rows.push_back(index.row());
    }
    std::sort(rows.begin(), rows.end());
    QVector<Track> tracks;
    for (int row : rows) {
        if (row >= 0 && row < m_trackModel.rowCount())
            tracks.push_back(m_trackModel.trackAt(row));
    }
    return tracks;
}

void MainWindow::addTracksToPlaylist(qint64 playlistId, const QList<qint64> &ids)
{
    const int before = m_db.playlistTrackCount(playlistId);
    int added = 0;
    bool full = before >= Database::kPlaylistTrackLimit;
    for (qint64 id : ids) {
        if (id < 0) continue;
        if (m_db.addTrackToPlaylist(playlistId, id)) {
            ++added;
            full = false;
        } else if (m_db.playlistTrackCount(playlistId) >= Database::kPlaylistTrackLimit) {
            full = true;
            break;
        }
    }
    if (m_activePlaylistId == playlistId)
        presentTracks(m_db.playlistTracks(playlistId), -1, false);
    if (added == 0 && full)
        statusBar()->showMessage(tr("В плейлисте уже %1 треков").arg(Database::kPlaylistTrackLimit), 3000);
    else if (full)
        statusBar()->showMessage(tr("Добавлено %1. Дальше лимит — %2 треков")
                                     .arg(added)
                                     .arg(Database::kPlaylistTrackLimit), 3000);
    else if (added == 0)
        statusBar()->showMessage(tr("Эти треки уже есть в плейлисте"), 2500);
    else
        statusBar()->showMessage(tr("В плейлист добавлено: %1").arg(added), 2500);
    adoptFirstTrackCovers();
}

void MainWindow::adoptFirstTrackCovers()
{
    bool changed = false;
    for (const Playlist &playlist : m_db.allPlaylists()) {
        if (!playlist.coverPath.isEmpty()) continue;
        const QVector<Track> tracks = m_db.playlistTracks(playlist.id);
        if (tracks.isEmpty() || tracks.first().coverPath.isEmpty()) continue;
        m_db.setPlaylistCover(playlist.id, tracks.first().coverPath);
        changed = true;
    }
    if (changed) refreshPlaylists();
}

void MainWindow::editTrackInfo(const Track &track)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Сведения о треке"));
    auto *title = new QLineEdit(track.title);
    auto *artist = new QLineEdit(track.artist);
    auto *album = new QLineEdit(track.album);
    auto *genre = new QLineEdit(track.genre);
    auto *form = new QFormLayout;
    form->addRow(tr("Название"), title);
    form->addRow(tr("Исполнитель"), artist);
    form->addRow(tr("Альбом"), album);
    form->addRow(tr("Жанр"), genre);
    auto *file = new QLabel(QFileInfo(track.path).fileName());
    file->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *root = new QVBoxLayout(&dialog);
    root->addWidget(file);
    root->addLayout(form);
    root->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    Track updated = track;
    updated.title = title->text().trimmed();
    updated.artist = artist->text().trimmed();
    updated.album = album->text().trimmed();
    updated.genre = genre->text().trimmed();
    if (updated.title.isEmpty()) {
        QMessageBox::warning(this, tr("Сведения о треке"), tr("Название не может быть пустым."));
        return;
    }
    if (!m_db.updateTrackInfo(updated.id, updated.title, updated.artist, updated.album, updated.genre)) {
        QMessageBox::warning(this, tr("Сведения о треке"), tr("Не удалось сохранить сведения."));
        return;
    }
    const bool written = m_library.writeTrackTags(updated);
    m_queue.updateDetails(updated);
    if (const auto playing = m_queue.current(); playing && playing->id == updated.id) {
        m_labelItem->setText(playing->title, playing->artist);
        m_panel->setTrack(playing);
    }
    refreshTracks(m_searchEdit->text());
    statusBar()->showMessage(written
        ? tr("Сведения обновлены")
        : tr("Сведения сохранены в библиотеке. Записать их в файл не удалось."), 4000);
}

void MainWindow::deleteTracksFromDisk(const QVector<Track> &tracks)
{
    if (tracks.isEmpty()) return;
    const QString name = tracks.first().title.isEmpty()
        ? QFileInfo(tracks.first().path).fileName() : tracks.first().title;
    const QString question = tracks.size() == 1
        ? tr("Удалить «%1» с диска?\nФайл будет стёрт безвозвратно и пропадёт из библиотеки и плейлистов.").arg(name)
        : tr("Удалить с диска выбранные треки (%1)?\nФайлы будут стёрты безвозвратно и пропадут из библиотеки и плейлистов.").arg(tracks.size());
    if (QMessageBox::question(this, tr("Удалить с диска"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QList<qint64> ids;
    for (const Track &track : tracks) ids.push_back(track.id);
    m_queue.removeIds(ids);

    int removed = 0;
    QStringList failed;
    for (const Track &track : tracks) {
        bool gone = track.path.isEmpty() || !QFileInfo::exists(track.path);
        if (!gone) {
            QFile file(track.path);
            gone = file.remove();
            if (!gone) {
                file.setPermissions(file.permissions() | QFile::WriteUser);
                gone = file.remove();
            }
        }
        if (!gone) {
            failed << (track.title.isEmpty() ? track.path : track.title);
            continue;
        }
        const bool dropCover = !track.coverPath.isEmpty() && !m_db.coverStillUsed(track.coverPath, track.id);
        m_db.deleteTrack(track.id);
        if (dropCover) QFile::remove(track.coverPath);
        ++removed;
    }

    refreshTracks(m_searchEdit->text());
    refreshPlaylists();

    if (!failed.isEmpty()) {
        QMessageBox::warning(this, tr("Удалить с диска"),
                             tr("Не удалось стереть %1. Файл, возможно, занят другой программой.\n%2")
                                 .arg(failed.size())
                                 .arg(failed.join(QLatin1Char('\n'))));
    }
    statusBar()->showMessage(tr("Удалено с диска: %1").arg(removed), 4000);
}

void MainWindow::addTracksFromLibrary(qint64 playlistId)
{
    const int room = Database::kPlaylistTrackLimit - m_db.playlistTrackCount(playlistId);
    if (room <= 0) {
        QMessageBox::information(this, tr("Плейлист"),
                                 tr("В плейлисте уже %1 треков.").arg(Database::kPlaylistTrackLimit));
        return;
    }
    const QVector<Track> library = m_db.queryTracks({}, -1, 0);
    if (library.isEmpty()) {
        QMessageBox::information(this, tr("Плейлист"), tr("В библиотеке пока нет треков."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Добавить треки"));
    dialog.resize(460, 560);
    auto *list = new QListWidget;
    for (const Track &track : library) {
        const QString title = track.artist.isEmpty() ? track.title : track.title + QStringLiteral(" — ") + track.artist;
        auto *item = new QListWidgetItem(title);
        item->setData(Qt::UserRole, track.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        list->addItem(item);
    }
    auto *hint = new QLabel(tr("Отметьте треки. Свободно мест: %1 из %2.")
                                .arg(room)
                                .arg(Database::kPlaylistTrackLimit));
    hint->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(hint);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    QList<qint64> ids;
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem *item = list->item(row);
        if (item->checkState() != Qt::Checked) continue;
        ids.push_back(item->data(Qt::UserRole).toLongLong());
        if (ids.size() >= room) break;
    }
    if (!ids.isEmpty()) addTracksToPlaylist(playlistId, ids);
}

void MainWindow::onAddTrackClicked()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Добавить треки"), {}, tr("Аудио (*.wav *.flac *.fla *.mp3)"));
    for (const QString &path : paths) m_library.addTrack(path);
    refreshTracks(m_searchEdit->text());
    m_fetcher.enqueueMissing();
}

void MainWindow::onImportFolderClicked()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Импорт папки"));
    if (folder.isEmpty()) return;
    const QVector<Track> imported = m_library.importFolder(folder, false, true);
    refreshTracks(m_searchEdit->text());
    refreshPlaylists();
    adoptFirstTrackCovers();
    m_fetcher.enqueueMissing();
    if (imported.isEmpty()) {
        statusBar()->showMessage(tr("В папке нет треков wav, flac или mp3"), 4000);
        return;
    }
    const QString name = QFileInfo(folder).fileName();
    qint64 playlistId = -1;
    for (const Playlist &playlist : m_db.allPlaylists()) {
        if (playlist.name == name) playlistId = playlist.id;
    }
    statusBar()->showMessage(tr("В библиотеку добавлено: %1. Плейлист «%2»").arg(imported.size()).arg(name), 4000);
    for (int row = 0; row < m_playlistModel.rowCount(); ++row) {
        if (m_playlistModel.idAt(row) != playlistId) continue;
        const QModelIndex index = m_playlistModel.index(row);
        m_playlistView->setCurrentIndex(index);
        onPlaylistSelected(index);
        break;
    }
}

void MainWindow::onNewPlaylistClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Новый плейлист"), tr("Название:"),
                                                QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const qint64 id = m_db.createPlaylist(name.trimmed());
    refreshPlaylists();
    for (int row = 0; row < m_playlistModel.rowCount(); ++row) {
        if (m_playlistModel.idAt(row) != id) continue;
        const QModelIndex index = m_playlistModel.index(row);
        m_playlistView->setCurrentIndex(index);
        onPlaylistSelected(index);
        break;
    }
}

void MainWindow::onPlaylistSelected(const QModelIndex &index)
{
    m_activePlaylistId = m_playlistModel.idAt(index.row());
    if (m_activePlaylistId < 0) return;
    m_searchEdit->blockSignals(true);
    m_searchEdit->clear();
    m_searchEdit->blockSignals(false);
    setReorderable(true);
    presentTracks(m_db.playlistTracks(m_activePlaylistId), -1, false);
}

void MainWindow::onTrackDoubleClicked(const QModelIndex &index)
{
    const Track chosen = m_trackModel.trackAt(index.row());
    if (const auto playing = m_queue.current(); playing && playing->id == chosen.id) {
        if (m_audio.isPlaying()) m_audio.pause();
        else m_audio.resume();
        return;
    }
    QVector<Track> context;
    int start = index.row();
    if (m_activePlaylistId >= 0) {
        for (int row = 0; row < m_trackModel.rowCount(); ++row)
            context.push_back(m_trackModel.trackAt(row));
    } else {
        context = m_db.queryTracks(m_searchEdit->text(), -1, 0);
        start = 0;
        for (int i = 0; i < context.size(); ++i) {
            if (context.at(i).id == chosen.id) {
                start = i;
                break;
            }
        }
    }
    m_queue.setTracks(context, start);
}

void MainWindow::onSearchChanged(const QString &text)
{
    m_activePlaylistId = -1;
    m_libraryShown = kLibraryPage;
    setReorderable(false);
    m_playlistView->clearSelection();
    refreshTracks(text);
}

void MainWindow::onPlayPauseClicked()
{
    if (!m_queue.current()) {
        if (!m_queue.tracks().isEmpty()) m_queue.jumpTo(0);
        return;
    }
    if (m_audio.isPlaying()) m_audio.pause();
    else m_audio.resume();
}

void MainWindow::onSeekRequested(qreal fraction01)
{
    m_audio.seekToMs(static_cast<qint64>(fraction01 * m_audio.durationMs()));
}

void MainWindow::onQueueCurrentChanged(std::optional<Track> track)
{
    m_panel->setQueue(m_queue.tracks(), m_queue.currentIndex());
    if (!track) {
        m_audio.stop();
        m_playPauseItem->setPlaying(false);
        m_panel->setPlaying(false);
        m_coverItem->setPlaybackActive(false);
        m_panel->setTrack(std::nullopt);
        m_labelItem->setText(tr("Ничего не играет"), tr("Нажмите, чтобы открыть плеер"));
        m_coverItem->setPixmap({});
        m_seekItem->setDurationMs(0);
        m_seekItem->setClock(QStringLiteral("--:--"), QStringLiteral("--:--"));
        m_seekItem->setProgress(0);
        return;
    }

    m_audio.loadAndPlay(track->path);
    m_playPauseItem->setPlaying(true);
    m_panel->setPlaying(true);
    m_coverItem->setPlaybackActive(true);
    m_labelItem->setText(track->title, track->artist);
    m_panel->setTrack(track);
    const QPixmap cover = track->coverPath.isEmpty() ? QPixmap{} : QPixmap(track->coverPath);
    m_coverItem->setPixmap(cover);
}

void MainWindow::onAudioPositionChanged(qint64 ms)
{
    const qint64 duration = m_audio.durationMs();
    m_seekItem->setDurationMs(duration);
    m_seekItem->setClock(formatClock(ms), formatClock(duration));
    m_seekItem->setProgress(duration > 0 ? double(ms) / double(duration) : 0.0);
    m_panel->setPosition(ms, duration);
}

void MainWindow::onAudioTrackFinished()
{
    if (!m_queue.advance()) {
        m_playPauseItem->setPlaying(false);
        m_panel->setPlaying(false);
        m_coverItem->setPlaybackActive(false);
    }
}

void MainWindow::onTrackContextMenuRequested(const QPoint &pos)
{
    const QModelIndex index = m_trackView->indexAt(pos);
    if (!index.isValid()) return;
    if (!m_trackView->selectionModel()->isSelected(index)) {
        m_trackView->selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    const QVector<Track> tracks = selectedTracks();
    if (tracks.isEmpty()) return;
    const Track &primary = tracks.first();

    QMenu menu(this);
    QAction *playNext = menu.addAction(tr("Играть следующим"));
    QAction *enqueue = menu.addAction(tr("Добавить в очередь"));
    menu.addSeparator();

    QMenu *playlistMenu = menu.addMenu(tr("Добавить в плейлист"));
    const QVector<Playlist> playlists = m_db.allPlaylists();
    if (playlists.isEmpty()) {
        QAction *none = playlistMenu->addAction(tr("Сначала создайте плейлист"));
        none->setEnabled(false);
    } else {
        for (const Playlist &playlist : playlists) {
            QAction *action = playlistMenu->addAction(playlist.name);
            connect(action, &QAction::triggered, this, [this, playlist, tracks]() {
                QList<qint64> ids;
                for (const Track &track : tracks) ids.push_back(track.id);
                addTracksToPlaylist(playlist.id, ids);
            });
        }
    }
    QAction *moveUp = nullptr;
    QAction *moveDown = nullptr;
    QAction *removeFromPlaylist = nullptr;
    if (m_activePlaylistId >= 0) {
        if (tracks.size() == 1) {
            moveUp = menu.addAction(tr("Выше"));
            moveDown = menu.addAction(tr("Ниже"));
            moveUp->setEnabled(index.row() > 0);
            moveDown->setEnabled(index.row() + 1 < m_trackModel.rowCount());
        }
        removeFromPlaylist = menu.addAction(tr("Удалить из плейлиста"));
    }

    menu.addSeparator();
    QAction *editInfo = menu.addAction(tr("Изменить сведения…"));
    editInfo->setEnabled(tracks.size() == 1);
    QAction *setCover = menu.addAction(tr("Задать обложку…"));
    QAction *findCover = menu.addAction(tr("Найти обложку в сети"));
    menu.addSeparator();
    QAction *erase = menu.addAction(tr("Удалить с диска…"));

    QAction *chosen = menu.exec(m_trackView->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == playNext) {
        for (int i = tracks.size() - 1; i >= 0; --i) m_queue.enqueueNext(tracks[i]);
    } else if (chosen == enqueue) {
        for (const Track &track : tracks) m_queue.append(track);
    } else if (chosen == moveUp) {
        m_trackModel.reorderRows(index.row(), index.row() - 1);
    } else if (chosen == moveDown) {
        m_trackModel.reorderRows(index.row(), index.row() + 1);
    } else if (chosen == removeFromPlaylist) {
        for (const Track &track : tracks)
            m_db.removeTrackFromPlaylist(m_activePlaylistId, track.id);
        presentTracks(m_db.playlistTracks(m_activePlaylistId), -1, true);
    } else if (chosen == editInfo) {
        editTrackInfo(primary);
    } else if (chosen == setCover) {
        onSetTrackCover(primary.id);
    } else if (chosen == findCover) {
        if (!primary.coverPath.isEmpty()) {
            const auto answer = QMessageBox::question(
                this, tr("Обложка"), tr("Заменить текущую обложку найденной в сети?"));
            if (answer != QMessageBox::Yes) return;
        }
        m_fetcher.forceEnqueue(primary);
    } else if (chosen == erase) {
        deleteTracksFromDisk(tracks);
    }
}

void MainWindow::onPlaylistContextMenuRequested(const QPoint &pos)
{
    const QModelIndex index = m_playlistView->indexAt(pos);
    if (!index.isValid()) return;
    const qint64 playlistId = m_playlistModel.idAt(index.row());
    const QString name = index.data(Qt::DisplayRole).toString();

    QMenu menu(this);
    QAction *addTracks = menu.addAction(tr("Добавить треки…"));
    QAction *rename = menu.addAction(tr("Переименовать…"));
    QAction *setCover = menu.addAction(tr("Задать обложку плейлиста…"));
    QAction *remove = menu.addAction(tr("Удалить плейлист"));
    QAction *chosen = menu.exec(m_playlistView->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == addTracks) {
        addTracksFromLibrary(playlistId);
        if (m_activePlaylistId != playlistId) {
            for (int row = 0; row < m_playlistModel.rowCount(); ++row) {
                if (m_playlistModel.idAt(row) != playlistId) continue;
                const QModelIndex next = m_playlistModel.index(row);
                m_playlistView->setCurrentIndex(next);
                onPlaylistSelected(next);
                break;
            }
        }
    } else if (chosen == rename) {
        bool ok = false;
        const QString next = QInputDialog::getText(this, tr("Переименовать"), tr("Название:"),
                                                    QLineEdit::Normal, name, &ok);
        if (!ok || next.trimmed().isEmpty()) return;
        m_db.renamePlaylist(playlistId, next.trimmed());
        refreshPlaylists();
    } else if (chosen == setCover) {
        onSetPlaylistCover(playlistId);
    } else if (chosen == remove) {
        const auto answer = QMessageBox::question(
            this, tr("Удалить плейлист"),
            tr("Удалить «%1»? Треки останутся в библиотеке.").arg(name));
        if (answer != QMessageBox::Yes) return;
        m_db.deletePlaylist(playlistId);
        if (m_activePlaylistId == playlistId) showLibrary();
        refreshPlaylists();
    }
}

void MainWindow::onSetTrackCover(qint64 trackId)
{
    const QString imagePath = QFileDialog::getOpenFileName(
        this, tr("Выбрать обложку"), {}, tr("Изображения (*.jpg *.jpeg *.png *.bmp)"));
    if (imagePath.isEmpty()) return;

    const QString localPath = m_covers.storeTrackCoverFromFile(trackId, imagePath);
    if (localPath.isEmpty()) return;

    m_db.setTrackCover(trackId, localPath, QStringLiteral("manual"));
    onCoverReady(trackId, localPath);
}

void MainWindow::onSetPlaylistCover(qint64 playlistId)
{
    const QString imagePath = QFileDialog::getOpenFileName(
        this, tr("Выбрать обложку плейлиста"), {}, tr("Изображения (*.jpg *.jpeg *.png *.bmp)"));
    if (imagePath.isEmpty()) return;

    const QString localPath = m_covers.storePlaylistCoverFromFile(playlistId, imagePath);
    if (localPath.isEmpty()) return;

    m_db.setPlaylistCover(playlistId, localPath);
    refreshPlaylists();
}

void MainWindow::onSkinLayoutChanged()
{
    if (!m_panel || !m_panel->canvas()) return;
    QJsonObject layout = m_panel->canvas()->serializeLayout();
    if (m_canvas) layout.insert(QStringLiteral("mini_bar"), m_canvas->serializeLayout());
    m_db.saveSkin(QStringLiteral("default"), QJsonDocument(layout).toJson(QJsonDocument::Compact));
    m_db.setActiveSkin(QStringLiteral("default"));
}

void MainWindow::onCoverReady(qint64 trackId, const QString &path)
{
    m_trackModel.updateTrackCover(trackId, path);
    m_queue.updateCover(trackId, path);
    if (auto current = m_queue.current(); current && current->id == trackId) {
        const QPixmap pixmap(path);
        m_coverItem->setPixmap(pixmap);
        m_panel->setCover(pixmap);
    }
    adoptFirstTrackCovers();
}

void MainWindow::loadOrCreateDefaultSkin()
{
    const QString json = m_db.activeSkinLayoutJson();
    if (json.isEmpty()) return;
    const QJsonObject layout = QJsonDocument::fromJson(json.toUtf8()).object();
    // Layouts saved by the old cramped bar have no version and would
    // pile every control on top of the new compact strip.
    m_panel->canvas()->applyLayout(layout);
    const QJsonObject mini = layout.value(QStringLiteral("mini_bar")).toObject();
    if (!mini.isEmpty() && m_canvas) m_canvas->applyLayout(mini);
    mirrorTabLook();
}

void MainWindow::mirrorTabLook()
{
    if (!m_panel || !m_panel->canvas() || !m_canvas) return;
    const auto find = [](SkinCanvas *canvas, const QString &id) -> DraggableItem * {
        for (DraggableItem *item : canvas->items()) {
            if (item->elementId() == id) return item;
        }
        return nullptr;
    };

    if (auto *source = dynamic_cast<CoverArtItem *>(find(m_panel->canvas(), QStringLiteral("cover_art")))) {
        m_coverItem->setShapeId(source->shapeId());
        if (source->hasFrame()) m_coverItem->setFramePath(source->framePath());
        else m_coverItem->clearFrame();
    }
    if (auto *source = find(m_panel->canvas(), QStringLiteral("play_pause_button"))) {
        if (source->hasSkinColor()) m_playPauseItem->setSkinColor(source->skinColor());
        else m_playPauseItem->clearSkinColor();
        if (source->hasCustomIcon()) m_playPauseItem->setCustomIconPath(source->customIconPath());
        else m_playPauseItem->clearCustomIcon();
        m_playPauseItem->setVisible(source->isVisible());
    }
    if (auto *source = find(m_panel->canvas(), QStringLiteral("seek_bar"))) {
        if (source->hasSkinColor()) m_seekItem->setSkinColor(source->skinColor());
        else m_seekItem->clearSkinColor();
    }
    if (auto *source = dynamic_cast<TrackLabelItem *>(find(m_panel->canvas(), QStringLiteral("track_label")))) {
        if (source->hasSkinColor()) m_labelItem->setSkinColor(source->skinColor());
        else m_labelItem->clearSkinColor();
    }
    if (m_panel->canvas()->hasBackground())
        m_canvas->setBackgroundPath(m_panel->canvas()->backgroundPath());
    else
        m_canvas->clearBackground();
}
