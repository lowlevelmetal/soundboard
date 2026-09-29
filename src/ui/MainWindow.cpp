#include "MainWindow.h"

#include "CategoryList.h"
#include "Icons.h"
#include "KeyCaptureEdit.h"
#include "LevelMeter.h"
#include "SeekSlider.h"
#include "SettingsDialog.h"
#include "SoundPropertiesDialog.h"
#include "SoundTableView.h"
#include "hotkeys/HotkeyManager.h"
#include "library/SoundFilterModel.h"
#include "library/SoundLibrary.h"
#include "platform/VirtualMic.h"
#ifdef Q_OS_LINUX
#include "platform/DesktopEntry.h"
#endif

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace sb {

namespace {

constexpr qint64 kAudioCacheLimit = 256LL * 1024 * 1024;

const QString kSoundHotkeyPrefix = QStringLiteral("sound:");
const QString kStopHotkeyId = QStringLiteral("action:stop");
const QString kPauseHotkeyId = QStringLiteral("action:pause");
const QString kRandomHotkeyId = QStringLiteral("action:random");
const QString kPlaySelectedHotkeyId = QStringLiteral("action:playSelected");

bool isWaylandSession()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
           || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
}

// Sliders are linear percentages; loudness is perceived logarithmically.
float sliderGain(int percent)
{
    const float x = static_cast<float>(percent) / 100.0f;
    return x * x;
}

QString formatSeconds(double seconds)
{
    return SoundLibrary::formatDuration(static_cast<qint64>(std::max(0.0, seconds) * 1000.0));
}

QStringList audioFileFilters()
{
    QStringList patterns;
    for (const QString &extension : AudioEngine::supportedExtensions())
        patterns << QStringLiteral("*.") + extension;
    return patterns;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(Settings::load())
{
    setWindowTitle(tr("Soundboard"));

    const QString libraryPath = Settings::dataDirectory() + QLatin1String("/library.json");
    m_library = new SoundLibrary(libraryPath, this);
    QString error;
    if (!m_library->load(&error)) {
        // Keep the unreadable file so saving the (empty) list can't destroy it.
        const QString backup = libraryPath + QLatin1String(".broken-")
                               + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::copy(libraryPath, backup);
        QMessageBox::warning(this, tr("Sound List"),
                             tr("The sound list could not be read and a copy was saved as %1.\n\n%2")
                                 .arg(QDir::toNativeSeparators(backup), error));
    }
    m_filter = new SoundFilterModel(this);
    m_filter->setSourceModel(m_library);
    m_engine = new AudioEngine(this);

    m_pushToTalkTimer = new QTimer(this);
    m_pushToTalkTimer->setSingleShot(true);
    connect(m_pushToTalkTimer, &QTimer::timeout, this, [this] {
        if (!m_engine->isPlaying())
            m_pushToTalk.release();
    });
    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(50);
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::tick);
    m_saveSettingsTimer = new QTimer(this);
    m_saveSettingsTimer->setSingleShot(true);
    m_saveSettingsTimer->setInterval(500);
    connect(m_saveSettingsTimer, &QTimer::timeout, this, [this] { m_settings.save(); });

    createActions();
    createMenus();
    createToolBar();
    createCentralWidget();
    createTray();
    restoreWindowState();

    connect(m_engine, &AudioEngine::voiceFinished, this, &MainWindow::onVoiceFinished);
    connect(m_engine, &AudioEngine::voicesChanged, this, &MainWindow::onVoicesChanged);
    connect(m_engine, &AudioEngine::deviceStopped, this, [this](const QString &device) {
        const QString problem = tr("\"%1\" stopped working (unplugged or removed?). Choose "
                                   "Try Again once it is back.")
                                    .arg(device);
        if (!m_audioProblems.contains(problem))
            setAudioProblems(m_audioProblems + QStringList{problem});
        statusBar()->showMessage(problem, 10000);
    });
    if (m_engine->initialize(&error))
        openAudio();
    else
        setAudioProblems({error});

#ifdef Q_OS_LINUX
    if (isWaylandSession() && !desktop::ensureDesktopEntry(QStringLiteral(SOUNDBOARD_APP_ID), &error))
        qWarning("Could not write the desktop entry: %s", qPrintable(error));
#endif
    m_hotkeys = new HotkeyManager(QStringLiteral(SOUNDBOARD_APP_ID), this);
    connect(m_hotkeys, &HotkeyManager::activated, this, &MainWindow::onHotkeyActivated);
    connect(m_hotkeys, &HotkeyManager::errorsChanged, this, &MainWindow::onHotkeyErrorsChanged);
    connect(m_hotkeys, &HotkeyManager::statusChanged, this,
            [this](const QString &status) { statusBar()->showMessage(status, 10000); });
    connect(m_library, &SoundLibrary::hotkeysChanged, this, &MainWindow::rebuildHotkeys);
    // Global hotkeys would swallow the very keys a hotkey field wants to record.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (m_hotkeys)
            m_hotkeys->setSuspended(qobject_cast<KeyCaptureEdit *>(now) != nullptr);
    });
    rebuildHotkeys();
    if (!m_hotkeys->isAvailable())
        statusBar()->showMessage(m_hotkeys->status());

    applyPushToTalk();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutdown);

    QList<int> unknownDurations;
    for (const Sound &sound : m_library->sounds()) {
        if (sound.durationMs < 0 && !m_library->isMissing(sound.id))
            unknownDurations << sound.id;
    }
    probeDurations(unknownDurations);

    onVoicesChanged();
    m_uiTimer->start();

#ifdef Q_OS_WIN
    if (!m_settings.windowsCableHintShown && m_engine->sinkDeviceName(Sink::Microphone).isEmpty()) {
        m_settings.windowsCableHintShown = true;
        m_settings.save();
        QTimer::singleShot(0, this, &MainWindow::showSetupHelp);
    }
#endif
}

MainWindow::~MainWindow()
{
    shutdown();
}

bool MainWindow::startsHidden() const
{
    return m_settings.startMinimized && m_tray;
}

void MainWindow::showAndRaise()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::createActions()
{
    using icons::Glyph;
    auto make = [this](Glyph glyph, const QString &text, auto slot,
                       const QKeySequence &shortcut = QKeySequence()) {
        auto *action = new QAction(icons::icon(glyph), text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    m_addFilesAction = make(Glyph::Add, tr("&Add Sounds…"), &MainWindow::addFilesDialog,
                            QKeySequence(tr("Ctrl+O")));
    m_addFilesAction->setToolTip(tr("Add sound files to the list"));
    m_addFolderAction = make(Glyph::AddFolder, tr("Add &Folder…"), &MainWindow::addFolderDialog,
                             QKeySequence(tr("Ctrl+Shift+O")));
    m_addFolderAction->setToolTip(tr("Add every sound in a folder"));
    m_recordAction = make(Glyph::Record, tr("&Record"), &MainWindow::toggleRecording,
                          QKeySequence(tr("Ctrl+R")));
    m_recordAction->setCheckable(true);
    m_recordAction->setToolTip(tr("Record a new sound (Ctrl+R)"));
    m_settingsAction = make(Glyph::Settings, tr("&Settings…"), &MainWindow::showSettings,
                            QKeySequence(tr("Ctrl+,")));
    m_quitAction = make(Glyph::Quit, tr("&Quit"), &MainWindow::quit, QKeySequence(tr("Ctrl+Q")));

    m_playAction = make(Glyph::Play, tr("&Play"), &MainWindow::playOrResume);
    m_playAction->setToolTip(tr("Play the selected sound (Enter)"));
    m_pauseAction = make(Glyph::Pause, tr("P&ause"), &MainWindow::togglePause,
                         QKeySequence(Qt::Key_Space));
    m_pauseAction->setCheckable(true);
    m_pauseAction->setToolTip(tr("Pause or resume (Space)"));
    m_stopAction = make(Glyph::Stop, tr("&Stop"), &MainWindow::stopAll,
                        QKeySequence(Qt::Key_Escape));
    m_stopAction->setToolTip(tr("Stop all sounds (Esc)"));
    m_previousAction = make(Glyph::Previous, tr("P&revious"), [this] { playAdjacent(-1); },
                            QKeySequence(tr("Ctrl+Left")));
    m_nextAction = make(Glyph::Next, tr("&Next"), [this] { playAdjacent(1); },
                        QKeySequence(tr("Ctrl+Right")));
    m_randomAction = make(Glyph::Shuffle, tr("Play Ra&ndom Sound"), &MainWindow::playRandom,
                          QKeySequence(tr("Ctrl+Shift+R")));
    m_loopAction = make(Glyph::Loop, tr("&Loop"), [](bool) {}, QKeySequence(tr("Ctrl+L")));
    m_loopAction->setCheckable(true);
    m_loopAction->setChecked(m_settings.loop);
    m_loopAction->setToolTip(tr("Repeat sounds until stopped (Ctrl+L)"));
    connect(m_loopAction, &QAction::toggled, this, [this](bool on) {
        m_settings.loop = on;
        m_engine->setAllLooping(on);
        saveSettingsSoon();
    });

    m_renameAction = make(Glyph::Edit, tr("Re&name"), &MainWindow::renameSelected,
                          QKeySequence(Qt::Key_F2));
    m_hotkeyAction = make(Glyph::Keyboard, tr("Set &Hotkey…"),
                          [this] { editProperties(currentId(), true); },
                          QKeySequence(tr("Ctrl+K")));
    m_clearHotkeyAction = new QAction(tr("Remove Hot&key"), this);
    connect(m_clearHotkeyAction, &QAction::triggered, this, &MainWindow::clearHotkeyOfSelected);
    m_propertiesAction = make(Glyph::Info, tr("Pr&operties…"),
                              [this] { editProperties(currentId(), false); },
                              QKeySequence(tr("Alt+Return")));
    m_showInFolderAction = make(Glyph::Folder, tr("Show in &Folder"), &MainWindow::showInFolder);
    m_removeAction = make(Glyph::Remove, tr("Re&move from List"), &MainWindow::removeSelected,
                          QKeySequence(QKeySequence::Delete));
    m_findAction = make(Glyph::Search, tr("&Find"), [this] {
        m_search->setFocus();
        m_search->selectAll();
    }, QKeySequence(QKeySequence::Find));
    m_setupHelpAction = make(Glyph::Microphone, tr("&Microphone Setup…"), &MainWindow::showSetupHelp);
    m_aboutAction = make(Glyph::Info, tr("&About Soundboard"), &MainWindow::showAbout);

    m_moveToMenu = new QMenu(tr("Move to &Category"), this);
    m_moveToMenu->setIcon(icons::icon(Glyph::Tag));
    connect(m_moveToMenu, &QMenu::aboutToShow, this, [this] { fillCategoryMenu(m_moveToMenu); });
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_addFilesAction);
    file->addAction(m_addFolderAction);
    file->addSeparator();
    file->addAction(m_recordAction);
    file->addSeparator();
    file->addAction(m_settingsAction);
    file->addSeparator();
    file->addAction(m_quitAction);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_renameAction);
    edit->addAction(m_hotkeyAction);
    edit->addAction(m_clearHotkeyAction);
    edit->addMenu(m_moveToMenu);
    edit->addAction(m_propertiesAction);
    edit->addAction(m_showInFolderAction);
    edit->addSeparator();
    edit->addAction(m_removeAction);
    edit->addSeparator();
    edit->addAction(m_findAction);

    QMenu *playback = menuBar()->addMenu(tr("&Playback"));
    playback->addAction(m_playAction);
    playback->addAction(m_pauseAction);
    playback->addAction(m_stopAction);
    playback->addSeparator();
    playback->addAction(m_previousAction);
    playback->addAction(m_nextAction);
    playback->addAction(m_randomAction);
    playback->addSeparator();
    playback->addAction(m_loopAction);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_setupHelpAction);
    help->addSeparator();
    help->addAction(m_aboutAction);
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createToolBar()
{
    QToolBar *toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->setIconSize(QSize(18, 18));
    toolBar->addAction(m_addFilesAction);
    toolBar->addAction(m_addFolderAction);
    toolBar->addSeparator();
    toolBar->addAction(m_recordAction);

    auto *spacer = new QWidget(toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);

    m_search = new QLineEdit(toolBar);
    m_search->setPlaceholderText(tr("Search sounds"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(icons::icon(icons::Glyph::Search), QLineEdit::LeadingPosition);
    m_search->setMaximumWidth(320);
    m_search->setMinimumWidth(160);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_filter->setSearchText(text);
        updateReorderState();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        if (m_filter->rowCount() == 0)
            return;
        const int id = m_filter->index(0, 0).data(SoundLibrary::IdRole).toInt();
        selectSound(id);
        playSound(id);
    });
    toolBar->addWidget(m_search);
    toolBar->addSeparator();
    toolBar->addAction(m_settingsAction);
}

void MainWindow::createCentralWidget()
{
    m_categories = new CategoryList(this);
    m_categories->setCategories(m_library->categories());
    connect(m_library, &SoundLibrary::categoriesChanged, this, [this] {
        m_categories->setCategories(m_library->categories());
        // The selected category may be gone; the list falls back to "All sounds".
        m_filter->setCategory(m_categories->currentCategory());
    });
    connect(m_categories, &CategoryList::categorySelected, this,
            [this](int id) { m_filter->setCategory(id); });
    connect(m_categories, &CategoryList::soundsDropped, this,
            [this](const QList<int> &ids, int category) { m_library->setCategory(ids, category); });
    connect(m_categories, &CategoryList::categoryRenamed, m_library, &SoundLibrary::renameCategory);
    connect(m_categories, &QWidget::customContextMenuRequested, this, &MainWindow::showCategoryMenu);

    m_table = new SoundTableView(this);
    m_table->setModel(m_filter);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->sortByColumn(SoundLibrary::IndexColumn, Qt::AscendingOrder);
    // Editing shortcuts act on the list only, not on the search box or categories.
    for (QAction *action : {m_renameAction, m_hotkeyAction, m_propertiesAction, m_removeAction}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_table->addAction(action);
    }
    connect(m_table, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        playSound(index.data(SoundLibrary::IdRole).toInt());
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, &MainWindow::showTableMenu);
    connect(m_table, &SoundTableView::filesDropped, this, &MainWindow::addPaths);
    connect(m_table, &SoundTableView::soundsReordered, this,
            [this](const QList<int> &ids, int beforeId) {
                m_library->moveSounds(ids, beforeId);
                for (int id : ids)
                    m_table->selectionModel()->select(
                        m_filter->mapFromSource(m_library->index(m_library->rowOf(id), 0)),
                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
            });
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            &MainWindow::updateReorderState);
    auto updateSelectionActions = [this] {
        const bool any = !selectedIds().isEmpty();
        for (QAction *action : {m_renameAction, m_hotkeyAction, m_clearHotkeyAction,
                                m_propertiesAction, m_showInFolderAction, m_removeAction})
            action->setEnabled(any);
        m_moveToMenu->setEnabled(any);
    };
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            updateSelectionActions);
    connect(m_filter, &QAbstractItemModel::modelReset, this, updateSelectionActions);
    updateSelectionActions();

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_categories);
    m_splitter->addWidget(m_table);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({190, 810});

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter, 1);
    auto *line = new QFrame(central);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    layout->addWidget(createPlaybackPanel());
    setCentralWidget(central);

    m_outputLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_outputLabel);
    m_problemsButton = new QToolButton(this);
    m_problemsButton->setIcon(icons::icon(icons::Glyph::Warning));
    m_problemsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_problemsButton->setAutoRaise(true);
    m_problemsButton->hide();
    connect(m_problemsButton, &QToolButton::clicked, this, &MainWindow::showAudioProblems);
    statusBar()->addPermanentWidget(m_problemsButton);
}

QWidget *MainWindow::createPlaybackPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    auto toolButton = [panel](QAction *action) {
        auto *button = new QToolButton(panel);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setIconSize(QSize(22, 22));
        return button;
    };
    auto *transport = new QHBoxLayout;
    transport->setSpacing(2);
    for (QAction *action : {m_previousAction, m_playAction, m_pauseAction, m_stopAction,
                            m_nextAction})
        transport->addWidget(toolButton(action));
    transport->addSpacing(6);
    transport->addWidget(toolButton(m_loopAction));
    transport->addWidget(toolButton(m_randomAction));
    transport->addSpacing(10);

    m_nowPlaying = new QLabel(panel);
    m_nowPlaying->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_nowPlaying->setMinimumWidth(120);
    transport->addWidget(m_nowPlaying, 2);
    m_seek = new SeekSlider(panel);
    connect(m_seek, &SeekSlider::seekRequested, this, &MainWindow::seekTo);
    transport->addWidget(m_seek, 5);
    m_time = new QLabel(panel);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QFont timeFont = m_time->font();
    timeFont.setFeature(QFont::Tag("tnum"), 1); // tabular digits so the label doesn't jitter
    m_time->setFont(timeFont);
#endif
    m_time->setMinimumWidth(m_time->fontMetrics().horizontalAdvance(QStringLiteral("00:00 / 00:00")));
    m_time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    transport->addWidget(m_time);
    layout->addLayout(transport);

    auto *outputs = new QHBoxLayout;
    outputs->setSpacing(6);
    auto addOutput = [&](QToolButton *&toggle, icons::Glyph glyph, const QString &text,
                         const QString &tooltip, QSlider *&slider, int maximum,
                         LevelMeter *&meter) {
        toggle = new QToolButton(panel);
        toggle->setIcon(icons::toggleIcon(glyph));
        toggle->setText(text);
        toggle->setToolTip(tooltip);
        toggle->setCheckable(true);
        toggle->setAutoRaise(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        slider = new QSlider(Qt::Horizontal, panel);
        slider->setRange(0, maximum);
        slider->setSingleStep(2);
        slider->setPageStep(10);
        slider->setMinimumWidth(80);
        slider->setMaximumWidth(160);
        meter = new LevelMeter(panel);
        meter->setMaximumWidth(160);
        auto *column = new QVBoxLayout;
        column->setSpacing(1);
        column->addWidget(slider);
        column->addWidget(meter);
        outputs->addWidget(toggle);
        outputs->addLayout(column, 1);
        outputs->addSpacing(12);
    };
    addOutput(m_speakersButton, icons::Glyph::Speaker, tr("Speakers"),
              tr("Play sounds on your speakers or headphones"), m_speakerVolume, 100,
              m_speakerMeter);
    addOutput(m_microphoneButton, icons::Glyph::Microphone, tr("Microphone"),
              tr("Play sounds into the microphone other people hear"), m_microphoneVolume, 100,
              m_microphoneMeter);
    addOutput(m_voiceButton, icons::Glyph::Microphone, tr("My voice"),
              tr("Mix your real microphone into the soundboard microphone"), m_voiceVolume, 200,
              m_voiceMeter);
    outputs->addStretch();
    layout->addLayout(outputs);

    auto changed = [this] {
        m_settings.playToSpeakers = m_speakersButton->isChecked();
        m_settings.playToMicrophone = m_microphoneButton->isChecked();
        m_settings.speakerVolume = m_speakerVolume->value();
        m_settings.microphoneVolume = m_microphoneVolume->value();
        m_settings.voiceVolume = m_voiceVolume->value();
        applyOutputSettings();
        saveSettingsSoon();
    };
    for (QToolButton *toggle : {m_speakersButton, m_microphoneButton})
        connect(toggle, &QToolButton::toggled, this, changed);
    for (QSlider *slider : {m_speakerVolume, m_microphoneVolume, m_voiceVolume}) {
        connect(slider, &QSlider::valueChanged, this, changed);
        connect(slider, &QSlider::valueChanged, slider,
                [slider](int value) { slider->setToolTip(tr("Volume: %1%").arg(value)); });
    }
    connect(m_voiceButton, &QToolButton::toggled, this, [this](bool on) {
        m_settings.voicePassthrough = on;
        const QString problem = m_engine->setVoicePassthrough(on);
        if (!problem.isEmpty())
            statusBar()->showMessage(problem, 10000);
        applyOutputSettings();
        saveSettingsSoon();
    });
    return panel;
}

void MainWindow::createTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(icons::appIcon(), this);
    m_tray->setToolTip(tr("Soundboard"));
    auto *menu = new QMenu(this);
    menu->addAction(tr("Show Soundboard"), this, &MainWindow::showAndRaise);
    menu->addSeparator();
    menu->addAction(m_stopAction);
    menu->addAction(m_pauseAction);
    menu->addAction(m_randomAction);
    menu->addSeparator();
    menu->addAction(m_quitAction);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                    return;
                if (isVisible() && !isMinimized() && isActiveWindow())
                    hide();
                else
                    showAndRaise();
            });
    m_tray->show();
}

void MainWindow::restoreWindowState()
{
    if (!restoreGeometry(Settings::uiState(QStringLiteral("geometry"))))
        resize(1000, 640);
    restoreState(Settings::uiState(QStringLiteral("windowState")));
    const QByteArray splitter = Settings::uiState(QStringLiteral("splitter"));
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);

    QHeaderView *header = m_table->horizontalHeader();
    const QByteArray headerState = Settings::uiState(QStringLiteral("header"));
    if (headerState.isEmpty() || !header->restoreState(headerState)) {
        header->resizeSection(SoundLibrary::IndexColumn, 44);
        header->resizeSection(SoundLibrary::TitleColumn, 280);
        header->resizeSection(SoundLibrary::HotkeyColumn, 110);
        header->resizeSection(SoundLibrary::DurationColumn, 70);
        header->resizeSection(SoundLibrary::CategoryColumn, 110);
        header->resizeSection(SoundLibrary::PlaysColumn, 60);
        header->resizeSection(SoundLibrary::AddedColumn, 90);
    }
    m_table->sortByColumn(header->sortIndicatorSection(), header->sortIndicatorOrder());
    updateReorderState();
}

void MainWindow::saveWindowState()
{
    Settings::setUiState(QStringLiteral("geometry"), saveGeometry());
    Settings::setUiState(QStringLiteral("windowState"), saveState());
    Settings::setUiState(QStringLiteral("splitter"), m_splitter->saveState());
    Settings::setUiState(QStringLiteral("header"), m_table->horizontalHeader()->saveState());
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void MainWindow::openAudio()
{
    QStringList problems;
#ifdef Q_OS_LINUX
    if (m_settings.createVirtualMicrophone) {
        QString error;
        m_virtualMicReady = virtualmic::create(&error);
        if (!m_virtualMicReady)
            problems << tr("The virtual microphone could not be created: %1").arg(error);
    }
#endif

    const DeviceList playback = m_engine->playbackDevices();
    const DeviceList capture = m_engine->captureDevices();
    AudioConfig config;
    config.speakerDevice = m_settings.speakerDevice;
    config.microphoneSinkDevice = m_settings.microphoneSinkDevice.isEmpty()
                                      ? virtualmic::findCableDevice(playback.names)
                                      : m_settings.microphoneSinkDevice;
    config.inputDevice = m_settings.inputDevice;
    config.voicePassthrough = m_settings.voicePassthrough;
    config.avoidInputDevices = virtualmic::ownCaptureDevices(capture.names);
    if (config.microphoneSinkDevice.isEmpty()) {
#ifdef Q_OS_LINUX
        problems << tr("There is no microphone output, so nobody else can hear the sounds. Turn "
                       "on the virtual microphone or pick a device in Settings → Audio.");
#else
        problems << tr("No virtual audio cable was found, so nobody else can hear the sounds. "
                       "See Help → Microphone Setup.");
#endif
    }
    problems += m_engine->open(config);
    applyOutputSettings();
    setAudioProblems(problems);

    const QString microphone = m_engine->sinkDeviceName(Sink::Microphone);
    m_outputLabel->setText(microphone.isEmpty()
                               ? tr("Microphone output: none")
                               : tr("Microphone output: %1").arg(microphone));
#ifdef Q_OS_LINUX
    if (m_virtualMicReady)
        m_outputLabel->setToolTip(tr("Select \"%1\" as the microphone in Discord, games and other "
                                     "apps.")
                                      .arg(virtualmic::sourceDescription()));
#endif
}

void MainWindow::applyOutputSettings()
{
    m_engine->setSinkEnabled(Sink::Speakers, m_settings.playToSpeakers);
    m_engine->setSinkVolume(Sink::Speakers, sliderGain(m_settings.speakerVolume));
    m_engine->setSinkEnabled(Sink::Microphone, m_settings.playToMicrophone);
    m_engine->setSinkVolume(Sink::Microphone, sliderGain(m_settings.microphoneVolume));
    m_engine->setVoiceVolume(sliderGain(m_settings.voiceVolume));
    m_engine->setMuteVoiceWhilePlaying(m_settings.muteVoiceWhilePlaying);

    const QSignalBlocker b1(m_speakersButton), b2(m_microphoneButton), b3(m_voiceButton);
    const QSignalBlocker b4(m_speakerVolume), b5(m_microphoneVolume), b6(m_voiceVolume);
    m_speakersButton->setChecked(m_settings.playToSpeakers);
    m_microphoneButton->setChecked(m_settings.playToMicrophone);
    m_voiceButton->setChecked(m_settings.voicePassthrough);
    m_speakerVolume->setValue(m_settings.speakerVolume);
    m_microphoneVolume->setValue(m_settings.microphoneVolume);
    m_voiceVolume->setValue(m_settings.voiceVolume);
    for (QSlider *slider : {m_speakerVolume, m_microphoneVolume, m_voiceVolume})
        slider->setToolTip(tr("Volume: %1%").arg(slider->value()));

    const bool micOpen = m_engine->isSinkOpen(Sink::Microphone);
    m_microphoneButton->setEnabled(micOpen);
    m_microphoneVolume->setEnabled(micOpen && m_settings.playToMicrophone);
    m_voiceButton->setEnabled(micOpen);
    m_voiceVolume->setEnabled(micOpen && m_settings.voicePassthrough);
    m_speakerVolume->setEnabled(m_settings.playToSpeakers);
}

void MainWindow::setAudioProblems(const QStringList &problems)
{
    m_audioProblems = problems;
    m_problemsButton->setVisible(!problems.isEmpty());
    m_problemsButton->setText(problems.size() == 1
                                  ? tr("Audio problem")
                                  : tr("%1 audio problems").arg(problems.size()));
    m_problemsButton->setToolTip(problems.join(QLatin1Char('\n')));
}

void MainWindow::showAudioProblems()
{
    if (m_audioProblems.isEmpty())
        return;
    QMessageBox box(QMessageBox::Warning, tr("Audio Problems"),
                    m_audioProblems.join(QLatin1String("\n\n")), QMessageBox::Close, this);
    QPushButton *settings = box.addButton(tr("Open Settings…"), QMessageBox::ActionRole);
    QPushButton *retry = box.addButton(tr("Try Again"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == settings)
        showSettings();
    else if (box.clickedButton() == retry)
        openAudio();
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

std::shared_ptr<const QByteArray> MainWindow::loadAudio(const Sound &sound, QString *error)
{
    const QFileInfo info(sound.path);
    const QDateTime modified = info.lastModified();
    auto it = m_audioCache.find(sound.path);
    if (it != m_audioCache.end() && it->modified == modified) {
        m_audioCacheOrder.removeOne(sound.path);
        m_audioCacheOrder.append(sound.path);
        return it->data;
    }

    QFile file(sound.path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return nullptr;
    }
    auto data = std::make_shared<const QByteArray>(file.readAll());

    if (it != m_audioCache.end()) {
        m_audioCacheBytes -= it->data->size();
        m_audioCacheOrder.removeOne(sound.path);
    }
    m_audioCache.insert(sound.path, {data, modified});
    m_audioCacheOrder.append(sound.path);
    m_audioCacheBytes += data->size();
    while (m_audioCacheBytes > kAudioCacheLimit && m_audioCacheOrder.size() > 1) {
        const QString oldest = m_audioCacheOrder.takeFirst();
        m_audioCacheBytes -= m_audioCache.value(oldest).data->size();
        m_audioCache.remove(oldest);
    }
    return data;
}

void MainWindow::playSound(int soundId, bool fromHotkey)
{
    const Sound *found = m_library->find(soundId);
    if (!found)
        return;
    const Sound sound = *found;

    QList<VoiceId> sameSound;
    for (VoiceId voice : m_engine->voices()) {
        if (m_engine->soundId(voice) == soundId)
            sameSound << voice;
    }
    if (fromHotkey && !m_settings.hotkeyRestartsSound && !sameSound.isEmpty()) {
        for (VoiceId voice : std::as_const(sameSound))
            m_engine->stop(voice);
        return;
    }
    if (!m_settings.allowOverlap)
        m_engine->stopAll();
    else
        for (VoiceId voice : std::as_const(sameSound))
            m_engine->stop(voice);

    QString error;
    const auto data = loadAudio(sound, &error);
    VoiceId voice = 0;
    if (data)
        voice = m_engine->play(soundId, data, static_cast<float>(sound.volume) / 100.0f,
                               m_settings.loop, &error);
    if (!voice) {
        statusBar()->showMessage(tr("Can't play \"%1\": %2").arg(sound.title, error), 10000);
        m_library->refreshMissing();
        return;
    }
    m_currentVoice = voice;
    m_library->recordPlay(soundId);
    pressPushToTalk();
    onVoicesChanged();
}

void MainWindow::playSelected()
{
    int id = currentId();
    if (id == 0 && m_filter->rowCount() > 0) {
        id = m_filter->index(0, 0).data(SoundLibrary::IdRole).toInt();
        selectSound(id);
    }
    if (id != 0)
        playSound(id);
}

void MainWindow::playOrResume()
{
    if (m_currentVoice && m_engine->isPaused(m_currentVoice))
        togglePause();
    else
        playSelected();
}

void MainWindow::togglePause()
{
    if (!m_currentVoice || !m_engine->isActive(m_currentVoice)) {
        onVoicesChanged();
        return;
    }
    const bool pause = !m_engine->isPaused(m_currentVoice);
    m_engine->setAllPaused(pause);
    if (pause)
        m_pushToTalk.release();
    else
        pressPushToTalk();
}

void MainWindow::stopAll()
{
    m_engine->stopAll();
}

void MainWindow::playRandom()
{
    const int count = m_filter->rowCount();
    if (count == 0)
        return;
    const int playing = m_engine->soundId(m_currentVoice);
    int id = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const int row = static_cast<int>(QRandomGenerator::global()->bounded(count));
        id = m_filter->index(row, 0).data(SoundLibrary::IdRole).toInt();
        if (id != playing || count == 1)
            break;
    }
    selectSound(id);
    playSound(id);
}

void MainWindow::playAdjacent(int delta)
{
    int row = -1;
    const int playing = m_engine->soundId(m_currentVoice);
    if (playing > 0) {
        row = m_filter->mapFromSource(m_library->index(m_library->rowOf(playing), 0)).row();
    }
    if (row < 0)
        row = m_table->currentIndex().row();
    const int target = row < 0 ? (delta > 0 ? 0 : m_filter->rowCount() - 1) : row + delta;
    if (target < 0 || target >= m_filter->rowCount())
        return;
    const int id = m_filter->index(target, 0).data(SoundLibrary::IdRole).toInt();
    selectSound(id);
    playSound(id);
}

void MainWindow::onVoiceFinished(VoiceId id, int)
{
    if (id == m_currentVoice) {
        const QList<VoiceId> voices = m_engine->voices();
        m_currentVoice = voices.isEmpty() ? 0 : voices.last();
    }
    if (m_engine->voices().isEmpty())
        releasePushToTalkSoon();
}

void MainWindow::onVoicesChanged()
{
    QSet<int> playing;
    const QList<VoiceId> voices = m_engine->voices();
    for (VoiceId voice : voices)
        playing.insert(m_engine->soundId(voice));
    m_library->setPlaying(playing);

    if (m_currentVoice && !m_engine->isActive(m_currentVoice))
        m_currentVoice = voices.isEmpty() ? 0 : voices.last();
    const bool active = m_currentVoice != 0;
    const bool paused = active && m_engine->isPaused(m_currentVoice);
    m_pauseAction->setEnabled(active);
    m_pauseAction->setChecked(paused);
    m_stopAction->setEnabled(!voices.isEmpty());
    m_seek->setEnabled(active);

    if (!active) {
        m_nowPlaying->setText(tr("Nothing playing"));
        m_nowPlaying->setToolTip(QString());
        m_nowPlaying->setEnabled(false);
        const QSignalBlocker blocker(m_seek);
        m_seek->setRange(0, 0);
        m_time->setText(QString());
    } else {
        const Sound *sound = m_library->find(m_engine->soundId(m_currentVoice));
        const QString title = sound ? sound->title : QString();
        const QString prefix = paused ? tr("Paused: ") : QString();
        m_nowPlaying->setEnabled(true);
        m_nowPlaying->setText(m_nowPlaying->fontMetrics().elidedText(
            prefix + title, Qt::ElideRight, std::max(60, m_nowPlaying->width())));
        m_nowPlaying->setToolTip(title);
    }
    tick();
}

void MainWindow::seekTo(int milliseconds)
{
    if (m_currentVoice)
        m_engine->seek(m_currentVoice, milliseconds / 1000.0);
}

void MainWindow::tick()
{
    if (m_currentVoice && m_engine->isActive(m_currentVoice)) {
        const double position = m_engine->position(m_currentVoice);
        const double duration = m_engine->duration(m_currentVoice);
        if (!m_seek->isSliderDown()) {
            const QSignalBlocker blocker(m_seek);
            m_seek->setRange(0, static_cast<int>(duration * 1000.0));
            m_seek->setValue(static_cast<int>(position * 1000.0));
        }
        m_time->setText(formatSeconds(position) + QLatin1String(" / ") + formatSeconds(duration));
    }
    if (isVisible()) {
        m_speakerMeter->setLevel(m_engine->takeSinkLevel(Sink::Speakers));
        m_microphoneMeter->setLevel(m_engine->takeSinkLevel(Sink::Microphone));
        m_voiceMeter->setLevel(m_engine->takeInputLevel());
    }
    if (m_engine->isRecording())
        statusBar()->showMessage(tr("Recording… %1  (Ctrl+R to stop)")
                                     .arg(formatSeconds(m_engine->recordingSeconds())));
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------

void MainWindow::addPaths(const QStringList &paths)
{
    const QStringList extensions = AudioEngine::supportedExtensions();
    const QStringList filters = audioFileFilters();
    QStringList files;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QStringList found;
            QDirIterator it(path, filters, QDir::Files | QDir::Readable,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
            while (it.hasNext())
                found << it.next();
            found.sort(Qt::CaseInsensitive);
            files += found;
        } else if (extensions.contains(info.suffix().toLower())) {
            files << path;
        }
    }
    if (files.isEmpty()) {
        statusBar()->showMessage(tr("No supported audio files found (%1).")
                                     .arg(extensions.join(QLatin1String(", "))),
                                 8000);
        return;
    }
    int skipped = 0;
    const QList<int> ids =
        m_library->addFiles(files, std::max(0, m_categories->currentCategory()), &skipped);
    probeDurations(ids);
    QString message = ids.size() == 1 ? tr("Added 1 sound.") : tr("Added %1 sounds.").arg(ids.size());
    if (skipped > 0)
        message += QLatin1Char(' ')
                   + (skipped == 1 ? tr("1 was already in the list.")
                                   : tr("%1 were already in the list.").arg(skipped));
    statusBar()->showMessage(message, 6000);
    if (!ids.isEmpty())
        selectSound(ids.last());
}

void MainWindow::addFilesDialog()
{
    const QString directory = QString::fromUtf8(Settings::uiState(QStringLiteral("lastDirectory")));
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Sounds"), directory,
        tr("Audio files (%1)").arg(audioFileFilters().join(QLatin1Char(' '))) + QLatin1String(";;")
            + tr("All files (*)"));
    if (files.isEmpty())
        return;
    Settings::setUiState(QStringLiteral("lastDirectory"),
                         QFileInfo(files.first()).absolutePath().toUtf8());
    addPaths(files);
}

void MainWindow::addFolderDialog()
{
    const QString directory = QString::fromUtf8(Settings::uiState(QStringLiteral("lastDirectory")));
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add Folder"), directory);
    if (folder.isEmpty())
        return;
    Settings::setUiState(QStringLiteral("lastDirectory"), folder.toUtf8());
    addPaths({folder});
}

void MainWindow::probeDurations(const QList<int> &ids)
{
    for (int id : ids) {
        const Sound *sound = m_library->find(id);
        if (!sound)
            continue;
        const QString path = sound->path;
        SoundLibrary *library = m_library;
        QThreadPool::globalInstance()->start([library, id, path] {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return;
            double seconds = 0.0;
            if (!AudioEngine::probeDuration(file.readAll(), &seconds, nullptr))
                return;
            QMetaObject::invokeMethod(
                library, [library, id, seconds] { library->setDuration(id, qRound64(seconds * 1000)); },
                Qt::QueuedConnection);
        });
    }
}

QList<int> MainWindow::selectedIds() const
{
    QList<int> ids;
    QModelIndexList rows = m_table->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &index : std::as_const(rows))
        ids << index.data(SoundLibrary::IdRole).toInt();
    return ids;
}

int MainWindow::currentId() const
{
    const QModelIndex current = m_table->currentIndex();
    if (current.isValid() && m_table->selectionModel()->isRowSelected(current.row(), {}))
        return current.data(SoundLibrary::IdRole).toInt();
    const QList<int> ids = selectedIds();
    return ids.isEmpty() ? 0 : ids.first();
}

void MainWindow::selectSound(int soundId)
{
    const int row = m_library->rowOf(soundId);
    if (row < 0)
        return;
    const QModelIndex index = m_filter->mapFromSource(m_library->index(row, 0));
    if (!index.isValid())
        return;
    m_table->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_table->scrollTo(index);
}

void MainWindow::removeSelected()
{
    const QList<int> ids = selectedIds();
    if (ids.isEmpty())
        return;
    QString question;
    if (ids.size() == 1) {
        const Sound *sound = m_library->find(ids.first());
        question = tr("Remove \"%1\" from the list?").arg(sound ? sound->title : QString());
    } else {
        question = tr("Remove %1 sounds from the list?").arg(ids.size());
    }
    const auto answer = QMessageBox::question(
        this, tr("Remove Sounds"), question + QLatin1String("\n\n") + tr("The files are not deleted."));
    if (answer != QMessageBox::Yes)
        return;
    for (VoiceId voice : m_engine->voices()) {
        if (ids.contains(m_engine->soundId(voice)))
            m_engine->stop(voice);
    }
    m_library->removeSounds(ids);
}

void MainWindow::renameSelected()
{
    const QModelIndex current = m_table->currentIndex();
    if (current.isValid())
        m_table->edit(current.siblingAtColumn(SoundLibrary::TitleColumn));
}

void MainWindow::editProperties(int soundId, bool focusHotkey)
{
    if (soundId == 0)
        return;
    SoundPropertiesDialog dialog(m_library, soundId, this);
    if (focusHotkey)
        dialog.focusHotkey();
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (const Sound *sound = m_library->find(soundId); sound && sound->durationMs < 0)
        probeDurations({soundId});
}

void MainWindow::clearHotkeyOfSelected()
{
    for (int id : selectedIds())
        m_library->setHotkey(id, QString());
}

void MainWindow::showInFolder()
{
    const Sound *sound = m_library->find(currentId());
    if (!sound)
        return;
#ifdef Q_OS_WIN
    if (QFileInfo::exists(sound->path)) {
        QProcess explorer;
        explorer.setProgram(QStringLiteral("explorer.exe"));
        explorer.setNativeArguments(
            QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(sound->path)));
        explorer.startDetached();
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(sound->path).absolutePath()));
}

void MainWindow::moveSelectedToCategory(int categoryId)
{
    m_library->setCategory(selectedIds(), categoryId);
}

void MainWindow::newCategory(const QList<int> &soundsToMove)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Category"), tr("Category name:"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    const int id = m_library->addCategory(name);
    if (!soundsToMove.isEmpty())
        m_library->setCategory(soundsToMove, id);
}

void MainWindow::deleteCategory(int categoryId)
{
    const QString name = m_library->categoryName(categoryId);
    const auto answer = QMessageBox::question(
        this, tr("Delete Category"),
        tr("Delete the category \"%1\"? Its sounds stay in the list.").arg(name));
    if (answer == QMessageBox::Yes)
        m_library->removeCategory(categoryId);
}

void MainWindow::fillCategoryMenu(QMenu *menu)
{
    menu->clear();
    const Sound *sound = m_library->find(currentId());
    const int current = sound ? sound->categoryId : -1;
    auto add = [this, menu, current](const QString &name, int id) {
        QAction *action = menu->addAction(name, this, [this, id] { moveSelectedToCategory(id); });
        action->setCheckable(true);
        action->setChecked(id == current);
    };
    add(tr("Uncategorized"), 0);
    for (const Category &category : m_library->categories())
        add(category.name, category.id);
    menu->addSeparator();
    menu->addAction(icons::icon(icons::Glyph::Add), tr("New Category…"), this,
                    [this] { newCategory(selectedIds()); });
}

void MainWindow::showTableMenu(const QPoint &pos)
{
    QMenu menu(this);
    if (!m_table->indexAt(pos).isValid()) {
        menu.addAction(m_addFilesAction);
        menu.addAction(m_addFolderAction);
    } else {
        QAction *play = menu.addAction(icons::icon(icons::Glyph::Play), tr("Play"), this,
                                       &MainWindow::playSelected);
        menu.setDefaultAction(play);
        menu.addSeparator();
        menu.addAction(m_hotkeyAction);
        menu.addAction(m_clearHotkeyAction);
        menu.addAction(m_renameAction);
        menu.addMenu(m_moveToMenu);
        menu.addAction(m_propertiesAction);
        menu.addAction(m_showInFolderAction);
        menu.addSeparator();
        menu.addAction(m_removeAction);
    }
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::showCategoryMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_categories->itemAt(pos);
    if (item)
        m_categories->setCurrentItem(item);
    const int id = m_categories->currentCategory();
    QMenu menu(this);
    menu.addAction(icons::icon(icons::Glyph::Add), tr("New Category…"), this,
                   [this] { newCategory(); });
    if (item && id > 0) {
        menu.addAction(icons::icon(icons::Glyph::Edit), tr("Rename"), this,
                       [this, item] { m_categories->editItem(item); });
        menu.addAction(icons::icon(icons::Glyph::Remove), tr("Delete"), this,
                       [this, id] { deleteCategory(id); });
    }
    menu.exec(m_categories->viewport()->mapToGlobal(pos));
}

void MainWindow::updateReorderState()
{
    const QHeaderView *header = m_table->horizontalHeader();
    m_table->setReorderEnabled(header->sortIndicatorSection() == SoundLibrary::IndexColumn
                               && header->sortIndicatorOrder() == Qt::AscendingOrder
                               && m_search->text().isEmpty());
}

// ---------------------------------------------------------------------------
// Hotkeys & push-to-talk
// ---------------------------------------------------------------------------

void MainWindow::rebuildHotkeys()
{
    QList<HotkeyBinding> bindings;
    auto add = [&bindings](const QString &id, const QString &key, const QString &description) {
        if (!key.isEmpty())
            bindings.append({id, QKeySequence(key, QKeySequence::PortableText), description});
    };
    add(kStopHotkeyId, m_settings.stopHotkey, tr("Stop all sounds"));
    add(kPauseHotkeyId, m_settings.pauseHotkey, tr("Pause or resume"));
    add(kRandomHotkeyId, m_settings.randomHotkey, tr("Play a random sound"));
    add(kPlaySelectedHotkeyId, m_settings.playSelectedHotkey, tr("Play the selected sound"));
    for (const Sound &sound : m_library->sounds())
        add(kSoundHotkeyPrefix + QString::number(sound.id), sound.hotkey,
            tr("Play \"%1\"").arg(sound.title));
    m_hotkeys->setBindings(bindings);
}

void MainWindow::onHotkeyActivated(const QString &id)
{
    if (id.startsWith(kSoundHotkeyPrefix))
        playSound(id.mid(kSoundHotkeyPrefix.size()).toInt(), true);
    else if (id == kStopHotkeyId)
        stopAll();
    else if (id == kPauseHotkeyId)
        togglePause();
    else if (id == kRandomHotkeyId)
        playRandom();
    else if (id == kPlaySelectedHotkeyId)
        playSelected();
}

void MainWindow::onHotkeyErrorsChanged()
{
    QHash<int, QString> soundErrors;
    QStringList actionErrors;
    const QHash<QString, QString> errors = m_hotkeys->errors();
    for (auto it = errors.begin(); it != errors.end(); ++it) {
        if (it.key().startsWith(kSoundHotkeyPrefix))
            soundErrors.insert(it.key().mid(kSoundHotkeyPrefix.size()).toInt(), it.value());
        else
            actionErrors << it.value();
    }
    QHash<int, QString> triggers;
    const QHash<QString, QString> effective = m_hotkeys->effectiveTriggers();
    for (auto it = effective.begin(); it != effective.end(); ++it) {
        if (it.key().startsWith(kSoundHotkeyPrefix))
            triggers.insert(it.key().mid(kSoundHotkeyPrefix.size()).toInt(), it.value());
    }
    m_library->setHotkeyErrors(soundErrors);
    m_library->setHotkeyTriggers(triggers);

    const qsizetype failed = soundErrors.size() + actionErrors.size();
    if (failed > 0) {
        QString message = failed == 1
                              ? tr("1 hotkey could not be registered.")
                              : tr("%1 hotkeys could not be registered.").arg(failed);
        message += QLatin1Char(' ')
                   + (actionErrors.isEmpty() ? tr("Hover over the warning icon for details.")
                                             : actionErrors.first());
        statusBar()->showMessage(message, 10000);
    }
}

void MainWindow::applyPushToTalk()
{
    m_pushToTalk.clear();
    if (!m_settings.pushToTalkEnabled || m_settings.pushToTalkKey.isEmpty())
        return;
    QString error;
    if (!m_pushToTalk.setKey(m_settings.pushToTalkKey, &error))
        statusBar()->showMessage(tr("Push-to-talk is off: %1").arg(error), 10000);
}

void MainWindow::pressPushToTalk()
{
    m_pushToTalkTimer->stop();
    if (m_settings.pushToTalkEnabled)
        m_pushToTalk.press();
}

void MainWindow::releasePushToTalkSoon()
{
    if (m_pushToTalk.isPressed())
        m_pushToTalkTimer->start(m_settings.pushToTalkReleaseDelayMs);
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void MainWindow::toggleRecording()
{
    if (m_engine->isRecording()) {
        finishRecording();
        return;
    }
    const QString folder = m_settings.effectiveRecordingsFolder();
    if (!QDir().mkpath(folder)) {
        QMessageBox::warning(this, tr("Recording"),
                             tr("Can't create the folder %1.").arg(QDir::toNativeSeparators(folder)));
        m_recordAction->setChecked(false);
        return;
    }
    const QString path = QDir(folder).filePath(
        tr("Recording %1.wav")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"))));
    const bool loopback = m_settings.recordingDevice == QLatin1String(Settings::LoopbackDevice);
    QString device = loopback ? m_settings.speakerDevice : m_settings.recordingDevice;
    if (!loopback && device.isEmpty())
        device = m_engine->inputDeviceName();
    QString error;
    if (!m_engine->startRecording(device, loopback, path, &error)) {
        QMessageBox::warning(this, tr("Recording"), error);
        m_recordAction->setChecked(false);
        return;
    }
    m_recordingPath = path;
    m_recordAction->setChecked(true);
    m_recordAction->setText(tr("Stop &Recording"));
    tick();
}

void MainWindow::finishRecording()
{
    if (!m_engine->isRecording())
        return;
    const double seconds = m_engine->recordingSeconds();
    m_engine->stopRecording();
    m_recordAction->setChecked(false);
    m_recordAction->setText(tr("&Record"));
    const QString path = std::exchange(m_recordingPath, QString());
    if (seconds < 0.25) {
        QFile::remove(path);
        statusBar()->showMessage(tr("The recording was too short and was discarded."), 6000);
        return;
    }
    const QList<int> ids = m_library->addFiles({path}, std::max(0, m_categories->currentCategory()));
    probeDurations(ids);
    if (!ids.isEmpty())
        selectSound(ids.first());
    statusBar()->showMessage(tr("Saved the recording as %1.").arg(QDir::toNativeSeparators(path)),
                             8000);
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void MainWindow::showSettings()
{
#ifdef Q_OS_WIN
    const bool loopback = true;
#else
    const bool loopback = false;
#endif
    QString hotkeyInfo = tr("Global hotkeys use: %1.").arg(m_hotkeys->backendName());
    if (!m_hotkeys->status().isEmpty())
        hotkeyInfo += QLatin1Char(' ') + m_hotkeys->status();
    SettingsDialog dialog(m_settings, m_engine->playbackDevices(), m_engine->captureDevices(),
                          hotkeyInfo, loopback, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Settings old = m_settings;
    m_settings = dialog.settings();
    m_settings.save();

#ifdef Q_OS_LINUX
    if (old.createVirtualMicrophone && !m_settings.createVirtualMicrophone && m_virtualMicReady) {
        m_engine->close();
        virtualmic::remove();
        m_virtualMicReady = false;
    }
#endif
    const bool reopen = old.speakerDevice != m_settings.speakerDevice
                        || old.microphoneSinkDevice != m_settings.microphoneSinkDevice
                        || old.inputDevice != m_settings.inputDevice
                        || old.voicePassthrough != m_settings.voicePassthrough
                        || old.createVirtualMicrophone != m_settings.createVirtualMicrophone;
    if (reopen)
        openAudio();
    else
        applyOutputSettings();

    if (old.stopHotkey != m_settings.stopHotkey || old.pauseHotkey != m_settings.pauseHotkey
        || old.randomHotkey != m_settings.randomHotkey
        || old.playSelectedHotkey != m_settings.playSelectedHotkey)
        rebuildHotkeys();
    if (old.pushToTalkEnabled != m_settings.pushToTalkEnabled
        || old.pushToTalkKey != m_settings.pushToTalkKey)
        applyPushToTalk();
}

void MainWindow::showSetupHelp()
{
    QString text;
#ifdef Q_OS_LINUX
    text = tr("<p>Soundboard creates a virtual microphone called <b>%1</b>. It carries the sounds "
              "you play and, unless you turn it off, your own voice.</p>"
              "<ol><li>In Discord, TeamSpeak, OBS or your game, open the voice settings.</li>"
              "<li>Choose <b>%1</b> as the input device (microphone).</li>"
              "<li>Play a sound. The <i>Microphone</i> meter at the bottom of the window shows "
              "what the others hear.</li></ol>"
              "<p>Keep your real microphone selected under Settings → Audio → Your voice so "
              "that you can still talk.</p>")
               .arg(virtualmic::sourceDescription().toHtmlEscaped());
#else
    text = tr("<p>To let other people hear the sounds, Soundboard needs a virtual audio cable:</p>"
              "<ol><li>Download and install <a href=\"https://vb-audio.com/Cable/\">VB-CABLE</a> "
              "(free), then restart Soundboard.</li>"
              "<li>Soundboard plays into <b>CABLE Input</b> automatically.</li>"
              "<li>In Discord, TeamSpeak, OBS or your game, choose <b>CABLE Output</b> as the "
              "input device (microphone).</li></ol>"
              "<p>Your real microphone is mixed in by Soundboard, so you can keep talking. Pick "
              "it under Settings → Audio → Your voice.</p>");
#endif
    const QString microphone = m_engine->sinkDeviceName(Sink::Microphone);
    text += microphone.isEmpty()
                ? tr("<p><b>Status:</b> no microphone output is open.</p>")
                : tr("<p><b>Status:</b> playing into <i>%1</i>.</p>").arg(microphone.toHtmlEscaped());
    QMessageBox box(QMessageBox::Information, tr("Microphone Setup"), text, QMessageBox::Close, this);
    box.setTextFormat(Qt::RichText);
    QPushButton *settings = box.addButton(tr("Open Settings…"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == settings)
        showSettings();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About Soundboard"),
        tr("<h3>Soundboard %1</h3>"
           "<p>Play sounds through your microphone and speakers.</p>"
           "<p>Audio: %2 (miniaudio %3)<br>Global hotkeys: %4<br>Qt %5</p>")
            .arg(QStringLiteral(SOUNDBOARD_VERSION), m_engine->backendName(),
                 AudioEngine::libraryVersion(), m_hotkeys->backendName(),
                 QString::fromLatin1(qVersion())));
}

void MainWindow::saveSettingsSoon()
{
    m_saveSettingsTimer->start();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down && m_filter->rowCount() > 0) {
            m_table->setFocus();
            if (!m_table->currentIndex().isValid())
                selectSound(m_filter->index(0, 0).data(SoundLibrary::IdRole).toInt());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_quitting && m_settings.closeToTray && m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_tray->showMessage(tr("Soundboard is still running"),
                                tr("Hotkeys keep working. Use the tray icon to open the window "
                                   "or quit."),
                                icons::appIcon(), 5000);
        }
        return;
    }
    event->accept();
    quit();
}

void MainWindow::quit()
{
    m_quitting = true;
    shutdown();
    QApplication::quit();
}

void MainWindow::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    saveWindowState();
    finishRecording();
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();
    m_uiTimer->stop();
    m_engine->close();
    m_pushToTalk.clear();
    delete m_hotkeys;
    m_hotkeys = nullptr;
#ifdef Q_OS_LINUX
    if (m_virtualMicReady && m_settings.removeVirtualMicrophoneOnExit)
        virtualmic::remove();
#endif
    m_library->save();
    m_settings.save();
}

} // namespace sb
