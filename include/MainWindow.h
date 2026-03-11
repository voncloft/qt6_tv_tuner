#pragma once

#include "DisplayTheme.h"
#include "TvGuideDialog.h"

#include <QByteArray>
#include <QKeySequence>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QProcess>
#include <QHash>
#include <QList>
#include <QSet>

class QComboBox;
class QDialog;
class QFontComboBox;
class QAction;
class QKeyEvent;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QShortcut;
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
class QResizeEvent;
class QWidget;
class QSplitter;
class QTabWidget;
class QVBoxLayout;
class QCheckBox;
class QSpinBox;
class QGroupBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
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
    void handleSignalMonitorFinished(int exitCode, QProcess::ExitStatus exitStatus);
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
    void handleGuideShowTodayOnlyListingsToggled(bool checked);
    void handleAutoFavoriteShowSchedulingToggled(bool checked);
    void handleAutoPictureInPictureToggled(bool checked);
    void handleHideStartupSwitchSummaryToggled(bool checked);
    void handleGuideRefreshIntervalChanged(int minutes);
    void handleGuideCacheRetentionChanged(int hours);
    void exportSchedulesDirectJson();
    void handleGuideWatchRequested(const QString &channelName, const TvGuideEntry &entry);
    void handleSearchScheduleRequested(const QString &favoriteShowTitle,
                                       const QString &channelName,
                                       const TvGuideEntry &entry);
    void addTestingBugItem();
    void removeSelectedTestingBugItem();
    void removeSelectedFavoriteShowRule();
    void removeSelectedScheduledSwitch();

private:
    struct DisplayFontEditorWidgets {
        QFontComboBox *family{};
        QSpinBox *size{};
        QCheckBox *bold{};
        QCheckBox *italic{};
        QCheckBox *underline{};
    };

    static constexpr int kQuickFavoriteCount = 10;

    void buildUi();
    void applyDisplayTheme(bool persistCurrentTheme);
    void syncConfigGroupBoxHeights();
    void refreshDisplayThemeControls();
    void refreshSavedDisplayThemeList(const QString &preferredSelection = QString());
    void chooseDisplayThemeColor(const QString &roleKey);
    void handleDisplayThemeFontEdited(const QString &roleKey);
    void loadSelectedDisplayTheme();
    void saveCurrentDisplayThemeAsNew();
    void overwriteSelectedDisplayTheme();
    void deleteSelectedDisplayTheme();
    void resetCurrentDisplayThemeToDefaults();
    bool persistDisplayThemeStore(const QString &statusMessage, bool appendToLog);
    void setDisplayThemeStatusMessage(const QString &text, bool appendToLog = false);
    void updateDisplayThemeColorButton(const QString &roleKey);
    void setScanningState(bool running);
    void appendLog(const QString &line);
    // Keep user/program behavior observable: new interaction paths should log entry and outcome here.
    void logInteraction(const QString &actor, const QString &action, const QString &details = QString());
    void parseAndStoreLine(const QString &line);
    bool persistChannelsFile();
    bool startWatchingChannel(const QString &channelName,
                              bool reconnectAttempt = false,
                              const QString &channelLine = QString());
    void refreshQuickButtons();
    void saveFavorites();
    void loadFavorites();
    void loadXspfChannelHints();
    void loadChannelsFileIfPresent();
    void startPlaybackFromDvr(const QString &dvrPath);
    bool refreshGuideData(bool interactive, bool updateDialog);
    bool refreshGuideDataFromSchedulesDirect(bool interactive, bool updateDialog);
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
    QString playbackSeekLabelText(qint64 positionMs) const;
    bool processedPlaybackEnabled() const;
    void applyAudioOutputState();
    void syncPlaybackSeekUi();
    void applyPlaybackSeekPosition(qint64 positionMs);
    void armRecoveryAudioMute(const QString &reason);
    void beginRecoveryAudioMute(const QString &reason);
    void clearRecoveryAudioMute();
    void refreshRecoveryAudioMuteGate();
    bool isRecoveryAudioStable() const;
    void startSignalMonitor(int adapter, int frontend);
    void stopSignalMonitor();
    void handleSignalMonitorOutput(const QString &chunk);
    void setSignalMonitorStatus(const QString &text, const QString &toolTip = QString());
    void setStatusBarStateMessage(const QString &text);
    void showTransientStatusBarMessage(const QString &text, int timeoutMs = 3000);
    QString channelTableShowText(const QString &channelName) const;
    void refreshChannelTableShowColumn();
    void updateTvGuideDialogFromCurrentCache(bool showStatusMessage = false);
    QStringList makeArguments() const;
    QString selectedChannelNameFromTable() const;
    QString selectedChannelLineFromTable() const;
    QString firstChannelLineForName(const QString &channelName) const;
    QString programIdForChannel(const QString &channelName) const;
    bool highlightChannelInTable(const QString &channelName);
    bool highlightChannelLineInTable(const QString &channelLine);
    void probeCurrentShowAfterTune(const QString &channelName, int lookupSerial);
    void refreshCurrentShowStatus();
    void scheduleCurrentShowRefresh(const QDateTime &refreshUtc);
    void setCurrentShowStatus(const QString &text,
                              const QString &toolTip = QString(),
                              const QString &synopsisText = QString());
    bool applyCurrentShowStatusFromGuideCache();
    void stopProcess(QProcess *process, int timeoutMs);
    QWidget *modalDialogParent() const;
    void prepareModalWindow(QWidget *window, const QString &reason = QString());
    void restoreAfterModalWindow();
    int execModalDialog(QDialog *dialog, const QString &reason = QString());
    void showAboutDialog(const QString &title, const QString &text);
    void showInformationDialog(const QString &title, const QString &text);
    void showWarningDialog(const QString &title, const QString &text);
    void showCriticalDialog(const QString &title, const QString &text);
    QString promptItemSelection(const QString &title,
                                const QString &label,
                                const QStringList &items,
                                int current = 0,
                                bool editable = false);
    void syncFullscreenOverlayState();
    void positionFullscreenOverlay();
    void showFullscreenCursor();
    void hideFullscreenCursor();
    void restartFullscreenCursorHideTimer();
    void enterFullscreen();
    void exitFullscreen();
    void saveChannelSidebarSizing();
    void restoreChannelSidebarSizing();
    void restoreLastPlayedChannel();
    void handleGuideScheduleToggle(const QString &channelName, const TvGuideEntry &entry, bool enabled);
    void handleObeyScheduledSwitchesChanged(bool obey);
    bool saveScheduledSwitches() const;
    bool loadScheduledSwitches();
    bool pruneExpiredScheduledSwitches(bool includeStartedSwitches = false);
    bool hasActiveScheduledSwitchesNow() const;
    void refreshScheduledSwitchTimer();
    void processScheduledSwitches();
    void loadFavoriteShowRules();
    void saveFavoriteShowRules() const;
    void refreshFavoriteShowRuleList();
    QString favoriteShowRuleDisplayLabel(const QString &title) const;
    int favoriteShowRating(const QString &title) const;
    void setFavoriteShowRating(const QString &title, int rating);
    void syncFavoriteShowRatingControls();
    void refreshScheduledSwitchList();
    bool useSchedulesDirectGuideSource() const;
    bool guideShowTodayOnlyListingsEnabled() const;
    bool refreshGuideWhenCacheRunsOutEnabled() const;
    bool maybeRefreshGuideWhenCacheRunsOut(bool updateDialog);
    void updateSchedulesDirectControls();
    bool ensureSchedulesDirectJson(bool allowCachedExport,
                                   bool *usedCachedExport,
                                   QString *summary,
                                   QString *errorText);
    void loadTestingBugItems();
    void saveTestingBugItems() const;
    bool addTestingBugItemEntry(const QString &text, bool checked);
    bool addFavoriteShowRule(const QString &title);
    bool removeFavoriteShowRule(const QString &title);
    bool resolveScheduledSwitchChoices(const QList<TvGuideScheduledSwitch> &candidates,
                                       const QString &sourceDescription,
                                       bool promptForConflict,
                                       bool forceRatingChoice = false);
    bool addScheduledSwitchCandidate(const TvGuideScheduledSwitch &candidate,
                                     const QString &sourceDescription,
                                     bool promptForConflict);
    void scheduleMatchingGuideEntriesForTitle(const QString &favoriteShowTitle,
                                              const TvGuideScheduledSwitch &seedCandidate,
                                              const QString &sourceDescription,
                                              bool promptForConflict);
    void autoScheduleFavoriteShowsFromGuideCache(bool promptForConflict, bool forceCurrentCacheSearch);
    void showStartupSwitchSummary();
    bool shouldDetachVideoForCurrentTab(int index) const;
    void detachVideoToPip();
    void attachVideoFromPip();
    bool applySchedulesDirectGuideFallback(QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                           int retentionHours,
                                           const QDateTime &nowUtc,
                                           QDateTime *latestEndUtc,
                                           QStringList *importedChannels,
                                           QStringList *unmatchedChannels,
                                           QStringList *skippedChannels,
                                           int *importedEntryCount);
    QHash<QString, QList<TvGuideEntry>>
    filterGuideEntriesForConfiguredListingsScope(const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                                 QDateTime *latestEndUtc = nullptr) const;
    void applyGuideFilterSettings();
    void applyGuideRefreshIntervalSetting();
    bool purgeExpiredGuideCacheFiles(bool clearLoadedState, bool includeSchedulesDirect = false);
    void clearLoadedGuideCache();
    void loadKeyBindings();
    void applyKeyBindings();
    void refreshKeyBindingEditors();
    bool updateKeyBinding(const QString &actionId, const QKeySequence &sequence, bool showConflictStatus = true);
    QString keyBindingActionLabel(const QString &actionId) const;
    QKeySequence keyBindingSequence(const QString &actionId) const;
    bool triggerKeyBindingAction(const QString &actionId);
    bool handlePlaybackKeyBinding(QKeyEvent *keyEvent);
    bool stepChannelSelection(int direction);
    void adjustVolumeByDelta(int delta);

    QComboBox *frontendTypeCombo_{};
    QLineEdit *countryEdit_{};
    QSpinBox *adapterSpin_{};
    QSpinBox *frontendSpin_{};
    QComboBox *outputFormatCombo_{};

    QPushButton *startButton_{};
    QPushButton *stopButton_{};
    QPushButton *watchButton_{};
    QPushButton *stopWatchButton_{};
    QPushButton *pauseButton_{};
    QPushButton *openFileButton_{};
    QPushButton *addFavoriteButton_{};
    QPushButton *removeFavoriteButton_{};
    QPushButton *quickFavoriteButtons_[kQuickFavoriteCount]{};
    QPushButton *muteButton_{};
    QPushButton *fullscreenButton_{};
    QPushButton *pipToggleButton_{};
    QPlainTextEdit *logOutput_{};
    QTableWidget *channelsTable_{};
    QVideoWidget *videoWidget_{};
    QVideoWidget *pipVideoWidget_{};
    QWidget *fullscreenWindow_{};
    QVideoWidget *fullscreenVideoWidget_{};
    QWidget *fullscreenOverlayContainer_{};
    QPushButton *fullscreenWatchButton_{};
    QPushButton *fullscreenStopWatchButton_{};
    QPushButton *fullscreenPauseButton_{};
    QPushButton *fullscreenMuteButton_{};
    QSlider *fullscreenVolumeSlider_{};
    QSlider *fullscreenSeekSlider_{};
    QLabel *fullscreenPlaybackStatusLabel_{};
    QLabel *fullscreenSignalMonitorLabel_{};
    QLabel *fullscreenCurrentShowLabel_{};
    QLabel *fullscreenCurrentShowSynopsisLabel_{};
    QLabel *fullscreenSeekPositionLabel_{};
    QLabel *fullscreenSeekDurationLabel_{};
    QWidget *fullscreenSeekControlsContainer_{};
    QLabel *videoDetachedPlaceholderLabel_{};
    QWidget *pipWindow_{};
    QLabel *playbackStatusLabel_{};
    QLabel *signalMonitorLabel_{};
    QLabel *currentShowLabel_{};
    QLabel *currentShowSynopsisLabel_{};
    QSlider *volumeSlider_{};
    QSlider *seekSlider_{};
    QLabel *seekPositionLabel_{};
    QLabel *seekDurationLabel_{};
    QWidget *seekControlsContainer_{};
    QCheckBox *hideNoEitChannelsCheckBox_{};
    QCheckBox *showFavoritesOnlyCheckBox_{};
    QCheckBox *showTodayOnlyListingsCheckBox_{};
    QCheckBox *obeyScheduledSwitchesCheckBox_{};
    QCheckBox *autoFavoriteShowSchedulingCheckBox_{};
    QCheckBox *favoriteShowRatingsOverrideCheckBox_{};
    QCheckBox *autoPictureInPictureCheckBox_{};
    QCheckBox *processedPlaybackCheckBox_{};
    QCheckBox *hideStartupSwitchSummaryCheckBox_{};
    QCheckBox *disableTooltipsCheckBox_{};
    QCheckBox *useSchedulesDirectGuideCheckBox_{};
    QCheckBox *refreshGuideWhenCacheRunsOutCheckBox_{};
    QCheckBox *logAutoScrollCheckBox_{};
    QComboBox *guideRefreshIntervalCombo_{};
    QComboBox *guideCacheRetentionCombo_{};
    QLineEdit *favoriteShowRuleEdit_{};
    QLineEdit *testingBugItemEdit_{};
    QLineEdit *schedulesDirectUsernameEdit_{};
    QLineEdit *schedulesDirectPasswordEdit_{};
    QLineEdit *schedulesDirectPostalCodeEdit_{};
    QSpinBox *favoriteShowRatingSpin_{};
    QListWidget *favoriteShowRulesList_{};
    QListWidget *scheduledSwitchesList_{};
    QListWidget *testingBugItemsList_{};
    QPushButton *addFavoriteShowRuleButton_{};
    QPushButton *exportSchedulesDirectButton_{};
    QPushButton *saveTestingBugItemButton_{};
    QPushButton *removeTestingBugItemButton_{};
    QPushButton *removeFavoriteShowRuleButton_{};
    QPushButton *removeScheduledSwitchButton_{};
    QLabel *schedulesDirectStatusLabel_{};
    QLabel *displayThemeFilePathLabel_{};
    QLabel *displayThemeStatusLabel_{};
    QTabWidget *tabs_{};
    QWidget *watchPage_{};
    QWidget *configPage_{};
    QWidget *displayOptionsPage_{};
    QWidget *keyBindingsPage_{};
    QGroupBox *configGuideOptionsGroup_{};
    QGroupBox *configPlaybackOptionsGroup_{};
    QGroupBox *configCacheOptionsGroup_{};
    QGroupBox *configSchedulesDirectGroup_{};
    QWidget *watchControlsContainer_{};
    QWidget *favoritesContainer_{};
    QWidget *statusContainer_{};
    QSplitter *contentSplitter_{};
    QVBoxLayout *pipWindowLayout_{};

    QProcess *scanProcess_{};
    QProcess *zapProcess_{};
    QProcess *streamBridgeProcess_{};
    QProcess *signalMonitorProcess_{};
    QMediaPlayer *mediaPlayer_{};
    QAudioOutput *audioOutput_{};
    QString partialStdOut_;
    QString partialStdErr_;
    QString partialSignalMonitorOutput_;
    QString channelsFilePath_;
    QString logFilePath_;
    QStringList channelLines_;
    QStringList favorites_;
    QStringList favoriteShowRules_;
    QHash<QString, int> favoriteShowRatings_;
    QHash<QString, QKeySequence> keyBindings_;
    QHash<QString, QString> xspfNumberByTuneKey_;
    QHash<QString, QString> xspfProgramByChannel_;
    QHash<QString, QStringList> pendingScanChannelNumbersByName_;
    QString currentChannelName_;
    QString currentChannelLine_;
    QString currentProgramId_;
    QString pendingDvrPath_;
    QHash<QString, QList<TvGuideEntry>> guideEntriesCache_;
    QStringList lastGuideChannelOrder_;
    QDateTime lastGuideWindowStartUtc_;
    int lastGuideSlotMinutes_{30};
    int lastGuideSlotCount_{12};
    QString lastGuideCacheGeneratedUtc_;
    QString lastGuideStatusText_;
    QString lastGuideDialogPresentationStamp_;
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
    bool videoOnlyAudioRecoveryTried_{false};
    bool processedPlaybackActive_{false};
    bool muteRecoveryAfterAudioRebuildFailure_{false};
    bool recoveryAudioMuted_{false};
    bool bridgeSawCodecParameterFailure_{false};
    bool waitingForDvrReady_{false};
    bool guideRefreshInProgress_{false};
    bool channelHintsDirty_{false};
    QString lastStatusBarMessage_{};
    bool fullscreenActive_{false};
    bool fullscreenCursorHidden_{false};
    bool manualPictureInPictureRequested_{false};
    bool disableTooltips_{false};
    QDateTime guideCacheRunoutRefreshRetryUtc_;
    QTimer *reconnectTimer_{};
    QTimer *currentShowTimer_{};
    QTimer *playbackAttachTimer_{};
    QTimer *guideRefreshTimer_{};
    QTimer *guideCachePollTimer_{};
    QTimer *scheduledSwitchTimer_{};
    QTimer *fullscreenCursorHideTimer_{};
    QTimer *audioRecoveryUnmuteTimer_{};
    TvGuideDialog *tvGuideDialog_{};
    int currentShowLookupSerial_{0};
    int playbackStartSerial_{0};
    QList<TvGuideScheduledSwitch> scheduledSwitches_;
    bool obeyScheduledSwitches_{true};
    bool videoDetachedToPip_{false};
    bool autoFavoriteShowSchedulingEnabled_{true};
    bool favoriteShowRatingsOverrideEnabled_{false};
    bool autoPictureInPictureEnabled_{true};
    bool deferStartupAutoFavoriteScheduling_{true};
    QString currentShowOverlayToolTip_;
    QString signalMonitorOverlayToolTip_;
    QStringList dismissedAutoFavoriteCandidates_;
    bool startupSwitchSummaryShown_{false};
    bool scheduledSwitchListRefreshPending_{false};
    QLineEdit *displayThemeNameEdit_{};
    QComboBox *displayThemeSavedThemesCombo_{};
    QPushButton *displayThemeLoadButton_{};
    QPushButton *displayThemeSaveAsButton_{};
    QPushButton *displayThemeOverwriteButton_{};
    QPushButton *displayThemeDeleteButton_{};
    QPushButton *displayThemeResetButton_{};
    QHash<QString, QPushButton *> displayThemeColorButtons_;
    QHash<QString, DisplayFontEditorWidgets> displayThemeFontEditors_;
    QHash<QString, QKeySequenceEdit *> keyBindingEditors_;
    QHash<QString, QLabel *> keyBindingConflictLabels_;
    QHash<QString, QShortcut *> keyBindingShortcuts_;
    QAction *aboutAction_{};
    DisplayTheme defaultDisplayTheme_;
    DisplayTheme currentDisplayTheme_;
    DisplayThemeStore displayThemeStore_;
    QString pendingDisplayThemeLoadError_;
    bool syncingDisplayThemeUi_{false};
};
