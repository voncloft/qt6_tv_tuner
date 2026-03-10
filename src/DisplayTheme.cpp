#include "DisplayTheme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QList<DisplayColorRoleSpec> &displayColorSpecsStorage()
{
    static const QList<DisplayColorRoleSpec> specs = {
        {DisplayThemeKeys::WindowBackground, "App background"},
        {DisplayThemeKeys::WindowText, "App text"},
        {DisplayThemeKeys::GroupBorder, "Group borders"},
        {DisplayThemeKeys::InputBackground, "Input background"},
        {DisplayThemeKeys::InputText, "Input text"},
        {DisplayThemeKeys::InputBorder, "Input borders"},
        {DisplayThemeKeys::ButtonBackground, "Button background"},
        {DisplayThemeKeys::ButtonText, "Button text"},
        {DisplayThemeKeys::ButtonBorder, "Button border"},
        {DisplayThemeKeys::ButtonDisabledText, "Disabled button text"},
        {DisplayThemeKeys::ButtonDisabledBorder, "Disabled button border"},
        {DisplayThemeKeys::CheckBoxIndicatorBackground, "Checkbox background"},
        {DisplayThemeKeys::CheckBoxIndicatorBorder, "Checkbox border"},
        {DisplayThemeKeys::CheckBoxIndicatorChecked, "Checkbox checked fill"},
        {DisplayThemeKeys::LabelText, "Label text"},
        {DisplayThemeKeys::MutedText, "Muted text"},
        {DisplayThemeKeys::TabBackground, "Main tab background"},
        {DisplayThemeKeys::TabSelectedBackground, "Main selected tab"},
        {DisplayThemeKeys::TabText, "Main tab text"},
        {DisplayThemeKeys::TabBorder, "Main tab border"},
        {DisplayThemeKeys::MenuBackground, "Menu background"},
        {DisplayThemeKeys::MenuText, "Menu text"},
        {DisplayThemeKeys::StatusBackground, "Status bar background"},
        {DisplayThemeKeys::StatusText, "Status bar text"},
        {DisplayThemeKeys::HeaderBackground, "Header background"},
        {DisplayThemeKeys::HeaderText, "Header text"},
        {DisplayThemeKeys::HeaderBorder, "Header border"},
        {DisplayThemeKeys::Highlight, "Highlight background"},
        {DisplayThemeKeys::HighlightText, "Highlight text"},
        {DisplayThemeKeys::Accent, "Accent"},
        {DisplayThemeKeys::ScrollbarTrack, "Scrollbar track"},
        {DisplayThemeKeys::ScrollbarThumb, "Scrollbar thumb"},
        {DisplayThemeKeys::ScrollbarThumbHover, "Scrollbar hover thumb"},
        {DisplayThemeKeys::ScrollbarBorder, "Scrollbar border"},
        {DisplayThemeKeys::SliderTrack, "Slider track"},
        {DisplayThemeKeys::SliderFilledTrack, "Slider filled track"},
        {DisplayThemeKeys::SliderHandle, "Slider handle"},
        {DisplayThemeKeys::SliderHandleHover, "Slider hover handle"},
        {DisplayThemeKeys::SliderHandleBorder, "Slider handle border"},
        {DisplayThemeKeys::FullscreenOverlayBackground, "Fullscreen overlay background"},
        {DisplayThemeKeys::FullscreenOverlayText, "Fullscreen overlay text"},
        {DisplayThemeKeys::GuideBackground, "Guide background"},
        {DisplayThemeKeys::GuideText, "Guide text"},
        {DisplayThemeKeys::GuideSecondaryText, "Guide secondary text"},
        {DisplayThemeKeys::GuideEpisodeText, "Guide episode text"},
        {DisplayThemeKeys::GuideBorder, "Guide border"},
        {DisplayThemeKeys::GuideTabBackground, "Guide tab background"},
        {DisplayThemeKeys::GuideTabSelectedBackground, "Guide selected tab"},
        {DisplayThemeKeys::GuideTabText, "Guide tab text"},
        {DisplayThemeKeys::GuideButtonBackground, "Guide button background"},
        {DisplayThemeKeys::GuideButtonText, "Guide button text"},
        {DisplayThemeKeys::GuideButtonBorder, "Guide button border"},
        {DisplayThemeKeys::GuideGridLine, "Guide grid line"},
        {DisplayThemeKeys::GuideEntryBackground, "Guide entry background"},
        {DisplayThemeKeys::GuideCurrentEntryBackground, "Guide current entry background"},
        {DisplayThemeKeys::GuideEntryBorder, "Guide entry border"},
        {DisplayThemeKeys::GuideNowLine, "Guide current-time line"},
        {DisplayThemeKeys::GuideActionBackground, "Guide action background"},
        {DisplayThemeKeys::GuideActionBorder, "Guide action border"},
        {DisplayThemeKeys::GuideActionText, "Guide action text"},
        {DisplayThemeKeys::GuideActionFavoriteText, "Guide favorite action text"},
        {DisplayThemeKeys::GuideEmptyText, "Guide empty-state text"},
    };
    return specs;
}

const QList<DisplayFontRoleSpec> &displayFontSpecsStorage()
{
    static const QList<DisplayFontRoleSpec> specs = {
        {DisplayThemeKeys::AppFont, "App font"},
        {DisplayThemeKeys::LabelFont, "Labels"},
        {DisplayThemeKeys::ButtonFont, "Buttons / checkboxes"},
        {DisplayThemeKeys::InputFont, "Inputs"},
        {DisplayThemeKeys::TabFont, "Tabs"},
        {DisplayThemeKeys::MenuFont, "Menus"},
        {DisplayThemeKeys::StatusFont, "Status bar"},
        {DisplayThemeKeys::LogFont, "Logs"},
        {DisplayThemeKeys::GuideFont, "Guide entries"},
        {DisplayThemeKeys::GuideHeaderFont, "Guide header"},
        {DisplayThemeKeys::GuideChannelFont, "Guide channels"},
        {DisplayThemeKeys::GuideSearchFont, "Guide search"},
        {DisplayThemeKeys::OverlayFont, "Fullscreen overlay"},
    };
    return specs;
}

QString colorToString(const QColor &color)
{
    return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
}

QColor colorFromJsonValue(const QJsonValue &value)
{
    if (!value.isString()) {
        return {};
    }
    const QColor color(value.toString());
    return color;
}

DisplayFontStyle displayFontStyleFromFont(const QFont &font)
{
    DisplayFontStyle style;
    style.family = font.family();
    style.pointSize = font.pointSize() > 0 ? font.pointSize() : 10;
    style.bold = font.bold();
    style.italic = font.italic();
    style.underline = font.underline();
    return style;
}

QJsonObject fontStyleToJson(const DisplayFontStyle &style)
{
    QJsonObject object;
    object.insert("family", style.family);
    object.insert("pointSize", style.pointSize);
    object.insert("bold", style.bold);
    object.insert("italic", style.italic);
    object.insert("underline", style.underline);
    return object;
}

DisplayFontStyle fontStyleFromJson(const QJsonObject &object)
{
    DisplayFontStyle style;
    style.family = object.value("family").toString().trimmed();
    style.pointSize = object.value("pointSize").toInt(10);
    style.bold = object.value("bold").toBool(false);
    style.italic = object.value("italic").toBool(false);
    style.underline = object.value("underline").toBool(false);
    return style;
}

DisplayTheme themeFromJson(const QJsonObject &object)
{
    DisplayTheme theme;
    theme.name = object.value("name").toString().trimmed();

    const QJsonObject colorsObject = object.value("colors").toObject();
    for (auto it = colorsObject.begin(); it != colorsObject.end(); ++it) {
        const QColor color = colorFromJsonValue(it.value());
        if (color.isValid()) {
            theme.colors.insert(it.key(), color);
        }
    }

    const QJsonObject fontsObject = object.value("fonts").toObject();
    for (auto it = fontsObject.begin(); it != fontsObject.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        theme.fonts.insert(it.key(), fontStyleFromJson(it.value().toObject()));
    }

    return theme;
}

QJsonObject themeToJson(const DisplayTheme &theme)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    QJsonObject object;
    object.insert("name", normalized.name);

    QJsonObject colorsObject;
    for (const DisplayColorRoleSpec &spec : displayColorSpecsStorage()) {
        colorsObject.insert(spec.key, colorToString(normalized.colors.value(spec.key)));
    }
    object.insert("colors", colorsObject);

    QJsonObject fontsObject;
    for (const DisplayFontRoleSpec &spec : displayFontSpecsStorage()) {
        fontsObject.insert(spec.key, fontStyleToJson(normalized.fonts.value(spec.key)));
    }
    object.insert("fonts", fontsObject);
    return object;
}

DisplayTheme buildDefaultTheme()
{
    const QFont baseFont = qApp != nullptr ? qApp->font() : QFont();
    DisplayTheme theme;
    theme.name = "Default";
    theme.colors = {
        {DisplayThemeKeys::WindowBackground, QColor("#000000")},
        {DisplayThemeKeys::WindowText, QColor("#ffffff")},
        {DisplayThemeKeys::GroupBorder, QColor("#262626")},
        {DisplayThemeKeys::InputBackground, QColor("#050505")},
        {DisplayThemeKeys::InputText, QColor("#ffffff")},
        {DisplayThemeKeys::InputBorder, QColor("#343434")},
        {DisplayThemeKeys::ButtonBackground, QColor("#090909")},
        {DisplayThemeKeys::ButtonText, QColor("#ffffff")},
        {DisplayThemeKeys::ButtonBorder, QColor("#343434")},
        {DisplayThemeKeys::ButtonDisabledText, QColor("#777777")},
        {DisplayThemeKeys::ButtonDisabledBorder, QColor("#1e1e1e")},
        {DisplayThemeKeys::CheckBoxIndicatorBackground, QColor("#050505")},
        {DisplayThemeKeys::CheckBoxIndicatorBorder, QColor("#7a0000")},
        {DisplayThemeKeys::CheckBoxIndicatorChecked, QColor("#ff3030")},
        {DisplayThemeKeys::LabelText, QColor("#ffffff")},
        {DisplayThemeKeys::MutedText, QColor("#d4d4d4")},
        {DisplayThemeKeys::TabBackground, QColor("#050505")},
        {DisplayThemeKeys::TabSelectedBackground, QColor("#101010")},
        {DisplayThemeKeys::TabText, QColor("#ffffff")},
        {DisplayThemeKeys::TabBorder, QColor("#343434")},
        {DisplayThemeKeys::MenuBackground, QColor("#000000")},
        {DisplayThemeKeys::MenuText, QColor("#ffffff")},
        {DisplayThemeKeys::StatusBackground, QColor("#000000")},
        {DisplayThemeKeys::StatusText, QColor("#ffffff")},
        {DisplayThemeKeys::HeaderBackground, QColor("#000000")},
        {DisplayThemeKeys::HeaderText, QColor("#ffffff")},
        {DisplayThemeKeys::HeaderBorder, QColor("#343434")},
        {DisplayThemeKeys::Highlight, QColor("#4a0000")},
        {DisplayThemeKeys::HighlightText, QColor("#ffffff")},
        {DisplayThemeKeys::Accent, QColor("#ff3030")},
        {DisplayThemeKeys::ScrollbarTrack, QColor("#000000")},
        {DisplayThemeKeys::ScrollbarThumb, QColor("#ff3030")},
        {DisplayThemeKeys::ScrollbarThumbHover, QColor("#ff5252")},
        {DisplayThemeKeys::ScrollbarBorder, QColor("#1a1a1a")},
        {DisplayThemeKeys::SliderTrack, QColor("#171717")},
        {DisplayThemeKeys::SliderFilledTrack, QColor("#ff3030")},
        {DisplayThemeKeys::SliderHandle, QColor("#ff3030")},
        {DisplayThemeKeys::SliderHandleHover, QColor("#ff5252")},
        {DisplayThemeKeys::SliderHandleBorder, QColor("#7a0000")},
        {DisplayThemeKeys::FullscreenOverlayBackground, QColor("#b36e6e6e")},
        {DisplayThemeKeys::FullscreenOverlayText, QColor("#f3f3f3")},
        {DisplayThemeKeys::GuideBackground, QColor("#000000")},
        {DisplayThemeKeys::GuideText, QColor("#ffffff")},
        {DisplayThemeKeys::GuideSecondaryText, QColor("#d4d4d4")},
        {DisplayThemeKeys::GuideEpisodeText, QColor("#f1d27a")},
        {DisplayThemeKeys::GuideBorder, QColor("#4a4a4a")},
        {DisplayThemeKeys::GuideTabBackground, QColor("#0a0a0a")},
        {DisplayThemeKeys::GuideTabSelectedBackground, QColor("#111111")},
        {DisplayThemeKeys::GuideTabText, QColor("#ffffff")},
        {DisplayThemeKeys::GuideButtonBackground, QColor("#0f0f0f")},
        {DisplayThemeKeys::GuideButtonText, QColor("#ffffff")},
        {DisplayThemeKeys::GuideButtonBorder, QColor("#444444")},
        {DisplayThemeKeys::GuideGridLine, QColor("#343434")},
        {DisplayThemeKeys::GuideEntryBackground, QColor("#101010")},
        {DisplayThemeKeys::GuideCurrentEntryBackground, QColor("#1c1c1c")},
        {DisplayThemeKeys::GuideEntryBorder, QColor("#787878")},
        {DisplayThemeKeys::GuideNowLine, QColor("#ff6060")},
        {DisplayThemeKeys::GuideActionBackground, QColor("#3a0c0c")},
        {DisplayThemeKeys::GuideActionBorder, QColor("#ff6060")},
        {DisplayThemeKeys::GuideActionText, QColor("#ffffff")},
        {DisplayThemeKeys::GuideActionFavoriteText, QColor("#60ff60")},
        {DisplayThemeKeys::GuideEmptyText, QColor("#a0a0a0")},
    };

    const DisplayFontStyle appFont = displayFontStyleFromFont(baseFont);
    DisplayFontStyle guideHeaderFont = appFont;
    guideHeaderFont.bold = true;
    DisplayFontStyle logFont = appFont;
    DisplayFontStyle overlayFont = appFont;

    theme.fonts = {
        {DisplayThemeKeys::AppFont, appFont},
        {DisplayThemeKeys::LabelFont, appFont},
        {DisplayThemeKeys::ButtonFont, appFont},
        {DisplayThemeKeys::InputFont, appFont},
        {DisplayThemeKeys::TabFont, appFont},
        {DisplayThemeKeys::MenuFont, appFont},
        {DisplayThemeKeys::StatusFont, appFont},
        {DisplayThemeKeys::LogFont, logFont},
        {DisplayThemeKeys::GuideFont, appFont},
        {DisplayThemeKeys::GuideHeaderFont, guideHeaderFont},
        {DisplayThemeKeys::GuideChannelFont, appFont},
        {DisplayThemeKeys::GuideSearchFont, appFont},
        {DisplayThemeKeys::OverlayFont, overlayFont},
    };
    return theme;
}

DisplayFontStyle normalizedFontStyle(const DisplayFontStyle &candidate, const DisplayFontStyle &fallback)
{
    DisplayFontStyle style = fallback;
    if (!candidate.family.trimmed().isEmpty()) {
        style.family = candidate.family.trimmed();
    }
    if (candidate.pointSize > 0) {
        style.pointSize = candidate.pointSize;
    }
    style.bold = candidate.bold;
    style.italic = candidate.italic;
    style.underline = candidate.underline;
    return style;
}

} // namespace

QList<DisplayColorRoleSpec> displayColorRoleSpecs()
{
    return displayColorSpecsStorage();
}

QList<DisplayFontRoleSpec> displayFontRoleSpecs()
{
    return displayFontSpecsStorage();
}

DisplayTheme defaultDisplayTheme()
{
    return buildDefaultTheme();
}

DisplayTheme normalizedDisplayTheme(const DisplayTheme &theme)
{
    DisplayTheme normalized = buildDefaultTheme();
    if (!theme.name.trimmed().isEmpty()) {
        normalized.name = theme.name.trimmed();
    }

    for (const DisplayColorRoleSpec &spec : displayColorSpecsStorage()) {
        const QColor color = theme.colors.value(spec.key);
        if (color.isValid()) {
            normalized.colors.insert(spec.key, color);
        }
    }

    for (const DisplayFontRoleSpec &spec : displayFontSpecsStorage()) {
        normalized.fonts.insert(spec.key,
                                normalizedFontStyle(theme.fonts.value(spec.key),
                                                    normalized.fonts.value(spec.key)));
    }

    return normalized;
}

DisplayThemeStore defaultDisplayThemeStore()
{
    DisplayThemeStore store;
    store.currentTheme = defaultDisplayTheme();
    return store;
}

DisplayThemeStore normalizedDisplayThemeStore(const DisplayThemeStore &store)
{
    DisplayThemeStore normalized;
    normalized.currentTheme = normalizedDisplayTheme(store.currentTheme);

    QHash<QString, int> indexesByName;
    for (const DisplayTheme &candidate : store.savedThemes) {
        const DisplayTheme theme = normalizedDisplayTheme(candidate);
        const QString name = theme.name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const QString normalizedName = name.toCaseFolded();
        if (indexesByName.contains(normalizedName)) {
            normalized.savedThemes[indexesByName.value(normalizedName)] = theme;
            continue;
        }
        indexesByName.insert(normalizedName, normalized.savedThemes.size());
        normalized.savedThemes.append(theme);
    }

    return normalized;
}

QString resolveDisplayThemeStorePath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.trimmed().isEmpty()) {
        return QDir::current().filePath("display_themes.json");
    }
    return QDir(appDataPath).filePath("display_themes.json");
}

bool loadDisplayThemeStore(DisplayThemeStore *store, QString *errorText)
{
    if (store == nullptr) {
        if (errorText != nullptr) {
            *errorText = "Display theme store pointer was null.";
        }
        return false;
    }

    const QString path = resolveDisplayThemeStorePath();
    QFile file(path);
    if (!file.exists()) {
        *store = defaultDisplayThemeStore();
        if (errorText != nullptr) {
            errorText->clear();
        }
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *store = defaultDisplayThemeStore();
        if (errorText != nullptr) {
            *errorText = QString("Could not open display theme file %1: %2").arg(path, file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *store = defaultDisplayThemeStore();
        if (errorText != nullptr) {
            *errorText = QString("Could not parse display theme file %1: %2")
                             .arg(path, parseError.errorString());
        }
        return false;
    }

    DisplayThemeStore parsed;
    const QJsonObject root = document.object();
    if (root.contains("currentTheme") && root.value("currentTheme").isObject()) {
        parsed.currentTheme = themeFromJson(root.value("currentTheme").toObject());
    }

    const QJsonArray savedThemes = root.value("savedThemes").toArray();
    for (const QJsonValue &value : savedThemes) {
        if (!value.isObject()) {
            continue;
        }
        parsed.savedThemes.append(themeFromJson(value.toObject()));
    }

    *store = normalizedDisplayThemeStore(parsed);
    if (errorText != nullptr) {
        errorText->clear();
    }
    return true;
}

bool saveDisplayThemeStore(const DisplayThemeStore &store, QString *errorText)
{
    const QString path = resolveDisplayThemeStorePath();
    QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorText != nullptr) {
            *errorText = QString("Could not create display theme folder %1").arg(dir.absolutePath());
        }
        return false;
    }

    const DisplayThemeStore normalized = normalizedDisplayThemeStore(store);
    QJsonObject root;
    root.insert("version", 1);
    root.insert("currentTheme", themeToJson(normalized.currentTheme));

    QJsonArray savedThemes;
    for (const DisplayTheme &theme : normalized.savedThemes) {
        savedThemes.append(themeToJson(theme));
    }
    root.insert("savedThemes", savedThemes);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorText != nullptr) {
            *errorText = QString("Could not open display theme file %1 for writing: %2")
                             .arg(path, file.errorString());
        }
        return false;
    }

    const QJsonDocument document(root);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorText != nullptr) {
            *errorText = QString("Could not save display theme file %1: %2").arg(path, file.errorString());
        }
        return false;
    }

    if (errorText != nullptr) {
        errorText->clear();
    }
    return true;
}

QColor displayThemeColor(const DisplayTheme &theme, const QString &key)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    return normalized.colors.value(key);
}

void setDisplayThemeColor(DisplayTheme *theme, const QString &key, const QColor &color)
{
    if (theme == nullptr || !color.isValid()) {
        return;
    }
    theme->colors.insert(key, color);
}

DisplayFontStyle displayThemeFontStyle(const DisplayTheme &theme, const QString &key)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    return normalized.fonts.value(key);
}

void setDisplayThemeFontStyle(DisplayTheme *theme, const QString &key, const DisplayFontStyle &style)
{
    if (theme == nullptr) {
        return;
    }
    theme->fonts.insert(key, style);
}

QFont qFontFromDisplayFontStyle(const DisplayFontStyle &style, const QFont &fallback)
{
    QFont font = fallback;
    if (!style.family.trimmed().isEmpty()) {
        font.setFamily(style.family.trimmed());
    }
    if (style.pointSize > 0) {
        font.setPointSize(style.pointSize);
    }
    font.setBold(style.bold);
    font.setItalic(style.italic);
    font.setUnderline(style.underline);
    return font;
}

QString styleSheetFontFragment(const DisplayFontStyle &style)
{
    const QFont font = qFontFromDisplayFontStyle(style);
    QStringList declarations;
    if (!font.family().trimmed().isEmpty()) {
        QString family = font.family().trimmed();
        family.replace('\'', "\\'");
        declarations.append(QString("font-family: '%1';").arg(family));
    }
    if (font.pointSize() > 0) {
        declarations.append(QString("font-size: %1pt;").arg(font.pointSize()));
    }
    declarations.append(QString("font-weight: %1;").arg(font.bold() ? 700 : 400));
    declarations.append(QString("font-style: %1;").arg(font.italic() ? "italic" : "normal"));
    declarations.append(QString("text-decoration: %1;").arg(font.underline() ? "underline" : "none"));
    return declarations.join(' ');
}

QPalette buildApplicationPalette(const DisplayTheme &theme, const QPalette &fallback)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    QPalette palette = fallback;
    palette.setColor(QPalette::Window, normalized.colors.value(DisplayThemeKeys::WindowBackground));
    palette.setColor(QPalette::WindowText, normalized.colors.value(DisplayThemeKeys::WindowText));
    palette.setColor(QPalette::Base, normalized.colors.value(DisplayThemeKeys::InputBackground));
    palette.setColor(QPalette::AlternateBase, normalized.colors.value(DisplayThemeKeys::ButtonBackground));
    palette.setColor(QPalette::ToolTipBase, normalized.colors.value(DisplayThemeKeys::WindowBackground));
    palette.setColor(QPalette::ToolTipText, normalized.colors.value(DisplayThemeKeys::WindowText));
    palette.setColor(QPalette::Text, normalized.colors.value(DisplayThemeKeys::InputText));
    palette.setColor(QPalette::Button, normalized.colors.value(DisplayThemeKeys::ButtonBackground));
    palette.setColor(QPalette::ButtonText, normalized.colors.value(DisplayThemeKeys::ButtonText));
    palette.setColor(QPalette::BrightText, normalized.colors.value(DisplayThemeKeys::Accent));
    palette.setColor(QPalette::Highlight, normalized.colors.value(DisplayThemeKeys::Highlight));
    palette.setColor(QPalette::HighlightedText, normalized.colors.value(DisplayThemeKeys::HighlightText));
    palette.setColor(QPalette::PlaceholderText, normalized.colors.value(DisplayThemeKeys::MutedText));
    palette.setColor(QPalette::Disabled,
                     QPalette::ButtonText,
                     normalized.colors.value(DisplayThemeKeys::ButtonDisabledText));
    palette.setColor(QPalette::Disabled, QPalette::Text, normalized.colors.value(DisplayThemeKeys::MutedText));
    return palette;
}

QString buildScrollBarStyleSheet(const DisplayTheme &theme)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    const QString track = colorToString(normalized.colors.value(DisplayThemeKeys::ScrollbarTrack));
    const QString thumb = colorToString(normalized.colors.value(DisplayThemeKeys::ScrollbarThumb));
    const QString thumbHover = colorToString(normalized.colors.value(DisplayThemeKeys::ScrollbarThumbHover));
    const QString border = colorToString(normalized.colors.value(DisplayThemeKeys::ScrollbarBorder));

    return QString(
               "QScrollBar:vertical {"
               " background: %1;"
               " width: 16px;"
               " margin: 0px;"
               " border: 1px solid %2;"
               "}"
               "QScrollBar::handle:vertical {"
               " background: %3;"
               " min-height: 28px;"
               " border: 1px solid %2;"
               " border-radius: 7px;"
               "}"
               "QScrollBar::handle:vertical:hover {"
               " background: %4;"
               "}"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
               " background: %1;"
               " border: none;"
               " height: 0px;"
               "}"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
               " background: %1;"
               "}"
               "QScrollBar:horizontal {"
               " background: %1;"
               " height: 16px;"
               " margin: 0px;"
               " border: 1px solid %2;"
               "}"
               "QScrollBar::handle:horizontal {"
               " background: %3;"
               " min-width: 28px;"
               " border: 1px solid %2;"
               " border-radius: 7px;"
               "}"
               "QScrollBar::handle:horizontal:hover {"
               " background: %4;"
               "}"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
               " background: %1;"
               " border: none;"
               " width: 0px;"
               "}"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
               " background: %1;"
               "}")
        .arg(track, border, thumb, thumbHover);
}

QString buildSliderStyleSheet(const DisplayTheme &theme)
{
    const DisplayTheme normalized = normalizedDisplayTheme(theme);
    const QString track = colorToString(normalized.colors.value(DisplayThemeKeys::SliderTrack));
    const QString filled = colorToString(normalized.colors.value(DisplayThemeKeys::SliderFilledTrack));
    const QString handle = colorToString(normalized.colors.value(DisplayThemeKeys::SliderHandle));
    const QString handleHover = colorToString(normalized.colors.value(DisplayThemeKeys::SliderHandleHover));
    const QString handleBorder = colorToString(normalized.colors.value(DisplayThemeKeys::SliderHandleBorder));

    return QString(
               "QSlider::groove:horizontal {"
               " border: 1px solid %1;"
               " height: 8px;"
               " background: %2;"
               " border-radius: 4px;"
               "}"
               "QSlider::sub-page:horizontal {"
               " background: %3;"
               " border: 1px solid %1;"
               " height: 8px;"
               " border-radius: 4px;"
               "}"
               "QSlider::add-page:horizontal {"
               " background: %2;"
               " border: 1px solid %1;"
               " height: 8px;"
               " border-radius: 4px;"
               "}"
               "QSlider::handle:horizontal {"
               " background: %4;"
               " border: 1px solid %5;"
               " width: 18px;"
               " margin: -6px 0;"
               " border-radius: 9px;"
               "}"
               "QSlider::handle:horizontal:hover {"
               " background: %6;"
               "}"
               "QSlider::groove:vertical {"
               " border: 1px solid %1;"
               " width: 8px;"
               " background: %2;"
               " border-radius: 4px;"
               "}"
               "QSlider::sub-page:vertical {"
               " background: %3;"
               " border: 1px solid %1;"
               " width: 8px;"
               " border-radius: 4px;"
               "}"
               "QSlider::add-page:vertical {"
               " background: %2;"
               " border: 1px solid %1;"
               " width: 8px;"
               " border-radius: 4px;"
               "}"
               "QSlider::handle:vertical {"
               " background: %4;"
               " border: 1px solid %5;"
               " height: 18px;"
               " margin: 0 -6px;"
               " border-radius: 9px;"
               "}"
               "QSlider::handle:vertical:hover {"
               " background: %6;"
               "}")
        .arg(handleBorder, track, filled, handle, handleBorder, handleHover);
}
