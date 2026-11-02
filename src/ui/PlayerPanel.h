#pragma once

#include <QWidget>
#include <QColor>
#include <QList>
#include <QPixmap>
#include <optional>

#include "../audio/PlaybackQueue.h"
#include "../db/Database.h"
#include "../models/QueueModel.h"
#include "SkinCanvas.h"
#include "items/CoverArtItem.h"
#include "items/PlayPauseButtonItem.h"
#include "items/SeekBarItem.h"
#include "items/TextButtonItem.h"
#include "items/TrackLabelItem.h"
#include "items/VolumeBarItem.h"

class QLabel;
class QSplitter;
class QListView;
class QToolButton;

// Full-window player. The left side is a skin canvas (buttons keep the
// positions and colors the user chose). The queue stays on the right.
class PlayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlayerPanel(QWidget *parent = nullptr);

    SkinCanvas *canvas() const { return m_canvas; }
    QListView *queueView() const { return m_queueView; }
    QToolButton *sleepButton() const { return m_sleepButton; }

    void setTrack(const std::optional<Track> &track);
    void setCover(const QPixmap &pixmap);
    void setPlaying(bool playing);
    void setPosition(qint64 positionMs, qint64 durationMs);
    void setVolumePercent(int percent);
    void setShuffle(bool on);
    void setRepeatMode(PlaybackQueue::RepeatMode mode);
    void setAccent(const QColor &color);
    void setQueue(const QVector<Track> &tracks, int currentIndex);
    void setCoverStatus(const QString &text);
    void setQueueRatio(qreal ratio);
    qreal queueRatio() const { return m_queueRatio; }
    // Canvas size inside a player panel of the given size. Used when the
    // panel is still hidden and has not been laid out yet.
    QSize preferredCanvasSize(const QSize &panelSize) const;
    // The editor pulls the canvas out of the splitter and puts it back.
    void setCanvasEmbedded(bool embedded);
    void setEditMode(bool on);
    bool editMode() const;
    void setCoverShapeId(const QString &id);
    QString coverShapeId() const;
    void setCoverFramePath(const QString &path);
    void clearCoverFrame();
    bool hasCoverFrame() const;

    QList<qint64> takePendingDropIds();

signals:
    void closeRequested();
    void playPauseClicked();
    void previousClicked();
    void nextClicked();
    void seekRequested(qreal fraction01);
    void volumeChanged(int percent);
    void shuffleToggled(bool on);
    void repeatClicked();
    void queueActivated(int row);
    void queueRemoveRequested(int row);
    void queueReordered(int from, int to);
    void tracksDroppedOnQueue(int row);
    void skinLayoutChanged();
    void queueRatioChanged(qreal ratio);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyQueueChrome();
    void applyQueueRatio();
    void syncBackdrop();
    static QString formatTime(qint64 ms);

    QWidget *m_queuePane = nullptr;
    QSplitter *m_split = nullptr;
    QLabel *m_queueTitle = nullptr;
    QLabel *m_coverStatus = nullptr;
    QLabel *m_editHint = nullptr;
    QListView *m_queueView = nullptr;
    QLabel *m_queueEmpty = nullptr;
    QToolButton *m_sleepButton = nullptr;
    QWidget *m_queueCard = nullptr;

    SkinCanvas *m_canvas = nullptr;
    CoverArtItem *m_cover = nullptr;
    TrackLabelItem *m_label = nullptr;
    PlayPauseButtonItem *m_play = nullptr;
    SeekBarItem *m_seek = nullptr;
    TextButtonItem *m_previous = nullptr;
    TextButtonItem *m_next = nullptr;
    TextButtonItem *m_shuffle = nullptr;
    TextButtonItem *m_repeat = nullptr;
    VolumeBarItem *m_volumeBar = nullptr;

    QueueModel m_queueModel;
    QList<qint64> m_pendingDropIds;
    QColor m_accent{"#c9974b"};
    int m_volumePercent = 80;
    bool m_shuffleOn = false;
    qreal m_queueRatio = 0.30;
    bool m_applyingRatio = false;
};
