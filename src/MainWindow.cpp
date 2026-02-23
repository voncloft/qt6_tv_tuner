#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAudioOutput>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>
#include <QScreen>
#include <QWindow>
#include <QCursor>
#include <functional>

namespace {
QString normalizeZapLine(const QString &line)
{
    const QStringList parts = line.split(':');
    if (parts.size() < 3) {
        return line;
    }

    QStringList normalizedParts = parts;
    const QString modulation = parts[2].trimmed().toUpper();
    if (modulation == "VSB_8") {
        normalizedParts[2] = "8VSB";
    } else if (modulation == "VSB_16") {
        normalizedParts[2] = "16VSB";
    }

    return normalizedParts.join(':');
}

QString resolveProjectLogPath()
{
    QDir dir(QDir::currentPath());
    if (dir.dirName() == "build") {
        dir.cdUp();
    }
    return dir.filePath("tv_tuner_gui.log");
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    qApp->installEventFilter(this);

    scanProcess_ = new QProcess(this);
    zapProcess_ = new QProcess(this);
    streamBridgeProcess_ = new QProcess(this);
    mediaPlayer_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);

    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setVideoOutput(videoWidget_);
    audioOutput_->setVolume(0.85);

    connect(scanProcess_, &QProcess::readyReadStandardOutput, this, &MainWindow::handleStdOut);
    connect(scanProcess_, &QProcess::readyReadStandardError, this, &MainWindow::handleStdErr);
    connect(scanProcess_, &QProcess::finished, this, &MainWindow::processFinished);
    connect(zapProcess_, &QProcess::readyReadStandardError, this, &MainWindow::handleZapStdErr);
    connect(zapProcess_, &QProcess::finished, this, &MainWindow::handleZapFinished);
    connect(streamBridgeProcess_, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(streamBridgeProcess_->readAllStandardError()).trimmed();
        if (!err.isEmpty()) {
            const QStringList lines = err.split('\n');
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) {
                    appendLog("ffmpeg: " + trimmed);
                }
            }
        }
    });
    connect(streamBridgeProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendLog(QString("ffmpeg bridge exited (code=%1, status=%2)")
                      .arg(exitCode)
                      .arg(status == QProcess::NormalExit ? "normal" : "crash"));
        if (!suppressBridgeExitReconnect_ && !userStoppedWatching_ && !currentChannelName_.isEmpty()) {
            if (tryDynamicBridgeFallback("Live stream bridge exited")) {
                return;
            }
            scheduleReconnect("Live stream bridge exited");
        }
    });
    connect(mediaPlayer_, &QMediaPlayer::mediaStatusChanged, this, &MainWindow::handleMediaStatusChanged);
    connect(mediaPlayer_, &QMediaPlayer::playbackStateChanged, this, [this]() {
        appendLog(QString("player: playbackStateChanged=%1").arg(static_cast<int>(mediaPlayer_->playbackState())));
        playbackStatusLabel_->setText(playbackStatusText());
    });
    connect(mediaPlayer_, &QMediaPlayer::errorChanged, this, [this]() {
        if (mediaPlayer_->error() != QMediaPlayer::NoError) {
            appendLog(QString("player: errorChanged code=%1 text=%2")
                          .arg(static_cast<int>(mediaPlayer_->error()))
                          .arg(mediaPlayer_->errorString()));
            handlePlayerError(mediaPlayer_->errorString());
        }
    });
    connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
        appendLog(QString("player: errorOccurred code=%1 text=%2").arg(static_cast<int>(error)).arg(errorString));
    });
    connect(mediaPlayer_, &QMediaPlayer::hasVideoChanged, this, [this](bool hasVideo) {
        appendLog(QString("player: hasVideoChanged=%1").arg(hasVideo ? "true" : "false"));
    });
    connect(mediaPlayer_, &QMediaPlayer::hasAudioChanged, this, [this](bool hasAudio) {
        appendLog(QString("player: hasAudioChanged=%1").arg(hasAudio ? "true" : "false"));
    });
    connect(mediaPlayer_, &QMediaPlayer::bufferProgressChanged, this, [this](float progress) {
        appendLog(QString("player: bufferProgress=%1").arg(progress, 0, 'f', 3));
    });
    connect(reconnectTimer_, &QTimer::timeout, this, &MainWindow::triggerReconnect);
    connect(volumeSlider_, &QSlider::valueChanged, this, &MainWindow::handleVolumeChanged);
    connect(muteButton_, &QPushButton::toggled, this, &MainWindow::handleMuteToggled);

    logFilePath_ = resolveProjectLogPath();
    loadFavorites();
    loadXspfChannelHints();
    loadChannelsFileIfPresent();
    refreshQuickButtons();
    playbackStatusLabel_->setText(playbackStatusText());
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    exitFullscreen();
    userStoppedWatching_ = true;
    if (reconnectTimer_ != nullptr) {
        reconnectTimer_->stop();
    }
    waitingForDvrReady_ = false;
    pendingDvrPath_.clear();

    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl());
    }

    if (dvrStream_ != nullptr) {
        if (dvrStream_->isOpen()) {
            dvrStream_->close();
        }
        delete dvrStream_;
        dvrStream_ = nullptr;
    }

    suppressBridgeExitReconnect_ = true;
    stopProcess(streamBridgeProcess_, 1200);
    suppressBridgeExitReconnect_ = false;

    suppressZapExitReconnect_ = true;
    stopProcess(zapProcess_, 1200);
    suppressZapExitReconnect_ = false;

    stopProcess(scanProcess_, 1200);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    appendLog("Application closing: releasing tuner resources.");
    exitFullscreen();
    stopWatching();
    stopProcess(scanProcess_, 1200);
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == videoWidget_ && event->type() == QEvent::MouseButtonDblClick) {
        enterFullscreen();
        return true;
    }

    if (watched == fullscreenVideoWidget_ && event->type() == QEvent::MouseButtonDblClick) {
        exitFullscreen();
        return true;
    }

    if (fullscreenActive_ && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            exitFullscreen();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void MainWindow::stopProcess(QProcess *process, int timeoutMs)
{
    if (process == nullptr || process->state() == QProcess::NotRunning) {
        return;
    }

    process->terminate();
    if (!process->waitForFinished(timeoutMs)) {
        process->kill();
        process->waitForFinished(timeoutMs);
    }
    process->close();
}

void MainWindow::buildUi()
{
    setWindowTitle("TV Tuner Watcher");
    resize(1320, 840);

    auto *root = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(root);

    auto *tabs = new QTabWidget(root);
    tabs->setTabPosition(QTabWidget::North);

    auto *watchPage = new QWidget(tabs);
    auto *watchLayout = new QVBoxLayout(watchPage);

    auto *tuningPage = new QWidget(tabs);
    auto *tuningLayout = new QVBoxLayout(tuningPage);

    auto *logsPage = new QWidget(tabs);
    auto *logsLayout = new QVBoxLayout(logsPage);

    auto *scanGroup = new QGroupBox("Scan Settings", tuningPage);
    auto *form = new QFormLayout(scanGroup);

    frontendTypeCombo_ = new QComboBox(scanGroup);
    frontendTypeCombo_->addItem("Terrestrial (DVB-T/T2)", "t");
    frontendTypeCombo_->addItem("Terrestrial DVB-T only", "t1");
    frontendTypeCombo_->addItem("Terrestrial DVB-T2 only", "t2");
    frontendTypeCombo_->addItem("ATSC", "a");
    frontendTypeCombo_->addItem("Cable", "c");
    frontendTypeCombo_->setCurrentIndex(0);

    countryEdit_ = new QLineEdit(scanGroup);
    countryEdit_->setText("US");
    countryEdit_->setMaxLength(2);
    countryEdit_->setPlaceholderText("US");

    adapterSpin_ = new QSpinBox(scanGroup);
    adapterSpin_->setRange(0, 32);
    adapterSpin_->setValue(0);

    frontendSpin_ = new QSpinBox(scanGroup);
    frontendSpin_->setRange(0, 32);
    frontendSpin_->setValue(0);

    outputFormatCombo_ = new QComboBox(scanGroup);
    outputFormatCombo_->addItem("xine/tzap/czap", "X");
    outputFormatCombo_->addItem("VLC playlist", "L");
    outputFormatCombo_->addItem("MPlayer", "M");
    outputFormatCombo_->addItem("DVBv5", "5");
    outputFormatCombo_->addItem("XML", "Z");
    outputFormatCombo_->setCurrentIndex(0);

    form->addRow("Frontend type:", frontendTypeCombo_);
    form->addRow("Country code:", countryEdit_);
    form->addRow("Adapter:", adapterSpin_);
    form->addRow("Frontend:", frontendSpin_);
    form->addRow("Output format:", outputFormatCombo_);

    auto *scanActionsRow = new QHBoxLayout();
    startButton_ = new QPushButton("Start Scan", tuningPage);
    stopButton_ = new QPushButton("Stop Scan", tuningPage);
    scanActionsRow->addWidget(startButton_);
    scanActionsRow->addWidget(stopButton_);
    scanActionsRow->addStretch(1);
    stopButton_->setEnabled(false);

    tuningLayout->addWidget(scanGroup);
    tuningLayout->addLayout(scanActionsRow);
    tuningLayout->addStretch(1);

    auto *watchControlsRow = new QHBoxLayout();
    watchButton_ = new QPushButton("Watch Selected", watchPage);
    stopWatchButton_ = new QPushButton("Stop Watching", watchPage);
    openFileButton_ = new QPushButton("Open File", watchPage);
    fullscreenButton_ = new QPushButton("Fullscreen", watchPage);
    muteButton_ = new QPushButton("Mute", watchPage);
    volumeSlider_ = new QSlider(Qt::Horizontal, watchPage);
    playbackStatusLabel_ = new QLabel("Idle", watchPage);
    addFavoriteButton_ = new QPushButton("Add Favorite", watchPage);
    removeFavoriteButton_ = new QPushButton("Remove Favorite", watchPage);

    stopButton_->setEnabled(false);
    stopWatchButton_->setEnabled(false);
    muteButton_->setCheckable(true);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(85);
    volumeSlider_->setFixedWidth(220);
    playbackStatusLabel_->setMinimumWidth(260);

    watchControlsRow->addWidget(watchButton_);
    watchControlsRow->addWidget(stopWatchButton_);
    watchControlsRow->addWidget(openFileButton_);
    watchControlsRow->addWidget(fullscreenButton_);
    watchControlsRow->addSpacing(12);
    watchControlsRow->addWidget(new QLabel("Volume:", watchPage));
    watchControlsRow->addWidget(volumeSlider_);
    watchControlsRow->addWidget(muteButton_);
    watchControlsRow->addSpacing(12);
    watchControlsRow->addWidget(new QLabel("Playback:", watchPage));
    watchControlsRow->addWidget(playbackStatusLabel_);
    watchControlsRow->addStretch(1);

    auto *contentSplitter = new QSplitter(Qt::Horizontal, watchPage);
    videoWidget_ = new QVideoWidget(contentSplitter);
    videoWidget_->setMinimumSize(640, 360);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget_->setStyleSheet("background: #000;");

    channelsTable_ = new QTableWidget(contentSplitter);
    channelsTable_->setColumnCount(3);
    channelsTable_->setHorizontalHeaderLabels({"Channel", "Provider", "Raw line"});
    channelsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    channelsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    channelsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    channelsTable_->setAlternatingRowColors(true);
    channelsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    contentSplitter->setStretchFactor(0, 5);
    contentSplitter->setStretchFactor(1, 3);

    auto *favoritesControlsRow = new QHBoxLayout();
    favoritesControlsRow->addWidget(addFavoriteButton_);
    favoritesControlsRow->addWidget(removeFavoriteButton_);
    favoritesControlsRow->addSpacing(8);
    favoritesControlsRow->addWidget(new QLabel("Favorites:", watchPage));
    for (int i = 0; i < 8; ++i) {
        quickFavoriteButtons_[i] = new QPushButton(QString::number(i + 1), watchPage);
        quickFavoriteButtons_[i]->setEnabled(false);
        quickFavoriteButtons_[i]->setMinimumWidth(120);
        favoritesControlsRow->addWidget(quickFavoriteButtons_[i]);
        connect(quickFavoriteButtons_[i], &QPushButton::clicked, this, &MainWindow::triggerQuickFavorite);
    }
    favoritesControlsRow->addStretch(1);

    watchLayout->addLayout(watchControlsRow);
    watchLayout->addWidget(contentSplitter, 1);
    watchLayout->addLayout(favoritesControlsRow);

    logOutput_ = new QPlainTextEdit(logsPage);
    logOutput_->setReadOnly(true);
    logOutput_->setMaximumBlockCount(4000);
    logOutput_->setPlaceholderText("w_scan2 and tuning output will appear here...");
    logsLayout->addWidget(logOutput_);

    tabs->addTab(watchPage, "Video");
    tabs->addTab(tuningPage, "Tuning");
    tabs->addTab(logsPage, "Logs");
    mainLayout->addWidget(tabs, 1);
    setCentralWidget(root);

    fullscreenWindow_ = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
    fullscreenWindow_->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    auto *fullscreenLayout = new QVBoxLayout(fullscreenWindow_);
    fullscreenLayout->setContentsMargins(0, 0, 0, 0);
    fullscreenLayout->setSpacing(0);
    fullscreenVideoWidget_ = new QVideoWidget(fullscreenWindow_);
    fullscreenVideoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fullscreenVideoWidget_->setStyleSheet("background: #000;");
    fullscreenVideoWidget_->setAspectRatioMode(Qt::IgnoreAspectRatio);
    fullscreenLayout->addWidget(fullscreenVideoWidget_);
    fullscreenWindow_->hide();

    videoWidget_->installEventFilter(this);
    fullscreenWindow_->installEventFilter(this);
    fullscreenVideoWidget_->installEventFilter(this);

    statusBar()->showMessage("Ready");

    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopScan);
    connect(watchButton_, &QPushButton::clicked, this, &MainWindow::watchSelectedChannel);
    connect(stopWatchButton_, &QPushButton::clicked, this, &MainWindow::stopWatching);
    connect(openFileButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
    connect(addFavoriteButton_, &QPushButton::clicked, this, &MainWindow::addSelectedFavorite);
    connect(removeFavoriteButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedFavorite);
    connect(channelsTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        watchSelectedChannel();
    });
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
}

QStringList MainWindow::makeArguments() const
{
    QStringList args;
    args << "-f" << frontendTypeCombo_->currentData().toString();

    const QString country = countryEdit_->text().trimmed().toUpper();
    if (!country.isEmpty()) {
        args << "-c" << country;
    }

    const QString dvbPath = QString("/dev/dvb/adapter%1/frontend%2")
                                .arg(adapterSpin_->value())
                                .arg(frontendSpin_->value());
    args << "-a" << dvbPath;

    const QString outputSwitch = outputFormatCombo_->currentData().toString();
    if (!outputSwitch.isEmpty()) {
        args << "-" + outputSwitch;
    }

    return args;
}

void MainWindow::startScan()
{
    if (scanProcess_->state() != QProcess::NotRunning) {
        return;
    }

    stopWatching();
    channelsTable_->setRowCount(0);
    logOutput_->clear();
    partialStdOut_.clear();
    partialStdErr_.clear();
    channelLines_.clear();

    const QString program = "w_scan2";
    const QStringList args = makeArguments();

    appendLog("[" + QDateTime::currentDateTime().toString(Qt::ISODate) + "] Starting: " + program + " " + args.join(' '));
    scanProcess_->start(program, args);

    if (!scanProcess_->waitForStarted(2000)) {
        QMessageBox::critical(this, "Failed to start", "Could not launch w_scan2. Check that it is in your PATH.");
        appendLog("Failed to start w_scan2.");
        return;
    }

    setScanningState(true);
    statusBar()->showMessage("Scanning...");
}

void MainWindow::stopScan()
{
    if (scanProcess_->state() == QProcess::NotRunning) {
        return;
    }

    appendLog("Stopping scan...");
    scanProcess_->terminate();
}

void MainWindow::handleStdOut()
{
    partialStdOut_.append(QString::fromUtf8(scanProcess_->readAllStandardOutput()));
    const QStringList lines = partialStdOut_.split('\n');
    partialStdOut_ = lines.back();

    for (int i = 0; i < lines.size() - 1; ++i) {
        const QString line = lines[i].trimmed();
        if (!line.isEmpty()) {
            appendLog(line);
            parseAndStoreLine(line);
        }
    }
}

void MainWindow::handleStdErr()
{
    partialStdErr_.append(QString::fromUtf8(scanProcess_->readAllStandardError()));
    const QStringList lines = partialStdErr_.split('\n');
    partialStdErr_ = lines.back();

    for (int i = 0; i < lines.size() - 1; ++i) {
        const QString line = lines[i].trimmed();
        if (!line.isEmpty()) {
            appendLog("stderr: " + line);
        }
    }
}

void MainWindow::processFinished(int exitCode)
{
    if (!partialStdOut_.trimmed().isEmpty()) {
        const QString line = partialStdOut_.trimmed();
        appendLog(line);
        parseAndStoreLine(line);
        partialStdOut_.clear();
    }
    if (!partialStdErr_.trimmed().isEmpty()) {
        appendLog("stderr: " + partialStdErr_.trimmed());
        partialStdErr_.clear();
    }

    setScanningState(false);
    persistChannelsFile();
    const int rowCount = channelsTable_->rowCount();
    const QString endMsg = QString("Scan finished (exit=%1). Channels parsed: %2").arg(exitCode).arg(rowCount);
    appendLog(endMsg);
    statusBar()->showMessage(endMsg);
}

void MainWindow::setScanningState(bool running)
{
    startButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
    watchButton_->setEnabled(!running);
    addFavoriteButton_->setEnabled(!running);
    removeFavoriteButton_->setEnabled(!running);
    for (auto *button : quickFavoriteButtons_) {
        if (button != nullptr) {
            button->setEnabled(!running && !button->property("channelName").toString().isEmpty());
        }
    }
    frontendTypeCombo_->setEnabled(!running);
    countryEdit_->setEnabled(!running);
    adapterSpin_->setEnabled(!running);
    frontendSpin_->setEnabled(!running);
    outputFormatCombo_->setEnabled(!running);
}

void MainWindow::appendLog(const QString &line)
{
    logOutput_->appendPlainText(line);
    QTextCursor cursor = logOutput_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logOutput_->setTextCursor(cursor);

    if (!logFilePath_.isEmpty()) {
        QFile logFile(logFilePath_);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            const QString entry = QString("[%1] %2\n")
                                      .arg(QDateTime::currentDateTime().toString(Qt::ISODate), line);
            logFile.write(entry.toUtf8());
        }
    }
}

void MainWindow::parseAndStoreLine(const QString &line)
{
    if (line.startsWith('#') || line.startsWith("stderr:")) {
        return;
    }

    const QString normalizedLine = normalizeZapLine(line);
    const QStringList parts = normalizedLine.split(':');
    if (parts.size() < 3) {
        return;
    }

    const QString channelName = parts[0].trimmed();
    if (channelName.isEmpty()) {
        return;
    }

    if (!channelLines_.contains(normalizedLine)) {
        channelLines_.append(normalizedLine);
    }

    QString provider = "Unknown";
    if (parts.size() > 10) {
        provider = parts[10].trimmed();
        if (provider.isEmpty()) {
            provider = "Unknown";
        }
    }

    const int row = channelsTable_->rowCount();
    channelsTable_->insertRow(row);
    channelsTable_->setItem(row, 0, new QTableWidgetItem(channelName));
    channelsTable_->setItem(row, 1, new QTableWidgetItem(provider));
    channelsTable_->setItem(row, 2, new QTableWidgetItem(normalizedLine));
}

bool MainWindow::persistChannelsFile()
{
    if (channelLines_.isEmpty()) {
        return false;
    }

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appendLog("Could not resolve app data directory for channels list.");
        return false;
    }

    QDir dir(appDataPath);
    if (!dir.exists() && !dir.mkpath(".")) {
        appendLog("Could not create app data directory: " + appDataPath);
        return false;
    }

    channelsFilePath_ = dir.filePath("channels.conf");
    QFile file(channelsFilePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendLog("Could not write channels file: " + channelsFilePath_);
        return false;
    }

    for (const QString &entry : channelLines_) {
        file.write(entry.toUtf8());
        file.write("\n");
    }
    file.close();
    appendLog("Channels saved: " + channelsFilePath_);
    return true;
}

QString MainWindow::selectedChannelNameFromTable() const
{
    const auto rows = channelsTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return {};
    }
    const int row = rows.first().row();
    const auto *item = channelsTable_->item(row, 0);
    if (item == nullptr) {
        return {};
    }
    return item->text().trimmed();
}

QString MainWindow::programIdForChannel(const QString &channelName) const
{
    if (channelName.isEmpty()) {
        return {};
    }

    if (xspfProgramByChannel_.contains(channelName)) {
        return xspfProgramByChannel_.value(channelName);
    }

    auto parseProgramFromLine = [&channelName](const QString &line) -> QString {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) {
            return {};
        }
        const QStringList parts = trimmedLine.split(':');
        if (parts.size() < 6) {
            return {};
        }
        if (parts[0].trimmed() != channelName) {
            return {};
        }
        return parts[5].trimmed();
    };

    for (const QString &line : channelLines_) {
        const QString programId = parseProgramFromLine(line);
        if (!programId.isEmpty()) {
            return programId;
        }
    }

    QFile file(channelsFilePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        const QString programId = parseProgramFromLine(line);
        if (!programId.isEmpty()) {
            return programId;
        }
    }
    return {};
}

void MainWindow::watchSelectedChannel()
{
    if (scanProcess_->state() != QProcess::NotRunning) {
        QMessageBox::warning(this, "Scan in progress", "Stop scanning before starting live viewing.");
        return;
    }

    const QString channelName = selectedChannelNameFromTable();
    if (channelName.isEmpty()) {
        QMessageBox::information(this, "Select a channel", "Select a channel row first.");
        return;
    }

    startWatchingChannel(channelName, false);
}

void MainWindow::openMediaFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Open Media File",
        QDir::homePath(),
        "Video Files (*.mp4 *.mkv *.webm *.avi *.mov *.ts *.m2ts);;All Files (*)");
    if (filePath.isEmpty()) {
        return;
    }

    stopWatching();
    userStoppedWatching_ = true;
    currentChannelName_ = "File: " + QFileInfo(filePath).fileName();

    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setSource(QUrl::fromLocalFile(filePath));
    mediaPlayer_->play();

    appendLog("player: Opened local media file: " + filePath);
    playbackStatusLabel_->setText(playbackStatusText());
    statusBar()->showMessage("Playing local file: " + QFileInfo(filePath).fileName());
}

bool MainWindow::startWatchingChannel(const QString &channelName, bool reconnectAttempt)
{
    if (channelName.isEmpty()) {
        return false;
    }

    if (!persistChannelsFile()) {
        QMessageBox::warning(this, "No channel list", "No saved channels are available yet. Run a scan first.");
        return false;
    }

    const QString zapExe = QStandardPaths::findExecutable("dvbv5-zap");
    if (zapExe.isEmpty()) {
        QMessageBox::critical(this, "Missing dependency", "dvbv5-zap was not found in PATH.");
        return false;
    }

    userStoppedWatching_ = false;
    reconnectTimer_->stop();
    if (!reconnectAttempt) {
        reconnectAttemptCount_ = 0;
        useResilientBridgeMode_ = false;
        resilientBridgeTried_ = false;
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1200);
        suppressBridgeExitReconnect_ = false;
    }

    if (zapProcess_->state() != QProcess::NotRunning) {
        suppressZapExitReconnect_ = true;
        stopProcess(zapProcess_, 1000);
        suppressZapExitReconnect_ = false;
    }

    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl());

    QStringList args;
    args << "-I" << "ZAP"
         << "-c" << channelsFilePath_
         << "-a" << QString::number(adapterSpin_->value())
         << "-f" << QString::number(frontendSpin_->value())
         << "-r"
         << "-P"
         << "-p";

    args << channelName;

    zapProcess_->start(zapExe, args);
    if (!zapProcess_->waitForStarted(2000)) {
        appendLog("Failed to start dvbv5-zap for " + channelName);
        scheduleReconnect("Failed to start tuner process");
        return false;
    }

    currentChannelName_ = channelName;
    currentProgramId_ = programIdForChannel(channelName);
    const QString dvrPath = QString("/dev/dvb/adapter%1/dvr0").arg(adapterSpin_->value());
    pendingDvrPath_ = dvrPath;
    waitingForDvrReady_ = true;
    appendLog(QString("Tuning channel: %1 (program=%2)")
                  .arg(channelName, currentProgramId_.isEmpty() ? "unknown" : currentProgramId_));

    // Start playback when zap reports that the DVR interface is ready.
    QTimer::singleShot(3500, this, [this, dvrPath]() {
        if (waitingForDvrReady_ && pendingDvrPath_ == dvrPath) {
            appendLog("DVR ready signal timeout; attempting playback anyway.");
            startPlaybackFromDvr(dvrPath);
        }
    });

    stopWatchButton_->setEnabled(true);
    playbackStatusLabel_->setText(playbackStatusText());
    statusBar()->showMessage("Watching: " + channelName);
    return true;
}

void MainWindow::stopWatching()
{
    exitFullscreen();
    userStoppedWatching_ = true;
    reconnectTimer_->stop();
    reconnectAttemptCount_ = 0;
    useResilientBridgeMode_ = false;
    resilientBridgeTried_ = false;
    currentChannelName_.clear();
    currentProgramId_.clear();
    pendingDvrPath_.clear();
    waitingForDvrReady_ = false;

    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl());
    }

    if (dvrStream_ != nullptr) {
        if (dvrStream_->isOpen()) {
            dvrStream_->close();
        }
        dvrStream_->deleteLater();
        dvrStream_ = nullptr;
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1200);
        suppressBridgeExitReconnect_ = false;
    }

    if (zapProcess_ != nullptr && zapProcess_->state() != QProcess::NotRunning) {
        suppressZapExitReconnect_ = true;
        stopProcess(zapProcess_, 1200);
        suppressZapExitReconnect_ = false;
    }

    stopWatchButton_->setEnabled(false);
    playbackStatusLabel_->setText(playbackStatusText());
    if (scanProcess_->state() == QProcess::NotRunning) {
        statusBar()->showMessage("Ready");
    }
}

void MainWindow::handleZapStdErr()
{
    const QString output = QString::fromUtf8(zapProcess_->readAllStandardError()).trimmed();
    if (output.isEmpty()) {
        return;
    }

    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            appendLog("zap: " + trimmed);
            if (waitingForDvrReady_ && trimmed.contains("DVR interface")
                && trimmed.contains("can now be opened")) {
                QString detectedPath = pendingDvrPath_;
                const QRegularExpression re("'([^']+/dvr0)'");
                const auto match = re.match(trimmed);
                if (match.hasMatch()) {
                    detectedPath = match.captured(1);
                }
                appendLog("player: DVR ready path detected: " + detectedPath);
                startPlaybackFromDvr(detectedPath);
            }
        }
    }
}

void MainWindow::startPlaybackFromDvr(const QString &dvrPath)
{
    if (dvrPath.isEmpty()) {
        return;
    }
    waitingForDvrReady_ = false;
    pendingDvrPath_.clear();

    if (dvrStream_ != nullptr) {
        if (dvrStream_->isOpen()) {
            dvrStream_->close();
        }
        dvrStream_->deleteLater();
        dvrStream_ = nullptr;
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1000);
        suppressBridgeExitReconnect_ = false;
    }

    const QString ffmpegExe = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpegExe.isEmpty()) {
        appendLog("player: ffmpeg not found in PATH for live DVB bridge.");
        scheduleReconnect("Missing ffmpeg for live stream");
        return;
    }

    QStringList ffmpegArgs;
    if (useResilientBridgeMode_) {
        ffmpegArgs << "-hide_banner"
                   << "-nostdin"
                   << "-loglevel" << "warning"
                   << "-fflags" << "+genpts+discardcorrupt"
                   << "-err_detect" << "ignore_err"
                   << "-analyzeduration" << "4M"
                   << "-probesize" << "4M"
                   << "-f" << "mpegts"
                   << "-i" << dvrPath;
        if (!currentProgramId_.isEmpty()) {
            ffmpegArgs << "-map" << QString("0:p:%1?").arg(currentProgramId_);
        } else {
            ffmpegArgs << "-map" << "0:v:0?"
                       << "-map" << "0:a:0?";
        }
        ffmpegArgs
                   << "-c:v" << "mpeg2video"
                   << "-q:v" << "3"
                   << "-c:a" << "mp2"
                   << "-b:a" << "192k"
                   << "-mpegts_flags" << "+resend_headers+pat_pmt_at_frames"
                   << "-flush_packets" << "1"
                   << "-f" << "mpegts"
                   << QString("udp://127.0.0.1:%1?pkt_size=1316").arg(23000 + adapterSpin_->value());
    } else {
        ffmpegArgs << "-hide_banner"
                   << "-nostdin"
                   << "-loglevel" << "warning"
                   << "-fflags" << "+genpts"
                   << "-analyzeduration" << "2M"
                   << "-probesize" << "2M"
                   << "-f" << "mpegts"
                   << "-i" << dvrPath;
        if (!currentProgramId_.isEmpty()) {
            ffmpegArgs << "-map" << QString("0:p:%1?").arg(currentProgramId_);
        } else {
            ffmpegArgs << "-map" << "0";
        }
        ffmpegArgs
                   << "-c" << "copy"
                   << "-mpegts_flags" << "+resend_headers+pat_pmt_at_frames"
                   << "-flush_packets" << "1"
                   << "-f" << "mpegts"
                   << QString("udp://127.0.0.1:%1?pkt_size=1316").arg(23000 + adapterSpin_->value());
    }

    streamBridgeProcess_->setProgram(ffmpegExe);
    streamBridgeProcess_->setArguments(ffmpegArgs);
    streamBridgeProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    streamBridgeProcess_->start();
    if (!streamBridgeProcess_->waitForStarted(2000)) {
        appendLog("player: Failed to start ffmpeg bridge for " + dvrPath);
        scheduleReconnect("Could not start ffmpeg bridge");
        return;
    }

    const int udpPort = 23000 + adapterSpin_->value();
    const QUrl liveUrl(QString("udp://127.0.0.1:%1").arg(udpPort));
    appendLog(QString("player: Starting playback from ffmpeg bridge on %1 via %2 (mode=%3, program=%4)")
                  .arg(dvrPath,
                       liveUrl.toString(),
                       useResilientBridgeMode_ ? "resilient" : "normal",
                       currentProgramId_.isEmpty() ? "unknown" : currentProgramId_));
    mediaPlayer_->setAudioOutput(audioOutput_);
    QTimer::singleShot(450, this, [this, liveUrl]() {
        if (streamBridgeProcess_ == nullptr || streamBridgeProcess_->state() != QProcess::Running) {
            appendLog("player: ffmpeg bridge exited before media attach.");
            return;
        }
        appendLog("player: Attaching UDP live stream to media player.");
        mediaPlayer_->setSource(liveUrl);
        mediaPlayer_->play();
    });
}

void MainWindow::addSelectedFavorite()
{
    const QString channelName = selectedChannelNameFromTable();
    if (channelName.isEmpty()) {
        QMessageBox::information(this, "Select channel", "Select a channel to add to favorites.");
        return;
    }

    if (favorites_.contains(channelName)) {
        return;
    }

    favorites_.append(channelName);
    saveFavorites();
    refreshQuickButtons();
}

void MainWindow::removeSelectedFavorite()
{
    QString name = selectedChannelNameFromTable();
    if (name.isEmpty() && !currentChannelName_.startsWith("File: ") && !currentChannelName_.isEmpty()) {
        name = currentChannelName_;
    }
    if (name.isEmpty()) {
        return;
    }

    if (!favorites_.contains(name)) {
        return;
    }

    favorites_.removeAll(name);
    saveFavorites();
    refreshQuickButtons();
}

void MainWindow::watchFavoriteItem(QListWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }
    startWatchingChannel(item->text(), false);
}

void MainWindow::handleZapFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit) {
        appendLog("zap: tuner process crashed");
    }
    if (!suppressZapExitReconnect_ && exitCode != 0 && !userStoppedWatching_ && !currentChannelName_.isEmpty()) {
        scheduleReconnect(QString("Tuner process exited (%1)").arg(exitCode));
    }
}

QString MainWindow::playbackStatusText() const
{
    if (currentChannelName_.isEmpty()) {
        return "Idle";
    }

    const QString stateText = (mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) ? "Playing" : "Buffering";
    return QString("%1 (%2)").arg(stateText, currentChannelName_);
}

void MainWindow::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    appendLog(QString("player: mediaStatusChanged=%1").arg(static_cast<int>(status)));
    playbackStatusLabel_->setText(playbackStatusText());
    if (!currentChannelName_.isEmpty() && status == QMediaPlayer::InvalidMedia && !userStoppedWatching_) {
        if (tryDynamicBridgeFallback("Media stream became invalid")) {
            return;
        }
        scheduleReconnect("Media stream became invalid");
    }
    if (!currentChannelName_.isEmpty() && status == QMediaPlayer::EndOfMedia && !userStoppedWatching_) {
        if (tryDynamicBridgeFallback("Media reached unexpected end")) {
            return;
        }
        scheduleReconnect("Media stream became invalid");
    }
}

void MainWindow::handlePlayerError(const QString &errorText)
{
    if (errorText.trimmed().isEmpty()) {
        return;
    }
    appendLog("player: " + errorText.trimmed());
    if (!userStoppedWatching_ && !currentChannelName_.isEmpty()) {
        if (tryDynamicBridgeFallback("Player error")) {
            return;
        }
        scheduleReconnect("Player error");
    }
}

bool MainWindow::tryDynamicBridgeFallback(const QString &reason)
{
    if (userStoppedWatching_ || currentChannelName_.isEmpty()) {
        return false;
    }
    if (resilientBridgeTried_ || useResilientBridgeMode_) {
        return false;
    }

    resilientBridgeTried_ = true;
    useResilientBridgeMode_ = true;
    reconnectTimer_->stop();
    reconnectAttemptCount_ = 0;
    appendLog(QString("player: %1; retrying with resilient bridge mode").arg(reason));
    startWatchingChannel(currentChannelName_, true);
    return true;
}

void MainWindow::triggerQuickFavorite()
{
    auto *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr) {
        return;
    }
    const QString channel = button->property("channelName").toString();
    if (channel.isEmpty()) {
        return;
    }
    startWatchingChannel(channel, false);
}

void MainWindow::scheduleReconnect(const QString &reason)
{
    if (currentChannelName_.isEmpty() || userStoppedWatching_) {
        return;
    }
    if (reconnectAttemptCount_ >= maxReconnectAttempts_) {
        appendLog("Reconnect failed after maximum attempts.");
        statusBar()->showMessage("Reconnect failed for " + currentChannelName_);
        return;
    }

    ++reconnectAttemptCount_;
    const int delayMs = 800 + (reconnectAttemptCount_ * 900);
    appendLog(QString("Reconnect attempt %1/%2 in %3 ms (%4)")
                  .arg(reconnectAttemptCount_)
                  .arg(maxReconnectAttempts_)
                  .arg(delayMs)
                  .arg(reason));
    statusBar()->showMessage(QString("Reconnecting to %1...").arg(currentChannelName_));
    reconnectTimer_->start(delayMs);
}

void MainWindow::triggerReconnect()
{
    if (currentChannelName_.isEmpty() || userStoppedWatching_) {
        return;
    }
    startWatchingChannel(currentChannelName_, true);
}

void MainWindow::handleMuteToggled(bool checked)
{
    audioOutput_->setMuted(checked);
    muteButton_->setText(checked ? "Unmute" : "Mute");
}

void MainWindow::handleVolumeChanged(int value)
{
    audioOutput_->setVolume(static_cast<float>(value) / 100.0f);
}

void MainWindow::toggleFullscreen()
{
    if (fullscreenActive_) {
        exitFullscreen();
    } else {
        enterFullscreen();
    }
}

void MainWindow::handleFullscreenChanged(bool fullScreen)
{
    fullscreenButton_->setText(fullScreen ? "Exit Fullscreen" : "Fullscreen");
}

void MainWindow::enterFullscreen()
{
    if (fullscreenActive_ || fullscreenWindow_ == nullptr || fullscreenVideoWidget_ == nullptr) {
        return;
    }

    QScreen *targetScreen = nullptr;
    if (videoWidget_ != nullptr && videoWidget_->windowHandle() != nullptr) {
        targetScreen = videoWidget_->windowHandle()->screen();
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen == nullptr) {
        return;
    }

    if (fullscreenWindow_->windowHandle() != nullptr) {
        fullscreenWindow_->windowHandle()->setScreen(targetScreen);
    }
    fullscreenWindow_->setGeometry(targetScreen->geometry());

    mediaPlayer_->setVideoOutput(fullscreenVideoWidget_);
    fullscreenVideoWidget_->setAspectRatioMode(Qt::IgnoreAspectRatio);
    fullscreenWindow_->showFullScreen();
    fullscreenWindow_->raise();
    fullscreenWindow_->activateWindow();
    fullscreenVideoWidget_->setFocus(Qt::ActiveWindowFocusReason);
    fullscreenActive_ = true;
    handleFullscreenChanged(true);
}

void MainWindow::exitFullscreen()
{
    if (!fullscreenActive_) {
        return;
    }

    fullscreenActive_ = false;
    if (fullscreenWindow_ != nullptr) {
        fullscreenWindow_->hide();
    }
    mediaPlayer_->setVideoOutput(videoWidget_);
    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    videoWidget_->updateGeometry();
    videoWidget_->update();
    handleFullscreenChanged(false);
}

void MainWindow::refreshQuickButtons()
{
    for (int i = 0; i < 8; ++i) {
        auto *button = quickFavoriteButtons_[i];
        if (button == nullptr) {
            continue;
        }
        if (i < favorites_.size()) {
            const QString channel = favorites_.at(i);
            QString label = channel;
            if (label.size() > 18) {
                label = label.left(15) + "...";
            }
            button->setText(QString("%1 %2").arg(i + 1).arg(label));
            button->setToolTip(channel);
            button->setProperty("channelName", channel);
            button->setEnabled(scanProcess_ == nullptr || scanProcess_->state() == QProcess::NotRunning);
        } else {
            button->setText(QString::number(i + 1));
            button->setToolTip(QString());
            button->setProperty("channelName", QString());
            button->setEnabled(false);
        }
    }
}

void MainWindow::saveFavorites()
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue("favorites", favorites_);
}

void MainWindow::loadFavorites()
{
    QSettings settings("tv_tuner_gui", "watcher");
    favorites_ = settings.value("favorites").toStringList();
    favorites_.removeDuplicates();
}

void MainWindow::loadXspfChannelHints()
{
    xspfProgramByChannel_.clear();

    const QString xspfPath = QDir::home().filePath("Desktop/tv.xspf");
    QFile file(xspfPath);
    if (!file.exists()) {
        appendLog("No XSPF playlist found on Desktop; using channels.conf metadata.");
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog("Could not open XSPF playlist: " + xspfPath);
        return;
    }

    QXmlStreamReader xml(&file);
    QString currentTitle;
    QString currentProgram;
    bool inTrack = false;
    bool inVlcOption = false;
    QString optionText;

    auto flushTrack = [this, &currentTitle, &currentProgram]() {
        if (currentTitle.isEmpty() || currentProgram.isEmpty()) {
            return;
        }
        const int firstSpace = currentTitle.indexOf(' ');
        if (firstSpace <= 0 || firstSpace >= currentTitle.size() - 1) {
            return;
        }
        const QString channelName = currentTitle.mid(firstSpace + 1).trimmed();
        if (!channelName.isEmpty()) {
            xspfProgramByChannel_.insert(channelName, currentProgram);
        }
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == u"track") {
                inTrack = true;
                currentTitle.clear();
                currentProgram.clear();
            } else if (inTrack && name == u"title") {
                currentTitle = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            } else if (inTrack && name == u"option" && xml.namespaceUri().toString().contains("videolan.org")) {
                inVlcOption = true;
                optionText.clear();
            }
        } else if (xml.isCharacters() && inVlcOption) {
            optionText += xml.text().toString();
        } else if (xml.isEndElement()) {
            const QStringView name = xml.name();
            if (inVlcOption && name == u"option") {
                inVlcOption = false;
                const QString opt = optionText.trimmed();
                if (opt.startsWith("program=")) {
                    currentProgram = opt.mid(QString("program=").size()).trimmed();
                }
                optionText.clear();
            } else if (inTrack && name == u"track") {
                flushTrack();
                inTrack = false;
                currentTitle.clear();
                currentProgram.clear();
            }
        }
    }

    if (xml.hasError()) {
        appendLog("Failed to parse XSPF playlist: " + xml.errorString());
        xspfProgramByChannel_.clear();
        return;
    }

    appendLog(QString("Loaded %1 XSPF program mappings from %2")
                  .arg(xspfProgramByChannel_.size())
                  .arg(xspfPath));
}

void MainWindow::loadChannelsFileIfPresent()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appendLog("Could not resolve app data directory for channels list.");
        return;
    }

    QDir dir(appDataPath);
    channelsFilePath_ = dir.filePath("channels.conf");
    QFile file(channelsFilePath_);
    if (!file.exists()) {
        appendLog("No saved channels file found. Run a scan once to create one.");
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog("Could not open channels file: " + channelsFilePath_);
        return;
    }

    channelsTable_->setRowCount(0);
    channelLines_.clear();

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty()) {
            parseAndStoreLine(line);
        }
    }

    appendLog(QString("Loaded %1 channel entries from %2")
                  .arg(channelLines_.size())
                  .arg(channelsFilePath_));
}
