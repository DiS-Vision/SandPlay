#pragma once
#include <QList>
#include <QMainWindow>
#include <QStringList>
#include <QTableView>
#include <QListView>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QColor>
#include <QSplitter>

class QResizeEvent;
class QCloseEvent;
class QKeyEvent;
class QPropertyAnimation;
class SkinCanvas;
class DraggableItem;

#include "../db/Database.h"
#include "../audio/AudioEngine.h"
#include "../audio/PlaybackQueue.h"
#include "../library/LibraryManager.h"
#include "../library/CoverCache.h"
#include "../library/CoverFetcher.h"
#include "../models/TrackTableModel.h"
#include "../models/PlaylistModel.h"
#include "SkinCanvas.h"
#include "PlayerPanel.h"
#include "items/PlayPauseButtonItem.h"
#include "items/SeekBarItem.h"
#include "items/CoverArtItem.h"
#include "items/TrackLabelItem.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Database &db, AudioEngine &audio, LibraryManager &library,
               CoverCache &covers, QWidget *parent = nullptr);

private slots:
    void onAddTrackClicked();
    void onImportFolderClicked();
    void onNewPlaylistClicked();
    void onPlaylistSelected(const QModelIndex &index);
    void onTrackDoubleClicked(const QModelIndex &index);
    void onSearchChanged(const QString &text);

    void onPlayPauseClicked();
    void onSeekRequested(qreal fraction01);
    void onQueueCurrentChanged(std::optional<Track> track);

    void onAudioPositionChanged(qint64 ms);
    void onAudioTrackFinished();

    void onTrackContextMenuRequested(const QPoint &pos);
    void onPlaylistContextMenuRequested(const QPoint &pos);
    void onSetTrackCover(qint64 trackId);
    void onSetPlaylistCover(qint64 playlistId);

    void onSkinLayoutChanged();
    void onCoverReady(qint64 trackId, const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Database &m_db;
    AudioEngine &m_audio;
    LibraryManager &m_library;
    CoverCache &m_covers;
    PlaybackQueue m_queue;
    CoverFetcher m_fetcher;

    TrackTableModel m_trackModel;
    PlaylistModel m_playlistModel;
    qint64 m_activePlaylistId = -1;
    QColor m_accent{"#c9974b"};

    QTableView *m_trackView = nullptr;
    QListView *m_playlistView = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_showMore = nullptr;
    static constexpr int kLibraryPage = 200;
    int m_libraryShown = kLibraryPage;
    QSplitter *m_librarySplit = nullptr;
    bool m_panelOpen = false;
    bool m_capturing = false;
    bool m_splitRestored = false;
    QPropertyAnimation *m_panelAnim = nullptr;
    QWidget *m_slideGhost = nullptr;

    SkinCanvas *m_canvas = nullptr;
    PlayPauseButtonItem *m_playPauseItem = nullptr;
    SeekBarItem *m_seekItem = nullptr;
    CoverArtItem *m_coverItem = nullptr;
    TrackLabelItem *m_labelItem = nullptr;
    PlayerPanel *m_panel = nullptr;

    void refreshTracks(const QString &search = {}, bool keepScroll = false);
    void presentTracks(const QVector<Track> &tracks, int libraryTotal, bool keepScroll);
    bool handlePlayerKey(QKeyEvent *event);
    void onLibraryFoldersChanged(int added, const QList<qint64> &removedIds,
                                 const QList<qint64> &renamedIds, const QStringList &renamedPaths);
    void refreshPlaylists();
    void loadOrCreateDefaultSkin();
    void showLibrary();
    void setReorderable(bool reorderable);
    void togglePlayer();
    void setPlayerOpen(bool open);
    QRect playerTarget() const;
    void applyAccent(const QColor &color);
    void openLogoMenu();
    void openPlayerEditor();
    void configureItem(DraggableItem *item);
    void configureCanvas(SkinCanvas *canvas);
    void mirrorTabLook();
    void editTrackInfo(const Track &track);
    void deleteTracksFromDisk(const QVector<Track> &tracks);
    void adoptFirstTrackCovers();
    void addTracksFromLibrary(qint64 playlistId);
    void openEqualizer();
    void cycleRepeatMode();
    QVector<Track> selectedTracks() const;
    void addTracksToPlaylist(qint64 playlistId, const QList<qint64> &ids);
};
