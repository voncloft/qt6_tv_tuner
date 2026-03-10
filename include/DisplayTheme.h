#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QList>
#include <QPalette>
#include <QString>

namespace DisplayThemeKeys {
inline constexpr auto WindowBackground = "windowBackground";
inline constexpr auto WindowText = "windowText";
inline constexpr auto GroupBorder = "groupBorder";
inline constexpr auto InputBackground = "inputBackground";
inline constexpr auto InputText = "inputText";
inline constexpr auto InputBorder = "inputBorder";
inline constexpr auto ButtonBackground = "buttonBackground";
inline constexpr auto ButtonText = "buttonText";
inline constexpr auto ButtonBorder = "buttonBorder";
inline constexpr auto ButtonDisabledText = "buttonDisabledText";
inline constexpr auto ButtonDisabledBorder = "buttonDisabledBorder";
inline constexpr auto CheckBoxIndicatorBackground = "checkBoxIndicatorBackground";
inline constexpr auto CheckBoxIndicatorBorder = "checkBoxIndicatorBorder";
inline constexpr auto CheckBoxIndicatorChecked = "checkBoxIndicatorChecked";
inline constexpr auto LabelText = "labelText";
inline constexpr auto MutedText = "mutedText";
inline constexpr auto TabBackground = "tabBackground";
inline constexpr auto TabSelectedBackground = "tabSelectedBackground";
inline constexpr auto TabText = "tabText";
inline constexpr auto TabBorder = "tabBorder";
inline constexpr auto MenuBackground = "menuBackground";
inline constexpr auto MenuText = "menuText";
inline constexpr auto StatusBackground = "statusBackground";
inline constexpr auto StatusText = "statusText";
inline constexpr auto HeaderBackground = "headerBackground";
inline constexpr auto HeaderText = "headerText";
inline constexpr auto HeaderBorder = "headerBorder";
inline constexpr auto Highlight = "highlight";
inline constexpr auto HighlightText = "highlightText";
inline constexpr auto Accent = "accent";
inline constexpr auto ScrollbarTrack = "scrollbarTrack";
inline constexpr auto ScrollbarThumb = "scrollbarThumb";
inline constexpr auto ScrollbarThumbHover = "scrollbarThumbHover";
inline constexpr auto ScrollbarBorder = "scrollbarBorder";
inline constexpr auto SliderTrack = "sliderTrack";
inline constexpr auto SliderFilledTrack = "sliderFilledTrack";
inline constexpr auto SliderHandle = "sliderHandle";
inline constexpr auto SliderHandleHover = "sliderHandleHover";
inline constexpr auto SliderHandleBorder = "sliderHandleBorder";
inline constexpr auto FullscreenOverlayBackground = "fullscreenOverlayBackground";
inline constexpr auto FullscreenOverlayText = "fullscreenOverlayText";
inline constexpr auto GuideBackground = "guideBackground";
inline constexpr auto GuideText = "guideText";
inline constexpr auto GuideSecondaryText = "guideSecondaryText";
inline constexpr auto GuideEpisodeText = "guideEpisodeText";
inline constexpr auto GuideBorder = "guideBorder";
inline constexpr auto GuideTabBackground = "guideTabBackground";
inline constexpr auto GuideTabSelectedBackground = "guideTabSelectedBackground";
inline constexpr auto GuideTabText = "guideTabText";
inline constexpr auto GuideButtonBackground = "guideButtonBackground";
inline constexpr auto GuideButtonText = "guideButtonText";
inline constexpr auto GuideButtonBorder = "guideButtonBorder";
inline constexpr auto GuideGridLine = "guideGridLine";
inline constexpr auto GuideEntryBackground = "guideEntryBackground";
inline constexpr auto GuideCurrentEntryBackground = "guideCurrentEntryBackground";
inline constexpr auto GuideEntryBorder = "guideEntryBorder";
inline constexpr auto GuideNowLine = "guideNowLine";
inline constexpr auto GuideActionBackground = "guideActionBackground";
inline constexpr auto GuideActionBorder = "guideActionBorder";
inline constexpr auto GuideActionText = "guideActionText";
inline constexpr auto GuideActionFavoriteText = "guideActionFavoriteText";
inline constexpr auto GuideEmptyText = "guideEmptyText";

inline constexpr auto AppFont = "appFont";
inline constexpr auto LabelFont = "labelFont";
inline constexpr auto ButtonFont = "buttonFont";
inline constexpr auto InputFont = "inputFont";
inline constexpr auto TabFont = "tabFont";
inline constexpr auto MenuFont = "menuFont";
inline constexpr auto StatusFont = "statusFont";
inline constexpr auto LogFont = "logFont";
inline constexpr auto GuideFont = "guideFont";
inline constexpr auto GuideHeaderFont = "guideHeaderFont";
inline constexpr auto GuideChannelFont = "guideChannelFont";
inline constexpr auto GuideSearchFont = "guideSearchFont";
inline constexpr auto OverlayFont = "overlayFont";
}

struct DisplayColorRoleSpec {
    QString key;
    QString label;
};

struct DisplayFontStyle {
    QString family;
    int pointSize{10};
    bool bold{false};
    bool italic{false};
    bool underline{false};
};

struct DisplayFontRoleSpec {
    QString key;
    QString label;
};

struct DisplayTheme {
    QString name;
    QHash<QString, QColor> colors;
    QHash<QString, DisplayFontStyle> fonts;
};

struct DisplayThemeStore {
    DisplayTheme currentTheme;
    QList<DisplayTheme> savedThemes;
};

QList<DisplayColorRoleSpec> displayColorRoleSpecs();
QList<DisplayFontRoleSpec> displayFontRoleSpecs();

DisplayTheme defaultDisplayTheme();
DisplayTheme normalizedDisplayTheme(const DisplayTheme &theme);
DisplayThemeStore defaultDisplayThemeStore();
DisplayThemeStore normalizedDisplayThemeStore(const DisplayThemeStore &store);

QString resolveDisplayThemeStorePath();
bool loadDisplayThemeStore(DisplayThemeStore *store, QString *errorText = nullptr);
bool saveDisplayThemeStore(const DisplayThemeStore &store, QString *errorText = nullptr);

QColor displayThemeColor(const DisplayTheme &theme, const QString &key);
void setDisplayThemeColor(DisplayTheme *theme, const QString &key, const QColor &color);
DisplayFontStyle displayThemeFontStyle(const DisplayTheme &theme, const QString &key);
void setDisplayThemeFontStyle(DisplayTheme *theme, const QString &key, const DisplayFontStyle &style);
QFont qFontFromDisplayFontStyle(const DisplayFontStyle &style, const QFont &fallback = QFont());
QString styleSheetFontFragment(const DisplayFontStyle &style);

QPalette buildApplicationPalette(const DisplayTheme &theme, const QPalette &fallback = QPalette());
QString buildScrollBarStyleSheet(const DisplayTheme &theme);
QString buildSliderStyleSheet(const DisplayTheme &theme);
