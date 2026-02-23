#pragma once

#include <QMainWindow>
#include <QMediaPlayer>
#include <QProcess>
#include <QHash>

class QComboBox;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class QSpinBox;
class QAudioOutput;
class QVideoWidget;
class QListWidgetItem;
class QLabel;
class QSlider;
class QTimer;
class QFile;
class QCloseEvent;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void startScan();
    void stopScan();
    void handleStdOut();
    void handleStdErr();
    void processFinished(int exitCode);
    void watchSelectedChannel();
    void stopWatching();
    void handleZapStdErr();
    void addSelectedFavorite();
    void removeSelectedFavorite();
    void watchFavoriteItem(QListWidgetItem *item);
    void handleZapFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handlePlayerError(const QString &errorText);
    void triggerQuickFavorite();
    void triggerReconnect();
    void handleMuteToggled(bool checked);
    void handleVolumeChanged(int value);
    void toggleFullscreen();
    void handleFullscreenChanged(bool fullScreen);
    void openMediaFile();

private:
    void buildUi();
    void setScanningState(bool running);
    void appendLog(const QString &line);
    void parseAndStoreLine(const QString &line);
    bool persistChannelsFile();
    bool startWatchingChannel(const QString &channelName, bool reconnectAttempt = false);
    void refreshQuickButtons();
    void saveFavorites();
    void loadFavorites();
    void loadXspfChannelHints();
    void loadChannelsFileIfPresent();
    void startPlaybackFromDvr(const QString &dvrPath);
    void scheduleReconnect(const QString &reason);
    bool tryDynamicBridgeFallback(const QString &reason);
    QString playbackStatusText() const;
    QStringList makeArguments() const;
    QString selectedChannelNameFromTable() const;
    QString programIdForChannel(const QString &channelName) const;
    void stopProcess(QProcess *process, int timeoutMs);
    void enterFullscreen();
    void exitFullscreen();

    QComboBox *frontendTypeCombo_{};
    QLineEdit *countryEdit_{};
    QSpinBox *adapterSpin_{};
    QSpinBox *frontendSpin_{};
    QComboBox *outputFormatCombo_{};

    QPushButton *startButton_{};
    QPushButton *stopButton_{};
    QPushButton *watchButton_{};
    QPushButton *stopWatchButton_{};
    QPushButton *openFileButton_{};
    QPushButton *addFavoriteButton_{};
    QPushButton *removeFavoriteButton_{};
    QPushButton *quickFavoriteButtons_[8]{};
    QPushButton *muteButton_{};
    QPushButton *fullscreenButton_{};
    QPlainTextEdit *logOutput_{};
    QTableWidget *channelsTable_{};
    QVideoWidget *videoWidget_{};
    QLabel *playbackStatusLabel_{};
    QSlider *volumeSlider_{};
    QWidget *fullscreenWindow_{};
    QVideoWidget *fullscreenVideoWidget_{};

    QProcess *scanProcess_{};
    QProcess *zapProcess_{};
    QProcess *streamBridgeProcess_{};
    QMediaPlayer *mediaPlayer_{};
    QAudioOutput *audioOutput_{};
    QString partialStdOut_;
    QString partialStdErr_;
    QString channelsFilePath_;
    QString logFilePath_;
    QStringList channelLines_;
    QStringList favorites_;
    QHash<QString, QString> xspfProgramByChannel_;
    QString currentChannelName_;
    QString currentProgramId_;
    QString pendingDvrPath_;
    bool waitingForDvrReady_{false};
    QFile *dvrStream_{};
    int reconnectAttemptCount_{0};
    const int maxReconnectAttempts_{6};
    bool userStoppedWatching_{false};
    bool suppressZapExitReconnect_{false};
    bool suppressBridgeExitReconnect_{false};
    bool useResilientBridgeMode_{false};
    bool resilientBridgeTried_{false};
    bool fullscreenActive_{false};
    QTimer *reconnectTimer_{};
};
