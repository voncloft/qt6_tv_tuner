#pragma once

#include "TvGuideDialog.h"

#include <QByteArray>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QProcess>
#include <QHash>
#include <QList>
#include <QSet>

class QComboBox;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class QSpinBox;
class QAudioOutput;
class QVideoWidget;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QSlider;
class QTimer;
class QFile;
class QCloseEvent;
class QWidget;
class QSplitter;
class QTabWidget;
class QVBoxLayout;
class QCheckBox;
class QSpinBox;

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
    void refreshTvGuide();
    void handleCurrentTabChanged(int index);
    void handleGuideHideNoEitToggled(bool checked);
    void handleGuideShowFavoritesOnlyToggled(bool checked);
    void handleAutoFavoriteShowSchedulingToggled(bool checked);
    void handleAutoPictureInPictureToggled(bool checked);
    void handleGuideRefreshIntervalChanged(int minutes);
    void handleGuideCacheRetentionChanged(int hours);
    void handleSearchScheduleRequested(const QString &favoriteShowTitle,
                                       const QString &channelName,
                                       const TvGuideEntry &entry);
    void addTestingBugItem();
    void removeSelectedTestingBugItem();
    void removeSelectedFavoriteShowRule();
    void removeSelectedScheduledSwitch();

private:
    static constexpr int kQuickFavoriteCount = 10;

    void buildUi();
    void setScanningState(bool running);
    void appendLog(const QString &line);
    // Keep user/program behavior observable: new interaction paths should log entry and outcome here.
    void logInteraction(const QString &actor, const QString &action, const QString &details = QString());
    void parseAndStoreLine(const QString &line);
    bool persistChannelsFile();
    bool startWatchingChannel(const QString &channelName, bool reconnectAttempt = false);
    void refreshQuickButtons();
    void saveFavorites();
    void loadFavorites();
    void loadXspfChannelHints();
    void loadChannelsFileIfPresent();
    void startPlaybackFromDvr(const QString &dvrPath);
    bool refreshGuideData(bool interactive, bool updateDialog);
    bool writeGuideCacheFile(const QStringList &channelOrder,
                             const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                             const QDateTime &windowStartUtc,
                             int slotMinutes,
                             int slotCount,
                             const QString &statusText);
    bool loadGuideCacheFile();
    void scheduleReconnect(const QString &reason);
    bool tryDynamicBridgeFallback(const QString &reason);
    QString playbackStatusText() const;
    void setStatusBarStateMessage(const QString &text);
    void updateTvGuideDialogFromCurrentCache(bool showStatusMessage = false);
    QStringList makeArguments() const;
    QString selectedChannelNameFromTable() const;
    QString programIdForChannel(const QString &channelName) const;
    bool highlightChannelInTable(const QString &channelName);
    void probeCurrentShowAfterTune(const QString &channelName, int lookupSerial);
    void refreshCurrentShowStatus();
    void scheduleCurrentShowRefresh(const QDateTime &refreshUtc);
    void setCurrentShowStatus(const QString &text,
                              const QString &toolTip = QString(),
                              const QString &synopsisText = QString());
    bool applyCurrentShowStatusFromGuideCache();
    void stopProcess(QProcess *process, int timeoutMs);
    void enterFullscreen();
    void exitFullscreen();
    void saveChannelSidebarSizing();
    void restoreChannelSidebarSizing();
    void restoreLastPlayedChannel();
    void handleGuideScheduleToggle(const QString &channelName, const TvGuideEntry &entry, bool enabled);
    void handleObeyScheduledSwitchesChanged(bool obey);
    bool saveScheduledSwitches() const;
    bool loadScheduledSwitches();
    bool pruneExpiredScheduledSwitches();
    void refreshScheduledSwitchTimer();
    void processScheduledSwitches();
    void loadFavoriteShowRules();
    void saveFavoriteShowRules() const;
    void refreshFavoriteShowRuleList();
    void refreshScheduledSwitchList();
    void loadTestingBugItems();
    void saveTestingBugItems() const;
    bool addTestingBugItemEntry(const QString &text, bool checked);
    bool addFavoriteShowRule(const QString &title);
    bool resolveScheduledSwitchChoices(const QList<TvGuideScheduledSwitch> &candidates,
                                       const QString &sourceDescription,
                                       bool promptForConflict);
    bool addScheduledSwitchCandidate(const TvGuideScheduledSwitch &candidate,
                                     const QString &sourceDescription,
                                     bool promptForConflict);
    void autoScheduleFavoriteShowsFromGuideCache(bool promptForConflict, bool forceCurrentCacheSearch);
    void showStartupSwitchSummary();
    bool shouldDetachVideoForCurrentTab(int index) const;
    void detachVideoToPip();
    void attachVideoFromPip();
    void applyGuideFilterSettings();
    void applyGuideRefreshIntervalSetting();
    bool purgeExpiredGuideCacheFiles(bool clearLoadedState);
    void clearLoadedGuideCache();

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
    QPushButton *quickFavoriteButtons_[kQuickFavoriteCount]{};
    QPushButton *muteButton_{};
    QPushButton *fullscreenButton_{};
    QPlainTextEdit *logOutput_{};
    QTableWidget *channelsTable_{};
    QVideoWidget *videoWidget_{};
    QVideoWidget *pipVideoWidget_{};
    QLabel *videoDetachedPlaceholderLabel_{};
    QWidget *pipWindow_{};
    QLabel *playbackStatusLabel_{};
    QLabel *currentShowLabel_{};
    QLabel *currentShowSynopsisLabel_{};
    QSlider *volumeSlider_{};
    QCheckBox *hideNoEitChannelsCheckBox_{};
    QCheckBox *showFavoritesOnlyCheckBox_{};
    QCheckBox *obeyScheduledSwitchesCheckBox_{};
    QCheckBox *autoFavoriteShowSchedulingCheckBox_{};
    QCheckBox *autoPictureInPictureCheckBox_{};
    QSpinBox *guideRefreshIntervalSpin_{};
    QSpinBox *guideCacheRetentionSpin_{};
    QLineEdit *favoriteShowRuleEdit_{};
    QLineEdit *testingBugItemEdit_{};
    QListWidget *favoriteShowRulesList_{};
    QListWidget *scheduledSwitchesList_{};
    QListWidget *testingBugItemsList_{};
    QPushButton *addFavoriteShowRuleButton_{};
    QPushButton *saveTestingBugItemButton_{};
    QPushButton *removeTestingBugItemButton_{};
    QPushButton *removeFavoriteShowRuleButton_{};
    QPushButton *removeScheduledSwitchButton_{};
    QTabWidget *tabs_{};
    QWidget *watchPage_{};
    QWidget *configPage_{};
    QWidget *watchControlsContainer_{};
    QWidget *favoritesContainer_{};
    QWidget *statusContainer_{};
    QSplitter *contentSplitter_{};
    QVBoxLayout *pipWindowLayout_{};

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
    QStringList favoriteShowRules_;
    QHash<QString, QString> xspfNumberByTuneKey_;
    QHash<QString, QString> xspfProgramByChannel_;
    QString currentChannelName_;
    QString currentProgramId_;
    QString pendingDvrPath_;
    QHash<QString, QList<TvGuideEntry>> guideEntriesCache_;
    QStringList lastGuideChannelOrder_;
    QDateTime lastGuideWindowStartUtc_;
    int lastGuideSlotMinutes_{30};
    int lastGuideSlotCount_{12};
    QString lastGuideCacheGeneratedUtc_;
    QString lastGuideStatusText_;
    QString lastAutoFavoriteScheduleStamp_;
    QSet<QString> noAutoCurrentShowLookupChannels_;
    int reconnectAttemptCount_{0};
    const int maxReconnectAttempts_{6};
    bool userStoppedWatching_{false};
    bool suppressZapExitReconnect_{false};
    bool suppressBridgeExitReconnect_{false};
    bool useResilientBridgeMode_{false};
    bool resilientBridgeTried_{false};
    bool useVideoOnlyBridgeMode_{false};
    bool videoOnlyBridgeTried_{false};
    bool bridgeSawCodecParameterFailure_{false};
    bool waitingForDvrReady_{false};
    bool guideRefreshInProgress_{false};
    QString lastStatusBarMessage_{};
    bool fullscreenActive_{false};
    bool wasMaximizedBeforeFullscreen_{false};
    bool menuBarWasVisibleBeforeFullscreen_{true};
    bool statusBarWasVisibleBeforeFullscreen_{true};
    bool tabBarWasVisibleBeforeFullscreen_{true};
    int previousTabIndex_{0};
    int splitterHandleWidthBeforeFullscreen_{-1};
    QList<int> splitterSizesBeforeFullscreen_;
    QByteArray windowGeometryBeforeFullscreen_;
    QTimer *reconnectTimer_{};
    QTimer *currentShowTimer_{};
    QTimer *playbackAttachTimer_{};
    QTimer *guideRefreshTimer_{};
    QTimer *guideCachePollTimer_{};
    QTimer *scheduledSwitchTimer_{};
    TvGuideDialog *tvGuideDialog_{};
    int currentShowLookupSerial_{0};
    int playbackStartSerial_{0};
    QList<TvGuideScheduledSwitch> scheduledSwitches_;
    bool obeyScheduledSwitches_{true};
    bool videoDetachedToPip_{false};
    bool autoFavoriteShowSchedulingEnabled_{true};
    bool autoPictureInPictureEnabled_{true};
    bool deferStartupAutoFavoriteScheduling_{true};
    QStringList dismissedAutoFavoriteCandidates_;
    QStringList lockedAutoFavoriteSelections_;
    bool startupSwitchSummaryShown_{false};
};
