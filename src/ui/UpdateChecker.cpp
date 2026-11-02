#include "UpdateChecker.h"

#include <optional>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// Tags compared here look like 0.1.0 or v0.1.0 (a single leading v/V is ignored).
// Two to four numeric dot-components are accepted. Missing components count as 0.
struct Version {
    int part[4] = {};
};

std::optional<Version> parseVersion(QString text)
{
    text = text.trimmed();
    if (!text.isEmpty()) {
        const QChar first = text.at(0);
        if (first == QLatin1Char('v') || first == QLatin1Char('V'))
            text.remove(0, 1);
    }

    const QStringList pieces = text.split(QLatin1Char('.'));
    if (pieces.size() < 2 || pieces.size() > 4)
        return std::nullopt;

    Version version;
    for (int index = 0; index < pieces.size(); ++index) {
        const QString piece = pieces.at(index);
        if (piece.isEmpty() || piece.size() > 9)
            return std::nullopt;
        for (const QChar ch : piece) {
            if (ch < QLatin1Char('0') || ch > QLatin1Char('9'))
                return std::nullopt;
        }
        bool ok = false;
        const int value = piece.toInt(&ok);
        if (!ok || value < 0)
            return std::nullopt;
        version.part[index] = value;
    }
    return version;
}

int compareVersion(const Version &left, const Version &right)
{
    for (int index = 0; index < 4; ++index) {
        if (left.part[index] == right.part[index]) continue;
        return left.part[index] < right.part[index] ? -1 : 1;
    }
    return 0;
}

bool isSafeAssetName(const QString &name)
{
    if (name.isEmpty() || name.size() > 180)
        return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name.contains(QLatin1String("..")) || name.startsWith(QLatin1Char('.')))
        return false;
    return true;
}

// SandPlay-setup.exe, or SandPlay-setup-<anything>.exe. Not any other executable.
bool isSetupInstallerName(const QString &name)
{
    if (!isSafeAssetName(name))
        return false;
    if (!name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        return false;
    const QString lower = name.toLower();
    const QString prefix = QStringLiteral("sandplay-setup");
    if (!lower.startsWith(prefix))
        return false;
    if (lower.size() == prefix.size() + 4)
        return true;
    return lower.size() > prefix.size() + 4 && lower.at(prefix.size()) == QLatin1Char('-');
}

bool isPortableZipName(const QString &name)
{
    return isSafeAssetName(name)
        && name.compare(QStringLiteral("SandPlay-windows-x64.zip"), Qt::CaseInsensitive) == 0;
}

// github.com only for this repo's release files. The CDN hosts are where GitHub
// redirects a browser_download_url; they are accepted only as that redirect.
bool isReleaseCdnHost(const QString &host)
{
    return host == QLatin1String("objects.githubusercontent.com")
        || host == QLatin1String("release-assets.githubusercontent.com")
        || host == QLatin1String("github-releases.githubusercontent.com");
}

bool httpsWithoutCredentials(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https"))
        return false;
    if (!url.userName().isEmpty() || !url.password().isEmpty())
        return false;
    if (url.port() != -1 && url.port() != 443)
        return false;
    return !url.host().isEmpty();
}

bool isAllowedDownloadTarget(const QUrl &url, bool allowCdn)
{
    if (!httpsWithoutCredentials(url))
        return false;
    const QString host = url.host().toLower();
    const QString path = url.path();
    if (path.contains(QLatin1String("..")) || path.contains(QLatin1Char('\\')))
        return false;
    if (host == QLatin1String("github.com") || host == QLatin1String("www.github.com")) {
        return path.startsWith(QStringLiteral("/DiS-Vision/SandPlay/releases/download/"),
                               Qt::CaseInsensitive);
    }
    return allowCdn && isReleaseCdnHost(host) && !path.isEmpty();
}

// The asset link GitHub puts on the release. The file name in the path must be
// the asset's own name, so a note in the release text cannot redirect this.
QUrl validatedReleaseAssetUrl(const QString &raw, const QString &assetName)
{
    const QUrl url(raw);
    if (!isAllowedDownloadTarget(url, false) || !isSafeAssetName(assetName))
        return {};
    const QString prefix = QStringLiteral("/DiS-Vision/SandPlay/releases/download/");
    const QString rest = url.path().mid(prefix.size());
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash <= 0 || rest.indexOf(QLatin1Char('/'), slash + 1) != -1)
        return {};
    const QString tag = rest.left(slash);
    const QString file = QUrl::fromPercentEncoding(rest.mid(slash + 1).toUtf8());
    if (tag.isEmpty() || tag.contains(QLatin1String("..")) || file != assetName)
        return {};
    return url;
}

bool contentTypeRejected(const QString &contentType)
{
    const QString mime = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (mime.isEmpty())
        return false;
    return mime.startsWith(QLatin1String("text/"))
        || mime == QLatin1String("application/json")
        || mime == QLatin1String("application/vnd.github+json");
}

bool payloadLooksRight(const QString &path, UpdateStatus::AssetKind kind)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    if (file.size() < 4096)
        return false;
    const QByteArray magic = file.read(2);
    if (magic.size() != 2)
        return false;
    if (kind == UpdateStatus::AssetKind::Installer)
        return magic.at(0) == 'M' && magic.at(1) == 'Z';
    if (kind == UpdateStatus::AssetKind::Zip)
        return magic.at(0) == 'P' && magic.at(1) == 'K';
    return false;
}

// Prefer SandPlay-setup.exe. Otherwise the portable zip published with the release.
// URLs are taken only from assets[].browser_download_url.
void selectReleaseAsset(const QJsonArray &assets, UpdateStatus &status)
{
    status.assetKind = UpdateStatus::AssetKind::None;
    status.assetName.clear();
    status.assetUrl.clear();

    int bestRank = 99;
    for (const QJsonValue &value : assets) {
        if (!value.isObject())
            continue;
        const QJsonObject asset = value.toObject();
        const QString state = asset.value(QStringLiteral("state")).toString();
        if (!state.isEmpty() && state != QLatin1String("uploaded"))
            continue;
        const QJsonValue sizeValue = asset.value(QStringLiteral("size"));
        if (sizeValue.isDouble() && sizeValue.toDouble() > 0 && sizeValue.toDouble() < 4096)
            continue;

        const QString name = asset.value(QStringLiteral("name")).toString().trimmed();
        int rank = -1;
        if (isSetupInstallerName(name))
            rank = name.compare(QStringLiteral("SandPlay-setup.exe"), Qt::CaseInsensitive) == 0 ? 0 : 1;
        else if (isPortableZipName(name))
            rank = 2;
        else
            continue;

        const QString rawUrl = asset.value(QStringLiteral("browser_download_url")).toString().trimmed();
        if (!validatedReleaseAssetUrl(rawUrl, name).isValid()) {
            qWarning("SandPlay update: ignored asset %s", qUtf8Printable(name));
            continue;
        }
        if (rank < bestRank) {
            bestRank = rank;
            status.assetName = name;
            status.assetUrl = rawUrl;
            status.assetKind = rank < 2 ? UpdateStatus::AssetKind::Installer
                                        : UpdateStatus::AssetKind::Zip;
            if (rank == 0)
                break;
        }
    }
}

constexpr qint64 kMaxDownloadBytes = 1024LL * 1024LL * 1024LL;

} // namespace

QUrl UpdateStatus::pageUrl() const
{
    const QUrl fallback(QStringLiteral("https://github.com/DiS-Vision/SandPlay/releases"));
    const QUrl parsed(releaseUrl);
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https"))
        return fallback;
    if (!parsed.userName().isEmpty() || !parsed.password().isEmpty())
        return fallback;
    const QString host = parsed.host().toLower();
    if (host != QLatin1String("github.com") && host != QLatin1String("www.github.com"))
        return fallback;
    const QString path = parsed.path();
    const QString prefix = QStringLiteral("/DiS-Vision/SandPlay");
    if (path.compare(prefix, Qt::CaseInsensitive) != 0
        && !path.startsWith(prefix + QLatin1Char('/'), Qt::CaseInsensitive))
        return fallback;
    return parsed;
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_status.currentVersion = QString::fromLatin1(kSandPlayVersion);
}

UpdateChecker::~UpdateChecker()
{
    m_closing = true;
    m_cancelRequested = true;
    if (m_download) {
        disconnect(m_download, nullptr, this, nullptr);
        m_download->abort();
        m_download->deleteLater();
        m_download = nullptr;
    }
    discardOutput();
}

void UpdateChecker::start()
{
    if (m_started) return;
    m_started = true;

    QNetworkRequest request{QUrl(QStringLiteral(
        "https://api.github.com/repos/DiS-Vision/SandPlay/releases/latest"))};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("SandPlay/") + QString::fromLatin1(kSandPlayVersion)
                          + QStringLiteral(" (update check)"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(20000);

    QNetworkReply *reply = m_network->get(request);
    if (!reply) {
        m_status.state = UpdateStatus::State::Failed;
        emit checkFinished(m_status);
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishRequest(reply); });
}

void UpdateChecker::finishRequest(QNetworkReply *reply)
{
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QString errorString = reply->errorString();
    const bool transportError = reply->error() != QNetworkReply::NoError && http == 0;
    reply->deleteLater();

    m_status.assetKind = UpdateStatus::AssetKind::None;
    m_status.assetName.clear();
    m_status.assetUrl.clear();
    m_status.latestTag.clear();
    m_status.releaseUrl.clear();

    if (transportError) {
        qWarning("SandPlay update check failed: %s", qUtf8Printable(errorString));
        m_status.state = UpdateStatus::State::Failed;
        emit checkFinished(m_status);
        return;
    }

    // /releases/latest does not return drafts or prereleases. 404 means nothing is published.
    if (http == 404) {
        m_status.state = UpdateStatus::State::UpToDate;
        emit checkFinished(m_status);
        return;
    }

    if (http != 200) {
        qWarning("SandPlay update check failed: HTTP %d", http);
        m_status.state = UpdateStatus::State::Failed;
        emit checkFinished(m_status);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("SandPlay update check failed: unexpected response");
        m_status.state = UpdateStatus::State::Failed;
        emit checkFinished(m_status);
        return;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("draft")).toBool()) {
        m_status.state = UpdateStatus::State::UpToDate;
        emit checkFinished(m_status);
        return;
    }

    const QString tag = object.value(QStringLiteral("tag_name")).toString().trimmed();
    const std::optional<Version> latest = parseVersion(tag);
    const std::optional<Version> current = parseVersion(m_status.currentVersion);
    if (!latest || !current) {
        qWarning("SandPlay update check failed: cannot compare tag %s", qUtf8Printable(tag));
        m_status.state = UpdateStatus::State::Failed;
        emit checkFinished(m_status);
        return;
    }

    m_status.latestTag = tag;
    m_status.releaseUrl = object.value(QStringLiteral("html_url")).toString();
    if (compareVersion(*latest, *current) > 0) {
        m_status.state = UpdateStatus::State::UpdateAvailable;
        selectReleaseAsset(object.value(QStringLiteral("assets")).toArray(), m_status);
        if (m_status.assetKind == UpdateStatus::AssetKind::None)
            qWarning("SandPlay update: release has no installer or windows zip");
    } else {
        m_status.state = UpdateStatus::State::UpToDate;
    }
    emit checkFinished(m_status);
}

QString UpdateChecker::outputDirectory() const
{
    if (m_status.assetKind == UpdateStatus::AssetKind::Zip) {
        const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (!downloads.isEmpty())
            return downloads;
        const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        if (!home.isEmpty())
            return home;
    }
    const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(temp).filePath(QStringLiteral("SandPlay-update"));
}

QString UpdateChecker::outputFileName() const
{
    if (m_status.assetKind == UpdateStatus::AssetKind::Zip)
        return QStringLiteral("SandPlay-windows-x64.zip");
    return QStringLiteral("SandPlay-setup.exe");
}

bool UpdateChecker::prepareOutput()
{
    if (m_output)
        return true;
    const QString dir = outputDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return false;
    m_outputPath = QDir(dir).filePath(outputFileName());
    auto *file = new QSaveFile(m_outputPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        m_outputPath.clear();
        return false;
    }
    m_output = file;
    return true;
}

void UpdateChecker::discardOutput()
{
    if (!m_output)
        return;
    m_output->cancelWriting();
    delete m_output;
    m_output = nullptr;
}

void UpdateChecker::downloadUpdate()
{
    if (m_closing || m_download)
        return;
    if (m_status.state != UpdateStatus::State::UpdateAvailable
        || m_status.assetKind == UpdateStatus::AssetKind::None
        || m_status.assetUrl.isEmpty()) {
        emit downloadFailed(tr("В этом выпуске нет файла для обновления."));
        return;
    }
    const QUrl url = validatedReleaseAssetUrl(m_status.assetUrl, m_status.assetName);
    if (!url.isValid()) {
        qWarning("SandPlay update download failed: asset URL rejected");
        emit downloadFailed(tr("В этом выпуске нет файла для обновления."));
        return;
    }
    m_cancelRequested = false;
    m_writeError = false;
    m_redirects = 0;
    startAssetGet(url);
}

void UpdateChecker::cancelDownload()
{
    if (!m_download)
        return;
    m_cancelRequested = true;
    m_download->abort();
}

void UpdateChecker::startAssetGet(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("SandPlay/") + QString::fromLatin1(kSandPlayVersion)
                          + QStringLiteral(" (update download)"));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(120000);

    QNetworkReply *reply = m_network->get(request);
    if (!reply) {
        discardOutput();
        if (!m_closing)
            emit downloadFailed(tr("Не удалось скачать обновление."));
        return;
    }
    m_download = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { takeAssetBytes(reply); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!m_closing)
            emit downloadProgress(received, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishDownload(reply); });
}

void UpdateChecker::takeAssetBytes(QNetworkReply *reply)
{
    if (!reply || reply != m_download || m_cancelRequested)
        return;
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (http != 200)
        return;
    if (!m_output && !prepareOutput()) {
        m_writeError = true;
        reply->abort();
        return;
    }
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
        return;
    if (m_output->write(chunk) != chunk.size() || m_output->size() > kMaxDownloadBytes) {
        m_writeError = true;
        reply->abort();
    }
}

void UpdateChecker::finishDownload(QNetworkReply *reply)
{
    if (reply != m_download) {
        reply->deleteLater();
        return;
    }

    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto netError = reply->error();
    const QString errorString = reply->errorString();

    if (m_cancelRequested) {
        m_download = nullptr;
        reply->deleteLater();
        discardOutput();
        m_cancelRequested = false;
        if (!m_closing)
            emit downloadCanceled();
        return;
    }

    if (http == 301 || http == 302 || http == 303 || http == 307 || http == 308) {
        QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (target.isRelative())
            target = reply->url().resolved(target);
        m_download = nullptr;
        reply->deleteLater();
        if (m_output) {
            // A redirect is not the file. Drop anything buffered before following it.
            discardOutput();
        }
        if (m_redirects >= 5 || !isAllowedDownloadTarget(target, true)) {
            qWarning("SandPlay update download failed: redirect refused");
            if (!m_closing)
                emit downloadFailed(tr("Не удалось скачать обновление."));
            return;
        }
        ++m_redirects;
        startAssetGet(target);
        return;
    }

    if (m_writeError || netError != QNetworkReply::NoError || http != 200) {
        qWarning("SandPlay update download failed: HTTP %d %s", http, qUtf8Printable(errorString));
        m_download = nullptr;
        reply->deleteLater();
        const bool couldNotSave = m_writeError;
        discardOutput();
        if (!m_closing) {
            emit downloadFailed(couldNotSave ? tr("Не удалось сохранить файл обновления.")
                                             : tr("Не удалось скачать обновление."));
        }
        return;
    }

    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QUrl finalUrl = reply->url();
    takeAssetBytes(reply);
    m_download = nullptr;
    reply->deleteLater();

    if (m_writeError || !m_output || contentTypeRejected(contentType)
        || !isAllowedDownloadTarget(finalUrl, true)) {
        qWarning("SandPlay update download failed: unexpected response");
        discardOutput();
        if (!m_closing)
            emit downloadFailed(tr("Не удалось скачать обновление."));
        return;
    }

    const UpdateStatus::AssetKind kind = m_status.assetKind;
    const QString path = m_outputPath;
    if (!m_output->commit()) {
        delete m_output;
        m_output = nullptr;
        QFile::remove(path);
        qWarning("SandPlay update download failed: could not save %s", qUtf8Printable(path));
        if (!m_closing)
            emit downloadFailed(tr("Не удалось сохранить файл обновления."));
        return;
    }
    delete m_output;
    m_output = nullptr;

    if (!payloadLooksRight(path, kind)) {
        QFile::remove(path);
        qWarning("SandPlay update download failed: file is not an installer or zip");
        if (!m_closing)
            emit downloadFailed(tr("Не удалось скачать обновление."));
        return;
    }

    if (!m_closing)
        emit downloadFinished(path, kind == UpdateStatus::AssetKind::Installer);
}
