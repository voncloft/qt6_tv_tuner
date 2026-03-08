#include "TvGuideDialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QFrame>
#include <QFontMetrics>
#include <QHeaderView>
#include <QApplication>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kGuideChannelLabelWidth = 190;
constexpr int kGuideSlotWidth = 150;
constexpr int kGuideVisibleColumnCount = 3;
constexpr int kGuideHeaderHeight = 40;
constexpr int kGuideRowHeight = 132;
constexpr int kGuideGridLineWidth = 2;
constexpr int kGuideBoxBorderWidth = 2;
constexpr int kGuideScheduleCheckboxSize = 16;
constexpr int kDefaultFavoriteShowRating = 1;

bool guideEntriesMatch(const TvGuideEntry &left, const TvGuideEntry &right)
{
    return left.startUtc == right.startUtc
           && left.endUtc == right.endUtc
           && left.title.trimmed() == right.title.trimmed();
}

bool scheduledSwitchMatchesEntry(const TvGuideScheduledSwitch &scheduledSwitch,
                                 const QString &channelName,
                                 const TvGuideEntry &entry)
{
    return scheduledSwitch.channelName.trimmed() == channelName.trimmed()
           && guideEntriesMatch(TvGuideEntry{scheduledSwitch.startUtc,
                                             scheduledSwitch.endUtc,
                                             scheduledSwitch.title,
                                             scheduledSwitch.episode,
                                             scheduledSwitch.synopsis},
                                entry);
}

struct GuideEntryDisplayParts {
    QString title;
    QString episodeTitle;
    QString synopsisBody;
};

GuideEntryDisplayParts displayPartsForEntry(const TvGuideEntry &entry)
{
    GuideEntryDisplayParts parts;
    parts.title = entry.title.trimmed();
    parts.episodeTitle = entry.episode.trimmed();
    parts.synopsisBody = entry.synopsis.trimmed();

    const QString synopsis = entry.synopsis.trimmed();
    if (synopsis.isEmpty() || !parts.episodeTitle.isEmpty()) {
        return parts;
    }

    const QStringList rawLines = synopsis.split('\n');
    QStringList lines;
    for (const QString &line : rawLines) {
        const QString trimmedLine = line.trimmed();
        if (!trimmedLine.isEmpty()) {
            lines.append(trimmedLine);
        }
    }

    if (lines.size() >= 2) {
        parts.episodeTitle = lines.takeFirst();
        parts.synopsisBody = lines.join('\n').trimmed();
    }
    return parts;
}

QString normalizeFavoriteShowRule(const QString &title)
{
    return title.simplified().toCaseFolded();
}

int favoriteShowRatingForTitle(const QHash<QString, int> &favoriteShowRatings, const QString &title)
{
    const QString normalizedTitle = normalizeFavoriteShowRule(title);
    if (normalizedTitle.isEmpty()) {
        return kDefaultFavoriteShowRating;
    }

    return std::max(kDefaultFavoriteShowRating, favoriteShowRatings.value(normalizedTitle, kDefaultFavoriteShowRating));
}

QString formatRatedShowTitle(const QString &title, const QHash<QString, int> &favoriteShowRatings)
{
    const QString trimmedTitle = title.simplified();
    if (trimmedTitle.isEmpty()) {
        return QString();
    }

    if (!favoriteShowRatings.contains(normalizeFavoriteShowRule(trimmedTitle))) {
        return QString("%1 (rating: N/A)").arg(trimmedTitle);
    }

    return QString("%1 (rating: %2)").arg(trimmedTitle).arg(favoriteShowRatingForTitle(favoriteShowRatings, trimmedTitle));
}

QString formatEntryLabel(const TvGuideEntry &entry, const QHash<QString, int> &favoriteShowRatings)
{
    const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
    QStringList lines;
    const QString ratedTitle = formatRatedShowTitle(parts.title, favoriteShowRatings);
    if (!ratedTitle.isEmpty()) {
        lines << ratedTitle;
    }
    if (!parts.episodeTitle.isEmpty()) {
        lines << parts.episodeTitle;
    }
    if (!parts.synopsisBody.isEmpty()) {
        lines << parts.synopsisBody;
    }
    return lines.join('\n');
}

QString formatEntryHtml(const TvGuideEntry &entry, const QHash<QString, int> &favoriteShowRatings)
{
    const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
    const QString ratedTitle = formatRatedShowTitle(parts.title, favoriteShowRatings);
    QString html = QString("<div style=\"color:#ffffff;\">"
                           "<div style=\"font-weight:600; margin-bottom:4px;\">%1</div>")
                       .arg(ratedTitle.toHtmlEscaped());
    if (!parts.episodeTitle.isEmpty()) {
        html += QString("<div style=\"color:#f1d27a; font-style:italic; margin-bottom:4px;\">%1</div>")
                    .arg(parts.episodeTitle.toHtmlEscaped());
    }
    if (!parts.synopsisBody.isEmpty()) {
        html += QString("<div style=\"color:#d4d4d4; font-size:90%%;\">%1</div>")
                    .arg(parts.synopsisBody.toHtmlEscaped().replace('\n', "<br/>"));
    }
    html += "</div>";
    return html;
}

QString formatEntryToolTip(const TvGuideEntry &entry, const QHash<QString, int> &favoriteShowRatings)
{
    const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
    const QString ratedTitle = formatRatedShowTitle(parts.title, favoriteShowRatings);
    QString text = QString("%1\n%2 - %3")
        .arg(ratedTitle,
             entry.startUtc.toLocalTime().toString("ddd h:mm AP"),
             entry.endUtc.toLocalTime().toString("ddd h:mm AP"));
    if (!parts.episodeTitle.isEmpty()) {
        text += "\nEpisode: " + parts.episodeTitle;
    }
    if (!parts.synopsisBody.isEmpty()) {
        text += "\n\nSynopsis: " + parts.synopsisBody;
    }
    return text;
}

int measureEntryTextHeight(const QFont &font,
                           int width,
                           const TvGuideEntry &entry,
                           const QHash<QString, int> &favoriteShowRatings)
{
    if (width <= 0 || entry.title.trimmed().isEmpty()) {
        return 0;
    }

    QTextDocument document;
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    document.setHtml(formatEntryHtml(entry, favoriteShowRatings));
    document.setTextWidth(width);
    return std::max(0, static_cast<int>(std::ceil(document.size().height())));
}

void drawEntryText(QPainter &painter,
                   const QRect &textRect,
                   const TvGuideEntry &entry,
                   const QHash<QString, int> &favoriteShowRatings)
{
    if (!textRect.isValid() || entry.title.trimmed().isEmpty()) {
        return;
    }

    QTextDocument document;
    document.setDefaultFont(painter.font());
    document.setDocumentMargin(0);
    document.setHtml(formatEntryHtml(entry, favoriteShowRatings));
    document.setTextWidth(textRect.width());

    painter.save();
    painter.setClipRect(textRect);
    painter.translate(textRect.topLeft());
    document.drawContents(&painter, QRectF(0, 0, textRect.width(), textRect.height()));
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
    GuideTimelineHeaderWidget(const QDateTime &windowStartUtc,
                              int slotMinutes,
                              int slotCount,
                              int timelineWidth,
                              QWidget *parent = nullptr)
        : QWidget(parent)
        , windowStartUtc_(windowStartUtc)
        , slotMinutes_(slotMinutes)
        , slotCount_(slotCount)
        , timelineWidth_(timelineWidth)
    {
        setMinimumHeight(kGuideHeaderHeight);
        setMaximumHeight(kGuideHeaderHeight);
        setMinimumWidth(std::max(timelineWidth_, 1));
    }

    QSize sizeHint() const override
    {
        return {std::max(timelineWidth_, 1), kGuideHeaderHeight};
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
    int timelineWidth_{0};
};

class GuideChannelBandWidget final : public QWidget
{
public:
    GuideChannelBandWidget(const QList<TvGuideEntry> &entries,
                           const QHash<QString, int> &favoriteShowRatings,
                           const QDateTime &windowStartUtc,
                           int slotMinutes,
                           int slotCount,
                           int timelineWidth,
                           std::function<bool(const TvGuideEntry &)> isEntryScheduled,
                           std::function<void(const TvGuideEntry &, bool)> toggleSchedule,
                           QWidget *parent = nullptr)
        : QWidget(parent)
        , entries_(entries)
        , favoriteShowRatings_(favoriteShowRatings)
        , windowStartUtc_(windowStartUtc)
        , slotMinutes_(slotMinutes)
        , slotCount_(slotCount)
        , timelineWidth_(timelineWidth)
        , isEntryScheduled_(std::move(isEntryScheduled))
        , toggleSchedule_(std::move(toggleSchedule))
    {
        setMouseTracking(true);
        setMinimumWidth(std::max(timelineWidth_, 1));
        rowHeight_ = preferredRowHeight();
        setMinimumHeight(rowHeight_);
        setMaximumHeight(rowHeight_);
    }

    QSize sizeHint() const override
    {
        return {std::max(timelineWidth_, 1), rowHeight_};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

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
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        bool renderedAny = false;
        QList<TvGuideEntry> entries = entries_;
        std::sort(entries.begin(), entries.end(), [](const TvGuideEntry &a, const TvGuideEntry &b) {
            return a.startUtc < b.startUtc;
        });
        scheduleToggleTargets_.clear();

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

            const bool airingNow = entry.startUtc <= nowUtc && entry.endUtc > nowUtc;
            const bool canSchedule = entry.startUtc > nowUtc;
            const bool scheduled = canSchedule && isEntryScheduled_ && isEntryScheduled_(entry);
            const int checkboxInset = canSchedule ? (kGuideScheduleCheckboxSize + 16) : 10;
            QRect checkboxRect;
            if (canSchedule) {
                checkboxRect = QRect(box.right() - kGuideScheduleCheckboxSize - 8,
                                     box.top() + 8,
                                     kGuideScheduleCheckboxSize,
                                     kGuideScheduleCheckboxSize);
            }
            painter.setPen(QPen(QColor(120, 120, 120), kGuideBoxBorderWidth));
            painter.setBrush(airingNow ? QColor(28, 28, 28) : QColor(16, 16, 16));
            painter.drawRect(box);

            if (canSchedule) {
                painter.setPen(QPen(QColor(205, 205, 205), 1));
                painter.setBrush(scheduled ? QColor(255, 96, 96) : QColor(0, 0, 0));
                painter.drawRect(checkboxRect);
                if (scheduled) {
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setPen(QPen(QColor(255, 255, 255), 2));
                    painter.drawLine(checkboxRect.left() + 3,
                                     checkboxRect.center().y(),
                                     checkboxRect.left() + 7,
                                     checkboxRect.bottom() - 3);
                    painter.drawLine(checkboxRect.left() + 7,
                                     checkboxRect.bottom() - 3,
                                     checkboxRect.right() - 2,
                                     checkboxRect.top() + 3);
                    painter.setRenderHint(QPainter::Antialiasing, false);
                }
                scheduleToggleTargets_.append({checkboxRect, box, entry, scheduled});
            }

            painter.setPen(QColor(255, 255, 255));
            drawEntryText(painter, box.adjusted(10, 8, -checkboxInset, -8), entry, favoriteShowRatings_);
            renderedAny = true;
        }

        if (!renderedAny) {
            painter.setPen(QColor(160, 160, 160));
            painter.drawText(rect().adjusted(10, 0, -10, 0), Qt::AlignCenter, "NO GUIDE DATA");
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

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event == nullptr) {
            return;
        }

        const ScheduleToggleTarget *target = targetAt(event->position().toPoint());
        if (target != nullptr) {
            setCursor(target->checkboxRect.contains(event->position().toPoint())
                          ? Qt::PointingHandCursor
                          : Qt::ArrowCursor);
            return;
        }

        unsetCursor();
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        unsetCursor();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event == nullptr || event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        const ScheduleToggleTarget *target = targetAt(event->position().toPoint());
        if (target == nullptr || !target->checkboxRect.contains(event->position().toPoint()) || !toggleSchedule_) {
            QWidget::mousePressEvent(event);
            return;
        }

        toggleSchedule_(target->entry, !target->scheduled);
        event->accept();
    }

private:
    struct ScheduleToggleTarget {
        QRect checkboxRect;
        QRect boxRect;
        TvGuideEntry entry;
        bool scheduled{false};
    };

    const ScheduleToggleTarget *targetAt(const QPoint &point) const
    {
        for (const ScheduleToggleTarget &target : scheduleToggleTargets_) {
            if (target.checkboxRect.contains(point) || target.boxRect.contains(point)) {
                return &target;
            }
        }
        return nullptr;
    }

    int preferredRowHeight() const
    {
        const qint64 totalSeconds = static_cast<qint64>(slotMinutes_) * std::max(slotCount_, 1) * 60;
        if (totalSeconds <= 0 || !windowStartUtc_.isValid()) {
            return kGuideRowHeight;
        }

        const QDateTime windowEndUtc = windowStartUtc_.addSecs(totalSeconds);
        const int totalWidth = std::max(timelineWidth_, 1);
        int preferred = kGuideRowHeight;

        for (const TvGuideEntry &entry : entries_) {
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

            const int left = std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleStart) * totalWidth / totalSeconds)),
                                        0,
                                        std::max(0, totalWidth - 1));
            const int right = std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleEnd) * totalWidth / totalSeconds)),
                                         left + 1,
                                         totalWidth);
            const int boxWidth = std::max(28, right - left - 4);
            const int textWidth = std::max(8,
                                           boxWidth - 20 - (entry.startUtc > QDateTime::currentDateTimeUtc()
                                                                ? (kGuideScheduleCheckboxSize + 16)
                                                                : 0));
            const int textHeight = measureEntryTextHeight(font(), textWidth, entry, favoriteShowRatings_);
            preferred = std::max(preferred, textHeight + 26);
        }

        return preferred;
    }

    QList<TvGuideEntry> entries_;
    QHash<QString, int> favoriteShowRatings_;
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
    int timelineWidth_{0};
    int rowHeight_{kGuideRowHeight};
    std::function<bool(const TvGuideEntry &)> isEntryScheduled_;
    std::function<void(const TvGuideEntry &, bool)> toggleSchedule_;
    QList<ScheduleToggleTarget> scheduleToggleTargets_;
};

}

TvGuideDialog::TvGuideDialog(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    setStyleSheet(
        "QWidget { background-color: #000000; color: #ffffff; }"
        "QPushButton { background-color: #0f0f0f; color: #ffffff; border: 1px solid #444444; padding: 6px 12px; }"
        "QPushButton:disabled { color: #888888; border-color: #222222; }"
        "QTabWidget::pane { border: 1px solid #4a4a4a; top: -1px; }"
        "QTabBar::tab { background-color: #0a0a0a; color: #ffffff; border: 1px solid #4a4a4a; padding: 8px 14px; min-width: 110px; }"
        "QTabBar::tab:selected { background-color: #111111; }"
        "QLineEdit, QListWidget { background-color: #050505; color: #ffffff; border: 1px solid #4a4a4a; }"
        "QPlainTextEdit { background-color: #000000; color: #ffffff; border: 1px solid #4a4a4a; }"
        "QTableWidget { background-color: #000000; alternate-background-color: #090909; color: #ffffff; gridline-color: #4a4a4a; }"
        "QHeaderView::section { background-color: #000000; color: #ffffff; border: 1px solid #4a4a4a; padding: 4px; }");

    auto *controls = new QHBoxLayout();
    refreshButton_ = new QPushButton("Reload Cache", this);
    connect(refreshButton_, &QPushButton::clicked, this, &TvGuideDialog::refreshRequested);

    controls->addStretch(1);
    controls->addWidget(refreshButton_, 0);
    layout->addLayout(controls);

    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_, 1);

    auto *guideTab = new QWidget(this);
    auto *guideLayout = new QVBoxLayout(guideTab);
    guideLayout->setContentsMargins(0, 0, 0, 0);

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
        applyGuideHorizontalScroll(value);
    });
    connect(guideScrollArea_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (guideChannelsContent_ == nullptr) {
            return;
        }
        guideChannelsContent_->move(0, -value);
    });
    tabs_->addTab(guideTab, "Guide");

    auto *searchTab = new QWidget(this);
    auto *searchLayout = new QVBoxLayout(searchTab);

    auto *searchRow = new QHBoxLayout();
    auto *searchLabel = new QLabel("Find show:", searchTab);
    showSearchEdit_ = new QLineEdit(searchTab);
    showSearchEdit_->setClearButtonEnabled(true);
    showSearchEdit_->setPlaceholderText("Search current guide data");
    searchRow->addWidget(searchLabel, 0);
    searchRow->addWidget(showSearchEdit_, 1);
    searchLayout->addLayout(searchRow);

    showSearchSummaryLabel_ = new QLabel("Search the current guide cache by title or synopsis.", searchTab);
    showSearchSummaryLabel_->setWordWrap(true);
    searchLayout->addWidget(showSearchSummaryLabel_);

    showSearchResultsList_ = new QListWidget(searchTab);
    showSearchResultsList_->setAlternatingRowColors(true);
    showSearchResultsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    searchLayout->addWidget(showSearchResultsList_, 1);

    scheduleSearchResultButton_ = new QPushButton("Add Favorite Switch", searchTab);
    scheduleSearchResultButton_->setEnabled(false);
    searchLayout->addWidget(scheduleSearchResultButton_, 0, Qt::AlignRight);

    connect(showSearchEdit_, &QLineEdit::textChanged, this, [this]() {
        updateSearchResults();
    });
    connect(showSearchResultsList_, &QListWidget::itemSelectionChanged, this, [this]() {
        updateSearchActionState();
    });
    connect(showSearchResultsList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        scheduleSelectedSearchResult();
    });
    connect(scheduleSearchResultButton_, &QPushButton::clicked, this, &TvGuideDialog::scheduleSelectedSearchResult);
    tabs_->addTab(searchTab, "Search");

    auto *logsTab = new QWidget(this);
    auto *logsLayout = new QVBoxLayout(logsTab);
    logsLayout->setContentsMargins(0, 0, 0, 0);

    logsView_ = new QPlainTextEdit(logsTab);
    logsView_->setReadOnly(true);
    logsView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logsView_->setPlainText("No guide data loaded yet.");
    logsLayout->addWidget(logsView_);
    tabs_->addTab(logsTab, "Status");
}

void TvGuideDialog::setLoadingState(const QString &message)
{
    logsView_->setPlainText(message);
    refreshButton_->setEnabled(false);
    tabs_->setCurrentIndex(0);
}

void TvGuideDialog::setGuideFilters(bool hideChannelsWithoutEitData, bool showFavoritesOnly)
{
    hideChannelsWithoutEitData_ = hideChannelsWithoutEitData;
    showFavoritesOnly_ = showFavoritesOnly;
    if (slotCount_ > 0) {
        renderGuideTable();
    }
}

void TvGuideDialog::syncToCurrentTime()
{
    pendingSyncToCurrentTime_ = true;

    if (slotCount_ <= 0
        || !isVisible()
        || guideScrollArea_ == nullptr
        || guideScrollArea_->viewport() == nullptr
        || guideScrollArea_->viewport()->width() <= 0) {
        return;
    }

    renderGuideTable();
}

QString TvGuideDialog::entryLabel(const TvGuideEntry &entry) const
{
    return formatEntryLabel(entry, favoriteShowRatings_);
}

QString TvGuideDialog::entryToolTip(const TvGuideEntry &entry) const
{
    return formatEntryToolTip(entry, favoriteShowRatings_);
}

void TvGuideDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (event == nullptr || slotCount_ <= 0 || guideScrollArea_ == nullptr) {
        return;
    }

    if (event->size().width() != event->oldSize().width()) {
        renderGuideTable();
    }
}

void TvGuideDialog::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (!pendingGuideRender_ && !pendingSyncToCurrentTime_) {
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (!isVisible()) {
            return;
        }
        if (pendingGuideRender_ || (pendingSyncToCurrentTime_ && slotCount_ > 0)) {
            pendingGuideRender_ = false;
            renderGuideTable();
        }
    });
}

int TvGuideDialog::guideSlotPixelWidth() const
{
    int viewportWidth = 0;
    if (guideScrollArea_ != nullptr && guideScrollArea_->viewport() != nullptr) {
        viewportWidth = guideScrollArea_->viewport()->width();
    }
    if (viewportWidth <= 0) {
        viewportWidth = kGuideVisibleColumnCount * kGuideSlotWidth;
    }
    return std::max(1, viewportWidth / kGuideVisibleColumnCount);
}

void TvGuideDialog::applyGuideHorizontalScroll(int value)
{
    if (guideScrollArea_ == nullptr) {
        return;
    }

    QScrollBar *bar = guideScrollArea_->horizontalScrollBar();
    if (bar == nullptr) {
        return;
    }

    if (guideHeaderContent_ != nullptr) {
        guideHeaderContent_->move(-std::clamp(value, 0, bar->maximum()), 0);
    }
}

void TvGuideDialog::setGuideData(const QStringList &channelOrder,
                                 const QStringList &favoriteChannels,
                                 const QHash<QString, int> &favoriteShowRatings,
                                 const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                 const QDateTime &windowStartUtc,
                                 int slotMinutes,
                                 int slotCount,
                                 const QList<TvGuideScheduledSwitch> &scheduledSwitches,
                                 const QString &statusText)
{
    refreshButton_->setEnabled(true);
    logsView_->setPlainText(statusText);
    channelOrder_ = channelOrder;
    favoriteChannels_ = favoriteChannels;
    favoriteChannels_.removeDuplicates();
    favoriteShowRatings_ = favoriteShowRatings;
    entriesByChannel_ = entriesByChannel;
    scheduledSwitches_ = scheduledSwitches;
    windowStartUtc_ = windowStartUtc;
    slotMinutes_ = slotMinutes;
    slotCount_ = slotCount;
    updateSearchResults();

    if (!isVisible()
        || guideScrollArea_ == nullptr
        || guideScrollArea_->viewport() == nullptr
        || guideScrollArea_->viewport()->width() <= 0) {
        pendingGuideRender_ = true;
        return;
    }

    pendingGuideRender_ = false;
    renderGuideTable();
}

void TvGuideDialog::updateSearchResults()
{
    if (showSearchResultsList_ == nullptr || showSearchSummaryLabel_ == nullptr || showSearchEdit_ == nullptr) {
        return;
    }

    SearchResult selectedResult;
    bool hadSelectedResult = false;
    const int previousRow = showSearchResultsList_->currentRow();
    if (previousRow >= 0 && previousRow < searchResults_.size()) {
        selectedResult = searchResults_.at(previousRow);
        hadSelectedResult = true;
    }

    searchResults_.clear();
    showSearchResultsList_->clear();

    const QString query = showSearchEdit_->text().simplified();
    if (query.isEmpty()) {
        showSearchSummaryLabel_->setText("Search the current guide cache by title or synopsis.");
        updateSearchActionState();
        return;
    }

    QStringList orderedChannels = channelOrder_;
    for (auto it = entriesByChannel_.cbegin(); it != entriesByChannel_.cend(); ++it) {
        if (!orderedChannels.contains(it.key())) {
            orderedChannels.append(it.key());
        }
    }

    QList<SearchResult> matchedResults;
    for (const QString &channelName : orderedChannels) {
        const QList<TvGuideEntry> entries = entriesByChannel_.value(channelName);
        for (const TvGuideEntry &entry : entries) {
            const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
            const QString title = parts.title;
            const QString episode = parts.episodeTitle;
            const QString synopsis = parts.synopsisBody;
            if (title.isEmpty()) {
                continue;
            }
            if (!title.contains(query, Qt::CaseInsensitive)
                && !episode.contains(query, Qt::CaseInsensitive)
                && !synopsis.contains(query, Qt::CaseInsensitive)) {
                continue;
            }

            SearchResult result;
            result.channelName = channelName;
            result.entry = entry;
            matchedResults.append(result);
        }
    }

    std::sort(matchedResults.begin(), matchedResults.end(), [](const SearchResult &left, const SearchResult &right) {
        if (left.entry.startUtc == right.entry.startUtc) {
            if (left.channelName == right.channelName) {
                return left.entry.title.localeAwareCompare(right.entry.title) < 0;
            }
            return left.channelName.localeAwareCompare(right.channelName) < 0;
        }
        return left.entry.startUtc < right.entry.startUtc;
    });

    searchResults_ = matchedResults;
    int restoredRow = -1;
    for (const SearchResult &result : searchResults_) {
        const GuideEntryDisplayParts parts = displayPartsForEntry(result.entry);
        const QString ratedTitle = formatRatedShowTitle(parts.title, favoriteShowRatings_);
        QStringList lines;
        lines << ratedTitle;
        if (!parts.episodeTitle.isEmpty()) {
            lines << "Episode: " + parts.episodeTitle;
        }
        lines << QString("%1 - %2 | %3")
                     .arg(result.entry.startUtc.toLocalTime().toString("ddd h:mm AP"),
                          result.entry.endUtc.toLocalTime().toString("h:mm AP"),
                          result.channelName);
        if (!parts.synopsisBody.isEmpty()) {
            lines << "Synopsis: " + parts.synopsisBody;
        }
        showSearchResultsList_->addItem(lines.join('\n'));

        if (restoredRow < 0
            && hadSelectedResult
            && result.channelName.trimmed() == selectedResult.channelName.trimmed()
            && guideEntriesMatch(result.entry, selectedResult.entry)) {
            restoredRow = showSearchResultsList_->count() - 1;
        }
    }

    if (searchResults_.isEmpty()) {
        showSearchSummaryLabel_->setText(QString("No guide entries matched \"%1\".").arg(query));
    } else {
        showSearchSummaryLabel_->setText(QString("%1 matching guide entr%2 found.")
                                             .arg(searchResults_.size())
                                             .arg(searchResults_.size() == 1 ? "y" : "ies"));
    }

    if (restoredRow >= 0) {
        showSearchResultsList_->setCurrentRow(restoredRow);
    }

    updateSearchActionState();
}

void TvGuideDialog::updateSearchActionState()
{
    if (scheduleSearchResultButton_ == nullptr || showSearchResultsList_ == nullptr) {
        return;
    }

    const int row = showSearchResultsList_->currentRow();
    scheduleSearchResultButton_->setEnabled(row >= 0 && row < searchResults_.size());
}

void TvGuideDialog::scheduleSelectedSearchResult()
{
    if (showSearchResultsList_ == nullptr) {
        return;
    }

    const int row = showSearchResultsList_->currentRow();
    if (row < 0 || row >= searchResults_.size()) {
        return;
    }

    const SearchResult result = searchResults_.at(row);
    emit searchScheduleRequested(result.entry.title.simplified(), result.channelName, result.entry);
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

bool TvGuideDialog::isEntryScheduled(const QString &channelName, const TvGuideEntry &entry) const
{
    for (const TvGuideScheduledSwitch &scheduledSwitch : scheduledSwitches_) {
        if (scheduledSwitchMatchesEntry(scheduledSwitch, channelName, entry)) {
            return true;
        }
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
        if (showFavoritesOnly_ && !favoriteChannels_.contains(channel)) {
            continue;
        }
        if (hideChannelsWithoutEitData_ && !channelHasVisibleData(channel)) {
            continue;
        }
        visibleChannels << channel;
    }

    clearLayout(guideHeaderLayout);
    clearLayout(guideChannelsLayout);
    clearLayout(guideRowsLayout);

    currentGuideSlotPixelWidth_ = guideSlotPixelWidth();
    const int timelineWidth = std::max(slotCount_, 1) * currentGuideSlotPixelWidth_;

    auto *timelineHeader = new GuideTimelineHeaderWidget(windowStartUtc_,
                                                         slotMinutes_,
                                                         slotCount_,
                                                         timelineWidth,
                                                         guideHeaderContent_);
    guideHeaderLayout->addWidget(timelineHeader);

    int rowsHeight = 0;
    for (const QString &channel : visibleChannels) {
        auto *bandWidget = new GuideChannelBandWidget(entriesByChannel_.value(channel),
                                                      favoriteShowRatings_,
                                                      windowStartUtc_,
                                                      slotMinutes_,
                                                      slotCount_,
                                                      timelineWidth,
                                                      [this, channel](const TvGuideEntry &entry) {
                                                          return isEntryScheduled(channel, entry);
                                                      },
                                                      [this, channel](const TvGuideEntry &entry, bool enabled) {
                                                          emit scheduleSwitchRequested(channel, entry, enabled);
                                                      },
                                                      guideContent_);
        const int rowHeight = bandWidget->sizeHint().height();

        auto *channelLabel = new QLabel(channel, guideChannelsContent_);
        channelLabel->setWordWrap(true);
        channelLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        channelLabel->setMinimumWidth(kGuideChannelLabelWidth);
        channelLabel->setMaximumWidth(kGuideChannelLabelWidth);
        channelLabel->setMinimumHeight(rowHeight);
        channelLabel->setMaximumHeight(rowHeight);
        channelLabel->setStyleSheet("QLabel { color: #ffffff; padding-right: 8px; }");
        guideChannelsLayout->addWidget(channelLabel);

        guideRowsLayout->addWidget(bandWidget);
        rowsHeight += rowHeight;
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
        rowsHeight = kGuideRowHeight;
    }

    guideHeaderContent_->setFixedSize(timelineWidth, kGuideHeaderHeight);
    guideChannelsContent_->setFixedSize(kGuideChannelLabelWidth, rowsHeight);
    guideContent_->setFixedSize(timelineWidth, rowsHeight);

    if (guideScrollArea_ != nullptr) {
        guideScrollArea_->horizontalScrollBar()->setSingleStep(std::max(12, currentGuideSlotPixelWidth_ / 8));
        guideScrollArea_->horizontalScrollBar()->setPageStep(
            guideScrollArea_->viewport() != nullptr
                ? std::max(24, guideScrollArea_->viewport()->width() - 48)
                : currentGuideSlotPixelWidth_ * kGuideVisibleColumnCount);
        guideScrollArea_->horizontalScrollBar()->setValue(horizontalScroll);
        guideScrollArea_->verticalScrollBar()->setValue(verticalScroll);
    }
    if (guideHeaderContent_ != nullptr) {
        applyGuideHorizontalScroll(horizontalScroll);
    }
    if (guideChannelsContent_ != nullptr) {
        guideChannelsContent_->move(0, -verticalScroll);
    }

    if (pendingSyncToCurrentTime_) {
        pendingSyncToCurrentTime_ = false;
        scrollGuideToCurrentTime(true);
    }
}

void TvGuideDialog::scrollGuideToCurrentTime(bool force)
{
    if (!windowStartUtc_.isValid()
        || slotMinutes_ <= 0
        || slotCount_ <= 0
        || guideScrollArea_ == nullptr
        || guideScrollArea_->viewport() == nullptr) {
        return;
    }

    QScrollBar *horizontalBar = guideScrollArea_->horizontalScrollBar();
    if (horizontalBar == nullptr) {
        return;
    }

    const qint64 totalSeconds = static_cast<qint64>(slotMinutes_) * slotCount_ * 60;
    if (totalSeconds <= 0) {
        return;
    }

    const qint64 nowOffset = windowStartUtc_.secsTo(QDateTime::currentDateTimeUtc());
    if (nowOffset < 0 || nowOffset > totalSeconds) {
        return;
    }

    const int timelineWidth = std::max(1, slotCount_) * std::max(1, currentGuideSlotPixelWidth_);
    const int nowX = std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * timelineWidth / totalSeconds)),
                                0,
                                std::max(0, timelineWidth - 1));
    const int viewportWidth = guideScrollArea_->viewport()->width();
    const int left = horizontalBar->value();
    const int right = left + viewportWidth;
    if (!force && nowX >= left && nowX <= right - std::max(1, currentGuideSlotPixelWidth_ / 2)) {
        return;
    }

    const int target = std::clamp(nowX - std::max(1, currentGuideSlotPixelWidth_),
                                  0,
                                  horizontalBar->maximum());
    horizontalBar->setValue(target);
}
