#pragma once

#include "DisplayTheme.h"

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
class QObject;
class QEvent;

struct TvGuideEntry {
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
    QString episode;
    QString synopsis;
};

struct TvGuideScheduledSwitch {
    QString channelName;
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
    QString episode;
    QString synopsis;
};

class TvGuideDialog : public QWidget
{
    Q_OBJECT

public:
    explicit TvGuideDialog(QWidget *parent = nullptr);

    void setLoadingState(const QString &message);
    void setDisplayTheme(const DisplayTheme &theme);
    void setGuideFilters(bool hideChannelsWithoutEitData, bool showFavoritesOnly);
    void syncToCurrentTime();
    void setGuideData(const QStringList &channelOrder,
                      const QStringList &favoriteChannels,
                      const QHash<QString, int> &favoriteShowRatings,
                      const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                      const QDateTime &windowStartUtc,
                      int slotMinutes,
                      int slotCount,
                      const QList<TvGuideScheduledSwitch> &scheduledSwitches,
                      const QString &statusText);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

signals:
    void refreshRequested();
    void watchRequested(const QString &channelName, const TvGuideEntry &entry);
    void scheduleSwitchRequested(const QString &channelName, const TvGuideEntry &entry, bool enabled);
    void searchScheduleRequested(const QString &favoriteShowTitle,
                                 const QString &channelName,
                                 const TvGuideEntry &entry);

private:
    struct SearchResult {
        QString channelName;
        TvGuideEntry entry;
        QString ratedTitle;
        QString episodeTitle;
        QString synopsisBody;
        QString timeChannelText;
        QString toolTip;
        QString normalizedHaystack;
        bool isFavorite{false};
    };

    QString entryLabel(const TvGuideEntry &entry) const;
    QString entryToolTip(const TvGuideEntry &entry) const;
    bool channelHasVisibleData(const QString &channel) const;
    bool isEntryScheduled(const QString &channelName, const TvGuideEntry &entry) const;
    int guideSlotPixelWidth() const;
    void applyGuideHorizontalScroll(int value);
    void scrollGuideToCurrentTime(bool force);
    void rebuildSearchIndex();
    void updateSearchResults();
    void updateSearchActionState();
    bool searchResultIsCurrent(const SearchResult &result) const;
    void scheduleSelectedSearchResult();
    void renderGuideTable();

    QLineEdit *showSearchEdit_{};
    QLabel *showSearchSummaryLabel_{};
    QListWidget *showSearchResultsList_{};
    QTimer *searchUpdateTimer_{};
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
    QHash<QString, int> favoriteShowRatings_;
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
    QList<SearchResult> searchIndex_;
    QList<SearchResult> searchResults_;
    DisplayTheme displayTheme_;
};
