#include "TvGuideDialog.h"

#include <QCheckBox>
#include <QColor>
#include <QFrame>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTextLayout>
#include <QTextOption>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kHideNoEitChannelsSetting = "tvGuide/hideChannelsWithoutEit";
constexpr auto kShowFavoritesOnlySetting = "tvGuide/showOnlyFavorites";
constexpr int kGuideChannelLabelWidth = 190;
constexpr int kGuideSlotWidth = 150;
constexpr int kGuideHeaderHeight = 40;
constexpr int kGuideRowHeight = 96;
constexpr int kGuideGridLineWidth = 2;
constexpr int kGuideBoxBorderWidth = 2;

QString formatEntryLabel(const TvGuideEntry &entry)
{
    return entry.title;
}

QString formatEntryToolTip(const TvGuideEntry &entry)
{
    return QString("%1\n%2 - %3")
        .arg(entry.title,
             entry.startUtc.toLocalTime().toString("ddd h:mm AP"),
             entry.endUtc.toLocalTime().toString("ddd h:mm AP"));
}

void drawEntryTitle(QPainter &painter, const QRect &textRect, const QString &title)
{
    if (!textRect.isValid() || title.trimmed().isEmpty()) {
        return;
    }

    const QFontMetrics metrics(painter.font());
    const int lineHeight = std::max(1, metrics.lineSpacing());
    const int maxLines = std::max(1, textRect.height() / lineHeight);

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextLayout layout(title, painter.font());
    layout.setTextOption(option);
    layout.beginLayout();

    QStringList lines;
    for (int lineIndex = 0; lineIndex < maxLines; ++lineIndex) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }

        line.setLineWidth(textRect.width());
        const int start = line.textStart();
        const int length = line.textLength();
        const bool hasMore = (start + length) < title.size();

        QString lineText;
        if (lineIndex == maxLines - 1 && hasMore) {
            lineText = metrics.elidedText(title.mid(start).trimmed(), Qt::ElideRight, textRect.width());
        } else {
            lineText = title.mid(start, length).trimmed();
        }

        if (!lineText.isEmpty()) {
            lines << lineText;
        }
    }

    layout.endLayout();

    painter.save();
    painter.setClipRect(textRect);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QRect lineRect(textRect.left(),
                             textRect.top() + lineIndex * lineHeight,
                             textRect.width(),
                             lineHeight);
        painter.drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, lines.at(lineIndex));
    }
    painter.restore();
}

void clearLayout(QLayout *layout)
{
    if (layout == nullptr) {
        return;
    }

    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        }
        if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

class GuideTimelineHeaderWidget final : public QWidget
{
public:
    GuideTimelineHeaderWidget(const QDateTime &windowStartUtc, int slotMinutes, int slotCount, QWidget *parent = nullptr)
        : QWidget(parent)
        , windowStartUtc_(windowStartUtc)
        , slotMinutes_(slotMinutes)
        , slotCount_(slotCount)
    {
        setMinimumHeight(kGuideHeaderHeight);
        setMaximumHeight(kGuideHeaderHeight);
        setMinimumWidth(std::max(slotCount_, 1) * kGuideSlotWidth);
    }

    QSize sizeHint() const override
    {
        return {std::max(slotCount_, 1) * kGuideSlotWidth, kGuideHeaderHeight};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0));
        painter.setPen(QPen(QColor(74, 74, 74), kGuideGridLineWidth));

        const int slotCount = std::max(slotCount_, 1);
        for (int col = 0; col <= slotCount; ++col) {
            const int x = std::lround(static_cast<double>(col) * width() / slotCount);
            painter.drawLine(x, 0, x, height());
        }
        painter.drawLine(0, height() - 1, width(), height() - 1);

        painter.setPen(QColor(255, 255, 255));
        for (int col = 0; col < slotCount_; ++col) {
            const int left = std::lround(static_cast<double>(col) * width() / slotCount);
            const int right = std::lround(static_cast<double>(col + 1) * width() / slotCount);
            const QRect slotRect(left, 0, std::max(1, right - left), height());
            const QString label = windowStartUtc_.addSecs(static_cast<qint64>(col) * slotMinutes_ * 60)
                                      .toLocalTime()
                                      .toString("h:mm AP");
            painter.drawText(slotRect.adjusted(6, 0, -6, 0), Qt::AlignHCenter | Qt::AlignVCenter, label);
        }

        const qint64 totalSeconds = static_cast<qint64>(slotMinutes_) * slotCount_ * 60;
        if (totalSeconds <= 0) {
            return;
        }

        const qint64 nowOffset = windowStartUtc_.secsTo(QDateTime::currentDateTimeUtc());
        if (nowOffset < 0 || nowOffset > totalSeconds) {
            return;
        }

        const int nowX = std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * width() / totalSeconds)),
                                    0,
                                    width() - 1);
        painter.setPen(QPen(QColor(255, 96, 96), 2));
        painter.drawLine(nowX, 0, nowX, height());
    }

private:
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
};

class GuideChannelBandWidget final : public QWidget
{
public:
    GuideChannelBandWidget(const QList<TvGuideEntry> &entries,
                           const QDateTime &windowStartUtc,
                           int slotMinutes,
                           int slotCount,
                           QWidget *parent = nullptr)
        : QWidget(parent)
        , entries_(entries)
        , windowStartUtc_(windowStartUtc)
        , slotMinutes_(slotMinutes)
        , slotCount_(slotCount)
    {
        setMouseTracking(true);
        setMinimumHeight(kGuideRowHeight);
        setMaximumHeight(kGuideRowHeight);
        setMinimumWidth(std::max(slotCount_, 1) * kGuideSlotWidth);
    }

    QSize sizeHint() const override
    {
        return {std::max(slotCount_, 1) * kGuideSlotWidth, kGuideRowHeight};
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip) {
            auto *helpEvent = static_cast<QHelpEvent *>(event);
            for (const RenderedEntry &rendered : renderedEntries_) {
                if (rendered.rect.contains(helpEvent->pos())) {
                    QToolTip::showText(helpEvent->globalPos(), formatEntryToolTip(rendered.entry), this, rendered.rect);
                    return true;
                }
            }
            QToolTip::hideText();
            event->ignore();
            return true;
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        renderedEntries_.clear();

        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0));

        const int slotCount = std::max(slotCount_, 1);
        painter.setPen(QPen(QColor(52, 52, 52), kGuideGridLineWidth));
        for (int col = 0; col <= slotCount; ++col) {
            const int x = std::lround(static_cast<double>(col) * width() / slotCount);
            painter.drawLine(x, 0, x, height());
        }
        painter.drawLine(0, height() - 1, width(), height() - 1);

        const qint64 totalSeconds = static_cast<qint64>(slotMinutes_) * slotCount_ * 60;
        if (totalSeconds <= 0) {
            return;
        }

        const QDateTime windowEndUtc = windowStartUtc_.addSecs(totalSeconds);
        bool renderedAny = false;
        QList<TvGuideEntry> entries = entries_;
        std::sort(entries.begin(), entries.end(), [](const TvGuideEntry &a, const TvGuideEntry &b) {
            return a.startUtc < b.startUtc;
        });

        for (const TvGuideEntry &entry : entries) {
            if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
                continue;
            }
            if (entry.endUtc <= windowStartUtc_ || entry.startUtc >= windowEndUtc) {
                continue;
            }

            const qint64 visibleStart = std::clamp(windowStartUtc_.secsTo(entry.startUtc), 0LL, totalSeconds);
            const qint64 visibleEnd = std::clamp(windowStartUtc_.secsTo(entry.endUtc), 0LL, totalSeconds);
            if (visibleEnd <= visibleStart) {
                continue;
            }

            const int left = std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleStart) * width() / totalSeconds)),
                                        0,
                                        width() - 1);
            const int right = std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleEnd) * width() / totalSeconds)),
                                         left + 1,
                                         width());
            QRect box(left + 2, 5, std::max(28, right - left - 4), height() - 10);

            const bool airingNow = entry.startUtc <= QDateTime::currentDateTimeUtc() &&
                                   entry.endUtc > QDateTime::currentDateTimeUtc();
            painter.setPen(QPen(QColor(120, 120, 120), kGuideBoxBorderWidth));
            painter.setBrush(airingNow ? QColor(28, 28, 28) : QColor(16, 16, 16));
            painter.drawRect(box);

            painter.setPen(QColor(255, 255, 255));
            drawEntryTitle(painter, box.adjusted(10, 8, -10, -8), formatEntryLabel(entry));
            renderedEntries_.append({box, entry});
            renderedAny = true;
        }

        if (!renderedAny) {
            painter.setPen(QColor(160, 160, 160));
            painter.drawText(rect().adjusted(10, 0, -10, 0), Qt::AlignCenter, "NO EIT DATA");
        }

        const qint64 nowOffset = windowStartUtc_.secsTo(QDateTime::currentDateTimeUtc());
        if (nowOffset >= 0 && nowOffset <= totalSeconds) {
            const int nowX = std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * width() / totalSeconds)),
                                        0,
                                        width() - 1);
            painter.setPen(QPen(QColor(255, 96, 96), 2));
            painter.drawLine(nowX, 0, nowX, height());
        }
    }

private:
    struct RenderedEntry {
        QRect rect;
        TvGuideEntry entry;
    };

    QList<TvGuideEntry> entries_;
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
    QVector<RenderedEntry> renderedEntries_;
};

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

    showFavoritesOnlyCheckBox_ = new QCheckBox("Show only favorites", guideTab);
    showFavoritesOnlyCheckBox_->setChecked(QSettings("tv_tuner_gui", "watcher")
                                               .value(kShowFavoritesOnlySetting, false)
                                               .toBool());
    connect(showFavoritesOnlyCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings("tv_tuner_gui", "watcher");
        settings.setValue(kShowFavoritesOnlySetting, checked);
        renderGuideTable();
    });
    guideLayout->addWidget(showFavoritesOnlyCheckBox_);

    auto *guideMatrix = new QWidget(guideTab);
    auto *guideMatrixLayout = new QGridLayout(guideMatrix);
    guideMatrixLayout->setContentsMargins(0, 0, 0, 0);
    guideMatrixLayout->setHorizontalSpacing(10);
    guideMatrixLayout->setVerticalSpacing(0);

    auto *cornerLabel = new QLabel("Channel", guideMatrix);
    cornerLabel->setMinimumWidth(kGuideChannelLabelWidth);
    cornerLabel->setMaximumWidth(kGuideChannelLabelWidth);
    cornerLabel->setMinimumHeight(kGuideHeaderHeight);
    cornerLabel->setMaximumHeight(kGuideHeaderHeight);
    cornerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    cornerLabel->setStyleSheet("QLabel { color: #ffffff; font-weight: 600; }");
    guideMatrixLayout->addWidget(cornerLabel, 0, 0);

    guideHeaderViewport_ = new QWidget(guideMatrix);
    guideHeaderViewport_->setMinimumHeight(kGuideHeaderHeight);
    guideHeaderViewport_->setMaximumHeight(kGuideHeaderHeight);
    guideHeaderViewport_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    guideHeaderViewport_->setContentsMargins(0, 0, 0, 0);
    guideHeaderContent_ = new QWidget(guideHeaderViewport_);
    auto *guideHeaderLayout = new QVBoxLayout(guideHeaderContent_);
    guideHeaderLayout->setContentsMargins(0, 0, 0, 0);
    guideHeaderLayout->setSpacing(0);
    guideMatrixLayout->addWidget(guideHeaderViewport_, 0, 1);

    guideChannelsViewport_ = new QWidget(guideMatrix);
    guideChannelsViewport_->setMinimumWidth(kGuideChannelLabelWidth);
    guideChannelsViewport_->setMaximumWidth(kGuideChannelLabelWidth);
    guideChannelsViewport_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    guideChannelsViewport_->setContentsMargins(0, 0, 0, 0);
    guideChannelsContent_ = new QWidget(guideChannelsViewport_);
    auto *guideChannelsLayout = new QVBoxLayout(guideChannelsContent_);
    guideChannelsLayout->setContentsMargins(0, 0, 0, 0);
    guideChannelsLayout->setSpacing(0);
    guideMatrixLayout->addWidget(guideChannelsViewport_, 1, 0);

    guideScrollArea_ = new QScrollArea(guideMatrix);
    guideScrollArea_->setFrameShape(QFrame::NoFrame);
    guideScrollArea_->setWidgetResizable(false);
    guideContent_ = new QWidget(guideScrollArea_);
    auto *guideRowsLayout = new QVBoxLayout(guideContent_);
    guideRowsLayout->setContentsMargins(0, 0, 0, 0);
    guideRowsLayout->setSpacing(0);
    guideScrollArea_->setWidget(guideContent_);
    guideMatrixLayout->addWidget(guideScrollArea_, 1, 1);
    guideMatrixLayout->setColumnStretch(1, 1);
    guideMatrixLayout->setRowStretch(1, 1);
    guideLayout->addWidget(guideMatrix, 1);

    connect(guideScrollArea_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (guideHeaderContent_ == nullptr) {
            return;
        }
        guideHeaderContent_->move(-value, 0);
    });
    connect(guideScrollArea_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (guideChannelsContent_ == nullptr) {
            return;
        }
        guideChannelsContent_->move(0, -value);
    });
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
    return formatEntryLabel(entry);
}

QString TvGuideDialog::entryToolTip(const TvGuideEntry &entry) const
{
    return formatEntryToolTip(entry);
}

void TvGuideDialog::setGuideData(const QStringList &channelOrder,
                                 const QStringList &favoriteChannels,
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
    favoriteChannels_ = favoriteChannels;
    favoriteChannels_.removeDuplicates();
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
    auto *guideHeaderLayout = qobject_cast<QVBoxLayout *>(guideHeaderContent_ != nullptr ? guideHeaderContent_->layout() : nullptr);
    auto *guideChannelsLayout = qobject_cast<QVBoxLayout *>(guideChannelsContent_ != nullptr ? guideChannelsContent_->layout() : nullptr);
    auto *guideRowsLayout = qobject_cast<QVBoxLayout *>(guideContent_ != nullptr ? guideContent_->layout() : nullptr);
    if (guideHeaderLayout == nullptr || guideChannelsLayout == nullptr || guideRowsLayout == nullptr) {
        return;
    }

    const int horizontalScroll = guideScrollArea_ != nullptr ? guideScrollArea_->horizontalScrollBar()->value() : 0;
    const int verticalScroll = guideScrollArea_ != nullptr ? guideScrollArea_->verticalScrollBar()->value() : 0;

    QStringList visibleChannels;
    visibleChannels.reserve(channelOrder_.size());
    for (const QString &channel : channelOrder_) {
        if (showFavoritesOnlyCheckBox_ != nullptr
            && showFavoritesOnlyCheckBox_->isChecked()
            && !favoriteChannels_.contains(channel)) {
            continue;
        }
        if (hideNoEitCheckBox_->isChecked() && !channelHasVisibleData(channel)) {
            continue;
        }
        visibleChannels << channel;
    }

    clearLayout(guideHeaderLayout);
    clearLayout(guideChannelsLayout);
    clearLayout(guideRowsLayout);

    auto *timelineHeader = new GuideTimelineHeaderWidget(windowStartUtc_, slotMinutes_, slotCount_, guideHeaderContent_);
    guideHeaderLayout->addWidget(timelineHeader);

    for (const QString &channel : visibleChannels) {
        auto *channelLabel = new QLabel(channel, guideChannelsContent_);
        channelLabel->setWordWrap(true);
        channelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        channelLabel->setMinimumWidth(kGuideChannelLabelWidth);
        channelLabel->setMaximumWidth(kGuideChannelLabelWidth);
        channelLabel->setMinimumHeight(kGuideRowHeight);
        channelLabel->setMaximumHeight(kGuideRowHeight);
        channelLabel->setStyleSheet("QLabel { color: #ffffff; padding-right: 8px; }");
        guideChannelsLayout->addWidget(channelLabel);

        auto *bandWidget = new GuideChannelBandWidget(entriesByChannel_.value(channel),
                                                      windowStartUtc_,
                                                      slotMinutes_,
                                                      slotCount_,
                                                      guideContent_);
        guideRowsLayout->addWidget(bandWidget);
    }

    if (visibleChannels.isEmpty()) {
        auto *emptyChannelLabel = new QLabel("", guideChannelsContent_);
        emptyChannelLabel->setMinimumWidth(kGuideChannelLabelWidth);
        emptyChannelLabel->setMaximumWidth(kGuideChannelLabelWidth);
        emptyChannelLabel->setMinimumHeight(kGuideRowHeight);
        emptyChannelLabel->setMaximumHeight(kGuideRowHeight);
        guideChannelsLayout->addWidget(emptyChannelLabel);

        auto *emptyLabel = new QLabel("No channels matched the current guide filter.", guideContent_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setMinimumHeight(kGuideRowHeight);
        emptyLabel->setMaximumHeight(kGuideRowHeight);
        guideRowsLayout->addWidget(emptyLabel);
    }

    const int timelineWidth = std::max(slotCount_, 1) * kGuideSlotWidth;
    const int rowCount = std::max<int>(visibleChannels.size(), 1);
    const int rowsHeight = rowCount * kGuideRowHeight;
    guideHeaderContent_->setFixedSize(timelineWidth, kGuideHeaderHeight);
    guideChannelsContent_->setFixedSize(kGuideChannelLabelWidth, rowsHeight);
    guideContent_->setFixedSize(timelineWidth, rowsHeight);

    if (guideScrollArea_ != nullptr) {
        guideScrollArea_->horizontalScrollBar()->setValue(horizontalScroll);
        guideScrollArea_->verticalScrollBar()->setValue(verticalScroll);
    }
    if (guideHeaderContent_ != nullptr) {
        guideHeaderContent_->move(-horizontalScroll, 0);
    }
    if (guideChannelsContent_ != nullptr) {
        guideChannelsContent_->move(0, -verticalScroll);
    }
}
