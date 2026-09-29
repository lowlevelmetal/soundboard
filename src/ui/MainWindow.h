#pragma once

#include "Settings.h"
#include "audio/AudioEngine.h"
#include "platform/KeyPresser.h"

#include <QDateTime>
#include <QHash>
#include <QMainWindow>

#include <memory>

class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QSlider;
class QSplitter;
class QSystemTrayIcon;
class QTimer;
class QToolButton;

namespace sb {

class CategoryList;
class HotkeyManager;
class LevelMeter;
class SeekSlider;
class SoundFilterModel;
class SoundLibrary;
class SoundTableView;
struct Sound;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool startsHidden() const;
    void showAndRaise();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Construction
    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralWidget();
    QWidget *createPlaybackPanel();
    void createTray();
    void restoreWindowState();
    void saveWindowState();

    // Audio
    void openAudio();
    void applyOutputSettings();
    void setAudioProblems(const QStringList &problems);
    void showAudioProblems();

    // Playback
    void playSound(int soundId, bool fromHotkey = false);
    void playSelected();
    void playOrResume();
    void togglePause();
    void stopAll();
    void playRandom();
    void playAdjacent(int delta);
    void onVoiceFinished(VoiceId id, int soundId);
    void onVoicesChanged();
    void seekTo(int milliseconds);
    void tick();
    std::shared_ptr<const QByteArray> loadAudio(const Sound &sound, QString *error);

    // Library
    void addPaths(const QStringList &paths);
    void addFilesDialog();
    void addFolderDialog();
    void probeDurations(const QList<int> &ids);
    void removeSelected();
    void renameSelected();
    void editProperties(int soundId, bool focusHotkey);
    void clearHotkeyOfSelected();
    void showInFolder();
    void moveSelectedToCategory(int categoryId);
    void newCategory(const QList<int> &soundsToMove = {});
    void deleteCategory(int categoryId);
    void showTableMenu(const QPoint &pos);
    void showCategoryMenu(const QPoint &pos);
    void fillCategoryMenu(QMenu *menu);
    void updateReorderState();
    QList<int> selectedIds() const;
    int currentId() const;
    void selectSound(int soundId);

    // Hotkeys & push-to-talk
    void rebuildHotkeys();
    void onHotkeyActivated(const QString &id);
    void onHotkeyErrorsChanged();
    void applyPushToTalk();
    void pressPushToTalk();
    void releasePushToTalkSoon();

    // Recording
    void toggleRecording();
    void finishRecording();

    // Misc
    void showSettings();
    void showSetupHelp();
    void showAbout();
    void saveSettingsSoon();
    void quit();
    void shutdown();

    Settings m_settings;
    SoundLibrary *m_library = nullptr;
    SoundFilterModel *m_filter = nullptr;
    AudioEngine *m_engine = nullptr;
    HotkeyManager *m_hotkeys = nullptr;
    KeyPresser m_pushToTalk;
    QTimer *m_pushToTalkTimer = nullptr;
    QTimer *m_uiTimer = nullptr;
    QTimer *m_saveSettingsTimer = nullptr;

    CategoryList *m_categories = nullptr;
    SoundTableView *m_table = nullptr;
    QSplitter *m_splitter = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_nowPlaying = nullptr;
    SeekSlider *m_seek = nullptr;
    QLabel *m_time = nullptr;
    QToolButton *m_speakersButton = nullptr;
    QToolButton *m_microphoneButton = nullptr;
    QToolButton *m_voiceButton = nullptr;
    QSlider *m_speakerVolume = nullptr;
    QSlider *m_microphoneVolume = nullptr;
    QSlider *m_voiceVolume = nullptr;
    LevelMeter *m_speakerMeter = nullptr;
    LevelMeter *m_microphoneMeter = nullptr;
    LevelMeter *m_voiceMeter = nullptr;
    QToolButton *m_problemsButton = nullptr;
    QLabel *m_outputLabel = nullptr;
    QSystemTrayIcon *m_tray = nullptr;

    QAction *m_addFilesAction = nullptr;
    QAction *m_addFolderAction = nullptr;
    QAction *m_recordAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_randomAction = nullptr;
    QAction *m_loopAction = nullptr;
    QAction *m_renameAction = nullptr;
    QAction *m_hotkeyAction = nullptr;
    QAction *m_clearHotkeyAction = nullptr;
    QAction *m_propertiesAction = nullptr;
    QAction *m_showInFolderAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_setupHelpAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QMenu *m_moveToMenu = nullptr;

    QStringList m_audioProblems;
    VoiceId m_currentVoice = 0;
    QString m_recordingPath;
    bool m_virtualMicReady = false;
    bool m_quitting = false;
    bool m_shutDown = false;
    bool m_trayHintShown = false;

    struct CachedAudio {
        std::shared_ptr<const QByteArray> data;
        QDateTime modified;
    };
    QHash<QString, CachedAudio> m_audioCache;
    QStringList m_audioCacheOrder; // least recently used first
    qint64 m_audioCacheBytes = 0;
};

} // namespace sb
