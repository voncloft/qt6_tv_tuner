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
class QTabWidget;
class QTableWidget;

struct TvGuideEntry {
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
};

class TvGuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TvGuideDialog(QWidget *parent = nullptr);

    void setLoadingState(const QString &message);
    void setGuideData(const QStringList &channelOrder,
                      const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                      const QDateTime &windowStartUtc,
                      int slotMinutes,
                      int slotCount,
                      const QString &statusText);

signals:
    void refreshRequested();

private:
    QString entryLabel(const TvGuideEntry &entry) const;
    QString entryToolTip(const TvGuideEntry &entry) const;
    bool channelHasVisibleData(const QString &channel) const;
    void renderGuideTable();

    QCheckBox *hideNoEitCheckBox_{};
    QPlainTextEdit *logsView_{};
    QPushButton *refreshButton_{};
    QTabWidget *tabs_{};
    QTableWidget *table_{};
    QStringList channelOrder_;
    QHash<QString, QList<TvGuideEntry>> entriesByChannel_;
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
};
