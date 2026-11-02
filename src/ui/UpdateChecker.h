#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

#include "../AppVersion.h"

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

// One GitHub release check. currentVersion is the installed app, without a leading v.
struct UpdateStatus {
    enum class State {
        Checking,
        UpToDate,
        UpdateAvailable,
        Failed
    };

    // Installer is preferred. Zip is the portable archive, used only when the
    // release has no setup executable. None means the release listed neither.
    enum class AssetKind {
        None,
        Installer,
        Zip
    };

    State state = State::Checking;
    AssetKind assetKind = AssetKind::None;
    QString currentVersion;
    QString latestTag;
    QString releaseUrl;
    // Name and browser_download_url of the chosen asset. Empty when assetKind is None.
    // Taken from the release assets array, never from the release notes.
    QString assetName;
    QString assetUrl;

    // The release html_url when it points at this repository, otherwise the releases page.
    QUrl pageUrl() const;
};

Q_DECLARE_METATYPE(UpdateStatus)

// Asks GitHub which release is current. A newer release can be downloaded
// from that release's own asset. The running executable is never replaced.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    void start();
    // Downloads the asset chosen during the last successful check.
    void downloadUpdate();
    void cancelDownload();
    UpdateStatus status() const { return m_status; }

signals:
    void checkFinished(const UpdateStatus &status);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &filePath, bool installer);
    void downloadFailed(const QString &message);
    void downloadCanceled();

private:
    void finishRequest(QNetworkReply *reply);
    void startAssetGet(const QUrl &url);
    void takeAssetBytes(QNetworkReply *reply);
    void finishDownload(QNetworkReply *reply);
    void discardOutput();
    bool prepareOutput();
    QString outputDirectory() const;
    QString outputFileName() const;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_download = nullptr;
    QSaveFile *m_output = nullptr;
    QString m_outputPath;
    UpdateStatus m_status;
    bool m_started = false;
    bool m_cancelRequested = false;
    bool m_closing = false;
    bool m_writeError = false;
    int m_redirects = 0;
};
