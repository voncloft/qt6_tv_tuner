#include "TvGuideDialog.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
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
#include <QPixmap>
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
constexpr int kGuideEntrySectionSpacing = 4;
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

struct GuideEntryTextSections {
    QString title;
    QString episodeTitle;
    QString synopsisBody;
};

struct GuideEntryFonts {
    QFont titleFont;
    QFont episodeFont;
    QFont synopsisFont;
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

GuideEntryFonts guideEntryFonts(const QFont &baseFont)
{
    GuideEntryFonts fonts;
    fonts.titleFont = baseFont;
    fonts.titleFont.setWeight(QFont::DemiBold);

    fonts.episodeFont = baseFont;
    fonts.episodeFont.setItalic(true);

    fonts.synopsisFont = baseFont;
    if (fonts.synopsisFont.pointSizeF() > 0.0) {
        fonts.synopsisFont.setPointSizeF(std::max(1.0, fonts.synopsisFont.pointSizeF() * 0.9));
    } else if (fonts.synopsisFont.pixelSize() > 0) {
        fonts.synopsisFont.setPixelSize(std::max(1, qRound(fonts.synopsisFont.pixelSize() * 0.9)));
    }

    return fonts;
}

int wrappedTextHeight(const QFont &font, int width, const QString &text)
{
    if (width <= 0 || text.isEmpty()) {
        return 0;
    }

    const QFontMetrics metrics(font);
    const QRect bounds =
        metrics.boundingRect(QRect(0, 0, width, 1000000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    return std::max(metrics.lineSpacing(), bounds.height());
}

bool pixmapMatchesSize(const QPixmap &pixmap, const QSize &logicalSize, qreal devicePixelRatio)
{
    if (logicalSize.width() <= 0 || logicalSize.height() <= 0 || pixmap.isNull()) {
        return false;
    }

    return qFuzzyCompare(pixmap.devicePixelRatio(), devicePixelRatio)
           && pixmap.width() == qRound(logicalSize.width() * devicePixelRatio)
           && pixmap.height() == qRound(logicalSize.height() * devicePixelRatio);
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

GuideEntryTextSections textSectionsForEntry(const TvGuideEntry &entry,
                                           const QHash<QString, int> &favoriteShowRatings)
{
    const GuideEntryDisplayParts parts = displayPartsForEntry(entry);
    return {formatRatedShowTitle(parts.title, favoriteShowRatings), parts.episodeTitle, parts.synopsisBody};
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

int measureEntryTextHeight(const QFont &font, int width, const GuideEntryTextSections &sections)
{
    if (width <= 0 || sections.title.isEmpty()) {
        return 0;
    }

    const GuideEntryFonts fonts = guideEntryFonts(font);
    int height = 0;
    auto appendHeight = [&](const QString &text, const QFont &sectionFont) {
        if (text.isEmpty()) {
            return;
        }
        if (height > 0) {
            height += kGuideEntrySectionSpacing;
        }
        height += wrappedTextHeight(sectionFont, width, text);
    };

    appendHeight(sections.title, fonts.titleFont);
    appendHeight(sections.episodeTitle, fonts.episodeFont);
    appendHeight(sections.synopsisBody, fonts.synopsisFont);
    return height;
}

void drawEntryText(QPainter &painter,
                   const QRect &textRect,
                   const GuideEntryTextSections &sections,
                   const TvGuideVisualTheme &visualTheme)
{
    if (!textRect.isValid() || sections.title.isEmpty()) {
        return;
    }

    const GuideEntryFonts fonts = guideEntryFonts(painter.font());
    int y = textRect.top();

    painter.save();
    painter.setClipRect(textRect);

    auto drawSection = [&](const QString &text, const QFont &sectionFont, const QColor &color) {
        if (text.isEmpty() || y > textRect.bottom()) {
            return;
        }

        const int sectionHeight = wrappedTextHeight(sectionFont, textRect.width(), text);
        const QRect sectionRect(textRect.left(),
                                y,
                                textRect.width(),
                                std::min(sectionHeight, textRect.bottom() - y + 1));
        painter.setFont(sectionFont);
        painter.setPen(color);
        painter.drawText(sectionRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
        y += sectionHeight + kGuideEntrySectionSpacing;
    };

    drawSection(sections.title, fonts.titleFont, visualTheme.text);
    drawSection(sections.episodeTitle, fonts.episodeFont, visualTheme.episodeText);
    drawSection(sections.synopsisBody, fonts.synopsisFont, visualTheme.secondaryText);
    painter.restore();
}

struct GuideEntryActionTarget {
    QRect checkboxRect;
    QRect watchRect;
    QRect boxRect;
    TvGuideEntry entry;
    bool scheduled{false};
};

struct GuidePreparedEntry {
    TvGuideEntry entry;
    GuideEntryTextSections textSections;
};

struct GuidePreparedRow {
    QString channelName;
    QList<GuidePreparedEntry> entries;
    int rowTop{0};
    int rowHeight{kGuideRowHeight};
    QList<GuideEntryActionTarget> actionTargets;
    QPixmap timelinePixmap;
};

int preferredGuideRowHeight(const QList<GuidePreparedEntry> &preparedEntries,
                            const QDateTime &windowStartUtc,
                            int slotMinutes,
                            int slotCount,
                            int timelineWidth,
                            const TvGuideVisualTheme &visualTheme,
                            bool hasWatchNowAction)
{
    const qint64 totalSeconds = static_cast<qint64>(slotMinutes) * std::max(slotCount, 1) * 60;
    if (totalSeconds <= 0 || !windowStartUtc.isValid()) {
        return kGuideRowHeight;
    }

    const QDateTime windowEndUtc = windowStartUtc.addSecs(totalSeconds);
    const int totalWidth = std::max(timelineWidth, 1);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    int preferred = kGuideRowHeight;

    for (const GuidePreparedEntry &preparedEntry : preparedEntries) {
        const TvGuideEntry &entry = preparedEntry.entry;
        if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
            continue;
        }
        if (entry.endUtc <= windowStartUtc || entry.startUtc >= windowEndUtc) {
            continue;
        }

        const qint64 visibleStart = std::clamp(windowStartUtc.secsTo(entry.startUtc), 0LL, totalSeconds);
        const qint64 visibleEnd = std::clamp(windowStartUtc.secsTo(entry.endUtc), 0LL, totalSeconds);
        if (visibleEnd <= visibleStart) {
            continue;
        }

        const int left =
            std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleStart) * totalWidth / totalSeconds)),
                       0,
                       std::max(0, totalWidth - 1));
        const int right =
            std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleEnd) * totalWidth / totalSeconds)),
                       left + 1,
                       totalWidth);
        const int boxWidth = std::max(28, right - left - 4);
        const bool airingNow = entry.startUtc <= nowUtc && entry.endUtc > nowUtc;
        const int textWidth =
            std::max(8,
                     boxWidth - 20
                         - (entry.startUtc > nowUtc ? (kGuideScheduleCheckboxSize + 16)
                                                    : (airingNow && hasWatchNowAction && boxWidth >= 62
                                                           ? (kGuideWatchNowButtonWidth + 16)
                                                           : 0)));
        const int textHeight = measureEntryTextHeight(visualTheme.guideFont, textWidth, preparedEntry.textSections);
        preferred = std::max(preferred, textHeight + 26);
    }

    return preferred;
}

void renderGuideHeaderPixmap(QPixmap &pixmap,
                             const QSize &logicalSize,
                             qreal devicePixelRatio,
                             const QDateTime &windowStartUtc,
                             int slotMinutes,
                             int slotCount,
                             const TvGuideVisualTheme &visualTheme)
{
    pixmap = QPixmap(qRound(logicalSize.width() * devicePixelRatio), qRound(logicalSize.height() * devicePixelRatio));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(QRect(QPoint(0, 0), logicalSize), visualTheme.background);
    painter.setPen(QPen(visualTheme.border, kGuideGridLineWidth));

    const int normalizedSlotCount = std::max(slotCount, 1);
    for (int col = 0; col <= normalizedSlotCount; ++col) {
        const int x = std::lround(static_cast<double>(col) * logicalSize.width() / normalizedSlotCount);
        painter.drawLine(x, 0, x, logicalSize.height());
    }
    painter.drawLine(0, logicalSize.height() - 1, logicalSize.width(), logicalSize.height() - 1);

    painter.setPen(visualTheme.text);
    painter.setFont(visualTheme.guideHeaderFont);
    const QDateTime windowEndUtc =
        windowStartUtc.addSecs(static_cast<qint64>(std::max(slotCount, 0)) * slotMinutes * 60);
    const bool spansMultipleDays =
        windowStartUtc.isValid()
        && windowEndUtc.isValid()
        && windowStartUtc.toLocalTime().date() != windowEndUtc.toLocalTime().date();
    for (int col = 0; col < slotCount; ++col) {
        const int left = std::lround(static_cast<double>(col) * logicalSize.width() / normalizedSlotCount);
        const int right = std::lround(static_cast<double>(col + 1) * logicalSize.width() / normalizedSlotCount);
        const QRect slotRect(left, 0, std::max(1, right - left), logicalSize.height());
        const QString label =
            formatGuideTimelineLabel(windowStartUtc.addSecs(static_cast<qint64>(col) * slotMinutes * 60),
                                     spansMultipleDays);
        painter.drawText(slotRect.adjusted(6, 0, -6, 0), Qt::AlignHCenter | Qt::AlignVCenter, label);
    }

    const qint64 totalSeconds = static_cast<qint64>(slotMinutes) * slotCount * 60;
    if (totalSeconds <= 0) {
        return;
    }

    const qint64 nowOffset = windowStartUtc.secsTo(QDateTime::currentDateTimeUtc());
    if (nowOffset < 0 || nowOffset > totalSeconds) {
        return;
    }

    const int nowX =
        std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * logicalSize.width() / totalSeconds)),
                   0,
                   logicalSize.width() - 1);
    painter.setPen(QPen(visualTheme.nowLine, 2));
    painter.drawLine(nowX, 0, nowX, logicalSize.height());
}

void renderGuideRowPixmap(GuidePreparedRow &row,
                          const QSize &logicalSize,
                          qreal devicePixelRatio,
                          const QDateTime &windowStartUtc,
                          int slotMinutes,
                          int slotCount,
                          const TvGuideVisualTheme &visualTheme,
                          bool hasWatchNowAction,
                          const std::function<bool(const QString &, const TvGuideEntry &)> &isEntryScheduled)
{
    row.timelinePixmap = QPixmap(qRound(logicalSize.width() * devicePixelRatio),
                                 qRound(logicalSize.height() * devicePixelRatio));
    row.timelinePixmap.setDevicePixelRatio(devicePixelRatio);
    row.timelinePixmap.fill(Qt::transparent);

    QPainter painter(&row.timelinePixmap);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(QRect(QPoint(0, 0), logicalSize), visualTheme.background);

    const int normalizedSlotCount = std::max(slotCount, 1);
    painter.setPen(QPen(visualTheme.gridLine, kGuideGridLineWidth));
    for (int col = 0; col <= normalizedSlotCount; ++col) {
        const int x = std::lround(static_cast<double>(col) * logicalSize.width() / normalizedSlotCount);
        painter.drawLine(x, 0, x, logicalSize.height());
    }
    painter.drawLine(0, logicalSize.height() - 1, logicalSize.width(), logicalSize.height() - 1);

    const qint64 totalSeconds = static_cast<qint64>(slotMinutes) * slotCount * 60;
    if (totalSeconds <= 0) {
        row.actionTargets.clear();
        return;
    }

    const QDateTime windowEndUtc = windowStartUtc.addSecs(totalSeconds);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    bool renderedAny = false;
    row.actionTargets.clear();
    row.actionTargets.reserve(row.entries.size());

    for (const GuidePreparedEntry &preparedEntry : row.entries) {
        const TvGuideEntry &entry = preparedEntry.entry;
        if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
            continue;
        }
        if (entry.endUtc <= windowStartUtc || entry.startUtc >= windowEndUtc) {
            continue;
        }

        const qint64 visibleStart = std::clamp(windowStartUtc.secsTo(entry.startUtc), 0LL, totalSeconds);
        const qint64 visibleEnd = std::clamp(windowStartUtc.secsTo(entry.endUtc), 0LL, totalSeconds);
        if (visibleEnd <= visibleStart) {
            continue;
        }

        const int left =
            std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleStart) * logicalSize.width() / totalSeconds)),
                       0,
                       logicalSize.width() - 1);
        const int right =
            std::clamp(static_cast<int>(std::llround(static_cast<double>(visibleEnd) * logicalSize.width() / totalSeconds)),
                       left + 1,
                       logicalSize.width());
        QRect box(left + 2, 5, std::max(28, right - left - 4), logicalSize.height() - 10);

        const bool airingNow = entry.startUtc <= nowUtc && entry.endUtc > nowUtc;
        const bool canSchedule = entry.startUtc > nowUtc;
        const bool canWatchNow = airingNow && hasWatchNowAction;
        const bool scheduled = canSchedule && isEntryScheduled && isEntryScheduled(row.channelName, entry);
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

        painter.setPen(QPen(visualTheme.entryBorder, kGuideBoxBorderWidth));
        painter.setBrush(airingNow ? visualTheme.currentEntryBackground : visualTheme.entryBackground);
        painter.drawRect(box);

        if (canSchedule) {
            painter.setPen(QPen(visualTheme.secondaryText, 1));
            painter.setBrush(scheduled ? visualTheme.nowLine : visualTheme.background);
            painter.drawRect(checkboxRect);
            if (scheduled) {
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(QPen(visualTheme.text, 2));
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
        } else if (canWatchNow && watchRect.width() >= 44) {
            const QString watchLabel = watchActionLabelForWidth(painter.fontMetrics(), watchRect.width());
            drawGuideStyleActionButton(painter, watchRect, watchLabel, visualTheme, visualTheme.actionText);
        }

        row.actionTargets.append({checkboxRect, watchRect, box, entry, scheduled});

        painter.setPen(visualTheme.text);
        painter.setFont(visualTheme.guideFont);
        drawEntryText(painter, box.adjusted(10, 8, -actionInset, -8), preparedEntry.textSections, visualTheme);
        renderedAny = true;
    }

    if (!renderedAny) {
        painter.setPen(visualTheme.emptyText);
        painter.drawText(QRect(QPoint(0, 0), logicalSize).adjusted(10, 0, -10, 0), Qt::AlignCenter, "NO GUIDE DATA");
    }

    const qint64 nowOffset = windowStartUtc.secsTo(QDateTime::currentDateTimeUtc());
    if (nowOffset >= 0 && nowOffset <= totalSeconds) {
        const int nowX =
            std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * logicalSize.width() / totalSeconds)),
                       0,
                       logicalSize.width() - 1);
        painter.setPen(QPen(visualTheme.nowLine, 2));
        painter.drawLine(nowX, 0, nowX, logicalSize.height());
    }
}

class GuideCanvasWidget final : public QAbstractScrollArea
{
public:
    explicit GuideCanvasWidget(QWidget *parent = nullptr)
        : QAbstractScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
    }

    void setGuideData(const QStringList &visibleChannels,
                      const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                      const QHash<QString, int> &favoriteShowRatings,
                      const QDateTime &windowStartUtc,
                      int slotMinutes,
                      int slotCount,
                      const TvGuideVisualTheme &visualTheme,
                      std::function<bool(const QString &, const TvGuideEntry &)> isEntryScheduled,
                      std::function<void(const QString &, const TvGuideEntry &, bool)> toggleSchedule,
                      std::function<void(const QString &, const TvGuideEntry &)> watchNow)
    {
        visibleChannels_ = visibleChannels;
        entriesByChannel_ = entriesByChannel;
        favoriteShowRatings_ = favoriteShowRatings;
        windowStartUtc_ = windowStartUtc;
        slotMinutes_ = slotMinutes;
        slotCount_ = slotCount;
        visualTheme_ = visualTheme;
        isEntryScheduled_ = std::move(isEntryScheduled);
        toggleSchedule_ = std::move(toggleSchedule);
        watchNow_ = std::move(watchNow);
        rebuildLayout(true);
    }

    void setVisualTheme(const TvGuideVisualTheme &visualTheme)
    {
        visualTheme_ = visualTheme;
        invalidateCaches();
        viewport()->update();
    }

    int slotPixelWidth() const
    {
        return currentGuideSlotPixelWidth_;
    }

    void scrollToCurrentTime(bool force)
    {
        if (!windowStartUtc_.isValid() || slotMinutes_ <= 0 || slotCount_ <= 0) {
            return;
        }

        QScrollBar *bar = horizontalScrollBar();
        if (bar == nullptr) {
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

        const int nowX =
            std::clamp(static_cast<int>(std::llround(static_cast<double>(nowOffset) * timelineWidth_ / totalSeconds)),
                       0,
                       std::max(0, timelineWidth_ - 1));
        const int viewportWidth = std::max(1, visibleTimelineWidth());
        const int left = bar->value();
        const int right = left + viewportWidth;
        if (!force && nowX >= left && nowX <= right - std::max(1, currentGuideSlotPixelWidth_ / 2)) {
            return;
        }

        const int target = std::clamp(nowX - std::max(1, currentGuideSlotPixelWidth_), 0, bar->maximum());
        bar->setValue(target);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), visualTheme_.background);

        const QRect cornerRect(0, 0, std::min(kGuideChannelLabelWidth, viewport()->width()), kGuideHeaderHeight);
        painter.setPen(QPen(visualTheme_.border, kGuideGridLineWidth));
        painter.setBrush(visualTheme_.background);
        painter.drawRect(cornerRect.adjusted(0, 0, -1, -1));
        painter.setPen(visualTheme_.text);
        painter.setFont(visualTheme_.guideHeaderFont);
        painter.drawText(cornerRect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, "Channel");

        const int timelineViewportWidth = visibleTimelineWidth();
        if (timelineViewportWidth > 0 && timelineWidth_ > 0 && slotCount_ > 0) {
            const QSize logicalHeaderSize(std::max(1, timelineWidth_), kGuideHeaderHeight);
            const qreal dpr = viewport()->devicePixelRatioF();
            if (!pixmapMatchesSize(headerPixmap_, logicalHeaderSize, dpr)) {
                renderGuideHeaderPixmap(
                    headerPixmap_, logicalHeaderSize, dpr, windowStartUtc_, slotMinutes_, slotCount_, visualTheme_);
            }
            const int horizontalOffset = horizontalScrollBar()->value();
            painter.drawPixmap(QRect(kGuideChannelLabelWidth, 0, timelineViewportWidth, kGuideHeaderHeight),
                               headerPixmap_,
                               QRect(horizontalOffset, 0, timelineViewportWidth, kGuideHeaderHeight));
        }

        const int verticalOffset = verticalScrollBar()->value();
        const int rowsViewportHeight = visibleRowsHeight();
        const int viewportBottom = verticalOffset + rowsViewportHeight;

        if (rows_.isEmpty()) {
            const QRect emptyLabelRect(0, kGuideHeaderHeight, kGuideChannelLabelWidth, rowsHeight_);
            painter.setPen(QPen(visualTheme_.gridLine, kGuideGridLineWidth));
            painter.drawLine(emptyLabelRect.left(),
                             emptyLabelRect.bottom() - 1,
                             emptyLabelRect.right(),
                             emptyLabelRect.bottom() - 1);
            if (timelineViewportWidth > 0) {
                const QRect emptyRect(kGuideChannelLabelWidth, kGuideHeaderHeight, timelineViewportWidth, rowsHeight_);
                painter.setPen(visualTheme_.emptyText);
                painter.drawText(emptyRect.adjusted(10, 0, -10, 0),
                                 Qt::AlignCenter,
                                 "No channels matched the current guide filter.");
            }
            return;
        }

        for (GuidePreparedRow &row : rows_) {
            if (row.rowTop + row.rowHeight < verticalOffset) {
                continue;
            }
            if (row.rowTop > viewportBottom) {
                break;
            }

            const int rowViewportTop = kGuideHeaderHeight + row.rowTop - verticalOffset;
            const QRect labelRect(0, rowViewportTop, kGuideChannelLabelWidth, row.rowHeight);
            painter.fillRect(labelRect, visualTheme_.background);
            painter.setPen(QPen(visualTheme_.border, kGuideGridLineWidth));
            painter.drawLine(labelRect.right(), labelRect.top(), labelRect.right(), labelRect.bottom());
            painter.setPen(QPen(visualTheme_.gridLine, kGuideGridLineWidth));
            painter.drawLine(labelRect.left(), labelRect.bottom() - 1, labelRect.right(), labelRect.bottom() - 1);
            painter.setPen(visualTheme_.text);
            painter.setFont(visualTheme_.guideChannelFont);
            painter.drawText(labelRect.adjusted(6, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, row.channelName);

            if (timelineViewportWidth <= 0 || timelineWidth_ <= 0) {
                continue;
            }

            const QSize logicalRowSize(std::max(1, timelineWidth_), row.rowHeight);
            const qreal dpr = viewport()->devicePixelRatioF();
            if (!pixmapMatchesSize(row.timelinePixmap, logicalRowSize, dpr)) {
                renderGuideRowPixmap(row,
                                     logicalRowSize,
                                     dpr,
                                     windowStartUtc_,
                                     slotMinutes_,
                                     slotCount_,
                                     visualTheme_,
                                     static_cast<bool>(watchNow_),
                                     isEntryScheduled_);
            }

            const int horizontalOffset = horizontalScrollBar()->value();
            painter.drawPixmap(QRect(kGuideChannelLabelWidth, rowViewportTop, timelineViewportWidth, row.rowHeight),
                               row.timelinePixmap,
                               QRect(horizontalOffset, 0, timelineViewportWidth, row.rowHeight));
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QAbstractScrollArea::resizeEvent(event);
        rebuildLayout(true);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event == nullptr) {
            return;
        }

        updateCursor(event->position().toPoint());
        QAbstractScrollArea::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event == nullptr || event->button() != Qt::LeftButton) {
            QAbstractScrollArea::mousePressEvent(event);
            return;
        }

        const HitTestResult hit = hitTest(event->position().toPoint());
        if (hit.row == nullptr || hit.target == nullptr) {
            QAbstractScrollArea::mousePressEvent(event);
            return;
        }

        const QPoint rowPoint(horizontalScrollBar()->value() + event->position().toPoint().x() - kGuideChannelLabelWidth,
                              verticalScrollBar()->value() + event->position().toPoint().y() - kGuideHeaderHeight - hit.row->rowTop);
        if (hit.target->checkboxRect.contains(rowPoint) && toggleSchedule_) {
            toggleSchedule_(hit.row->channelName, hit.target->entry, !hit.target->scheduled);
            event->accept();
            return;
        }
        if (hit.target->watchRect.contains(rowPoint) && watchNow_) {
            watchNow_(hit.row->channelName, hit.target->entry);
            event->accept();
            return;
        }

        QAbstractScrollArea::mousePressEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        QAbstractScrollArea::leaveEvent(event);
        viewport()->unsetCursor();
    }

    void scrollContentsBy(int dx, int dy) override
    {
        Q_UNUSED(dx);
        Q_UNUSED(dy);
        viewport()->update();
    }

private:
    struct HitTestResult {
        const GuidePreparedRow *row{nullptr};
        const GuideEntryActionTarget *target{nullptr};
    };

    int visibleTimelineWidth() const
    {
        const int width = viewport() != nullptr ? viewport()->width() - kGuideChannelLabelWidth : 0;
        return std::max(0, width);
    }

    int visibleRowsHeight() const
    {
        const int height = viewport() != nullptr ? viewport()->height() - kGuideHeaderHeight : 0;
        return std::max(0, height);
    }

    void invalidateCaches()
    {
        headerPixmap_ = QPixmap();
        for (GuidePreparedRow &row : rows_) {
            row.timelinePixmap = QPixmap();
            row.actionTargets.clear();
        }
    }

    void rebuildLayout(bool preserveScroll)
    {
        const int previousHorizontal = horizontalScrollBar()->value();
        const int previousVertical = verticalScrollBar()->value();

        const int timelineViewportWidth =
            visibleTimelineWidth() > 0 ? visibleTimelineWidth() : (kGuideVisibleColumnCount * kGuideSlotWidth);
        currentGuideSlotPixelWidth_ = std::max(1, timelineViewportWidth / kGuideVisibleColumnCount);
        timelineWidth_ = std::max(slotCount_, 1) * currentGuideSlotPixelWidth_;
        rows_.clear();
        rows_.reserve(visibleChannels_.size());

        int rowTop = 0;
        for (const QString &channelName : visibleChannels_) {
            GuidePreparedRow row;
            row.channelName = channelName;

            const QList<TvGuideEntry> entries = entriesByChannel_.value(channelName);
            row.entries.reserve(entries.size());
            for (const TvGuideEntry &entry : entries) {
                row.entries.append({entry, textSectionsForEntry(entry, favoriteShowRatings_)});
            }
            std::sort(row.entries.begin(), row.entries.end(), [](const GuidePreparedEntry &left, const GuidePreparedEntry &right) {
                return left.entry.startUtc < right.entry.startUtc;
            });

            row.rowHeight = preferredGuideRowHeight(row.entries,
                                                    windowStartUtc_,
                                                    slotMinutes_,
                                                    slotCount_,
                                                    timelineWidth_,
                                                    visualTheme_,
                                                    static_cast<bool>(watchNow_));
            row.rowTop = rowTop;
            rowTop += row.rowHeight;
            rows_.append(row);
        }

        rowsHeight_ = rows_.isEmpty() ? kGuideRowHeight : rowTop;
        invalidateCaches();

        horizontalScrollBar()->setSingleStep(std::max(12, currentGuideSlotPixelWidth_ / 8));
        horizontalScrollBar()->setPageStep(std::max(24, visibleTimelineWidth()));
        horizontalScrollBar()->setRange(0, std::max(0, timelineWidth_ - visibleTimelineWidth()));

        verticalScrollBar()->setSingleStep(std::max(24, kGuideRowHeight / 2));
        verticalScrollBar()->setPageStep(std::max(24, visibleRowsHeight()));
        verticalScrollBar()->setRange(0, std::max(0, rowsHeight_ - visibleRowsHeight()));

        if (preserveScroll) {
            horizontalScrollBar()->setValue(std::clamp(previousHorizontal, 0, horizontalScrollBar()->maximum()));
            verticalScrollBar()->setValue(std::clamp(previousVertical, 0, verticalScrollBar()->maximum()));
        } else {
            horizontalScrollBar()->setValue(0);
            verticalScrollBar()->setValue(0);
        }

        viewport()->update();
    }

    HitTestResult hitTest(const QPoint &viewportPoint) const
    {
        if (viewportPoint.y() < kGuideHeaderHeight || viewportPoint.x() < kGuideChannelLabelWidth) {
            return {};
        }

        const int contentY = verticalScrollBar()->value() + viewportPoint.y() - kGuideHeaderHeight;
        const int contentX = horizontalScrollBar()->value() + viewportPoint.x() - kGuideChannelLabelWidth;
        if (contentY < 0 || contentX < 0) {
            return {};
        }

        for (const GuidePreparedRow &row : rows_) {
            if (contentY < row.rowTop) {
                break;
            }
            if (contentY >= row.rowTop + row.rowHeight) {
                continue;
            }

            const QPoint rowPoint(contentX, contentY - row.rowTop);
            for (const GuideEntryActionTarget &target : row.actionTargets) {
                if (target.checkboxRect.contains(rowPoint)
                    || target.watchRect.contains(rowPoint)
                    || target.boxRect.contains(rowPoint)) {
                    return {&row, &target};
                }
            }
            break;
        }

        return {};
    }

    void updateCursor(const QPoint &viewportPoint)
    {
        const HitTestResult hit = hitTest(viewportPoint);
        if (hit.row == nullptr || hit.target == nullptr) {
            viewport()->unsetCursor();
            return;
        }

        const QPoint rowPoint(horizontalScrollBar()->value() + viewportPoint.x() - kGuideChannelLabelWidth,
                              verticalScrollBar()->value() + viewportPoint.y() - kGuideHeaderHeight - hit.row->rowTop);
        viewport()->setCursor((hit.target->checkboxRect.contains(rowPoint) || hit.target->watchRect.contains(rowPoint))
                                  ? Qt::PointingHandCursor
                                  : Qt::ArrowCursor);
    }

    QStringList visibleChannels_;
    QHash<QString, QList<TvGuideEntry>> entriesByChannel_;
    QHash<QString, int> favoriteShowRatings_;
    QDateTime windowStartUtc_;
    int slotMinutes_{30};
    int slotCount_{0};
    int currentGuideSlotPixelWidth_{kGuideSlotWidth};
    int timelineWidth_{kGuideVisibleColumnCount * kGuideSlotWidth};
    int rowsHeight_{kGuideRowHeight};
    TvGuideVisualTheme visualTheme_;
    QPixmap headerPixmap_;
    QList<GuidePreparedRow> rows_;
    std::function<bool(const QString &, const TvGuideEntry &)> isEntryScheduled_;
    std::function<void(const QString &, const TvGuideEntry &, bool)> toggleSchedule_;
    std::function<void(const QString &, const TvGuideEntry &)> watchNow_;
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
    guideView_ = new GuideCanvasWidget(guideTab);
    guideLayout->addWidget(guideView_, 1);
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
    if (guideView_ != nullptr) {
        static_cast<GuideCanvasWidget *>(guideView_)->setVisualTheme(visualTheme);
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

    if (slotCount_ <= 0 || !isVisible() || guideView_ == nullptr || guideView_->width() <= 0) {
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

    if (event == nullptr || slotCount_ <= 0 || guideView_ == nullptr) {
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
    if (guideView_ != nullptr) {
        return static_cast<GuideCanvasWidget *>(guideView_)->slotPixelWidth();
    }
    return kGuideSlotWidth;
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

    if (!isVisible() || guideView_ == nullptr || guideView_->width() <= 0) {
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
    if (guideView_ == nullptr) {
        return;
    }

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
    const TvGuideVisualTheme visualTheme = guideVisualThemeFor(displayTheme_);
    auto *guideView = static_cast<GuideCanvasWidget *>(guideView_);
    guideView->setGuideData(visibleChannels,
                            entriesByChannel_,
                            favoriteShowRatings_,
                            windowStartUtc_,
                            slotMinutes_,
                            slotCount_,
                            visualTheme,
                            [this](const QString &channelName, const TvGuideEntry &entry) {
                                return isEntryScheduled(channelName, entry);
                            },
                            [this](const QString &channelName, const TvGuideEntry &entry, bool enabled) {
                                emit scheduleSwitchRequested(channelName, entry, enabled);
                            },
                            [this](const QString &channelName, const TvGuideEntry &entry) {
                                emit watchRequested(channelName, entry);
                            });
    currentGuideSlotPixelWidth_ = guideView->slotPixelWidth();

    if (pendingSyncToCurrentTime_) {
        pendingSyncToCurrentTime_ = false;
        scrollGuideToCurrentTime(true);
    }
}

void TvGuideDialog::scrollGuideToCurrentTime(bool force)
{
    if (guideView_ == nullptr) {
        return;
    }
    static_cast<GuideCanvasWidget *>(guideView_)->scrollToCurrentTime(force);
}
