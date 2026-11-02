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
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QScrollArea>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QPainterPath>
#include <QSizePolicy>
#include <QComboBox>
#include <QScrollBar>
#include <QSlider>
#include <QWidgetAction>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <QSettings>
#include <QDirIterator>
#include <QFrame>
#include <QComboBox>
#include "SmoothScroll.h"
#include "SettingsDialog.h"
#include "UpdateChecker.h"
#include <QDesktopServices>
#include <QProcess>
#include <QProgressDialog>
#include <QTimer>
#include <QDateTime>
#include <QUrl>
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
#include <iterator>

class TourShade : public QWidget {
public:
    explicit TourShade(QWidget *parent) : QWidget(parent) {}

    void setHole(const QRect &area, const QRect &hole)
    {
        setGeometry(area);
        m_hole = hole;
        QRegion mask(QRect(QPoint(0, 0), area.size()));
        if (hole.isValid()) mask -= hole;
        setMask(mask);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(6, 8, 12, 88));
        const QRect area = rect();
        const QRect hole = m_hole;
        painter.drawRect(QRect(area.left(), area.top(), area.width(), hole.top()));
        painter.drawRect(QRect(area.left(), hole.bottom(), area.width(), area.bottom() - hole.bottom()));
        painter.drawRect(QRect(area.left(), hole.top(), hole.left(), hole.height()));
        painter.drawRect(QRect(hole.right(), hole.top(), area.right() - hole.right(), hole.height()));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(QStringLiteral("#c9974b")), 2));
        painter.drawRoundedRect(hole.adjusted(1, 1, -2, -2), 12, 12);
    }

private:
    QRect m_hole;
};

namespace {

QString libraryRootPath()
{
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("library/root")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        return QDir(saved).absolutePath();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).absolutePath();
}

bool copyTree(const QString &fromDir, const QString &toDir)
{
    QDir source(fromDir);
    if (!source.exists()) return true;
    QDirIterator it(fromDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QString relative = source.relativeFilePath(sourcePath);
        const QString destPath = QDir(toDir).filePath(relative);
        QDir().mkpath(QFileInfo(destPath).absolutePath());
        if (QFile::exists(destPath)) QFile::remove(destPath);
        if (!QFile::copy(sourcePath, destPath)) return false;
    }
    return true;
}

void removeTree(const QString &dirPath)
{
    QDir dir(dirPath);
    if (dir.exists()) dir.removeRecursively();
}

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

QColor gSelection{QStringLiteral("#3a3428")};

QColor mutedSelection(const QColor &accent)
{
    const QColor surface(27, 30, 39);
    QColor mixed((accent.red() * 2 + surface.red() * 3) / 5,
                 (accent.green() * 2 + surface.green() * 3) / 5,
                 (accent.blue() * 2 + surface.blue() * 3) / 5);
    int hue = 0;
    int sat = 0;
    int value = 0;
    mixed.getHsv(&hue, &sat, &value);
    if (hue < 0) hue = std::max(0, accent.hsvHue());
    mixed.setHsv(hue, std::clamp(sat * 3 / 5, 32, 80), std::clamp(value, 50, 84));
    return mixed;
}

// Same corner radius as the library cover, and the side it has in a 72px row.
constexpr int kLibraryCoverRadius = 8;
constexpr int kLibraryCoverRow = 72;

class TrackCoverDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->fillRect(option.rect, selected ? gSelection : QColor(QStringLiteral("#1b1e27")));
        if (index.column() == TrackTableModel::ColCover) {
            const int side = std::min(option.rect.width(), option.rect.height()) - 16;
            if (side < 8) return;
            const QRect box(option.rect.center().x() - side / 2, option.rect.center().y() - side / 2, side, side);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            QString shape;
            for (const QWidget *widget = option.widget; widget && shape.isEmpty(); widget = widget->parentWidget())
                shape = widget->property("coverShape").toString();
            QPainterPath clip;
            if (shape == QLatin1String("vinyl"))
                clip.addEllipse(box);
            else if (shape == QLatin1String("square"))
                clip.addRect(box);
            else
                clip.addRoundedRect(box, kLibraryCoverRadius, kLibraryCoverRadius);
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
        if (index.data(PlaylistModel::HeaderRole).toBool()) {
            painter->save();
            painter->fillRect(option.rect, QColor(QStringLiteral("#12141a")));
            painter->setPen(QColor(QStringLiteral("#9a9388")));
            QFont font = option.font;
            font.setBold(true);
            painter->setFont(font);
            const QRect textRect = option.rect.adjusted(12, 0, -8, 0);
            const QString label = index.data(Qt::DisplayRole).toString();
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(label, Qt::ElideRight, textRect.width()));
            painter->restore();
            return;
        }
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->fillRect(option.rect, selected ? gSelection : QColor(QStringLiteral("#1b1e27")));
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        QRect textRect = opt.rect.adjusted(8, 0, -10, 0);
        const int side = std::min(kLibraryCoverRow - 16, std::min(opt.rect.width(), opt.rect.height()) - 16);
        if (side >= 8) {
            const int inset = std::max(0, (opt.rect.height() - side) / 2);
            const QRect box(opt.rect.left() + inset, opt.rect.top() + inset, side, side);
            if (opt.icon.isNull()) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(QStringLiteral("#8b8a86")));
                painter->drawRoundedRect(QRectF(box), kLibraryCoverRadius, kLibraryCoverRadius);
            } else {
                QPainterPath clip;
                clip.addRoundedRect(QRectF(box), kLibraryCoverRadius, kLibraryCoverRadius);
                painter->setClipPath(clip);
                painter->drawPixmap(box, opt.icon.pixmap(side, side));
                painter->setClipping(false);
            }
            textRect.setLeft(box.right() + 8);
        }
        painter->setPen(QColor(QStringLiteral("#f3efe6")));
        painter->setFont(opt.font);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(PlaylistModel::HeaderRole).toBool())
            return QSize(120, 26);
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), kLibraryCoverRow));
        return size;
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

QString trackColumnKey(int column)
{
    switch (column) {
    case TrackTableModel::ColCover: return QStringLiteral("col_cover");
    case TrackTableModel::ColTitle: return QStringLiteral("col_title");
    case TrackTableModel::ColArtistAlbum: return QStringLiteral("col_artist");
    case TrackTableModel::ColDuration: return QStringLiteral("col_duration");
    }
    return {};
}

// After the editor has its real size hint, keep the window on the screen that
// holds the main window. A font combo or a full-size canvas can be wider than
// that screen; scrolling the content avoids parking the frame at a huge x.
void placeEditorDialog(QDialog &dialog, QWidget *content, QWidget *anchor)
{
    QWidget *host = anchor ? anchor->window() : nullptr;
    QScreen *screen = nullptr;
    if (host && host->windowHandle())
        screen = host->windowHandle()->screen();
    if (!screen && host)
        screen = QGuiApplication::screenAt(host->frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);

    if (content->layout()) content->layout()->activate();
    content->ensurePolished();
    content->adjustSize();
    QSize hint = content->sizeHint().expandedTo(content->minimumSize());
    hint = hint.expandedTo(QSize(320, 180));
    content->resize(hint);

    // Client size, with room left for the title bar and the window borders.
    const int maxW = std::max(320, avail.width() - 32);
    const int maxH = std::max(180, avail.height() - 72);
    const bool overflow = hint.width() > maxW || hint.height() > maxH;

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSizeConstraint(QLayout::SetNoConstraint);
    if (overflow) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(false);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        scroll->setWidget(content);
        outer->addWidget(scroll);
    } else {
        outer->addWidget(content);
    }

    const QSize fitted(std::min(hint.width(), maxW), std::min(hint.height(), maxH));
    dialog.setFixedSize(fitted);

    const QRect basis = (host && host->isVisible()) ? host->frameGeometry() : avail;
    int x = basis.center().x() - fitted.width() / 2;
    int y = basis.center().y() - fitted.height() / 2;
    x = std::clamp(x, avail.left(), std::max(avail.left(), avail.right() - fitted.width() + 1));
    y = std::clamp(y, avail.top(), std::max(avail.top(), avail.bottom() - fitted.height() + 1));
    if (y < avail.top()) y = avail.top();
    dialog.move(x, y);
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
    applyWindowChrome();

    auto *addButton = new QPushButton(tr("+ Добавить трек"));
    m_addButton = addButton;
    auto *importButton = new QPushButton(tr("+ Импорт папки"));
    auto *libraryButton = new QPushButton(tr("Вся библиотека"));
    auto *newPlaylistButton = new QPushButton(tr("+ Новый плейлист"));

    auto *playlistView = new PlaylistDropView;
    m_playlistView = playlistView;
    m_playlistView->setModel(&m_playlistModel);
    m_playlistView->setIconSize(QSize(56, 56));
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
    railLayout->addWidget(newPlaylistButton);
    railLayout->addWidget(m_playlistView, 1);
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
    m_trackView->horizontalHeader()->setMinimumSectionSize(1);
    m_trackView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_trackView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_trackView->setColumnWidth(TrackTableModel::ColCover, 76);
    m_trackView->setColumnWidth(TrackTableModel::ColTitle, 280);
    m_trackView->setColumnWidth(TrackTableModel::ColArtistAlbum, 400);
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

    auto *sleepButton = new QToolButton;
    sleepButton->setObjectName(QStringLiteral("sleepTimer"));
    sleepButton->setText(tr("Таймер"));
    sleepButton->setCursor(Qt::PointingHandCursor);
    sleepButton->setFocusPolicy(Qt::NoFocus);
    sleepButton->setMinimumWidth(104);
    m_sleepButton = sleepButton;

    auto *miniLayout = new QHBoxLayout;
    miniLayout->setContentsMargins(8, 4, 8, 4);
    miniLayout->addWidget(m_canvas, 1);
    miniLayout->addWidget(sleepButton, 0, Qt::AlignVCenter);
    auto *miniBar = new QWidget;
    miniBar->setObjectName(QStringLiteral("miniBar"));
    miniBar->setLayout(miniLayout);
    m_miniBar = miniBar;

    auto *logoButton = new QToolButton;
    logoButton->setObjectName(QStringLiteral("appLogo"));
    logoButton->setFixedSize(44, 44);
    logoButton->setIconSize(QSize(36, 36));
    logoButton->setAutoRaise(true);
    logoButton->setCursor(Qt::PointingHandCursor);
    logoButton->setIcon(sandPlayLogoIcon());
    m_settingsButton = logoButton;
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
        armUpcoming();
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
    connect(logoButton, &QToolButton::clicked, this, &MainWindow::openSettings);
    connect(m_sleepButton, &QToolButton::clicked, this, [this]() { openSleepMenu(m_sleepButton); });
    connect(m_panel->sleepButton(), &QToolButton::clicked, this, [this]() { openSleepMenu(m_panel->sleepButton()); });
    m_gapTimer.setSingleShot(true);
    connect(&m_gapTimer, &QTimer::timeout, this, &MainWindow::onTrackGapElapsed);
    m_sleepTicker.setInterval(1000);
    connect(&m_sleepTicker, &QTimer::timeout, this, &MainWindow::onSleepTick);
    connect(m_trackView->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int section, int oldSize, int newSize) {
                if (m_fittingColumns || oldSize == newSize || !m_trackView) return;
                if ((QApplication::mouseButtons() & Qt::LeftButton) == 0) return;
                m_userColumnDrag = true;
                QHeaderView *header = m_trackView->horizontalHeader();
                const int count = TrackTableModel::ColCount;
                // The handle is the right edge of this section. Only the next
                // column absorbs the delta, so the edge under the cursor stays put.
                if (section < 0 || section + 1 >= count) return;
                const int neighbor = section + 1;
                const int neighborWidth = std::max(1, header->sectionSize(neighbor) - (newSize - oldSize));
                if (header->sectionSize(neighbor) == neighborWidth) return;
                m_fittingColumns = true;
                header->resizeSection(neighbor, neighborWidth);
                m_fittingColumns = false;
            });
    new SmoothScroller(m_trackView);
    new SmoothScroller(m_playlistView);
    if (m_panel->queueView()) new SmoothScroller(m_panel->queueView());
    if (m_db.getSetting(QStringLiteral("tour_done")).isEmpty())
        QTimer::singleShot(500, this, &MainWindow::startTour);

    connect(&m_queue, &PlaybackQueue::currentChanged, this, &MainWindow::onQueueCurrentChanged);
    connect(&m_queue, &PlaybackQueue::queueChanged, this, [this]() {
        m_panel->setQueue(m_queue.tracks(), m_queue.currentIndex());
        armUpcoming();
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

    m_gapless = m_db.getSetting(QStringLiteral("gapless"), QStringLiteral("0")) == QLatin1String("1");
    m_audio.setGapless(m_gapless);
    {
        QString gapText = m_db.getSetting(QStringLiteral("track_gap"), QStringLiteral("0")).trimmed();
        gapText.replace(QLatin1Char(','), QLatin1Char('.'));
        bool gapOk = false;
        const double gapSeconds = gapText.toDouble(&gapOk);
        const double clamped = gapOk ? std::clamp(gapSeconds, 0.0, 10.0) : 0.0;
        m_trackGapSec = std::round(clamped * 100.0) / 100.0;
    }

    int repeat = m_db.getSetting(QStringLiteral("repeat"), QStringLiteral("0")).toInt();
    if (repeat < 0 || repeat > 2) repeat = 0;
    const auto repeatMode = static_cast<PlaybackQueue::RepeatMode>(repeat);
    m_queue.setRepeatMode(repeatMode);
    m_panel->setRepeatMode(repeatMode);

    const QString fontFile = m_db.getSetting(QStringLiteral("ui_font_file"));
    if (!fontFile.isEmpty() && QFile::exists(fontFile))
        QFontDatabase::addApplicationFont(fontFile);
    m_uiFontFamily = m_db.getSetting(QStringLiteral("ui_font"));
    m_uiFontSize = std::clamp(m_db.getSetting(QStringLiteral("ui_font_size"), QStringLiteral("10")).toInt(), 9, 18);
    {
        int buttonStyle = m_db.getSetting(QStringLiteral("ui_button_style"), QStringLiteral("0")).toInt();
        if (buttonStyle == 2) {
            buttonStyle = 0;
            m_db.setSetting(QStringLiteral("ui_button_style"), QStringLiteral("0"));
        }
        m_buttonStyle = buttonStyle == 1 ? 1 : 0;
    }
    restoreColumnWeights();
    QTimer::singleShot(0, this, [this]() { applyColumnRatios(); });

    QColor accent(m_db.getSetting(QStringLiteral("accent"), QStringLiteral("#c9974b")));
    if (!accent.isValid()) accent = QColor(QStringLiteral("#c9974b"));
    applyAccent(accent);

    loadOrCreateDefaultSkin();
    syncLibraryCoverShape();
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

    m_updates = new UpdateChecker(this);
    connect(m_updates, &UpdateChecker::checkFinished, this, [this](const UpdateStatus &) {
        onUpdateChecked();
    });
    QTimer::singleShot(0, this, [this]() {
        if (m_updates) m_updates->start();
    });
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
    const qint64 keep = m_activePlaylistId;
    m_playlistModel.setPlaylists(m_db.allPlaylists());
    if (keep < 0 || !m_playlistView) return;
    for (int row = 0; row < m_playlistModel.rowCount(); ++row) {
        if (m_playlistModel.idAt(row) != keep) continue;
        m_playlistView->setCurrentIndex(m_playlistModel.index(row));
        break;
    }
}

void MainWindow::armUpcoming()
{
    if (!m_gapless || m_stopAfterTrack) {
        m_audio.clearNext();
        return;
    }
    const auto current = m_queue.current();
    if (!current || m_audio.loadedPath() != current->path)
        return;
    if (const auto next = m_queue.upcoming())
        m_audio.prepareNext(next->path);
    else
        m_audio.clearNext();
}

void MainWindow::refreshSleepControls()
{
    QString text = tr("Таймер");
    bool running = false;
    if (m_stopAfterTrack) {
        text = tr("Конец трека");
        running = true;
    } else if (m_sleepEndsAtMs > 0) {
        const qint64 left = std::max<qint64>(0, m_sleepEndsAtMs - QDateTime::currentMSecsSinceEpoch());
        const int total = int((left + 999) / 1000);
        text = tr("Таймер %1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
        running = true;
    }
    const auto apply = [&](QToolButton *button) {
        if (!button) return;
        button->setText(text);
        button->setProperty("running", running);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    };
    apply(m_sleepButton);
    if (m_panel) apply(m_panel->sleepButton());
}

void MainWindow::openSleepMenu(QWidget *anchor)
{
    if (!anchor) return;
    QMenu menu(this);
    QAction *off = menu.addAction(tr("Выключить"));
    off->setCheckable(true);
    off->setChecked(!m_stopAfterTrack && m_sleepMinutes <= 0);

    const int choices[] = {15, 30, 45, 60, 90};
    QAction *minuteActions[5] = {};
    for (int i = 0; i < 5; ++i) {
        minuteActions[i] = menu.addAction(tr("%1 минут").arg(choices[i]));
        minuteActions[i]->setCheckable(true);
        minuteActions[i]->setChecked(!m_stopAfterTrack && m_sleepMinutes == choices[i]);
        minuteActions[i]->setData(choices[i]);
    }
    menu.addSeparator();
    QAction *afterTrack = menu.addAction(tr("Остановить после этого трека"));
    afterTrack->setCheckable(true);
    afterTrack->setChecked(m_stopAfterTrack);

    QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
    if (!chosen) return;

    if (chosen == off) {
        m_stopAfterTrack = false;
        m_sleepMinutes = 0;
        m_sleepEndsAtMs = 0;
        m_sleepTicker.stop();
        armUpcoming();
        refreshSleepControls();
        return;
    }
    if (chosen == afterTrack) {
        m_stopAfterTrack = true;
        m_sleepMinutes = 0;
        m_sleepEndsAtMs = 0;
        m_sleepTicker.stop();
        m_gapTimer.stop();
        armUpcoming();
        refreshSleepControls();
        return;
    }
    const int minutes = chosen->data().toInt();
    if (minutes <= 0) return;
    m_stopAfterTrack = false;
    m_sleepMinutes = minutes;
    m_sleepEndsAtMs = QDateTime::currentMSecsSinceEpoch() + qint64(minutes) * 60 * 1000;
    if (!m_sleepTicker.isActive()) m_sleepTicker.start();
    armUpcoming();
    refreshSleepControls();
}

void MainWindow::onTrackGapElapsed()
{
    if (m_stopAfterTrack) return;
    if (!m_queue.advance()) {
        m_playPauseItem->setPlaying(false);
        m_panel->setPlaying(false);
        m_coverItem->setPlaybackActive(false);
    }
}

void MainWindow::onSleepTick()
{
    if (m_sleepEndsAtMs <= 0) {
        m_sleepTicker.stop();
        refreshSleepControls();
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() >= m_sleepEndsAtMs) {
        m_sleepEndsAtMs = 0;
        m_sleepMinutes = 0;
        m_sleepTicker.stop();
        m_gapTimer.stop();
        m_audio.pause();
        refreshSleepControls();
        return;
    }
    refreshSleepControls();
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

void MainWindow::restoreColumnWeights()
{
    const int defaults[TrackTableModel::ColCount] = {76, 280, 400, 84};
    int saved[TrackTableModel::ColCount];
    bool complete = true;
    for (int column = 0; column < TrackTableModel::ColCount; ++column) {
        saved[column] = m_db.getSetting(trackColumnKey(column)).toInt();
        if (saved[column] < 1) complete = false;
    }
    if (complete) {
        for (int column = 0; column < TrackTableModel::ColCount; ++column)
            m_columnWeights[column] = saved[column];
        m_columnPreferenceSaved = true;
        return;
    }
    m_columnPreferenceSaved = false;
    for (int column = 0; column < TrackTableModel::ColCount; ++column)
        m_columnWeights[column] = defaults[column];
    if (saved[TrackTableModel::ColTitle] >= 140)
        m_columnWeights[TrackTableModel::ColTitle] = saved[TrackTableModel::ColTitle];
}

bool MainWindow::rememberColumnRatios()
{
    if (!m_trackView || m_fittingColumns) return false;
    QHeaderView *header = m_trackView->horizontalHeader();
    if (!header || header->count() < TrackTableModel::ColCount) return false;
    int weights[TrackTableModel::ColCount];
    for (int column = 0; column < TrackTableModel::ColCount; ++column) {
        weights[column] = header->sectionSize(column);
        if (weights[column] < 1) return false;
    }
    for (int column = 0; column < TrackTableModel::ColCount; ++column) {
        m_columnWeights[column] = weights[column];
        m_db.setSetting(trackColumnKey(column), QString::number(weights[column]));
    }
    m_columnPreferenceSaved = true;
    return true;
}

void MainWindow::applyColumnRatios()
{
    if (!m_trackView || m_userColumnDrag || m_fittingColumns) return;
    QHeaderView *header = m_trackView->horizontalHeader();
    if (!header || header->count() < TrackTableModel::ColCount) return;
    const int count = TrackTableModel::ColCount;
    const int available = m_trackView->viewport()->width();
    if (available < count) return;

    m_fittingColumns = true;
    if (!m_columnPreferenceSaved) {
        const int cover = std::clamp(m_columnWeights[TrackTableModel::ColCover], 1, available - (count - 1));
        header->resizeSection(TrackTableModel::ColCover, cover);
        const int restAvailable = std::max(count - 1, available - header->sectionSize(TrackTableModel::ColCover));
        int restSum = 0;
        for (int column = 1; column < count; ++column)
            restSum += std::max(1, m_columnWeights[column]);
        int used = 0;
        for (int column = 1; column < count; ++column) {
            int width;
            if (column == count - 1) {
                width = std::max(1, restAvailable - used);
            } else {
                width = int((qint64(restAvailable) * std::max(1, m_columnWeights[column])) / restSum);
                const int maxWidth = restAvailable - used - (count - column - 1);
                width = std::clamp(width, 1, std::max(1, maxWidth));
            }
            header->resizeSection(column, width);
            used += header->sectionSize(column);
        }
    } else {
        int sum = 0;
        for (int column = 0; column < count; ++column) {
            if (m_columnWeights[column] < 1) m_columnWeights[column] = 1;
            sum += m_columnWeights[column];
        }
        int used = 0;
        for (int column = 0; column < count; ++column) {
            int width;
            if (column == count - 1) {
                width = std::max(1, available - used);
            } else {
                width = int((qint64(available) * m_columnWeights[column]) / sum);
                const int maxWidth = available - used - (count - column - 1);
                width = std::clamp(width, 1, std::max(1, maxWidth));
            }
            header->resizeSection(column, width);
            used += header->sectionSize(column);
        }
    }
    const int overflow = header->length() - available;
    if (overflow > 0)
        header->resizeSection(count - 1, std::max(1, header->sectionSize(count - 1) - overflow));
    if (header->offset() != 0)
        header->setOffset(0);
    m_fittingColumns = false;
}

void MainWindow::syncLibraryCoverShape()
{
    if (!m_trackView || !m_panel) return;
    QString shape = m_panel->coverShapeId();
    if (shape != QLatin1String("square") && shape != QLatin1String("vinyl"))
        shape = QStringLiteral("rounded");
    m_trackView->setProperty("coverShape", shape);
    if (QWidget *viewport = m_trackView->viewport()) {
        viewport->setProperty("coverShape", shape);
        viewport->update();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_tour && m_tour->isVisible()) placeTour();
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
    if (event->type() == QEvent::Wheel) {
        auto *widget = qobject_cast<QWidget *>(watched);
        auto *combo = qobject_cast<QComboBox *>(widget);
        if (!combo && widget)
            combo = qobject_cast<QComboBox *>(widget->parentWidget());
        if (combo && combo->view() && !combo->view()->isVisible())
            return true;
    }
    if (m_librarySplit && watched == m_librarySplit->handle(1)
        && event->type() == QEvent::MouseButtonDblClick) {
        const int total = std::max(m_librarySplit->width(), 480);
        const int left = std::min(240, std::max(160, total - 280));
        m_librarySplit->setSizes({left, std::max(200, total - left)});
        m_db.setSetting(QStringLiteral("library_split"), QString::number(m_librarySplit->sizes().value(0)));
        return true;
    }
    if (m_userColumnDrag && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_userColumnDrag) return;
            m_userColumnDrag = false;
            if (rememberColumnRatios()) applyColumnRatios();
        });
    }
    if (m_trackView && watched == m_trackView->viewport() && event->type() == QEvent::Resize)
        applyColumnRatios();
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
    if (m_canvas) m_canvas->applyAccent(color);
    if (m_playPauseItem && color.isValid()) {
        m_playPauseItem->clearSkinColor();
        m_playPauseItem->setAccent(color);
    }
    if (m_panel) m_panel->setAccent(color);
    applyWindowChrome();
}

void MainWindow::applyWindowChrome()
{
    gSelection = mutedSelection(m_accent.isValid() ? m_accent : QColor(QStringLiteral("#c9974b")));
    const QString accent = m_accent.isValid() ? m_accent.name(QColor::HexRgb) : QStringLiteral("#c9974b");
    const QString selection = gSelection.name(QColor::HexRgb);
    QString buttons;
    if (m_buttonStyle == 1) {
        buttons = QStringLiteral(
            "QPushButton { background: %1; color: #14161c; border: none; border-radius: 6px; padding: 6px 10px; }"
            "QPushButton:hover { background: %1; }").arg(accent);
    } else {
        buttons = QStringLiteral(
            "QPushButton { background: #242833; color: #ece8e0; border: 1px solid %1; border-radius: 6px; padding: 6px 10px; }"
            "QPushButton:hover { background: #2e3340; border-color: %1; }").arg(accent);
    }
    setStyleSheet(QString(QStringLiteral(R"(
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
        QListView::item:selected { background-color: #3a3428; color: #f3efe6; }
        QPushButton:checked { color: #14161c; }
        QSplitter::handle { background: #2a2e3a; }
        QGraphicsView { background: #1b1e27; border: none; }
        QStatusBar { color: #8b8a86; }
        #playerPanel, #playerLogo, #playerGutter, #playerTop { background: #181b22; border: none; }
        #playerPanel QSplitter, #playerQueue { background: transparent; border: none; }
        #queueCard { background: transparent; border: none; border-radius: 18px; }
        #queueCard QListView, #queueCard QListView::viewport, #queueCard QLabel {
            background: transparent; border: none; border-radius: 0px;
            color: #f3efe6;
        }
        #logoStrip { background: #14161c; border: none; }
        #appLogo { background: transparent; border: none; border-radius: 12px; }
        #appLogo:hover { background: #242833; }
        #tourCard { background: #242833; border: 1px solid #c9974b; border-radius: 12px; }
        #tourCard QLabel { color: #f3efe6; background: transparent; }
        #miniBar { background: #1b1e27; border-top: 1px solid #2a2e3a; }
    )")).replace(QStringLiteral("#3a3428"), selection) + buttons + QStringLiteral(
        "QToolButton#sleepTimer { color: #ece8e0; background: #242833; border: 1px solid #2a2e3a; "
        "border-radius: 8px; padding: 4px 10px; }"
        "QToolButton#sleepTimer:hover { background: #2e3340; }"
        "QToolButton#sleepTimer[running=\"true\"] { color: %1; border-color: %1; }").arg(accent));

    const auto paintSelection = [](QAbstractItemView *view) {
        if (!view) return;
        QPalette palette = view->palette();
        palette.setColor(QPalette::Highlight, gSelection);
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#f3efe6")));
        view->setPalette(palette);
        view->viewport()->setPalette(palette);
        view->viewport()->update();
    };
    paintSelection(m_trackView);
    paintSelection(m_playlistView);
    if (m_panel) paintSelection(m_panel->queueView());

    const auto styleTransport = [this](SkinCanvas *canvas) {
        if (!canvas) return;
        for (DraggableItem *item : canvas->items()) {
            if (auto *button = dynamic_cast<TextButtonItem *>(item))
                button->setButtonStyle(m_buttonStyle == 1 ? 1 : 0);
        }
    };
    styleTransport(m_canvas);
    if (m_panel) styleTransport(m_panel->canvas());

    if (!m_librarySplit) return;
    QFont font = m_librarySplit->font();
    if (!m_uiFontFamily.isEmpty()) font.setFamily(m_uiFontFamily);
    font.setPointSize(std::clamp(m_uiFontSize, 9, 18));
    const auto widgets = m_librarySplit->findChildren<QWidget *>();
    m_librarySplit->setFont(font);
    for (QWidget *widget : widgets) widget->setFont(font);
    refreshSleepControls();
}

void MainWindow::openSettings()
{
    enum class FollowUp { None, Editor, Window, Tour };
    FollowUp follow = FollowUp::None;
    // Keep the startup notice out of this function. A second modal exec while
    // SettingsDialog is open freezes the process on Windows. The equalizer is
    // shown with show() from this loop and is not started after the dialog closes.
    m_deferUpdateNotice = true;
    {
        const UpdateStatus update = m_updates ? m_updates->status() : UpdateStatus{};
        SettingsDialog dialog(m_audio.sampleRate(), m_audio.bitDepth(), m_gapless, m_trackGapSec,
                              libraryRootPath(), update, this);
        if (m_updates)
            connect(m_updates, &UpdateChecker::checkFinished, &dialog, &SettingsDialog::setUpdateStatus);
        connect(&dialog, &SettingsDialog::editPlayerRequested, this, [&]() {
            follow = FollowUp::Editor;
            dialog.accept();
        });
        connect(&dialog, &SettingsDialog::editWindowRequested, this, [&]() {
            follow = FollowUp::Window;
            dialog.accept();
        });
        connect(&dialog, &SettingsDialog::accentRequested, this, [this]() {
            const QColor picked = QColorDialog::getColor(m_accent, this, tr("Цвет плеера"));
            if (!picked.isValid()) return;
            applyAccent(picked);
            m_db.setSetting(QStringLiteral("accent"), picked.name(QColor::HexRgb));
        });
        connect(&dialog, &SettingsDialog::equalizerRequested, this, [this, &dialog]() {
            openEqualizer(&dialog);
        });
        connect(&dialog, &SettingsDialog::outputChanged, this, [this, &dialog](int sampleRate, int bitDepth) {
            const int previousRate = m_audio.sampleRate();
            const int previousDepth = m_audio.bitDepth();
            if (!m_audio.configureOutput(sampleRate, bitDepth)) {
                dialog.setOutput(previousRate, previousDepth);
                QMessageBox::warning(this, tr("Звучание"),
                                     tr("Устройство не приняло этот формат. Оставлен прежний вывод."));
                armUpcoming();
                return;
            }
            m_db.setSetting(QStringLiteral("sample_rate"), QString::number(m_audio.sampleRate()));
            m_db.setSetting(QStringLiteral("bit_depth"), QString::number(m_audio.bitDepth()));
            armUpcoming();
        });
        connect(&dialog, &SettingsDialog::gaplessChanged, this, [this](bool on) {
            m_gapless = on;
            m_db.setSetting(QStringLiteral("gapless"), on ? QStringLiteral("1") : QStringLiteral("0"));
            m_audio.setGapless(on);
            if (on && m_gapTimer.isActive()) {
                m_gapTimer.stop();
                onTrackGapElapsed();
            } else {
                armUpcoming();
            }
        });
        connect(&dialog, &SettingsDialog::trackGapChanged, this, [this](double seconds) {
            const double clamped = std::clamp(seconds, 0.0, 10.0);
            m_trackGapSec = std::round(clamped * 100.0) / 100.0;
            m_db.setSetting(QStringLiteral("track_gap"), QString::number(m_trackGapSec, 'f', 2));
        });
        connect(&dialog, &SettingsDialog::libraryChangeRequested, this, [this, &dialog]() {
            if (chooseLibraryDirectory()) dialog.setLibraryPath(libraryRootPath());
        });
        connect(&dialog, &SettingsDialog::tourRequested, this, [&]() {
            follow = FollowUp::Tour;
            dialog.accept();
        });
        dialog.exec();
    }
    if (follow == FollowUp::Editor) openPlayerEditor();
    else if (follow == FollowUp::Window) openWindowEditor();
    else if (follow == FollowUp::Tour) startTour();
    m_deferUpdateNotice = false;
    if (m_updateDownloadPending) {
        m_updateDownloadPending = false;
        m_updateNoticePending = false;
        startUpdateDownload();
    } else if (m_updateNoticePending) {
        showUpdateNotice();
    }
}

void MainWindow::onUpdateChecked()
{
    if (!m_updates) return;
    if (m_updates->status().state != UpdateStatus::State::UpdateAvailable || m_updateNoticeShown)
        return;
    if (m_deferUpdateNotice) {
        m_updateNoticePending = true;
        return;
    }
    showUpdateNotice();
}

void MainWindow::showUpdateNotice()
{
    if (m_deferUpdateNotice || m_updateNoticeShown || !m_updates || !isVisible()) return;
    const UpdateStatus status = m_updates->status();
    if (status.state != UpdateStatus::State::UpdateAvailable) {
        m_updateNoticePending = false;
        return;
    }
    m_updateNoticeShown = true;
    m_updateNoticePending = false;

    QString version = status.latestTag.trimmed();
    if (version.size() > 1
        && (version.at(0) == QLatin1Char('v') || version.at(0) == QLatin1Char('V')))
        version.remove(0, 1);
    if (version.isEmpty())
        version = status.latestTag;

    // Non-modal on purpose: a second modal exec while SettingsDialog is open
    // freezes the process. This window is shown only after that dialog closes.
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Обновление"));
    dialog->setModal(false);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(dialog);
    layout->setSpacing(12);
    auto *text = new QLabel(tr("Доступна новая версия %1").arg(version));
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: #ece8e0; background: transparent; border: none;"));
    layout->addWidget(text);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *stay = new QPushButton(tr("Остаться на этой"));
    auto *update = new QPushButton(tr("Обновить"));
    stay->setCursor(Qt::PointingHandCursor);
    update->setCursor(Qt::PointingHandCursor);
    update->setDefault(true);
    update->setAutoDefault(true);
    stay->setAutoDefault(false);
    row->addWidget(stay);
    row->addWidget(update);
    layout->addLayout(row);

    connect(stay, &QPushButton::clicked, dialog, &QDialog::close);
    connect(update, &QPushButton::clicked, dialog, [dialog]() {
        dialog->setProperty("sandplayUpdate", true);
        dialog->close();
    });
    connect(dialog, &QDialog::finished, this, [this, dialog](int) {
        const bool updateNow = dialog->property("sandplayUpdate").toBool();
        dialog->deleteLater();
        if (updateNow)
            startUpdateDownload();
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::startUpdateDownload()
{
    if (!m_updates)
        return;
    // SettingsDialog::exec is still on the stack. A modal progress box here freezes the app.
    if (m_deferUpdateNotice) {
        m_updateDownloadPending = true;
        return;
    }

    const UpdateStatus status = m_updates->status();
    if (status.state != UpdateStatus::State::UpdateAvailable)
        return;
    if (status.assetKind == UpdateStatus::AssetKind::None) {
        QMessageBox::information(this, tr("Обновление"),
                                 tr("В этом выпуске нет файла для обновления."));
        return;
    }

    QProgressDialog progress(tr("Загрузка обновления…"), tr("Отмена"), 0, 0, this);
    progress.setWindowTitle(tr("Обновление"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumWidth(420);

    bool canceled = false;
    bool failed = false;
    QString error;
    QString savedPath;
    bool installer = false;

    connect(m_updates, &UpdateChecker::downloadProgress, &progress,
            [&progress](qint64 received, qint64 total) {
        if (total <= 0) {
            progress.setMaximum(0);
            return;
        }
        progress.setMaximum(100);
        const int percent = static_cast<int>(std::clamp(received * 100 / total, qint64(0), qint64(100)));
        progress.setValue(percent);
    });
    connect(m_updates, &UpdateChecker::downloadFinished, &progress,
            [&](const QString &path, bool isInstaller) {
        savedPath = path;
        installer = isInstaller;
        progress.accept();
    });
    connect(m_updates, &UpdateChecker::downloadFailed, &progress, [&](const QString &message) {
        failed = true;
        error = message;
        progress.reject();
    });
    connect(m_updates, &UpdateChecker::downloadCanceled, &progress, [&]() {
        canceled = true;
        progress.reject();
    });
    connect(&progress, &QProgressDialog::canceled, this, [this, &canceled]() {
        canceled = true;
        if (m_updates)
            m_updates->cancelDownload();
    });

    m_updates->downloadUpdate();
    if (savedPath.isEmpty() && !failed && !canceled)
        progress.exec();

    disconnect(m_updates, nullptr, &progress, nullptr);
    disconnect(&progress, nullptr, this, nullptr);

    if (canceled || progress.wasCanceled())
        return;

    if (failed || savedPath.isEmpty()) {
        if (error.isEmpty())
            error = tr("Не удалось скачать обновление.");
        QMessageBox::warning(this, tr("Обновление"), error);
        m_updateNoticeShown = false;
        showUpdateNotice();
        return;
    }

    if (installer) {
        const QFileInfo info(savedPath);
        if (!QProcess::startDetached(info.absoluteFilePath(), {}, info.absolutePath())) {
            QMessageBox::warning(this, tr("Обновление"), tr("Не удалось запустить установку."));
            m_updateNoticeShown = false;
            showUpdateNotice();
        }
        return;
    }

    const QString folder = QFileInfo(savedPath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const bool inDownloads = !downloads.isEmpty()
        && QDir(folder).absolutePath().compare(QDir(downloads).absolutePath(), Qt::CaseInsensitive) == 0;
    QMessageBox::information(this, tr("Обновление"),
        inDownloads
            ? tr("Архив сохранён в папку «Загрузки». Распакуйте его и запустите SandPlay.exe.")
            : tr("Архив сохранён. Распакуйте его и запустите SandPlay.exe."));
}

bool MainWindow::adoptFontFile(const QString &path)
{
    const QString fontsDir = libraryRootPath() + QStringLiteral("/fonts");
    QDir().mkpath(fontsDir);
    const QString dest = QDir(fontsDir).filePath(QFileInfo(path).fileName());
    if (QFileInfo(path).absoluteFilePath().compare(QFileInfo(dest).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
        if (QFile::exists(dest)) QFile::remove(dest);
        if (!QFile::copy(path, dest)) {
            QMessageBox::warning(this, tr("Шрифт"), tr("Не удалось скопировать файл шрифта."));
            return false;
        }
    }
    const int id = QFontDatabase::addApplicationFont(dest);
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (id < 0 || families.isEmpty()) {
        QMessageBox::warning(this, tr("Шрифт"), tr("Этот файл не удалось открыть как шрифт."));
        return false;
    }
    m_uiFontFamily = families.first();
    m_db.setSetting(QStringLiteral("ui_font"), m_uiFontFamily);
    m_db.setSetting(QStringLiteral("ui_font_file"), dest);
    applyWindowChrome();
    return true;
}

void MainWindow::openWindowEditor()
{
    if (!m_librarySplit) return;
    auto *hostLayout = qobject_cast<QVBoxLayout *>(m_librarySplit->parentWidget()
                                                       ? m_librarySplit->parentWidget()->layout() : nullptr);
    const int index = hostLayout ? hostLayout->indexOf(m_librarySplit) : -1;
    const QSize liveSize = m_librarySplit->size();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Редактировать окно"));
    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    auto *hint = new QLabel(tr("Тяните границу колонок и заголовки таблицы — размер меняется сразу."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *toolRow = new QHBoxLayout;
    auto *styleBox = new QComboBox;
    styleBox->addItem(tr("Кнопки: контур"), 0);
    styleBox->addItem(tr("Кнопки: заливка"), 1);
    const int styleIndex = styleBox->findData(m_buttonStyle == 1 ? 1 : 0);
    styleBox->setCurrentIndex(styleIndex >= 0 ? styleIndex : 0);
    auto *sizeBox = new QComboBox;
    for (int size = 9; size <= 18; ++size)
        sizeBox->addItem(tr("Размер %1").arg(size), size);
    const int sizeIndex = sizeBox->findData(m_uiFontSize);
    sizeBox->setCurrentIndex(sizeIndex >= 0 ? sizeIndex : 1);
    auto *fontBox = new QFontComboBox;
    fontBox->setCurrentFont(QFont(m_uiFontFamily.isEmpty() ? font().family() : m_uiFontFamily));
    auto *otherFont = new QPushButton(tr("Другой шрифт"));
    otherFont->setCursor(Qt::PointingHandCursor);
    toolRow->addWidget(styleBox);
    toolRow->addWidget(sizeBox);
    toolRow->addWidget(fontBox, 1);
    toolRow->addWidget(otherFont);
    root->addLayout(toolRow);

    connect(fontBox, &QFontComboBox::currentFontChanged, this, [this](const QFont &chosen) {
        m_uiFontFamily = chosen.family();
        m_db.setSetting(QStringLiteral("ui_font"), m_uiFontFamily);
        applyWindowChrome();
    });
    connect(otherFont, &QPushButton::clicked, this, [this, fontBox]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Шрифт"), {}, tr("Шрифты (*.ttf *.otf *.ttc)"));
        if (path.isEmpty() || !adoptFontFile(path)) return;
        fontBox->blockSignals(true);
        fontBox->setCurrentFont(QFont(m_uiFontFamily));
        fontBox->blockSignals(false);
    });
    connect(sizeBox, &QComboBox::currentIndexChanged, this, [this, sizeBox]() {
        m_uiFontSize = sizeBox->currentData().toInt();
        m_db.setSetting(QStringLiteral("ui_font_size"), QString::number(m_uiFontSize));
        applyWindowChrome();
    });
    connect(styleBox, &QComboBox::currentIndexChanged, this, [this, styleBox]() {
        m_buttonStyle = styleBox->currentData().toInt() == 1 ? 1 : 0;
        m_db.setSetting(QStringLiteral("ui_button_style"), QString::number(m_buttonStyle));
        applyWindowChrome();
    });

    m_librarySplit->setParent(nullptr);
    if (liveSize.width() > 80 && liveSize.height() > 80)
        m_librarySplit->setMinimumSize(liveSize);
    root->addWidget(m_librarySplit, 1);

    auto *done = new QPushButton(tr("Готово"));
    connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
    root->addWidget(done, 0, Qt::AlignRight);

    const auto putBack = [&]() {
        m_librarySplit->setParent(nullptr);
        m_librarySplit->setMinimumSize(0, 0);
        m_librarySplit->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (hostLayout && index >= 0)
            hostLayout->insertWidget(index, m_librarySplit, 1);
    };
    struct Restore {
        std::function<void()> fn;
        ~Restore() { if (fn) fn(); }
    } restore{putBack};

    placeEditorDialog(dialog, content, this);
    dialog.exec();
    restore.fn();
    restore.fn = nullptr;
}

void MainWindow::startTour()
{
    const bool closing = m_panelOpen;
    if (closing) setPlayerOpen(false);
    if (!m_tour) {
        m_tour = new QFrame(this);
        m_tour->setObjectName(QStringLiteral("tourCard"));
        m_tour->setFixedWidth(340);
        auto *layout = new QVBoxLayout(m_tour);
        layout->setContentsMargins(16, 14, 16, 14);
        m_tourTitle = new QLabel;
        m_tourTitle->setWordWrap(true);
        QFont titleFont = m_tourTitle->font();
        titleFont.setBold(true);
        m_tourTitle->setFont(titleFont);
        m_tourBody = new QLabel;
        m_tourBody->setWordWrap(true);
        layout->addWidget(m_tourTitle);
        layout->addWidget(m_tourBody);
        auto *row = new QHBoxLayout;
        auto *skip = new QPushButton(tr("Пропустить"));
        m_tourNext = new QPushButton(tr("Дальше"));
        skip->setCursor(Qt::PointingHandCursor);
        m_tourNext->setCursor(Qt::PointingHandCursor);
        row->addWidget(skip);
        row->addStretch(1);
        row->addWidget(m_tourNext);
        layout->addLayout(row);
        connect(skip, &QPushButton::clicked, this, &MainWindow::finishTour);
        connect(m_tourNext, &QPushButton::clicked, this, [this]() {
            ++m_tourStep;
            showTourStep();
        });
    }
    m_tourStep = 0;
    QTimer::singleShot(closing ? 320 : 0, this, &MainWindow::showTourStep);
}

void MainWindow::showTourStep()
{
    struct Step {
        QWidget *target;
        const char *title;
        const char *body;
    };
    const Step steps[] = {
        {m_settingsButton, "Настройки",
         "Логотип открывает настройки: звук, цвет, эквалайзер, оформление окна, папку библиотеки и правку плеера."},
        {m_addButton, "Библиотека",
         "«Добавить трек» берёт отдельные файлы. «Импорт папки» следит за папкой и подхватывает новые записи."},
        {m_searchEdit, "Поиск",
         "Строка ищет по названию, исполнителю и альбому. Несколько слов должны встретиться все."},
        {m_playlistView, "Плейлисты",
         "Список слева. Трек можно перетащить на плейлист. Двойной щелчок по разделителю возвращает ширину колонки."},
        {m_trackView, "Треки",
         "Двойной щелчок играет. Повторный щелчок по текущему треку ставит паузу. Правый клик — сведения и обложка."},
        {m_miniBar, "Плеер и очередь",
         "Нижняя панель открывает вкладку плеера. Очередь на этой вкладке справа. Пробел — пауза, если курсор не в поле ввода."},
    };
    if (m_tourStep < 0 || m_tourStep >= int(std::size(steps)) || !m_tour) {
        finishTour();
        return;
    }
    const Step &step = steps[m_tourStep];
    m_tourTitle->setText(QString::fromUtf8(step.title));
    m_tourBody->setText(QString::fromUtf8(step.body));
    m_tourNext->setText(m_tourStep + 1 == int(std::size(steps)) ? tr("Понятно") : tr("Дальше"));
    QWidget *host = centralWidget() ? centralWidget() : static_cast<QWidget *>(this);
    if (m_tour->parentWidget() != host) m_tour->setParent(host);
    m_tour->adjustSize();
    m_tour->show();
    m_tour->raise();
    updateTourShade();
    placeTour();
}

void MainWindow::placeTour()
{
    if (!m_tour || !m_tour->isVisible()) return;
    QWidget *targets[] = {m_settingsButton, m_addButton, m_searchEdit, m_playlistView, m_trackView, m_miniBar};
    QWidget *target = m_tourStep >= 0 && m_tourStep < int(std::size(targets)) ? targets[m_tourStep] : nullptr;
    QWidget *host = m_tour->parentWidget();
    if (!target || !host) return;
    const QPoint below = target->mapTo(host, QPoint(target->width() / 2, target->height()));
    int x = below.x() - m_tour->width() / 2;
    int y = below.y() + 10;
    if (y + m_tour->height() > host->height() - 8)
        y = target->mapTo(host, QPoint(0, 0)).y() - m_tour->height() - 10;
    x = std::clamp(x, 8, std::max(8, host->width() - m_tour->width() - 8));
    y = std::clamp(y, 8, std::max(8, host->height() - m_tour->height() - 8));
    m_tour->move(x, y);
    updateTourShade();
    m_tour->raise();
}

void MainWindow::updateTourShade()
{
    QWidget *host = centralWidget();
    if (!host) return;
    if (!m_tourShade) m_tourShade = new TourShade(host);
    if (m_tourShade->parentWidget() != host) m_tourShade->setParent(host);
    if (!m_tour || !m_tour->isVisible()) {
        m_tourShade->hide();
        return;
    }
    QWidget *targets[] = {m_settingsButton, m_addButton, m_searchEdit, m_playlistView, m_trackView, m_miniBar};
    QWidget *target = m_tourStep >= 0 && m_tourStep < int(std::size(targets)) ? targets[m_tourStep] : nullptr;
    if (!target) {
        m_tourShade->hide();
        return;
    }
    QRect hole(target->mapTo(host, QPoint(0, 0)), target->size());
    hole = hole.adjusted(-8, -6, 8, 6).intersected(host->rect());
    m_tourShade->setHole(host->rect(), hole);
    m_tourShade->show();
    m_tourShade->raise();
    m_tour->raise();
}

void MainWindow::finishTour()
{
    m_db.setSetting(QStringLiteral("tour_done"), QStringLiteral("1"));
    if (m_tour) m_tour->hide();
    if (m_tourShade) m_tourShade->hide();
}

bool MainWindow::chooseLibraryDirectory()
{
    const QString current = libraryRootPath();
    const QString picked = QFileDialog::getExistingDirectory(this, tr("Папка библиотеки"), current);
    if (picked.isEmpty()) return false;
    const QString next = QDir(picked).absolutePath();
    if (QDir(next) == QDir(current)) return false;
    const QString currentClean = QDir::cleanPath(current);
    const QString nextClean = QDir::cleanPath(next);
    if (nextClean.startsWith(currentClean + QLatin1Char('/'), Qt::CaseInsensitive)
        || currentClean.startsWith(nextClean + QLatin1Char('/'), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Папка библиотеки"),
                             tr("Новая папка не должна лежать внутри текущей, и наоборот."));
        return false;
    }
    if (QFile::exists(QDir(next).filePath(QStringLiteral("library.db")))) {
        QMessageBox::warning(this, tr("Папка библиотеки"),
                             tr("В этой папке уже есть библиотека. Выберите пустую."));
        return false;
    }

    const QString dbName = QStringLiteral("library.db");
    const QString newDb = QDir(next).filePath(dbName);
    QDir().mkpath(next);
    if (QFile::exists(newDb)) QFile::remove(newDb);
    m_db.close();
    const QString newCovers = QDir(next).filePath(QStringLiteral("covers"));
    const QString newIcons = QDir(next).filePath(QStringLiteral("icons"));
    if (!QFile::copy(QDir(current).filePath(dbName), newDb)
        || !copyTree(QDir(current).filePath(QStringLiteral("covers")), newCovers)
        || !copyTree(QDir(current).filePath(QStringLiteral("icons")), newIcons)
        || !m_db.reopen(newDb)
        || !m_db.replaceStoredPathPrefix(current, next)) {
        QFile::remove(newDb);
        removeTree(newCovers);
        removeTree(newIcons);
        m_db.reopen(QDir(current).filePath(dbName));
        QMessageBox::warning(this, tr("Папка библиотеки"),
                             tr("Не удалось перенести библиотеку. Она осталась на прежнем месте."));
        return false;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("library/root"), next);
    m_covers.setDirectory(QDir(next).filePath(QStringLiteral("covers")));
    for (const Track &track : m_queue.tracks()) {
        if (const auto fresh = m_db.trackById(track.id))
            m_queue.updateCover(track.id, fresh->coverPath);
    }
    refreshTracks(m_searchEdit ? m_searchEdit->text() : QString(), true);
    refreshPlaylists();
    loadOrCreateDefaultSkin();
    QFile::remove(QDir(current).filePath(dbName));
    QFile::remove(QDir(current).filePath(dbName + QStringLiteral("-wal")));
    QFile::remove(QDir(current).filePath(dbName + QStringLiteral("-shm")));
    removeTree(QDir(current).filePath(QStringLiteral("covers")));
    removeTree(QDir(current).filePath(QStringLiteral("icons")));
    statusBar()->showMessage(tr("Библиотека перенесена"), 4000);
    return true;
}

void MainWindow::openPlayerEditor()
{
    SkinCanvas *tab = m_panel->canvas();
    auto *split = qobject_cast<QSplitter *>(tab->parentWidget());
    const int splitIndex = split ? split->indexOf(tab) : 0;
    auto *miniLayout = qobject_cast<QHBoxLayout *>(m_canvas->parentWidget() ? m_canvas->parentWidget()->layout() : nullptr);
    const int miniIndex = miniLayout ? miniLayout->indexOf(m_canvas) : 0;

    // The hidden player has not been laid out yet, so its canvas size is the
    // widget default. Measuring that put the editor controls on top of each other.
    QSize tabSize;
    if (m_panel->isVisible() && tab->width() >= 240 && tab->height() >= 240)
        tabSize = tab->size();
    else
        tabSize = m_panel->preferredCanvasSize(playerTarget().size());
    QSize miniSize = m_canvas->size();
    if (miniSize.width() < 80) miniSize.setWidth(std::max(240, width() - 220));
    if (miniSize.height() < 40) miniSize.setHeight(72);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Редактировать плеер"));
    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    auto *hint = new QLabel(tr(
        "Холст того же размера, что и в плеере. Потяните угол: обложка и воспроизведение сохраняют пропорции, "
        "остальные кнопки тянутся свободно. Правый клик по кнопке ставит текстуру PNG, по обложке — форму и рамку, по пустому месту — фон."));
    hint->setWordWrap(true);
    hint->setMaximumWidth(std::max(tabSize.width(), miniSize.width()));
    root->addWidget(hint);

    root->addWidget(new QLabel(tr("Вкладка")));
    m_panel->setCanvasEmbedded(false);
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

    const auto putCanvasesBack = [&]() {
        tab->setParent(nullptr);
        m_canvas->setParent(nullptr);
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
        m_panel->setCanvasEmbedded(true);
        m_panel->setEditMode(false);
        m_canvas->setEditMode(false);
    };
    struct Restore {
        std::function<void()> fn;
        ~Restore() { if (fn) fn(); }
    } restore{putCanvasesBack};

    m_panel->setEditMode(true);
    m_canvas->setEditMode(true);
    placeEditorDialog(dialog, content, this);
    dialog.exec();
    restore.fn();
    restore.fn = nullptr;
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
        const QString dir = libraryRootPath() + QStringLiteral("/icons");
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
        const QString dir = libraryRootPath() + QStringLiteral("/icons");
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
    syncLibraryCoverShape();
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
        const QString dir = libraryRootPath() + QStringLiteral("/icons");
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

void MainWindow::openEqualizer(QWidget *parent)
{
    QWidget *owner = parent ? parent : static_cast<QWidget *>(this);
    if (QDialog *existing = owner->findChild<QDialog *>(QStringLiteral("equalizerDialog"), Qt::FindDirectChildrenOnly)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    // show(), not exec() or open(). SettingsDialog::exec() is already running,
    // and a nested modal loop freezes the process on Windows. This window is a
    // child of settings, so closing settings deletes it; closing it leaves settings up.
    auto *dialog = new QDialog(owner);
    dialog->setObjectName(QStringLiteral("equalizerDialog"));
    dialog->setModal(false);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowTitle(tr("Эквалайзер"));
    dialog->resize(560, 280);
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
    connect(reset, &QPushButton::clicked, dialog, [sliders]() {
        for (QSlider *slider : sliders) slider->setValue(0);
    });
    auto *root = new QVBoxLayout(dialog);
    root->addLayout(columns);
    root->addWidget(reset, 0, Qt::AlignRight);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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
    armUpcoming();
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
    if (added > 0) refreshPlaylists();
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
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Новый плейлист"));
    dialog.setMinimumWidth(360);
    auto *name = new QLineEdit;
    auto *kind = new QComboBox;
    kind->addItem(PlaylistModel::categoryLabel(0), 0);
    kind->addItem(PlaylistModel::categoryLabel(3), 3);
    kind->addItem(PlaylistModel::categoryLabel(2), 2);
    kind->addItem(PlaylistModel::categoryLabel(1), 1);
    auto *form = new QFormLayout;
    form->addRow(tr("Название:"), name);
    form->addRow(tr("Категория:"), kind);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Создать"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (name->text().trimmed().isEmpty()) return;
    const qint64 id = m_db.createPlaylist(name->text().trimmed(), kind->currentData().toInt());
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
    if (!index.isValid() || m_playlistModel.isHeader(index.row())) return;
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
    if (m_gapTimer.isActive()) {
        m_gapTimer.stop();
        onTrackGapElapsed();
        return;
    }
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
    m_gapTimer.stop();
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
    armUpcoming();
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
    if (m_stopAfterTrack) {
        m_stopAfterTrack = false;
        m_gapTimer.stop();
        m_audio.clearNext();
        m_audio.pause();
        refreshSleepControls();
        m_playPauseItem->setPlaying(false);
        m_panel->setPlaying(false);
        m_coverItem->setPlaybackActive(false);
        return;
    }
    const int gapMs = int(std::lround(std::clamp(m_trackGapSec, 0.0, 10.0) * 1000.0));
    if (!m_gapless && gapMs > 0 && m_queue.upcoming()) {
        m_playPauseItem->setPlaying(false);
        m_panel->setPlaying(false);
        m_coverItem->setPlaybackActive(false);
        m_gapTimer.start(gapMs);
        return;
    }
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
        refreshPlaylists();
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
    if (!index.isValid() || m_playlistModel.isHeader(index.row())) return;
    const Playlist playlist = m_playlistModel.playlistAt(index.row());
    const qint64 playlistId = playlist.id;
    const QString name = playlist.name;

    QMenu menu(this);
    QAction *addTracks = menu.addAction(tr("Добавить треки…"));
    QAction *rename = menu.addAction(tr("Переименовать…"));
    QAction *setCover = menu.addAction(tr("Задать обложку плейлиста…"));
    QMenu *categoryMenu = menu.addMenu(tr("Категория"));
    const int shown = PlaylistModel::effectiveCategory(playlist);
    QAction *categoryActions[4] = {};
    const int categories[] = {3, 2, 1, 0};
    for (int i = 0; i < 4; ++i) {
        categoryActions[i] = categoryMenu->addAction(PlaylistModel::categoryLabel(categories[i]));
        categoryActions[i]->setCheckable(true);
        categoryActions[i]->setChecked(categories[i] == shown);
        categoryActions[i]->setData(categories[i]);
    }
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Удалить плейлист"));
    QAction *chosen = menu.exec(m_playlistView->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    for (QAction *action : categoryActions) {
        if (chosen != action) continue;
        m_db.setPlaylistCategory(playlistId, action->data().toInt());
        refreshPlaylists();
        return;
    }

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
    if (m_accent.isValid()) applyAccent(m_accent);
    syncLibraryCoverShape();
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
