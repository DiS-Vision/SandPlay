#include "SettingsDialog.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

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

} // namespace

SettingsDialog::SettingsDialog(int sampleRate, int bitDepth, const QString &libraryPath, QWidget *parent)
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
