#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

void fillRates(QComboBox *box)
{
    box->addItem(QObject::tr("Как у устройства"), 0);
    box->addItem(QObject::tr("44,1 кГц"), 44100);
    box->addItem(QObject::tr("48 кГц"), 48000);
    box->addItem(QObject::tr("96 кГц"), 96000);
}

void fillDepths(QComboBox *box)
{
    box->addItem(QObject::tr("32-бит float"), 0);
    box->addItem(QObject::tr("16 бит"), 16);
    box->addItem(QObject::tr("24 бит"), 24);
    box->addItem(QObject::tr("32 бит"), 32);
}

int indexOfData(QComboBox *box, int value)
{
    const int index = box->findData(value);
    return index >= 0 ? index : 0;
}

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color: #b7b1a6; background: transparent; border: none;"));
    return label;
}

QGroupBox *block(const QString &title)
{
    auto *box = new QGroupBox(title);
    auto *layout = new QVBoxLayout(box);
    layout->setSpacing(8);
    return box;
}

// QDoubleSpinBox normally accepts only the widget locale's decimal mark.
// Russian comma and a typed dot both have to become a pause in seconds.
class GapSecondsBox : public QDoubleSpinBox {
public:
    explicit GapSecondsBox(QWidget *parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setLocale(QLocale(QLocale::Russian));
        setRange(0.0, 10.0);
        setDecimals(2);
        setSingleStep(0.1);
        // Tracking would rewrite "0." into "0,00" before the rest of the
        // number is typed. Commit on focus-out so both 0.33 and 0,33 stick.
        setKeyboardTracking(false);
        setGroupSeparatorShown(false);
        setSuffix(QObject::tr(" с"));
    }

    QValidator::State validate(QString &input, int &pos) const override
    {
        Q_UNUSED(pos);
        const QString body = stripped(input);
        if (body.isEmpty())
            return QValidator::Intermediate;

        int separators = 0;
        for (const QChar ch : body) {
            if (ch == QLatin1Char('.') || ch == QLatin1Char(',')) {
                ++separators;
                continue;
            }
            if (!ch.isDigit())
                return QValidator::Invalid;
        }
        if (separators > 1)
            return QValidator::Invalid;

        const int dot = body.indexOf(QLatin1Char('.'));
        const int comma = body.indexOf(QLatin1Char(','));
        const int sep = dot >= 0 ? dot : comma;
        if (sep >= 0 && body.size() - sep - 1 > decimals())
            return QValidator::Invalid;
        if (sep == body.size() - 1)
            return QValidator::Intermediate;

        bool ok = false;
        const double number = QLocale::c().toDouble(normalized(body), &ok);
        if (!ok)
            return QValidator::Intermediate;
        if (number < minimum() || number > maximum())
            return QValidator::Invalid;
        return QValidator::Acceptable;
    }

    double valueFromText(const QString &text) const override
    {
        QString body = stripped(text);
        if (body.endsWith(QLatin1Char('.')) || body.endsWith(QLatin1Char(',')))
            body.chop(1);
        bool ok = false;
        const double number = QLocale::c().toDouble(normalized(body), &ok);
        if (!ok)
            return value();
        const double clamped = std::clamp(number, minimum(), maximum());
        return std::round(clamped * 100.0) / 100.0;
    }

    QString textFromValue(double val) const override
    {
        return QString::number(val, 'f', decimals()).replace(QLatin1Char('.'), QLatin1Char(','));
    }

private:
    QString stripped(const QString &text) const
    {
        QString body = text;
        const QString end = suffix();
        if (!end.isEmpty() && body.endsWith(end))
            body.chop(end.size());
        const QString head = prefix();
        if (!head.isEmpty() && body.startsWith(head))
            body.remove(0, head.size());
        return body.trimmed();
    }

    static QString normalized(QString body)
    {
        body.replace(QLatin1Char(','), QLatin1Char('.'));
        return body;
    }
};

} // namespace

SettingsDialog::SettingsDialog(int sampleRate, int bitDepth, bool gapless, double trackGapSec,
                               const QString &libraryPath, const UpdateStatus &update, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Настройки"));
    setMinimumWidth(460);
    setStyleSheet(QStringLiteral(
        "QGroupBox { border: 1px solid #2a2e3a; border-radius: 10px; margin-top: 12px; padding: 12px 10px 10px 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }"));

    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);

    auto *audio = block(tr("Аудио"));
    auto *audioLayout = qobject_cast<QVBoxLayout *>(audio->layout());
    audioLayout->addWidget(fieldLabel(tr("Частота дискретизации")));
    m_rate = new QComboBox;
    fillRates(m_rate);
    m_rate->setCurrentIndex(indexOfData(m_rate, sampleRate));
    audioLayout->addWidget(m_rate);
    audioLayout->addWidget(fieldLabel(tr("Глубина кодирования")));
    m_depth = new QComboBox;
    fillDepths(m_depth);
    m_depth->setCurrentIndex(indexOfData(m_depth, bitDepth));
    audioLayout->addWidget(m_depth);
    m_gaplessBox = new QCheckBox(tr("Стык без паузы"));
    m_gaplessBox->setChecked(gapless);
    m_gaplessBox->setCursor(Qt::PointingHandCursor);
    audioLayout->addWidget(m_gaplessBox);
    audioLayout->addWidget(fieldLabel(tr("Пауза между треками")));
    m_gap = new GapSecondsBox;
    if (trackGapSec < 0) trackGapSec = 0;
    if (trackGapSec > 10) trackGapSec = 10;
    trackGapSec = std::round(trackGapSec * 100.0) / 100.0;
    m_gap->setValue(trackGapSec);
    m_gap->setEnabled(!gapless);
    m_gap->setToolTip(tr("Пока включён стык без паузы, эта пауза не вставляется. Можно ввести 0,33 или 0.33"));
    m_gap->setStyleSheet(QStringLiteral(
        "QDoubleSpinBox { background: #1b1e27; color: #ece8e0; border: 1px solid #2a2e3a; border-radius: 6px; padding: 4px 8px; }"
        "QDoubleSpinBox:disabled { color: #6f6a62; }"));
    audioLayout->addWidget(m_gap);
    auto *eq = new QPushButton(tr("Эквалайзер…"));
    eq->setCursor(Qt::PointingHandCursor);
    audioLayout->addWidget(eq);
    connect(eq, &QPushButton::clicked, this, &SettingsDialog::equalizerRequested);
    root->addWidget(audio);

    auto emitOutput = [this]() {
        if (m_updating) return;
        emit outputChanged(m_rate->currentData().toInt(), m_depth->currentData().toInt());
    };
    connect(m_rate, &QComboBox::currentIndexChanged, this, emitOutput);
    connect(m_depth, &QComboBox::currentIndexChanged, this, emitOutput);
    connect(m_gaplessBox, &QCheckBox::toggled, this, [this](bool on) {
        m_gap->setEnabled(!on);
        if (!m_updating) emit gaplessChanged(on);
    });
    connect(m_gap, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        if (!m_updating) emit trackGapChanged(seconds);
    });

    auto *look = block(tr("Оформление"));
    auto *lookLayout = qobject_cast<QVBoxLayout *>(look->layout());
    auto *player = new QPushButton(tr("Редактировать плеер"));
    player->setCursor(Qt::PointingHandCursor);
    lookLayout->addWidget(player);
    connect(player, &QPushButton::clicked, this, &SettingsDialog::editPlayerRequested);
    auto *window = new QPushButton(tr("Редактировать окно"));
    window->setCursor(Qt::PointingHandCursor);
    lookLayout->addWidget(window);
    connect(window, &QPushButton::clicked, this, &SettingsDialog::editWindowRequested);
    auto *accent = new QPushButton(tr("Цвет кнопок и полос…"));
    accent->setCursor(Qt::PointingHandCursor);
    lookLayout->addWidget(accent);
    connect(accent, &QPushButton::clicked, this, &SettingsDialog::accentRequested);
    root->addWidget(look);

    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(12);

    auto *library = block(tr("Библиотека"));
    auto *libraryLayout = qobject_cast<QVBoxLayout *>(library->layout());
    m_libraryPath = new QLabel(libraryPath);
    m_libraryPath->setWordWrap(true);
    m_libraryPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_libraryPath->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    libraryLayout->addWidget(m_libraryPath);
    auto *browse = new QPushButton(tr("Изменить папку…"));
    browse->setCursor(Qt::PointingHandCursor);
    libraryLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::libraryChangeRequested);
    bottom->addWidget(library, 1);

    auto *hints = block(tr("Подсказки"));
    auto *hintsLayout = qobject_cast<QVBoxLayout *>(hints->layout());
    auto *tour = new QPushButton(tr("Показать подсказки"));
    tour->setCursor(Qt::PointingHandCursor);
    hintsLayout->addWidget(tour);
    hintsLayout->addStretch(1);
    connect(tour, &QPushButton::clicked, this, &SettingsDialog::tourRequested);
    bottom->addWidget(hints, 1);
    root->addLayout(bottom);

    auto *updates = block(tr("Обновления"));
    auto *updatesLayout = qobject_cast<QVBoxLayout *>(updates->layout());
    m_versionLabel = new QLabel;
    m_versionLabel->setWordWrap(true);
    m_versionLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    updatesLayout->addWidget(m_versionLabel);
    m_updateLabel = new QLabel;
    m_updateLabel->setWordWrap(true);
    updatesLayout->addWidget(m_updateLabel);
    auto *release = new QPushButton(tr("Открыть страницу релиза"));
    release->setCursor(Qt::PointingHandCursor);
    updatesLayout->addWidget(release);
    connect(release, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(m_updateStatus.pageUrl());
    });
    root->addWidget(updates);
    setUpdateStatus(update);

    auto *close = new QPushButton(tr("Закрыть"));
    close->setCursor(Qt::PointingHandCursor);
    root->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void SettingsDialog::setLibraryPath(const QString &path)
{
    m_libraryPath->setText(path);
}

void SettingsDialog::setOutput(int sampleRate, int bitDepth)
{
    m_updating = true;
    m_rate->setCurrentIndex(indexOfData(m_rate, sampleRate));
    m_depth->setCurrentIndex(indexOfData(m_depth, bitDepth));
    m_updating = false;
}

void SettingsDialog::setUpdateStatus(const UpdateStatus &status)
{
    m_updateStatus = status;
    const QString current = status.currentVersion.isEmpty()
        ? QString::fromLatin1(kSandPlayVersion)
        : status.currentVersion;
    m_versionLabel->setText(tr("Текущая версия: %1").arg(current));

    QString detail;
    bool highlight = false;
    switch (status.state) {
    case UpdateStatus::State::Checking:
        detail = tr("Проверка обновлений…");
        break;
    case UpdateStatus::State::UpdateAvailable:
        detail = tr("Доступна версия %1").arg(status.latestTag);
        highlight = true;
        break;
    case UpdateStatus::State::UpToDate:
        detail = tr("Новых версий нет");
        break;
    case UpdateStatus::State::Failed:
        detail = tr("Не удалось проверить обновления");
        break;
    }
    m_updateLabel->setText(detail);
    m_updateLabel->setStyleSheet(highlight
        ? QStringLiteral("color: #c9974b; background: transparent; border: none;")
        : QStringLiteral("color: #b7b1a6; background: transparent; border: none;"));
}
