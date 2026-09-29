#include "SettingsDialog.h"

#include "KeyCaptureEdit.h"
#include "platform/VirtualMic.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sb {

namespace {

// Fills a device combo: a first entry meaning "default/automatic", then the
// devices. A saved device that is currently missing is kept, marked as such.
void fillDevices(QComboBox *combo, const QString &firstLabel, const QStringList &devices,
                 const QString &current, const QStringList &exclude = {})
{
    combo->addItem(firstLabel, QString());
    for (const QString &device : devices) {
        if (!exclude.contains(device))
            combo->addItem(device, device);
    }
    if (!current.isEmpty() && combo->findData(current) < 0)
        combo->addItem(QCoreApplication::translate("SettingsDialog", "%1 (not connected)")
                           .arg(current),
                       current);
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
}

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.92);
    label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(const Settings &settings, const DeviceList &playback,
                               const DeviceList &capture, const QString &hotkeyStatus,
                               bool loopbackSupported, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Settings"));
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createAudioPage(playback, capture, loopbackSupported), tr("Audio"));
    tabs->addTab(createHotkeysPage(hotkeyStatus), tr("Playback && Hotkeys"));
    tabs->addTab(createPushToTalkPage(), tr("Push-to-Talk"));
    tabs->addTab(createGeneralPage(), tr("General"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    resize(620, sizeHint().height());
}

QWidget *SettingsDialog::createAudioPage(const DeviceList &playback, const DeviceList &capture,
                                         bool loopbackSupported)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    // Where sounds go.
    auto *outputs = new QGroupBox(tr("Outputs"), page);
    auto *outputForm = new QFormLayout(outputs);
    m_speakerDevice = new QComboBox(outputs);
    fillDevices(m_speakerDevice, tr("System default"), playback.names, m_settings.speakerDevice);
    outputForm->addRow(tr("&Speakers / headphones:"), m_speakerDevice);

    m_microphoneSinkDevice = new QComboBox(outputs);
    fillDevices(m_microphoneSinkDevice, tr("Automatic"), playback.names,
                m_settings.microphoneSinkDevice);
    outputForm->addRow(tr("&Microphone output:"), m_microphoneSinkDevice);

#ifdef Q_OS_LINUX
    m_createVirtualMic = new QCheckBox(
        tr("Create the \"%1\" virtual microphone").arg(virtualmic::sourceDescription()), outputs);
    m_createVirtualMic->setChecked(m_settings.createVirtualMicrophone);
    m_createVirtualMic->setEnabled(virtualmic::canCreate());
    m_removeVirtualMic = new QCheckBox(tr("Remove it when Soundboard quits"), outputs);
    m_removeVirtualMic->setChecked(m_settings.removeVirtualMicrophoneOnExit);
    connect(m_createVirtualMic, &QCheckBox::toggled, m_removeVirtualMic, &QWidget::setEnabled);
    m_removeVirtualMic->setEnabled(m_createVirtualMic->isChecked());
    outputForm->addRow(m_createVirtualMic);
    outputForm->addRow(m_removeVirtualMic);
    outputForm->addRow(hint(tr("\"Automatic\" plays into <b>%1</b>. In Discord, your game or any "
                               "other app, choose <b>%2</b> as the microphone.")
                                .arg(virtualmic::sinkDescription(),
                                     virtualmic::sourceDescription()),
                            outputs));
#else
    outputForm->addRow(hint(tr("The microphone output must be a virtual audio cable. Install "
                               "<a href=\"https://vb-audio.com/Cable/\">VB-CABLE</a>, pick "
                               "<b>CABLE Input</b> here (\"Automatic\" finds it) and choose "
                               "<b>CABLE Output</b> as the microphone in Discord or your game."),
                            outputs));
#endif
    layout->addWidget(outputs);

    // The user's real microphone.
    auto *voice = new QGroupBox(tr("Your voice"), page);
    auto *voiceForm = new QFormLayout(voice);
    const QStringList own = virtualmic::ownCaptureDevices(capture.names);
    m_inputDevice = new QComboBox(voice);
    fillDevices(m_inputDevice, tr("System default"), capture.names, m_settings.inputDevice, own);
    voiceForm->addRow(tr("Micro&phone:"), m_inputDevice);
    m_voicePassthrough = new QCheckBox(tr("Mix my microphone into the microphone output"), voice);
    m_voicePassthrough->setChecked(m_settings.voicePassthrough);
    voiceForm->addRow(m_voicePassthrough);
    m_muteVoice = new QCheckBox(tr("Mute my microphone while a sound is playing"), voice);
    m_muteVoice->setChecked(m_settings.muteVoiceWhilePlaying);
    voiceForm->addRow(m_muteVoice);
    voiceForm->addRow(hint(tr("Voice chat apps can only record one microphone, so Soundboard "
                              "passes your voice through together with the sounds."),
                           voice));
    layout->addWidget(voice);

    auto *recording = new QGroupBox(tr("Recording"), page);
    auto *recordingForm = new QFormLayout(recording);
    m_recordingDevice = new QComboBox(recording);
    fillDevices(m_recordingDevice, tr("Same as my microphone"), capture.names,
                m_settings.recordingDevice == QLatin1String(Settings::LoopbackDevice)
                    ? QString()
                    : m_settings.recordingDevice);
    if (loopbackSupported) {
        m_recordingDevice->insertItem(1, tr("What the speakers play"),
                                      QString::fromLatin1(Settings::LoopbackDevice));
        if (m_settings.recordingDevice == QLatin1String(Settings::LoopbackDevice))
            m_recordingDevice->setCurrentIndex(1);
    }
    recordingForm->addRow(tr("&Record from:"), m_recordingDevice);
    m_recordingsFolder = new QLineEdit(QDir::toNativeSeparators(m_settings.effectiveRecordingsFolder()),
                                       recording);
    auto *browse = new QPushButton(tr("Browse…"), recording);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, tr("Recordings Folder"), QDir::fromNativeSeparators(m_recordingsFolder->text()));
        if (!folder.isEmpty())
            m_recordingsFolder->setText(QDir::toNativeSeparators(folder));
    });
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_recordingsFolder, 1);
    folderRow->addWidget(browse);
    auto *folderLabel = new QLabel(tr("Save &to:"), recording);
    folderLabel->setBuddy(m_recordingsFolder);
    recordingForm->addRow(folderLabel, folderRow);
    layout->addWidget(recording);
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createHotkeysPage(const QString &hotkeyStatus)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *playback = new QGroupBox(tr("Playback"), page);
    auto *playbackForm = new QFormLayout(playback);
    m_allowOverlap = new QCheckBox(tr("Let several sounds play at the same time"), playback);
    m_allowOverlap->setChecked(m_settings.allowOverlap);
    playbackForm->addRow(m_allowOverlap);
    m_hotkeyBehaviour = new QComboBox(playback);
    m_hotkeyBehaviour->addItem(tr("Restarts it"), true);
    m_hotkeyBehaviour->addItem(tr("Stops it"), false);
    m_hotkeyBehaviour->setCurrentIndex(m_settings.hotkeyRestartsSound ? 0 : 1);
    playbackForm->addRow(tr("Hotkey of a sound that is playing:"), m_hotkeyBehaviour);
    layout->addWidget(playback);

    auto *global = new QGroupBox(tr("Global hotkeys"), page);
    auto *globalForm = new QFormLayout(global);
    auto makeEdit = [global](const QString &value) {
        auto *edit = new KeyCaptureEdit(global);
        edit->setPortableText(value);
        return edit;
    };
    m_stopHotkey = makeEdit(m_settings.stopHotkey);
    m_pauseHotkey = makeEdit(m_settings.pauseHotkey);
    m_randomHotkey = makeEdit(m_settings.randomHotkey);
    m_playSelectedHotkey = makeEdit(m_settings.playSelectedHotkey);
    globalForm->addRow(tr("&Stop all sounds:"), m_stopHotkey);
    globalForm->addRow(tr("&Pause / resume:"), m_pauseHotkey);
    globalForm->addRow(tr("Play a &random sound:"), m_randomHotkey);
    globalForm->addRow(tr("Play the se&lected sound:"), m_playSelectedHotkey);
    globalForm->addRow(hint(tr("Hotkeys for individual sounds are set from the sound list "
                               "(right-click a sound, or press Ctrl+K)."),
                            global));
    if (!hotkeyStatus.isEmpty())
        globalForm->addRow(hint(hotkeyStatus, global));
    layout->addWidget(global);
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createPushToTalkPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *group = new QGroupBox(tr("Push-to-talk"), page);
    auto *form = new QFormLayout(group);
    m_pttEnabled = new QCheckBox(tr("Hold a key down while a sound is playing"), group);
    m_pttEnabled->setChecked(m_settings.pushToTalkEnabled);
    form->addRow(m_pttEnabled);
    m_pttKey = new KeyCaptureEdit(group);
    m_pttKey->setAllowModifierOnly(true);
    m_pttKey->setPortableText(m_settings.pushToTalkKey);
    form->addRow(tr("&Key:"), m_pttKey);
    m_pttDelay = new QSpinBox(group);
    m_pttDelay->setRange(0, 5000);
    m_pttDelay->setSingleStep(50);
    m_pttDelay->setSuffix(tr(" ms"));
    m_pttDelay->setValue(m_settings.pushToTalkReleaseDelayMs);
    form->addRow(tr("&Release after:"), m_pttDelay);
    QString text = tr("Use this when your voice chat is set to push-to-talk: set the same key "
                      "here and the other players will hear the sounds without you pressing it.");
#ifdef Q_OS_LINUX
    text += QLatin1String("<br><br>")
            + tr("On Linux the key is pressed through a virtual keyboard (/dev/uinput). Apps "
                 "running under XWayland only see keys while they have focus; voice activation "
                 "avoids that limitation.");
#endif
    form->addRow(hint(text, group));
    auto updateEnabled = [this](bool on) {
        m_pttKey->setEnabled(on);
        m_pttDelay->setEnabled(on);
    };
    connect(m_pttEnabled, &QCheckBox::toggled, this, updateEnabled);
    updateEnabled(m_pttEnabled->isChecked());
    layout->addWidget(group);
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createGeneralPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *group = new QGroupBox(tr("Window"), page);
    auto *form = new QVBoxLayout(group);
    m_closeToTray = new QCheckBox(tr("Keep running in the system tray when the window is closed"),
                                  group);
    m_closeToTray->setChecked(m_settings.closeToTray);
    m_startMinimized = new QCheckBox(tr("Start minimized to the system tray"), group);
    m_startMinimized->setChecked(m_settings.startMinimized);
    form->addWidget(m_closeToTray);
    form->addWidget(m_startMinimized);
    layout->addWidget(group);
    layout->addStretch();
    return page;
}

Settings SettingsDialog::settings() const
{
    Settings s = m_settings;
    s.speakerDevice = m_speakerDevice->currentData().toString();
    s.microphoneSinkDevice = m_microphoneSinkDevice->currentData().toString();
    s.inputDevice = m_inputDevice->currentData().toString();
    s.recordingDevice = m_recordingDevice->currentData().toString();
    if (m_createVirtualMic) {
        s.createVirtualMicrophone = m_createVirtualMic->isChecked();
        s.removeVirtualMicrophoneOnExit = m_removeVirtualMic->isChecked();
    }
    s.voicePassthrough = m_voicePassthrough->isChecked();
    s.muteVoiceWhilePlaying = m_muteVoice->isChecked();

    s.allowOverlap = m_allowOverlap->isChecked();
    s.hotkeyRestartsSound = m_hotkeyBehaviour->currentData().toBool();
    s.stopHotkey = m_stopHotkey->portableText();
    s.pauseHotkey = m_pauseHotkey->portableText();
    s.randomHotkey = m_randomHotkey->portableText();
    s.playSelectedHotkey = m_playSelectedHotkey->portableText();

    s.pushToTalkEnabled = m_pttEnabled->isChecked();
    s.pushToTalkKey = m_pttKey->portableText();
    s.pushToTalkReleaseDelayMs = m_pttDelay->value();

    s.closeToTray = m_closeToTray->isChecked();
    s.startMinimized = m_startMinimized->isChecked();
    const QString folder = QDir::fromNativeSeparators(m_recordingsFolder->text().trimmed());
    s.recordingsFolder = folder == Settings().effectiveRecordingsFolder() ? QString() : folder;
    return s;
}

} // namespace sb
