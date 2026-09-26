#pragma once

#include <QDialog>

class QLabel;
class QComboBox;

// Sound, looks, the library folder, and the hint replay.
// Queue placement is edited with the player, not here.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int sampleRate, int bitDepth, const QString &libraryPath, QWidget *parent = nullptr);

    void setLibraryPath(const QString &path);
    void setOutput(int sampleRate, int bitDepth);

signals:
    void editPlayerRequested();
    void editWindowRequested();
    void accentRequested();
    void equalizerRequested();
    void outputChanged(int sampleRate, int bitDepth);
    void libraryChangeRequested();
    void tourRequested();

private:
    QLabel *m_libraryPath = nullptr;
    QComboBox *m_rate = nullptr;
    QComboBox *m_depth = nullptr;
    bool m_updating = false;
};
