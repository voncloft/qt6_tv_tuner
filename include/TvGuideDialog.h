#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTabWidget;
class QTimer;
class QWidget;
class QLineEdit;
class QLabel;
class QListWidget;

struct TvGuideEntry {
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
    QString synopsis;
};

struct TvGuideScheduledSwitch {
    QString channelName;
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
    QString synopsis;
};

class TvGuideDialog : public QWidget
{
    Q_OBJECT

public:
    explicit TvGuideDialog(QWidget *parent = nullptr);

    void setLoadingState(const QString &message);
    void setGuideFilters(bool hideChannelsWithoutEitData, bool showFavoritesOnly);
    void syncToCurrentTime();
    void setGuideData(const QStringList &channelOrder,
                      const QStringList &favoriteChannels,
                      const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                      const QDateTime &windowStartUtc,
                      int slotMinutes,
                      int slotCount,
                      const QList<TvGuideScheduledSwitch> &scheduledSwitches,
                      const QString &statusText);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

signals:
    void refreshRequested();
    void scheduleSwitchRequested(const QString &channelName, const TvGuideEntry &entry, bool enabled);
    void searchScheduleRequested(const QString &favoriteShowTitle,
                                 const QString &channelName,
                                 const TvGuideEntry &entry);

private:
    struct SearchResult {
        QString channelName;
        TvGuideEntry entry;
    };

    QString entryLabel(const TvGuideEntry &entry) const;
    QString entryToolTip(const TvGuideEntry &entry) const;
    bool channelHasVisibleData(const QString &channel) const;
    bool isEntryScheduled(const QString &channelName, const TvGuideEntry &entry) const;
    int guideSlotPixelWidth() const;
    void applyGuideHorizontalScroll(int value);
    void scrollGuideToCurrentTime(bool force);
    void updateSearchResults();
    void updateSearchActionState();
    void scheduleSelectedSearchResult();
    void renderGuideTable();

    QLineEdit *showSearchEdit_{};
    QLabel *showSearchSummaryLabel_{};
    QListWidget *showSearchResultsList_{};
    QPushButton *scheduleSearchResultButton_{};
    QPlainTextEdit *logsView_{};
    QPushButton *refreshButton_{};
    QTabWidget *tabs_{};
    QWidget *guideHeaderViewport_{};
    QWidget *guideHeaderContent_{};
    QWidget *guideChannelsViewport_{};
    QWidget *guideChannelsContent_{};
    QScrollArea *guideScrollArea_{};
    QWidget *guideContent_{};
    QStringList channelOrder_;
    QStringList favoriteChannels_;
    QHash<QString, QList<TvGuideEntry>> entriesByChannel_;
    QList<TvGuideScheduledSwitch> scheduledSwitches_;
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
    int currentGuideSlotPixelWidth_{150};
    bool pendingGuideRender_{false};
    bool hideChannelsWithoutEitData_{false};
    bool showFavoritesOnly_{false};
    bool pendingSyncToCurrentTime_{false};
    QList<SearchResult> searchResults_;
};
