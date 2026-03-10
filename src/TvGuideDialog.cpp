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
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionButton>
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
constexpr int kGuideWatchNowButtonWidth = 118;
constexpr int kGuideWatchNowButtonHeight = 24;
constexpr int kDefaultFavoriteShowRating = 1;
constexpr int kSearchResultMargin = 8;
constexpr int kSearchResultSpacing = 10;
constexpr int kSearchButtonMinWidth = 132;
constexpr int kSearchButtonHeight = 28;
constexpr int kSearchButtonSpacing = 6;
constexpr int kSearchButtonHorizontalPadding = 28;
constexpr int kSearchSynopsisLineBudget = 2;

enum SearchResultRoles {
    SearchTitleRole = Qt::UserRole + 1,
    SearchTimeChannelRole,
    SearchEpisodeRole,
    SearchSynopsisRole,
    SearchIsCurrentRole,
    SearchIsFavoriteRole
};

struct TvGuideVisualTheme {
    QColor background;
    QColor text;
    QColor secondaryText;
    QColor episodeText;
    QColor border;
    QColor tabBackground;
    QColor tabSelectedBackground;
    QColor tabText;
    QColor buttonBackground;
    QColor buttonText;
    QColor buttonBorder;
    QColor gridLine;
    QColor entryBackground;
    QColor currentEntryBackground;
    QColor entryBorder;
    QColor nowLine;
    QColor actionBackground;
    QColor actionBorder;
    QColor actionText;
    QColor actionFavoriteText;
    QColor emptyText;
    QFont guideFont;
    QFont guideHeaderFont;
    QFont guideChannelFont;
    QFont guideSearchFont;
    QFont logFont;
    QFont tabFont;
    QFont buttonFont;
    QFont inputFont;
};

TvGuideVisualTheme guideVisualThemeFor(const DisplayTheme &theme)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    const QFont fallbackFont = QApplication::font();

    TvGuideVisualTheme visualTheme;
    visualTheme.background = displayThemeColor(normalized, DisplayThemeKeys::GuideBackground);
    visualTheme.text = displayThemeColor(normalized, DisplayThemeKeys::GuideText);
    visualTheme.secondaryText = displayThemeColor(normalized, DisplayThemeKeys::GuideSecondaryText);
    visualTheme.episodeText = displayThemeColor(normalized, DisplayThemeKeys::GuideEpisodeText);
    visualTheme.border = displayThemeColor(normalized, DisplayThemeKeys::GuideBorder);
    visualTheme.tabBackground = displayThemeColor(normalized, DisplayThemeKeys::GuideTabBackground);
    visualTheme.tabSelectedBackground = displayThemeColor(normalized, DisplayThemeKeys::GuideTabSelectedBackground);
    visualTheme.tabText = displayThemeColor(normalized, DisplayThemeKeys::GuideTabText);
    visualTheme.buttonBackground = displayThemeColor(normalized, DisplayThemeKeys::GuideButtonBackground);
    visualTheme.buttonText = displayThemeColor(normalized, DisplayThemeKeys::GuideButtonText);
    visualTheme.buttonBorder = displayThemeColor(normalized, DisplayThemeKeys::GuideButtonBorder);
    visualTheme.gridLine = displayThemeColor(normalized, DisplayThemeKeys::GuideGridLine);
    visualTheme.entryBackground = displayThemeColor(normalized, DisplayThemeKeys::GuideEntryBackground);
    visualTheme.currentEntryBackground = displayThemeColor(normalized, DisplayThemeKeys::GuideCurrentEntryBackground);
    visualTheme.entryBorder = displayThemeColor(normalized, DisplayThemeKeys::GuideEntryBorder);
    visualTheme.nowLine = displayThemeColor(normalized, DisplayThemeKeys::GuideNowLine);
    visualTheme.actionBackground = displayThemeColor(normalized, DisplayThemeKeys::GuideActionBackground);
    visualTheme.actionBorder = displayThemeColor(normalized, DisplayThemeKeys::GuideActionBorder);
    visualTheme.actionText = displayThemeColor(normalized, DisplayThemeKeys::GuideActionText);
    visualTheme.actionFavoriteText = displayThemeColor(normalized, DisplayThemeKeys::GuideActionFavoriteText);
    visualTheme.emptyText = displayThemeColor(normalized, DisplayThemeKeys::GuideEmptyText);
    visualTheme.guideFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::GuideFont), fallbackFont);
    visualTheme.guideHeaderFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::GuideHeaderFont), fallbackFont);
    visualTheme.guideChannelFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::GuideChannelFont), fallbackFont);
    visualTheme.guideSearchFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::GuideSearchFont), fallbackFont);
    visualTheme.logFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::LogFont), fallbackFont);
    visualTheme.tabFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::TabFont), fallbackFont);
    visualTheme.buttonFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::ButtonFont), fallbackFont);
    visualTheme.inputFont =
        qFontFromDisplayFontStyle(displayThemeFontStyle(normalized, DisplayThemeKeys::InputFont), fallbackFont);
    return visualTheme;
}

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

QString formatGuideTimelineLabel(const QDateTime &slotStartUtc, bool spansMultipleDays)
{
    const QDateTime localStart = slotStartUtc.toLocalTime();
    return spansMultipleDays ? localStart.toString("ddd HH:mm") : localStart.toString("h:mm AP");
}

QString formatGuideSearchDateTime(const QDateTime &utcDateTime)
{
    return utcDateTime.toLocalTime().toString("MM/dd/yyyy ddd h:mm AP");
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

QString formatEntryHtml(const TvGuideEntry &entry,
                        const QHash<QString, int> &favoriteShowRatings,
                        const TvGuideVisualTheme &visualTheme)
{
    const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
    const QString ratedTitle = formatRatedShowTitle(parts.title, favoriteShowRatings);
    QString html = QString("<div style=\"color:#ffffff;\">"
                           "<div style=\"font-weight:600; margin-bottom:4px;\">%1</div>")
                       .arg(ratedTitle.toHtmlEscaped());
    html.replace("#ffffff", visualTheme.text.name());
    if (!parts.episodeTitle.isEmpty()) {
        html += QString("<div style=\"color:%1; font-style:italic; margin-bottom:4px;\">%2</div>")
                    .arg(visualTheme.episodeText.name(), parts.episodeTitle.toHtmlEscaped());
    }
    if (!parts.synopsisBody.isEmpty()) {
        html += QString("<div style=\"color:%1; font-size:90%%;\">%2</div>")
                    .arg(visualTheme.secondaryText.name(),
                         parts.synopsisBody.toHtmlEscaped().replace('\n', "<br/>"));
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
             formatGuideSearchDateTime(entry.startUtc),
             formatGuideSearchDateTime(entry.endUtc));
    if (!parts.episodeTitle.isEmpty()) {
        text += "\nEpisode: " + parts.episodeTitle;
    }
    if (!parts.synopsisBody.isEmpty()) {
        text += "\n\nSynopsis: " + parts.synopsisBody;
    }
    return text;
}

QString watchActionLabelForWidth(const QFontMetrics &metrics, int width)
{
    const int availableTextWidth = std::max(0, width - 12);
    if (availableTextWidth >= metrics.horizontalAdvance("Watch Now")) {
        return "Watch Now";
    }
    if (availableTextWidth >= metrics.horizontalAdvance("Watch")) {
        return "Watch";
    }
    return {};
}

int searchResultButtonWidth(const QFontMetrics &metrics)
{
    return std::max(kSearchButtonMinWidth,
                    std::max({metrics.horizontalAdvance("Watch Now"),
                              metrics.horizontalAdvance("Add to Favorites"),
                              metrics.horizontalAdvance("Remove from Favorites")})
                        + kSearchButtonHorizontalPadding);
}

QString favoriteActionLabel(bool isFavorite)
{
    return isFavorite ? "Remove from Favorites" : "Add to Favorites";
}

QString normalizedSearchText(const QString &title, const QString &episode, const QString &synopsis)
{
    return QString("%1\n%2\n%3")
        .arg(title.simplified(), episode.simplified(), synopsis.simplified())
        .toCaseFolded();
}

struct SearchResultTextColors {
    QColor title;
    QColor meta;
    QColor episode;
    QColor synopsis;
};

SearchResultTextColors searchResultTextColors(const QPalette &palette,
                                             bool selected,
                                             const TvGuideVisualTheme &visualTheme)
{
    if (!selected) {
        return {visualTheme.text, visualTheme.secondaryText, visualTheme.episodeText, visualTheme.secondaryText};
    }

    const QColor highlightedText = palette.color(QPalette::HighlightedText);
    return {highlightedText,
            highlightedText.lighter(125),
            highlightedText.lighter(110),
            highlightedText.lighter(115)};
}

QString formatSearchResultHtml(const QString &title,
                              const QString &timeChannel,
                              const QString &episode,
                              const QString &synopsis,
                              const SearchResultTextColors &colors)
{
    QString html = QString("<div style=\"color:%1;\">"
                           "<div style=\"font-weight:600; margin-bottom:4px;\">%2</div>")
                       .arg(colors.title.name(), title.toHtmlEscaped());
    if (!timeChannel.isEmpty()) {
        html += QString("<div style=\"color:%1; margin-bottom:4px;\">%2</div>")
                    .arg(colors.meta.name(), timeChannel.toHtmlEscaped());
    }
    if (!episode.isEmpty()) {
        html += QString("<div style=\"color:%1; font-style:italic; margin-bottom:4px;\">Episode: %2</div>")
                    .arg(colors.episode.name(), episode.toHtmlEscaped());
    }
    if (!synopsis.isEmpty()) {
        html += QString("<div style=\"color:%1;\">Synopsis: %2</div>")
                    .arg(colors.synopsis.name(), synopsis.toHtmlEscaped().replace('\n', "<br/>"));
    }
    html += "</div>";
    return html;
}

int searchResultItemHeight(const QFontMetrics &metrics)
{
    const int visibleLineCount = 4 + kSearchSynopsisLineBudget;
    const int textHeight = (visibleLineCount * metrics.lineSpacing()) + 10;
    const int buttonHeight = (2 * kSearchButtonHeight) + kSearchButtonSpacing;
    return std::max(textHeight, buttonHeight) + (2 * kSearchResultMargin);
}

void drawSearchResultText(QPainter &painter,
                          const QRect &textRect,
                          const QString &title,
                          const QString &timeChannel,
                          const QString &episode,
                          const QString &synopsis,
                          const QPalette &palette,
                          bool selected,
                          const TvGuideVisualTheme &visualTheme)
{
    if (!textRect.isValid() || title.isEmpty()) {
        return;
    }

    QTextDocument document;
    document.setDefaultFont(painter.font());
    document.setDocumentMargin(0);
    document.setHtml(
        formatSearchResultHtml(title,
                               timeChannel,
                               episode,
                               synopsis,
                               searchResultTextColors(palette, selected, visualTheme)));
    document.setTextWidth(textRect.width());

    painter.save();
    painter.translate(textRect.topLeft());
    const QRectF clipRect(0, 0, textRect.width(), textRect.height());
    document.drawContents(&painter, clipRect);
    painter.restore();
}

struct SearchResultLayoutRects {
    QRect textRect;
    QRect watchRect;
    QRect favoriteRect;
};

SearchResultLayoutRects searchResultLayoutRects(const QRect &itemRect, bool isCurrent, int buttonWidth)
{
    SearchResultLayoutRects rects;
    if (!itemRect.isValid()) {
        return rects;
    }

    const int contentWidth =
        std::max(0, itemRect.width() - (2 * kSearchResultMargin) - buttonWidth - kSearchResultSpacing);
    const int contentHeight = std::max(0, itemRect.height() - (2 * kSearchResultMargin));
    const int contentLeft = itemRect.left() + kSearchResultMargin;
    const int contentTop = itemRect.top() + kSearchResultMargin;
    const int buttonLeft = itemRect.right() - kSearchResultMargin - buttonWidth + 1;

    rects.textRect = QRect(contentLeft, contentTop, contentWidth, contentHeight);
    rects.favoriteRect =
        QRect(buttonLeft,
              contentTop + (isCurrent ? kSearchButtonHeight + kSearchButtonSpacing : 0),
              buttonWidth,
              kSearchButtonHeight);
    if (isCurrent) {
        rects.watchRect = QRect(buttonLeft, contentTop, buttonWidth, kSearchButtonHeight);
    }
    return rects;
}

void drawGuideStyleActionButton(QPainter &painter,
                                const QRect &rect,
                                const QString &text,
                                const TvGuideVisualTheme &visualTheme,
                                const QColor &textColor)
{
    if (!rect.isValid() || text.isEmpty()) {
        return;
    }

    painter.save();
    painter.setPen(QPen(visualTheme.actionBorder, 1));
    painter.setBrush(visualTheme.actionBackground);
    painter.drawRoundedRect(rect, 4, 4);
    painter.setPen(textColor);
    painter.drawText(rect.adjusted(6, 0, -6, 0), Qt::AlignCenter, text);
    painter.restore();
}

class SearchResultItemDelegate final : public QStyledItemDelegate
{
public:
    explicit SearchResultItemDelegate(const TvGuideVisualTheme &visualTheme, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , visualTheme_(visualTheme)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QSize explicitSize = index.data(Qt::SizeHintRole).toSize();
        if (explicitSize.isValid()) {
            return explicitSize;
        }
        return QStyledItemDelegate::sizeHint(option, index);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (painter == nullptr) {
            return;
        }

        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);
        drawOption.text.clear();
        drawOption.icon = QIcon();

        QStyle *style = drawOption.widget != nullptr ? drawOption.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &drawOption, painter, drawOption.widget);

        const QString title = index.data(SearchTitleRole).toString();
        if (title.isEmpty()) {
            return;
        }

        const bool isCurrent = index.data(SearchIsCurrentRole).toBool();
        const bool isFavorite = index.data(SearchIsFavoriteRole).toBool();
        const SearchResultLayoutRects rects =
            searchResultLayoutRects(option.rect, isCurrent, searchResultButtonWidth(option.fontMetrics));
        const bool isSelected = option.state.testFlag(QStyle::State_Selected);

        drawSearchResultText(*painter,
                             rects.textRect,
                             title,
                             index.data(SearchTimeChannelRole).toString(),
                             index.data(SearchEpisodeRole).toString(),
                             index.data(SearchSynopsisRole).toString(),
                             option.palette,
                             isSelected,
                             visualTheme_);

        if (isCurrent) {
            drawGuideStyleActionButton(*painter,
                                       rects.watchRect,
                                       watchActionLabelForWidth(option.fontMetrics, rects.watchRect.width()),
                                       visualTheme_,
                                       visualTheme_.actionText);
        }
        drawGuideStyleActionButton(
            *painter, rects.favoriteRect, favoriteActionLabel(isFavorite), visualTheme_, visualTheme_.actionFavoriteText);
    }

private:
    TvGuideVisualTheme visualTheme_;
};

int measureEntryTextHeight(const QFont &font,
                           int width,
                           const TvGuideEntry &entry,
                           const QHash<QString, int> &favoriteShowRatings,
                           const TvGuideVisualTheme &visualTheme)
{
    if (width <= 0 || entry.title.trimmed().isEmpty()) {
        return 0;
    }

    QTextDocument document;
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    document.setHtml(formatEntryHtml(entry, favoriteShowRatings, visualTheme));
    document.setTextWidth(width);
    return std::max(0, static_cast<int>(std::ceil(document.size().height())));
}

void drawEntryText(QPainter &painter,
                   const QRect &textRect,
                   const TvGuideEntry &entry,
                   const QHash<QString, int> &favoriteShowRatings,
                   const TvGuideVisualTheme &visualTheme)
{
    if (!textRect.isValid() || entry.title.trimmed().isEmpty()) {
        return;
    }

    QTextDocument document;
    document.setDefaultFont(painter.font());
    document.setDocumentMargin(0);
    document.setHtml(formatEntryHtml(entry, favoriteShowRatings, visualTheme));
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
                              const TvGuideVisualTheme &visualTheme,
                              QWidget *parent = nullptr)
        : QWidget(parent)
        , windowStartUtc_(windowStartUtc)
        , slotMinutes_(slotMinutes)
        , slotCount_(slotCount)
        , timelineWidth_(timelineWidth)
        , visualTheme_(visualTheme)
    {
        setMinimumHeight(kGuideHeaderHeight);
        setMaximumHeight(kGuideHeaderHeight);
        setMinimumWidth(std::max(timelineWidth_, 1));
        setFont(visualTheme_.guideHeaderFont);
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
        painter.fillRect(rect(), visualTheme_.background);
        painter.setPen(QPen(visualTheme_.border, kGuideGridLineWidth));

        const int slotCount = std::max(slotCount_, 1);
        for (int col = 0; col <= slotCount; ++col) {
            const int x = std::lround(static_cast<double>(col) * width() / slotCount);
            painter.drawLine(x, 0, x, height());
        }
        painter.drawLine(0, height() - 1, width(), height() - 1);

        painter.setPen(visualTheme_.text);
        painter.setFont(visualTheme_.guideHeaderFont);
        const QDateTime windowEndUtc =
            windowStartUtc_.addSecs(static_cast<qint64>(std::max(slotCount_, 0)) * slotMinutes_ * 60);
        const bool spansMultipleDays =
            windowStartUtc_.isValid()
            && windowEndUtc.isValid()
            && windowStartUtc_.toLocalTime().date() != windowEndUtc.toLocalTime().date();
        for (int col = 0; col < slotCount_; ++col) {
            const int left = std::lround(static_cast<double>(col) * width() / slotCount);
            const int right = std::lround(static_cast<double>(col + 1) * width() / slotCount);
            const QRect slotRect(left, 0, std::max(1, right - left), height());
            const QString label =
                formatGuideTimelineLabel(windowStartUtc_.addSecs(static_cast<qint64>(col) * slotMinutes_ * 60),
                                         spansMultipleDays);
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
        painter.setPen(QPen(visualTheme_.nowLine, 2));
        painter.drawLine(nowX, 0, nowX, height());
    }

private:
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
    int timelineWidth_{0};
    TvGuideVisualTheme visualTheme_;
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
                           const TvGuideVisualTheme &visualTheme,
                           std::function<bool(const TvGuideEntry &)> isEntryScheduled,
                           std::function<void(const TvGuideEntry &, bool)> toggleSchedule,
                           std::function<void(const TvGuideEntry &)> watchNow,
                           QWidget *parent = nullptr)
        : QWidget(parent)
        , entries_(entries)
        , favoriteShowRatings_(favoriteShowRatings)
        , windowStartUtc_(windowStartUtc)
        , slotMinutes_(slotMinutes)
        , slotCount_(slotCount)
        , timelineWidth_(timelineWidth)
        , visualTheme_(visualTheme)
        , isEntryScheduled_(std::move(isEntryScheduled))
        , toggleSchedule_(std::move(toggleSchedule))
        , watchNow_(std::move(watchNow))
    {
        setMouseTracking(true);
        setFont(visualTheme_.guideFont);
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
        painter.fillRect(rect(), visualTheme_.background);

        const int slotCount = std::max(slotCount_, 1);
        painter.setPen(QPen(visualTheme_.gridLine, kGuideGridLineWidth));
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
        entryActionTargets_.clear();

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
            const bool canWatchNow = airingNow && static_cast<bool>(watchNow_);
            const bool scheduled = canSchedule && isEntryScheduled_ && isEntryScheduled_(entry);
            QRect checkboxRect;
            QRect watchRect;
            int actionInset = canSchedule ? (kGuideScheduleCheckboxSize + 16) : 10;
            if (canSchedule) {
                checkboxRect = QRect(box.right() - kGuideScheduleCheckboxSize - 8,
                                     box.top() + 8,
                                     kGuideScheduleCheckboxSize,
                                     kGuideScheduleCheckboxSize);
            } else if (canWatchNow) {
                const int maxWatchButtonWidth = std::max(0, box.width() - 18);
                const int watchButtonWidth = std::min(kGuideWatchNowButtonWidth, maxWatchButtonWidth);
                if (watchButtonWidth >= 44) {
                    watchRect = QRect(box.right() - watchButtonWidth - 8,
                                      box.top() + 6,
                                      watchButtonWidth,
                                      kGuideWatchNowButtonHeight);
                    actionInset = watchButtonWidth + 16;
                } else {
                    watchRect = box.adjusted(6, 6, -6, -6);
                }
            }
            painter.setPen(QPen(visualTheme_.entryBorder, kGuideBoxBorderWidth));
            painter.setBrush(airingNow ? visualTheme_.currentEntryBackground : visualTheme_.entryBackground);
            painter.drawRect(box);

            if (canSchedule) {
                painter.setPen(QPen(visualTheme_.secondaryText, 1));
                painter.setBrush(scheduled ? visualTheme_.nowLine : visualTheme_.background);
                painter.drawRect(checkboxRect);
                if (scheduled) {
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setPen(QPen(visualTheme_.text, 2));
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
            } else if (canWatchNow) {
                if (watchRect.width() >= 44) {
                    const QString watchLabel = watchActionLabelForWidth(painter.fontMetrics(), watchRect.width());
                    drawGuideStyleActionButton(painter, watchRect, watchLabel, visualTheme_, visualTheme_.actionText);
                }
            }
            entryActionTargets_.append({checkboxRect, watchRect, box, entry, scheduled});

            painter.setPen(visualTheme_.text);
            painter.setFont(visualTheme_.guideFont);
            drawEntryText(
                painter, box.adjusted(10, 8, -actionInset, -8), entry, favoriteShowRatings_, visualTheme_);
            renderedAny = true;
        }

        if (!renderedAny) {
            painter.setPen(visualTheme_.emptyText);
            painter.drawText(rect().adjusted(10, 0, -10, 0), Qt::AlignCenter, "NO GUIDE DATA");
        }

        const qint64 nowOffset = windowStartUtc_.secsTo(QDateTime::currentDateTimeUtc());
        if (nowOffset >= 0 && nowOffset <= totalSeconds) {
            const int nowX = std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * width() / totalSeconds)),
                                        0,
                                        width() - 1);
            painter.setPen(QPen(visualTheme_.nowLine, 2));
            painter.drawLine(nowX, 0, nowX, height());
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event == nullptr) {
            return;
        }

        const EntryActionTarget *target = targetAt(event->position().toPoint());
        if (target != nullptr) {
            const QPoint point = event->position().toPoint();
            setCursor((target->checkboxRect.contains(point) || target->watchRect.contains(point))
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

        const EntryActionTarget *target = targetAt(event->position().toPoint());
        if (target == nullptr) {
            QWidget::mousePressEvent(event);
            return;
        }

        const QPoint point = event->position().toPoint();
        if (target->checkboxRect.contains(point) && toggleSchedule_) {
            toggleSchedule_(target->entry, !target->scheduled);
            event->accept();
            return;
        }
        if (target->watchRect.contains(point) && watchNow_) {
            watchNow_(target->entry);
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

private:
    struct EntryActionTarget {
        QRect checkboxRect;
        QRect watchRect;
        QRect boxRect;
        TvGuideEntry entry;
        bool scheduled{false};
    };

    const EntryActionTarget *targetAt(const QPoint &point) const
    {
        for (const EntryActionTarget &target : entryActionTargets_) {
            if (target.checkboxRect.contains(point)
                || target.watchRect.contains(point)
                || target.boxRect.contains(point)) {
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
            const bool airingNow = entry.startUtc <= QDateTime::currentDateTimeUtc()
                                   && entry.endUtc > QDateTime::currentDateTimeUtc();
            const int textWidth = std::max(8,
                                           boxWidth - 20
                                               - (entry.startUtc > QDateTime::currentDateTimeUtc()
                                                      ? (kGuideScheduleCheckboxSize + 16)
                                                      : (airingNow && static_cast<bool>(watchNow_) && boxWidth >= 62
                                                             ? (kGuideWatchNowButtonWidth + 16)
                                                             : 0)));
            const int textHeight =
                measureEntryTextHeight(visualTheme_.guideFont, textWidth, entry, favoriteShowRatings_, visualTheme_);
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
    TvGuideVisualTheme visualTheme_;
    std::function<bool(const TvGuideEntry &)> isEntryScheduled_;
    std::function<void(const TvGuideEntry &, bool)> toggleSchedule_;
    std::function<void(const TvGuideEntry &)> watchNow_;
    QList<EntryActionTarget> entryActionTargets_;
};

}

TvGuideDialog::TvGuideDialog(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

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
    cornerLabel->setObjectName("guideCornerLabel");
    cornerLabel->setMinimumWidth(kGuideChannelLabelWidth);
    cornerLabel->setMaximumWidth(kGuideChannelLabelWidth);
    cornerLabel->setMinimumHeight(kGuideHeaderHeight);
    cornerLabel->setMaximumHeight(kGuideHeaderHeight);
    cornerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
    showSearchResultsList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    showSearchResultsList_->setUniformItemSizes(true);
    showSearchResultsList_->setWordWrap(true);
    showSearchResultsList_->viewport()->installEventFilter(this);
    searchLayout->addWidget(showSearchResultsList_, 1);

    searchUpdateTimer_ = new QTimer(this);
    searchUpdateTimer_->setSingleShot(true);
    searchUpdateTimer_->setInterval(140);
    connect(searchUpdateTimer_, &QTimer::timeout, this, &TvGuideDialog::updateSearchResults);

    connect(showSearchEdit_, &QLineEdit::textChanged, this, [this]() {
        if (showSearchSummaryLabel_ != nullptr) {
            const QString query = showSearchEdit_ != nullptr ? showSearchEdit_->text().simplified() : QString();
            showSearchSummaryLabel_->setText(query.isEmpty()
                                                 ? "Search the current guide cache by title or synopsis."
                                                 : "Searching current guide data...");
        }
        if (searchUpdateTimer_ != nullptr) {
            searchUpdateTimer_->start();
        } else {
            updateSearchResults();
        }
    });
    connect(showSearchResultsList_, &QListWidget::itemSelectionChanged, this, [this]() {
        updateSearchActionState();
    });
    connect(showSearchResultsList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        scheduleSelectedSearchResult();
    });
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

    setDisplayTheme(defaultDisplayTheme());
}

void TvGuideDialog::setDisplayTheme(const DisplayTheme &theme)
{
    displayTheme_ = normalizedDisplayTheme(theme);
    const TvGuideVisualTheme visualTheme = guideVisualThemeFor(displayTheme_);
    const QString tabFontCss =
        styleSheetFontFragment(displayThemeFontStyle(displayTheme_, DisplayThemeKeys::TabFont));

    setFont(visualTheme.guideFont);
    setStyleSheet(
        QString(
            "QWidget { background-color: %1; color: %2; }"
            "QPushButton { background-color: %3; color: %4; border: 1px solid %5; padding: 6px 12px; }"
            "QPushButton:disabled { color: %6; border-color: %7; }"
            "QTabWidget::pane { border: 1px solid %8; top: -1px; }"
            "QTabBar::tab { background-color: %9; color: %10; border: 1px solid %8; padding: 8px 14px; min-width: 110px; %14 }"
            "QTabBar::tab:selected { background-color: %11; }"
            "QLineEdit, QListWidget { background-color: %12; color: %13; border: 1px solid %8; }"
            "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %8; }"
            "QTableWidget { background-color: %1; alternate-background-color: %12; color: %2; gridline-color: %8; }"
            "QHeaderView::section { background-color: %1; color: %2; border: 1px solid %8; padding: 4px; }")
            .arg(visualTheme.background.name(),
                 visualTheme.text.name(),
                 visualTheme.buttonBackground.name(),
                 visualTheme.buttonText.name(),
                 visualTheme.buttonBorder.name(),
                 displayThemeColor(displayTheme_, DisplayThemeKeys::ButtonDisabledText).name(),
                 displayThemeColor(displayTheme_, DisplayThemeKeys::ButtonDisabledBorder).name(),
                 visualTheme.border.name(),
                 visualTheme.tabBackground.name(),
                 visualTheme.tabText.name(),
                 visualTheme.tabSelectedBackground.name(),
                 displayThemeColor(displayTheme_, DisplayThemeKeys::InputBackground).name(),
                 displayThemeColor(displayTheme_, DisplayThemeKeys::InputText).name(),
                 tabFontCss));

    if (tabs_ != nullptr && tabs_->tabBar() != nullptr) {
        tabs_->tabBar()->setFont(visualTheme.tabFont);
    }
    for (QLabel *label : findChildren<QLabel *>()) {
        if (label != nullptr) {
            label->setFont(visualTheme.guideSearchFont);
        }
    }
    if (QLabel *cornerLabel = findChild<QLabel *>("guideCornerLabel")) {
        cornerLabel->setFont(visualTheme.guideHeaderFont);
        cornerLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(visualTheme.text.name()));
    }
    if (refreshButton_ != nullptr) {
        refreshButton_->setFont(visualTheme.buttonFont);
    }
    if (showSearchEdit_ != nullptr) {
        showSearchEdit_->setFont(visualTheme.inputFont);
    }
    if (showSearchResultsList_ != nullptr) {
        showSearchResultsList_->setFont(visualTheme.guideSearchFont);
        if (QAbstractItemDelegate *existingDelegate = showSearchResultsList_->itemDelegate();
            existingDelegate != nullptr && existingDelegate->parent() == showSearchResultsList_) {
            existingDelegate->deleteLater();
        }
        showSearchResultsList_->setItemDelegate(new SearchResultItemDelegate(visualTheme, showSearchResultsList_));
    }
    if (showSearchSummaryLabel_ != nullptr) {
        showSearchSummaryLabel_->setFont(visualTheme.guideSearchFont);
    }
    if (logsView_ != nullptr) {
        logsView_->setFont(visualTheme.logFont);
    }

    if (slotCount_ > 0) {
        updateSearchResults();
        renderGuideTable();
    }
}

bool TvGuideDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (showSearchResultsList_ != nullptr
        && watched == showSearchResultsList_->viewport()
        && event != nullptr) {
        if (event->type() == QEvent::Leave) {
            showSearchResultsList_->viewport()->unsetCursor();
        } else if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            QListWidgetItem *item = showSearchResultsList_->itemAt(mouseEvent->pos());
            bool overActionButton = false;
            if (item != nullptr) {
                const QRect itemRect = showSearchResultsList_->visualItemRect(item);
                const bool isCurrent = item->data(SearchIsCurrentRole).toBool();
                const SearchResultLayoutRects rects =
                    searchResultLayoutRects(itemRect,
                                            isCurrent,
                                            searchResultButtonWidth(QFontMetrics(showSearchResultsList_->font())));
                overActionButton =
                    rects.favoriteRect.contains(mouseEvent->pos()) || rects.watchRect.contains(mouseEvent->pos());

                if (event->type() == QEvent::MouseButtonRelease
                    && mouseEvent->button() == Qt::LeftButton
                    && overActionButton) {
                    const int row = showSearchResultsList_->row(item);
                    if (row >= 0 && row < searchResults_.size()) {
                        showSearchResultsList_->setCurrentItem(item);
                        const SearchResult &result = searchResults_.at(row);
                        if (rects.watchRect.contains(mouseEvent->pos()) && isCurrent) {
                            emit watchRequested(result.channelName, result.entry);
                        } else if (rects.favoriteRect.contains(mouseEvent->pos())) {
                            emit searchScheduleRequested(
                                result.entry.title.simplified(), result.channelName, result.entry);
                        }
                        return true;
                    }
                }
            }

            showSearchResultsList_->viewport()->setCursor(overActionButton ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TvGuideDialog::setLoadingState(const QString &message)
{
    searchIndex_.clear();
    searchResults_.clear();
    if (showSearchResultsList_ != nullptr) {
        showSearchResultsList_->clear();
    }
    if (showSearchSummaryLabel_ != nullptr) {
        showSearchSummaryLabel_->setText("Search the current guide cache by title or synopsis.");
    }
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
        if (!showSearchEdit_->text().simplified().isEmpty()) {
            updateSearchResults();
        }
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
            updateSearchResults();
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
    rebuildSearchIndex();

    if (!isVisible()
        || guideScrollArea_ == nullptr
        || guideScrollArea_->viewport() == nullptr
        || guideScrollArea_->viewport()->width() <= 0) {
        pendingGuideRender_ = true;
        return;
    }

    pendingGuideRender_ = false;
    updateSearchResults();
    renderGuideTable();
}

void TvGuideDialog::rebuildSearchIndex()
{
    searchIndex_.clear();

    QStringList orderedChannels = channelOrder_;
    for (auto it = entriesByChannel_.cbegin(); it != entriesByChannel_.cend(); ++it) {
        if (!orderedChannels.contains(it.key())) {
            orderedChannels.append(it.key());
        }
    }

    for (const QString &channelName : orderedChannels) {
        const QList<TvGuideEntry> entries = entriesByChannel_.value(channelName);
        for (const TvGuideEntry &entry : entries) {
            const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
            if (parts.title.isEmpty()) {
                continue;
            }

            SearchResult result;
            result.channelName = channelName;
            result.entry = entry;
            result.ratedTitle = formatRatedShowTitle(parts.title, favoriteShowRatings_);
            result.episodeTitle = parts.episodeTitle;
            result.synopsisBody = parts.synopsisBody;
            result.timeChannelText = QString("%1 - %2 | Channel: %3")
                                         .arg(formatGuideSearchDateTime(entry.startUtc),
                                              formatGuideSearchDateTime(entry.endUtc),
                                              channelName);
            result.toolTip = formatEntryToolTip(entry, favoriteShowRatings_);
            result.normalizedHaystack =
                normalizedSearchText(parts.title, parts.episodeTitle, parts.synopsisBody);
            result.isFavorite = favoriteShowRatings_.contains(normalizeFavoriteShowRule(parts.title));
            searchIndex_.append(result);
        }
    }

    std::sort(searchIndex_.begin(), searchIndex_.end(), [](const SearchResult &left, const SearchResult &right) {
        if (left.entry.startUtc == right.entry.startUtc) {
            if (left.channelName == right.channelName) {
                return left.entry.title.localeAwareCompare(right.entry.title) < 0;
            }
            return left.channelName.localeAwareCompare(right.channelName) < 0;
        }
        return left.entry.startUtc < right.entry.startUtc;
    });
}

void TvGuideDialog::updateSearchResults()
{
    if (showSearchResultsList_ == nullptr || showSearchSummaryLabel_ == nullptr || showSearchEdit_ == nullptr) {
        return;
    }

    const bool restoreUpdates = showSearchResultsList_->updatesEnabled();
    showSearchResultsList_->setUpdatesEnabled(false);

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
        showSearchResultsList_->setUpdatesEnabled(restoreUpdates);
        updateSearchActionState();
        return;
    }

    const QString normalizedQuery = query.toCaseFolded();
    searchResults_.reserve(searchIndex_.size());
    for (const SearchResult &indexedResult : searchIndex_) {
        if (indexedResult.normalizedHaystack.contains(normalizedQuery)) {
            searchResults_.append(indexedResult);
        }
    }

    const int viewportWidth =
        showSearchResultsList_->viewport() != nullptr ? showSearchResultsList_->viewport()->width() : 720;
    const QFontMetrics fontMetrics(showSearchResultsList_->font());
    int restoredRow = -1;
    for (const SearchResult &result : searchResults_) {
        const bool isCurrent = searchResultIsCurrent(result);

        QListWidgetItem *item = new QListWidgetItem(showSearchResultsList_);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setToolTip(result.toolTip);
        item->setData(SearchTitleRole, result.ratedTitle);
        item->setData(SearchTimeChannelRole, result.timeChannelText);
        item->setData(SearchEpisodeRole, result.episodeTitle);
        item->setData(SearchSynopsisRole, result.synopsisBody);
        item->setData(SearchIsCurrentRole, isCurrent);
        item->setData(SearchIsFavoriteRole, result.isFavorite);
        item->setData(
            Qt::SizeHintRole,
            QSize(viewportWidth, searchResultItemHeight(fontMetrics)));

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

    showSearchResultsList_->setUpdatesEnabled(restoreUpdates);
    updateSearchActionState();
}

void TvGuideDialog::updateSearchActionState()
{
    if (showSearchResultsList_ == nullptr) {
        return;
    }
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

bool TvGuideDialog::searchResultIsCurrent(const SearchResult &result) const
{
    if (!result.entry.startUtc.isValid() || !result.entry.endUtc.isValid()) {
        return false;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    return result.entry.startUtc <= nowUtc && nowUtc < result.entry.endUtc;
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
    const TvGuideVisualTheme visualTheme = guideVisualThemeFor(displayTheme_);

    auto *timelineHeader = new GuideTimelineHeaderWidget(windowStartUtc_,
                                                         slotMinutes_,
                                                         slotCount_,
                                                         timelineWidth,
                                                         visualTheme,
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
                                                      visualTheme,
                                                      [this, channel](const TvGuideEntry &entry) {
                                                          return isEntryScheduled(channel, entry);
                                                      },
                                                      [this, channel](const TvGuideEntry &entry, bool enabled) {
                                                          emit scheduleSwitchRequested(channel, entry, enabled);
                                                      },
                                                      [this, channel](const TvGuideEntry &entry) {
                                                          emit watchRequested(channel, entry);
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
        channelLabel->setFont(visualTheme.guideChannelFont);
        channelLabel->setStyleSheet(
            QString("QLabel { color: %1; padding-right: 8px; }").arg(visualTheme.text.name()));
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
        emptyLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(visualTheme.emptyText.name()));
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
