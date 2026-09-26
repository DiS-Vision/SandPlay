#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

#include "db/Database.h"
#include "ui/SandPlayLogo.h"
#include "audio/AudioEngine.h"
#include "library/CoverCache.h"
#include "library/LibraryManager.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette theme = app.palette();
    const QColor selection(QStringLiteral("#3a3428"));
    const QColor selectionText(QStringLiteral("#f3efe6"));
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        theme.setColor(group, QPalette::Highlight, selection);
        theme.setColor(group, QPalette::HighlightedText, selectionText);
        theme.setColor(group, QPalette::Accent, selection);
    }
    app.setPalette(theme);
    // Keep this id stable: it is the AppData folder that already holds the library.
    QApplication::setApplicationName("music-player");
    QApplication::setApplicationDisplayName(QStringLiteral("SandPlay"));
    QApplication::setOrganizationName("local");
    QApplication::setWindowIcon(sandPlayLogoIcon());

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QSettings librarySettings;
    QString libraryRoot = librarySettings.value(QStringLiteral("library/root")).toString();
    if (libraryRoot.isEmpty() || !QDir(libraryRoot).exists())
        libraryRoot = appDataDir;
    QDir().mkpath(libraryRoot);

    Database db(QDir(libraryRoot).filePath(QStringLiteral("library.db")));
    if (!db.init()) {
        qFatal("could not initialize the library database at %s", qUtf8Printable(libraryRoot));
    }

    CoverCache covers(QDir(libraryRoot).filePath(QStringLiteral("covers")));
    LibraryManager library(db, covers);

    AudioEngine audio;
    const int sampleRate = db.getSetting(QStringLiteral("sample_rate"), QStringLiteral("0")).toInt();
    const int bitDepth = db.getSetting(QStringLiteral("bit_depth"), QStringLiteral("0")).toInt();
    audio.setOutputPreference(sampleRate, bitDepth);
    if (!audio.init()) {
        audio.setOutputPreference(0, 0);
        db.setSetting(QStringLiteral("sample_rate"), QStringLiteral("0"));
        db.setSetting(QStringLiteral("bit_depth"), QStringLiteral("0"));
        if (!audio.init())
            qWarning("audio device init failed — playback will not work, but the UI still runs");
    }

    MainWindow window(db, audio, library, covers);
    window.show();

    return app.exec();
}
