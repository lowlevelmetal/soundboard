#include "SoundPropertiesDialog.h"

#include "KeyCaptureEdit.h"
#include "audio/AudioEngine.h"
#include "library/SoundLibrary.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace sb {

SoundPropertiesDialog::SoundPropertiesDialog(SoundLibrary *library, int soundId, QWidget *parent)
    : QDialog(parent)
    , m_library(library)
    , m_soundId(soundId)
    , m_title(new QLineEdit(this))
    , m_path(new QLineEdit(this))
    , m_category(new QComboBox(this))
    , m_hotkey(new KeyCaptureEdit(this))
    , m_volume(new QSlider(Qt::Horizontal, this))
    , m_volumeLabel(new QLabel(this))
{
    setWindowTitle(tr("Sound Properties"));
    const Sound *sound = library->find(soundId);
    Q_ASSERT(sound);

    m_title->setText(sound->title);

    m_path->setText(QDir::toNativeSeparators(sound->path));
    m_path->setReadOnly(true);
    auto *browse = new QPushButton(tr("Change…"), this);
    connect(browse, &QPushButton::clicked, this, &SoundPropertiesDialog::browseFile);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);

    m_category->addItem(tr("Uncategorized"), 0);
    for (const Category &category : library->categories())
        m_category->addItem(category.name, category.id);
    m_category->setCurrentIndex(std::max(0, m_category->findData(sound->categoryId)));

    m_hotkey->setPortableText(sound->hotkey);

    m_volume->setRange(0, 200);
    m_volume->setSingleStep(5);
    m_volume->setPageStep(10);
    m_volume->setValue(sound->volume);
    auto updateVolumeLabel = [this](int value) { m_volumeLabel->setText(tr("%1%").arg(value)); };
    updateVolumeLabel(sound->volume);
    connect(m_volume, &QSlider::valueChanged, this, updateVolumeLabel);
    m_volumeLabel->setMinimumWidth(m_volumeLabel->fontMetrics().horizontalAdvance(QStringLiteral("200%")));
    auto *volumeRow = new QHBoxLayout;
    volumeRow->addWidget(m_volume, 1);
    volumeRow->addWidget(m_volumeLabel);

    auto *form = new QFormLayout;
    form->addRow(tr("&Title:"), m_title);
    auto *pathLabel = new QLabel(tr("&File:"), this);
    pathLabel->setBuddy(m_path);
    form->addRow(pathLabel, pathRow);
    form->addRow(tr("&Category:"), m_category);
    form->addRow(tr("&Hotkey:"), m_hotkey);
    auto *volumeLabel = new QLabel(tr("&Volume:"), this);
    volumeLabel->setBuddy(m_volume);
    form->addRow(volumeLabel, volumeRow);

    const QLocale locale;
    QString stats = tr("Duration: %1").arg(sound->durationMs >= 0
                                                ? SoundLibrary::formatDuration(sound->durationMs)
                                                : tr("unknown"));
    stats += QStringLiteral("  ·  ")
             + (sound->playCount == 1 ? tr("Played once")
                                      : tr("Played %1 times").arg(sound->playCount));
    if (sound->lastPlayedAt.isValid())
        stats += QStringLiteral("  ·  ")
                 + tr("last on %1").arg(locale.toString(sound->lastPlayedAt, QLocale::ShortFormat));
    auto *statsLabel = new QLabel(stats, this);
    statsLabel->setEnabled(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(statsLabel);
    layout->addWidget(buttons);
    resize(520, sizeHint().height());
}

void SoundPropertiesDialog::focusHotkey()
{
    m_hotkey->setFocus();
}

void SoundPropertiesDialog::browseFile()
{
    QStringList patterns;
    for (const QString &extension : AudioEngine::supportedExtensions())
        patterns << QStringLiteral("*.") + extension;
    const QString current = QDir::fromNativeSeparators(m_path->text());
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose Audio File"), QFileInfo(current).absolutePath(),
        tr("Audio files (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (!path.isEmpty())
        m_path->setText(QDir::toNativeSeparators(path));
}

void SoundPropertiesDialog::accept()
{
    const QString hotkey = m_hotkey->portableText();
    const int owner = m_library->findByHotkey(hotkey, m_soundId);
    if (owner != 0) {
        const Sound *other = m_library->find(owner);
        const auto answer = QMessageBox::question(
            this, tr("Hotkey In Use"),
            tr("%1 is already the hotkey of \"%2\". Move it to this sound?")
                .arg(m_hotkey->text(), other ? other->title : QString()));
        if (answer != QMessageBox::Yes)
            return;
        m_library->setHotkey(owner, QString());
    }

    const QString title = m_title->text().trimmed();
    if (!title.isEmpty())
        m_library->setTitle(m_soundId, title);
    const QString path = QDir::fromNativeSeparators(m_path->text());
    if (const Sound *sound = m_library->find(m_soundId); sound && sound->path != path)
        m_library->setPath(m_soundId, path);
    m_library->setCategory({m_soundId}, m_category->currentData().toInt());
    m_library->setHotkey(m_soundId, hotkey);
    m_library->setVolume(m_soundId, m_volume->value());
    QDialog::accept();
}

} // namespace sb
