#pragma once

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTabWidget;
class QWidget;

struct TvGuideEntry {
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
    QString synopsis;
};

class TvGuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TvGuideDialog(QWidget *parent = nullptr);

    void setLoadingState(const QString &message);
    void setGuideData(const QStringList &channelOrder,
                      const QStringList &favoriteChannels,
                      const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                      const QDateTime &windowStartUtc,
                      int slotMinutes,
                      int slotCount,
                      const QString &statusText);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

signals:
    void refreshRequested();

private:
    QString entryLabel(const TvGuideEntry &entry) const;
    QString entryToolTip(const TvGuideEntry &entry) const;
    bool channelHasVisibleData(const QString &channel) const;
    int guideSlotPixelWidth() const;
    void applyGuideHorizontalScroll(int value);
    void renderGuideTable();

    QCheckBox *hideNoEitCheckBox_{};
    QCheckBox *showFavoritesOnlyCheckBox_{};
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
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
    int currentGuideSlotPixelWidth_{150};
    bool applyingGuideHorizontalScroll_{false};
    bool pendingGuideRender_{false};
};
