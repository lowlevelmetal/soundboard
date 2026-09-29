#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSlider;

namespace sb {

class KeyCaptureEdit;
class SoundLibrary;

// Edits one sound: title, file, category, hotkey and volume.
class SoundPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    SoundPropertiesDialog(SoundLibrary *library, int soundId, QWidget *parent = nullptr);

    void focusHotkey();
    void accept() override;

private:
    void browseFile();

    SoundLibrary *m_library;
    int m_soundId;
    QLineEdit *m_title;
    QLineEdit *m_path;
    QComboBox *m_category;
    KeyCaptureEdit *m_hotkey;
    QSlider *m_volume;
    QLabel *m_volumeLabel;
};

} // namespace sb
