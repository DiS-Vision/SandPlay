#pragma once

#include <QDialog>

#include "UpdateChecker.h"

class QLabel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;

// Sound, looks, the library folder, hints, and the update check.
// Queue placement is edited with the player, not here.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int sampleRate, int bitDepth, bool gapless, double trackGapSec,
                   const QString &libraryPath, const UpdateStatus &update,
                   QWidget *parent = nullptr);

    void setLibraryPath(const QString &path);
    void setOutput(int sampleRate, int bitDepth);
    void setUpdateStatus(const UpdateStatus &status);

signals:
    void editPlayerRequested();
    void editWindowRequested();
    void accentRequested();
    void equalizerRequested();
    void outputChanged(int sampleRate, int bitDepth);
    void gaplessChanged(bool on);
    void trackGapChanged(double seconds);
    void libraryChangeRequested();
    void tourRequested();

private:
    QLabel *m_libraryPath = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_updateLabel = nullptr;
    QComboBox *m_rate = nullptr;
    QComboBox *m_depth = nullptr;
    QCheckBox *m_gaplessBox = nullptr;
    QDoubleSpinBox *m_gap = nullptr;
    UpdateStatus m_updateStatus;
    bool m_updating = false;
};
