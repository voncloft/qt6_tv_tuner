#include "TvGuideDialog.h"

#include <QCheckBox>
#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kHideNoEitChannelsSetting = "tvGuide/hideChannelsWithoutEit";

}

TvGuideDialog::TvGuideDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("TV Guide");
    resize(1240, 700);

    auto *layout = new QVBoxLayout(this);
    setStyleSheet(
        "QDialog { background-color: #000000; color: #ffffff; }"
        "QPushButton { background-color: #0f0f0f; color: #ffffff; border: 1px solid #444444; padding: 6px 12px; }"
        "QPushButton:disabled { color: #888888; border-color: #222222; }"
        "QTabWidget::pane { border: 1px solid #4a4a4a; top: -1px; }"
        "QTabBar::tab { background-color: #0a0a0a; color: #ffffff; border: 1px solid #4a4a4a; padding: 8px 14px; min-width: 110px; }"
        "QTabBar::tab:selected { background-color: #111111; }"
        "QPlainTextEdit { background-color: #000000; color: #ffffff; border: 1px solid #4a4a4a; }"
        "QTableWidget { background-color: #000000; alternate-background-color: #090909; color: #ffffff; gridline-color: #4a4a4a; }"
        "QHeaderView::section { background-color: #000000; color: #ffffff; border: 1px solid #4a4a4a; padding: 4px; }");

    auto *controls = new QHBoxLayout();
    refreshButton_ = new QPushButton("Refresh", this);
    connect(refreshButton_, &QPushButton::clicked, this, &TvGuideDialog::refreshRequested);

    controls->addStretch(1);
    controls->addWidget(refreshButton_, 0);
    layout->addLayout(controls);

    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_, 1);

    auto *guideTab = new QWidget(this);
    auto *guideLayout = new QVBoxLayout(guideTab);
    guideLayout->setContentsMargins(0, 0, 0, 0);

    hideNoEitCheckBox_ = new QCheckBox("Hide channels without EIT data", guideTab);
    hideNoEitCheckBox_->setChecked(QSettings("tv_tuner_gui", "watcher")
                                       .value(kHideNoEitChannelsSetting, false)
                                       .toBool());
    connect(hideNoEitCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings("tv_tuner_gui", "watcher");
        settings.setValue(kHideNoEitChannelsSetting, checked);
        renderGuideTable();
    });
    guideLayout->addWidget(hideNoEitCheckBox_);

    table_ = new QTableWidget(guideTab);
    table_->setAlternatingRowColors(true);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(true);
    table_->setWordWrap(true);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setDefaultSectionSize(54);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->horizontalHeader()->setDefaultSectionSize(150);
    guideLayout->addWidget(table_);
    tabs_->addTab(guideTab, "EIT Data");

    auto *logsTab = new QWidget(this);
    auto *logsLayout = new QVBoxLayout(logsTab);
    logsLayout->setContentsMargins(0, 0, 0, 0);

    logsView_ = new QPlainTextEdit(logsTab);
    logsView_->setReadOnly(true);
    logsView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logsView_->setPlainText("No guide data loaded yet.");
    logsLayout->addWidget(logsView_);
    tabs_->addTab(logsTab, "Logs");
}

void TvGuideDialog::setLoadingState(const QString &message)
{
    logsView_->setPlainText(message);
    refreshButton_->setEnabled(false);
    tabs_->setCurrentIndex(0);
}

QString TvGuideDialog::entryLabel(const TvGuideEntry &entry) const
{
    const QString startLocal = entry.startUtc.toLocalTime().toString("h:mm AP");
    const QString endLocal = entry.endUtc.toLocalTime().toString("h:mm AP");
    return QString("%1\n%2 - %3").arg(entry.title, startLocal, endLocal);
}

QString TvGuideDialog::entryToolTip(const TvGuideEntry &entry) const
{
    return QString("%1\n%2 - %3")
        .arg(entry.title,
             entry.startUtc.toLocalTime().toString("ddd h:mm AP"),
             entry.endUtc.toLocalTime().toString("ddd h:mm AP"));
}

void TvGuideDialog::setGuideData(const QStringList &channelOrder,
                                 const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                 const QDateTime &windowStartUtc,
                                 int slotMinutes,
                                 int slotCount,
                                 const QString &statusText)
{
    refreshButton_->setEnabled(true);
    logsView_->setPlainText(statusText);
    tabs_->setCurrentIndex(0);
    channelOrder_ = channelOrder;
    entriesByChannel_ = entriesByChannel;
    windowStartUtc_ = windowStartUtc;
    slotMinutes_ = slotMinutes;
    slotCount_ = slotCount;

    renderGuideTable();
}

bool TvGuideDialog::channelHasVisibleData(const QString &channel) const
{
    if (!windowStartUtc_.isValid() || slotMinutes_ <= 0 || slotCount_ <= 0) {
        return false;
    }

    const QDateTime windowEndUtc =
        windowStartUtc_.addSecs(static_cast<qint64>(slotMinutes_) * slotCount_ * 60);

    const QList<TvGuideEntry> entries = entriesByChannel_.value(channel);
    for (const TvGuideEntry &entry : entries) {
        if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
            continue;
        }
        if (entry.endUtc <= windowStartUtc_ || entry.startUtc >= windowEndUtc) {
            continue;
        }
        return true;
    }

    return false;
}

void TvGuideDialog::renderGuideTable()
{
    QStringList visibleChannels;
    visibleChannels.reserve(channelOrder_.size());
    for (const QString &channel : channelOrder_) {
        if (hideNoEitCheckBox_->isChecked() && !channelHasVisibleData(channel)) {
            continue;
        }
        visibleChannels << channel;
    }

    table_->clearSpans();
    table_->clearContents();
    table_->setRowCount(visibleChannels.size());
    table_->setColumnCount(std::max(slotCount_, 1));
    table_->setVerticalHeaderLabels(visibleChannels);

    QStringList horizontalLabels;
    for (int col = 0; col < slotCount_; ++col) {
        horizontalLabels << windowStartUtc_.addSecs(static_cast<qint64>(col) * slotMinutes_ * 60)
                                .toLocalTime()
                                .toString("h:mm AP");
    }
    if (horizontalLabels.isEmpty()) {
        horizontalLabels << "Now";
    }
    table_->setHorizontalHeaderLabels(horizontalLabels);

    const QDateTime windowEndUtc =
        windowStartUtc_.addSecs(static_cast<qint64>(slotMinutes_) * slotCount_ * 60);
    const qint64 slotSeconds = static_cast<qint64>(slotMinutes_) * 60;

    const int currentColumn = slotSeconds > 0
                                  ? static_cast<int>(windowStartUtc_.secsTo(QDateTime::currentDateTimeUtc()) / slotSeconds)
                                  : -1;

    for (int row = 0; row < visibleChannels.size(); ++row) {
        for (int col = 0; col < std::max(slotCount_, 1); ++col) {
            auto *emptyItem = new QTableWidgetItem;
            emptyItem->setBackground(QColor(0, 0, 0));
            emptyItem->setForeground(QColor(255, 255, 255));
            table_->setItem(row, col, emptyItem);
        }

        const QString channel = visibleChannels.at(row);
        QList<TvGuideEntry> entries = entriesByChannel_.value(channel);
        std::sort(entries.begin(), entries.end(), [](const TvGuideEntry &a, const TvGuideEntry &b) {
            return a.startUtc < b.startUtc;
        });

        QVector<bool> occupied(std::max(slotCount_, 1), false);
        bool insertedAny = false;

        for (const TvGuideEntry &entry : entries) {
            if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
                continue;
            }
            if (entry.endUtc <= windowStartUtc_ || entry.startUtc >= windowEndUtc) {
                continue;
            }

            const qint64 startOffset = windowStartUtc_.secsTo(entry.startUtc);
            const qint64 endOffset = windowStartUtc_.secsTo(entry.endUtc);
            int startCol = static_cast<int>(std::floor(static_cast<double>(startOffset) / slotSeconds));
            int endCol = static_cast<int>(std::ceil(static_cast<double>(endOffset) / slotSeconds));
            startCol = std::max(startCol, 0);
            endCol = std::min(endCol, slotCount_);
            if (endCol <= startCol) {
                continue;
            }

            while (startCol < slotCount_ && occupied[startCol]) {
                ++startCol;
            }
            if (startCol >= slotCount_) {
                continue;
            }
            if (endCol <= startCol) {
                endCol = startCol + 1;
            }
            const int span = std::max(1, endCol - startCol);

            for (int col = startCol; col < std::min(slotCount_, startCol + span); ++col) {
                auto *item = new QTableWidgetItem(col == startCol ? entryLabel(entry) : QString());
                item->setToolTip(entryToolTip(entry));
                item->setTextAlignment(Qt::AlignTop | Qt::AlignLeft);
                item->setBackground(QColor(0, 0, 0));
                item->setForeground(QColor(255, 255, 255));
                table_->setItem(row, col, item);
                occupied[col] = true;
            }
            insertedAny = true;
        }

        if (!insertedAny) {
            for (int col = 0; col < std::max(slotCount_, 1); ++col) {
                auto *noData = new QTableWidgetItem("NO EIT DATA");
                noData->setTextAlignment(Qt::AlignCenter);
                noData->setBackground(QColor(0, 0, 0));
                noData->setForeground(QColor(255, 255, 255));
                table_->setItem(row, col, noData);
            }
        }
    }

    if (currentColumn >= 0 && currentColumn < slotCount_) {
        for (int row = 0; row < table_->rowCount(); ++row) {
            if (table_->item(row, currentColumn) == nullptr) {
                auto *marker = new QTableWidgetItem;
                marker->setFlags(marker->flags() & ~Qt::ItemIsEnabled);
                marker->setBackground(QColor(0, 0, 0));
                marker->setForeground(QColor(255, 255, 255));
                table_->setItem(row, currentColumn, marker);
            }
            table_->item(row, currentColumn)->setBackground(QColor(0, 0, 0));
            table_->item(row, currentColumn)->setForeground(QColor(255, 255, 255));
        }
        if (table_->horizontalHeaderItem(currentColumn) != nullptr) {
            table_->horizontalHeaderItem(currentColumn)->setBackground(QColor(0, 0, 0));
            table_->horizontalHeaderItem(currentColumn)->setForeground(QColor(255, 255, 255));
        }
    }
}
