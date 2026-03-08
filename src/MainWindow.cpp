#include "MainWindow.h"
#include "TvGuideDialog.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSettings>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>
#include <QScreen>
#include <QWindow>
#include <QCursor>
#include <QCryptographicHash>
#include <QSet>
#include <linux/dvb/dmx.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>
#include <limits>

namespace {
constexpr auto kChannelSidebarSplitterStateSetting = "watch/channel_sidebar_splitter_state";
constexpr auto kLastPlayedChannelSetting = "watch/last_played_channel";
constexpr auto kLastPlayedChannelLineSetting = "watch/last_played_channel_line";
constexpr auto kObeyScheduledSwitchesSetting = "tvGuide/obeyScheduledSwitches";
constexpr auto kHideNoEitChannelsSetting = "tvGuide/hideChannelsWithoutEit";
constexpr auto kShowFavoritesOnlySetting = "tvGuide/showOnlyFavorites";
constexpr auto kAutoFavoriteShowSchedulingSetting = "tvGuide/autoFavoriteShowScheduling";
constexpr auto kFavoriteShowRulesSetting = "tvGuide/favoriteShowRules";
constexpr auto kFavoriteShowRatingsSetting = "tvGuide/favoriteShowRatings";
constexpr auto kFavoriteShowRatingsOverrideSetting = "tvGuide/favoriteShowRatingsOverride";
constexpr auto kDismissedAutoFavoriteCandidatesSetting = "tvGuide/dismissedAutoFavoriteCandidates";
constexpr auto kDismissedAutoFavoriteCandidatesStampSetting = "tvGuide/dismissedAutoFavoriteCandidatesStamp";
constexpr auto kLockedAutoFavoriteSelectionsSetting = "tvGuide/lockedAutoFavoriteSelections";
constexpr auto kTestingBugItemsSetting = "testing/bugItems";
constexpr auto kAutoPictureInPictureSetting = "video/autoPictureInPicture";
constexpr auto kHideStartupSwitchSummarySetting = "tvGuide/hideStartupSwitchSummary";
constexpr auto kGuideRefreshIntervalMinutesSetting = "tvGuide/refreshIntervalMinutes";
constexpr auto kGuideCacheRetentionHoursSetting = "tvGuide/cacheRetentionHours";
constexpr auto kLockedScheduledSwitchesSetting = "tvGuide/lockedScheduledSwitches";
constexpr auto kUseSchedulesDirectGuideSetting = "tvGuide/useSchedulesDirect";
constexpr auto kSchedulesDirectUsernameSetting = "schedulesDirect/username";
constexpr auto kSchedulesDirectPasswordSha1Setting = "schedulesDirect/passwordSha1";
constexpr auto kSchedulesDirectPostalCodeSetting = "schedulesDirect/postalCode";
constexpr int kDefaultGuideRefreshIntervalMinutes = 60;
constexpr int kDefaultGuideCacheRetentionHours = 24;
constexpr int kDefaultFavoriteShowRating = 1;
constexpr int kMaxFavoriteShowRating = 5;

int guideCacheRetentionHoursValue(const QSpinBox *spinBox = nullptr)
{
    const int configuredHours = spinBox != nullptr
                                    ? spinBox->value()
                                    : QSettings("tv_tuner_gui", "watcher")
                                          .value(kGuideCacheRetentionHoursSetting,
                                                 kDefaultGuideCacheRetentionHours)
                                          .toInt();
    return std::max(0, configuredHours);
}

QString quoteArg(const QString &arg)
{
    QString escaped = arg;
    escaped.replace('\'', "'\\''");
    return "'" + escaped + "'";
}

QString formatCommandLine(const QString &program, const QStringList &arguments)
{
    QStringList parts;
    parts << quoteArg(program);
    for (const QString &arg : arguments) {
        parts << quoteArg(arg);
    }
    return parts.join(' ');
}

QString processErrorToString(QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart:
        return "FailedToStart";
    case QProcess::Crashed:
        return "Crashed";
    case QProcess::Timedout:
        return "Timedout";
    case QProcess::ReadError:
        return "ReadError";
    case QProcess::WriteError:
        return "WriteError";
    case QProcess::UnknownError:
    default:
        return "UnknownError";
    }
}

struct SignalMonitorReading {
    QString lockStatus;
    QString quality;
    QString signalValue;
    QString carrierToNoise;
    QString rawLine;

    bool isValid() const
    {
        return !signalValue.isEmpty() || !quality.isEmpty() || !lockStatus.isEmpty();
    }
};

QString captureSignalMonitorField(const QString &line, const QString &fieldName)
{
    static const QStringList fieldNames = {
        "Quality=",
        "Signal=",
        "C/N=",
        "SNR=",
        "UCB=",
        "postBER=",
        "preBER=",
        "BER=",
        "PER=",
        "UNC="
    };

    const int fieldIndex = line.indexOf(fieldName);
    if (fieldIndex < 0) {
        return {};
    }

    int valueStart = fieldIndex + fieldName.size();
    while (valueStart < line.size() && line.at(valueStart).isSpace()) {
        ++valueStart;
    }

    int valueEnd = line.size();
    for (const QString &candidate : fieldNames) {
        if (candidate == fieldName) {
            continue;
        }
        const int candidateIndex = line.indexOf(candidate, valueStart);
        if (candidateIndex >= 0 && candidateIndex < valueEnd) {
            valueEnd = candidateIndex;
        }
    }

    return line.mid(valueStart, valueEnd - valueStart).trimmed();
}

bool parseSignalValueNumber(const QString &text, double &value, QString &unit)
{
    static const QRegularExpression valueRegex(R"(^\s*(-?\d+(?:\.\d+)?)\s*([%[:alpha:]]+)?\s*$)");
    const QRegularExpressionMatch match = valueRegex.match(text.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    bool ok = false;
    const double parsedValue = match.captured(1).toDouble(&ok);
    if (!ok) {
        return false;
    }

    value = parsedValue;
    unit = match.captured(2).trimmed();
    return true;
}

QString signalCategoryFromPercent(double percent)
{
    if (percent >= 85.0) {
        return "Excellent";
    }
    if (percent >= 70.0) {
        return "Good";
    }
    if (percent >= 55.0) {
        return "Ok";
    }
    if (percent >= 40.0) {
        return "Mild";
    }
    return "Poor";
}

QString signalCategoryFromDbm(double dbm)
{
    if (dbm >= -50.0) {
        return "Excellent";
    }
    if (dbm >= -60.0) {
        return "Good";
    }
    if (dbm >= -70.0) {
        return "Ok";
    }
    if (dbm >= -80.0) {
        return "Mild";
    }
    return "Poor";
}

QString signalCategoryLabel(const SignalMonitorReading &reading)
{
    if (reading.lockStatus.contains("No lock", Qt::CaseInsensitive)) {
        return "Poor";
    }

    double numericValue = 0.0;
    QString unit;
    if (parseSignalValueNumber(reading.signalValue, numericValue, unit)) {
        if (unit == "%") {
            return signalCategoryFromPercent(numericValue);
        }
        if (unit.compare("dBm", Qt::CaseInsensitive) == 0) {
            return signalCategoryFromDbm(numericValue);
        }
    }

    const QString normalizedQuality = reading.quality.trimmed().toCaseFolded();
    if (normalizedQuality.contains("excellent")) {
        return "Excellent";
    }
    if (normalizedQuality.contains("good")) {
        return "Good";
    }
    if (normalizedQuality == "ok" || normalizedQuality.contains("okay")) {
        return "Ok";
    }
    if (normalizedQuality.contains("fair") || normalizedQuality.contains("medium")) {
        return "Mild";
    }
    if (normalizedQuality.contains("poor") || normalizedQuality.contains("bad") || normalizedQuality.contains("weak")) {
        return "Poor";
    }

    if (!reading.lockStatus.trimmed().isEmpty()) {
        return "Ok";
    }

    return {};
}

SignalMonitorReading parseSignalMonitorLine(const QString &line)
{
    const QString trimmedLine = line.trimmed();
    SignalMonitorReading reading;
    reading.rawLine = trimmedLine;
    if (trimmedLine.isEmpty()) {
        return reading;
    }

    const int parenIndex = trimmedLine.indexOf('(');
    if (parenIndex > 0) {
        reading.lockStatus = trimmedLine.left(parenIndex).trimmed();
    } else if (trimmedLine.startsWith("Lock", Qt::CaseInsensitive)
               || trimmedLine.startsWith("No lock", Qt::CaseInsensitive)) {
        const int qualityIndex = trimmedLine.indexOf("Quality=");
        reading.lockStatus = (qualityIndex > 0 ? trimmedLine.left(qualityIndex) : trimmedLine).trimmed();
    }

    reading.quality = captureSignalMonitorField(trimmedLine, "Quality=");
    reading.signalValue = captureSignalMonitorField(trimmedLine, "Signal=");
    reading.carrierToNoise = captureSignalMonitorField(trimmedLine, "C/N=");
    if (reading.carrierToNoise.isEmpty()) {
        reading.carrierToNoise = captureSignalMonitorField(trimmedLine, "SNR=");
    }

    return reading;
}

QString signalMonitorDisplayText(const SignalMonitorReading &reading)
{
    const QString category = signalCategoryLabel(reading);
    if (category.isEmpty()) {
        return {};
    }

    return "Signal: " + category;
}

QString signalMonitorToolTip(const SignalMonitorReading &reading, const QString &monitorDescription)
{
    QStringList lines;
    if (!monitorDescription.trimmed().isEmpty()) {
        lines << monitorDescription.trimmed();
    }
    if (!reading.signalValue.isEmpty()) {
        lines << QString("Signal: %1").arg(reading.signalValue);
    }
    if (!reading.quality.isEmpty()) {
        lines << QString("Quality: %1").arg(reading.quality);
    }
    if (!reading.lockStatus.isEmpty()) {
        lines << QString("Lock: %1").arg(reading.lockStatus);
    }
    if (!reading.carrierToNoise.isEmpty()) {
        lines << QString("C/N: %1").arg(reading.carrierToNoise);
    }
    if (!reading.rawLine.isEmpty()) {
        lines << QString("Raw: %1").arg(reading.rawLine);
    }
    return lines.join('\n');
}

QString normalizeZapLine(const QString &line)
{
    const QStringList parts = line.split(':');
    if (parts.size() < 3) {
        return line;
    }

    QStringList normalizedParts = parts;
    const QString modulation = parts[2].trimmed().toUpper();
    if (modulation == "VSB_8") {
        normalizedParts[2] = "8VSB";
    } else if (modulation == "VSB_16") {
        normalizedParts[2] = "16VSB";
    }

    return normalizedParts.join(':');
}

QString normalizeChannelNumberHint(const QString &channelNumber)
{
    QString normalized = channelNumber.trimmed();
    if (normalized.isEmpty()) {
        return {};
    }
    normalized.replace('-', ':');
    normalized.replace('.', ':');
    return normalized;
}

QString displayChannelNumber(const QString &channelNumber)
{
    QString display = normalizeChannelNumberHint(channelNumber);
    if (display.isEmpty()) {
        return {};
    }
    display.replace(':', '-');
    return display;
}

QString channelNameFromZapLine(const QString &line)
{
    const QString normalizedLine = normalizeZapLine(line).trimmed();
    if (normalizedLine.isEmpty()) {
        return {};
    }

    const QStringList parts = normalizedLine.split(':');
    if (parts.size() < 6) {
        return {};
    }

    return parts.at(0).trimmed();
}

QString programIdFromZapLine(const QString &line)
{
    const QString normalizedLine = normalizeZapLine(line).trimmed();
    if (normalizedLine.isEmpty()) {
        return {};
    }

    const QStringList parts = normalizedLine.split(':');
    if (parts.size() < 6) {
        return {};
    }

    return parts.at(5).trimmed();
}

QString channelNumberHintForParts(const QStringList &parts,
                                  const QHash<QString, QString> *numberByTuneKey = nullptr)
{
    if (parts.size() < 6) {
        return {};
    }

    const QString storedHint =
        numberByTuneKey != nullptr
            ? normalizeChannelNumberHint(numberByTuneKey->value(parts.at(1).trimmed() + "|" + parts.at(5).trimmed()))
            : QString();
    if (!storedHint.isEmpty()) {
        return storedHint;
    }

    bool frequencyOk = false;
    bool serviceOk = false;
    const qint64 frequencyHz = parts.at(1).trimmed().toLongLong(&frequencyOk);
    const int serviceId = parts.at(5).trimmed().toInt(&serviceOk, 0);
    if (!frequencyOk || !serviceOk || frequencyHz <= 0 || serviceId < 1001 || serviceId >= 2000) {
        return {};
    }

    const int mhz = static_cast<int>(std::llround(static_cast<double>(frequencyHz) / 1000000.0));
    int rfChannel = -1;
    if (mhz >= 57 && mhz <= 81 && ((mhz - 57) % 6) == 0) {
        rfChannel = 2 + ((mhz - 57) / 6);
    } else if (mhz >= 177 && mhz <= 213 && ((mhz - 177) % 6) == 0) {
        rfChannel = 7 + ((mhz - 177) / 6);
    } else if (mhz >= 473 && mhz <= 695 && ((mhz - 473) % 6) == 0) {
        rfChannel = 14 + ((mhz - 473) / 6);
    }
    if (rfChannel <= 0) {
        return {};
    }

    return QString("%1:%2").arg(rfChannel).arg(serviceId - 1000);
}

QString channelDisplayLabel(const QString &baseName, const QString &channelNumberHint)
{
    const QString trimmedBaseName = baseName.trimmed();
    const QString displayNumber = displayChannelNumber(channelNumberHint);
    if (trimmedBaseName.isEmpty() || displayNumber.isEmpty()) {
        return trimmedBaseName;
    }
    return QString("%1 %2").arg(displayNumber, trimmedBaseName);
}

QString channelDisplayLabelForParts(const QStringList &parts,
                                    const QHash<QString, QString> *numberByTuneKey = nullptr)
{
    if (parts.size() < 6) {
        return {};
    }
    return channelDisplayLabel(parts.at(0).trimmed(), channelNumberHintForParts(parts, numberByTuneKey));
}

QString channelDisplayLabelForLine(const QString &line,
                                   const QHash<QString, QString> *numberByTuneKey = nullptr)
{
    const QString normalizedLine = normalizeZapLine(line).trimmed();
    if (normalizedLine.isEmpty()) {
        return {};
    }
    return channelDisplayLabelForParts(normalizedLine.split(':'), numberByTuneKey);
}

QString resolveProjectLogPath()
{
    const QString envPath = qEnvironmentVariable("TV_TUNER_GUI_LOG_PATH");
    if (!envPath.isEmpty()) {
        return envPath;
    }

    QDir sourceDir(QStringLiteral(TV_TUNER_GUI_SOURCE_DIR));
    if (sourceDir.exists()) {
        return sourceDir.filePath("tv_tuner_gui.log");
    }

    QDir cwdDir(QDir::currentPath());
    if (cwdDir.dirName() == "build") {
        cwdDir.cdUp();
    }
    return cwdDir.filePath("tv_tuner_gui.log");
}

QString resolveGuideCachePath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("guide_cache.json");
}

QString resolveGuideCacheBetaPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("guide_cache_beta.json");
}

QString resolveGuideSchedulePath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("guide_scheduled_switches.json");
}

QString resolveSchedulesDirectExportPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("schedules_direct.org.json");
}

QString resolveLegacySchedulesDirectExportPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("schedules_direct.org");
}

QString resolveChannelHintsJsonPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("channel_hints.json");
}

QString resolveActiveTuneChannelPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    return QDir(appDataPath).filePath("active_tune_channel.conf");
}

QString normalizeFavoriteShowRule(const QString &title)
{
    return title.simplified().toCaseFolded();
}

QString formatRatedShowTitle(const QString &title, const QHash<QString, int> &favoriteShowRatings)
{
    const QString trimmedTitle = title.simplified();
    if (trimmedTitle.isEmpty()) {
        return QString();
    }

    const QString normalizedTitle = normalizeFavoriteShowRule(trimmedTitle);
    if (!favoriteShowRatings.contains(normalizedTitle)) {
        return QString("%1 (rating: N/A)").arg(trimmedTitle);
    }

    const int rating = std::clamp(favoriteShowRatings.value(normalizedTitle, kDefaultFavoriteShowRating),
                                  kDefaultFavoriteShowRating,
                                  kMaxFavoriteShowRating);
    return QString("%1 (rating: %2)").arg(trimmedTitle).arg(rating);
}

struct GuideEntryDisplayParts {
    QString title;
    QString episodeTitle;
    QString synopsisBody;
};

GuideEntryDisplayParts displayPartsForGuideEntry(const TvGuideEntry &entry)
{
    GuideEntryDisplayParts parts;
    parts.title = entry.title.trimmed();
    parts.episodeTitle = entry.episode.trimmed();
    parts.synopsisBody = entry.synopsis.trimmed();

    if (!parts.episodeTitle.isEmpty()) {
        return parts;
    }

    const QString rawSynopsis = entry.synopsis.trimmed();
    if (!rawSynopsis.contains('\n')) {
        return parts;
    }

    const QStringList rawLines = rawSynopsis.split('\n');
    QStringList lines;
    for (const QString &line : rawLines) {
        const QString trimmedLine = line.trimmed();
        if (!trimmedLine.isEmpty()) {
            lines.append(trimmedLine);
        }
    }

    if (lines.isEmpty()) {
        parts.synopsisBody.clear();
        return parts;
    }

    parts.episodeTitle = lines.takeFirst();
    parts.synopsisBody = lines.join('\n').trimmed();
    return parts;
}

bool looksLikeSha1Hex(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{40}$"));
    return pattern.match(text.trimmed()).hasMatch();
}

QString currentGuideCacheStamp(const QString &generatedUtc,
                               const QDateTime &windowStartUtc,
                               int slotMinutes,
                               int slotCount)
{
    QString cacheStamp = generatedUtc.trimmed();
    if (cacheStamp.isEmpty()) {
        cacheStamp = QString("%1|%2|%3")
                         .arg(windowStartUtc.toString(Qt::ISODateWithMs))
                         .arg(slotMinutes)
                         .arg(slotCount);
    }
    return cacheStamp;
}

struct JsonRequestResult {
    QJsonDocument document;
    QByteArray body;
    QString errorText;
    int httpStatus{0};
    bool timedOut{false};

    bool ok() const
    {
        return errorText.trimmed().isEmpty() && !timedOut && httpStatus >= 200 && httpStatus < 300;
    }
};

JsonRequestResult performJsonRequest(QNetworkAccessManager &networkManager,
                                     const QString &userAgent,
                                     const QUrl &url,
                                     const QString &token = QString(),
                                     const QJsonDocument &requestBody = QJsonDocument(),
                                     const QByteArray &method = QByteArray(),
                                     int timeoutMs = 15000)
{
    JsonRequestResult result;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", userAgent.toUtf8());
    if (!token.trimmed().isEmpty()) {
        request.setRawHeader("token", token.trimmed().toUtf8());
    }

    const QByteArray body = requestBody.isNull() ? QByteArray() : requestBody.toJson(QJsonDocument::Compact);
    const QByteArray effectiveMethod = method.trimmed().toUpper();
    QNetworkReply *reply = nullptr;
    if (effectiveMethod.isEmpty()) {
        reply = requestBody.isNull()
                    ? networkManager.get(request)
                    : networkManager.post(request, body);
    } else if (effectiveMethod == "GET") {
        reply = networkManager.get(request);
    } else if (effectiveMethod == "POST") {
        reply = networkManager.post(request, body);
    } else {
        reply = networkManager.sendCustomRequest(request, effectiveMethod, body);
    }
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        result.timedOut = true;
        result.errorText = QString("Request timed out after %1 ms").arg(timeoutMs);
        reply->abort();
        loop.quit();
    });

    timeoutTimer.start(timeoutMs);
    loop.exec();
    timeoutTimer.stop();

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    result.httpStatus = statusCode.isValid() ? statusCode.toInt() : 0;
    result.body = reply->readAll();

    QJsonParseError parseError{};
    result.document = QJsonDocument::fromJson(result.body, &parseError);
    if (!result.body.trimmed().isEmpty() && parseError.error != QJsonParseError::NoError) {
        result.errorText = QString("Invalid JSON response: %1").arg(parseError.errorString());
    }

    if (!result.timedOut && reply->error() != QNetworkReply::NoError) {
        QString serverMessage;
        if (result.document.isObject()) {
            const QJsonObject object = result.document.object();
            const QString message = object.value("message").toString().trimmed();
            const QString response = object.value("response").toString().trimmed();
            if (!message.isEmpty() && !response.isEmpty()) {
                serverMessage = QString("%1 (%2)").arg(message, response);
            } else {
                serverMessage = !message.isEmpty() ? message : response;
            }
        }

        result.errorText = serverMessage.isEmpty()
                               ? reply->errorString()
                               : QString("%1: %2").arg(reply->errorString(), serverMessage);
    } else if (!result.timedOut && result.errorText.trimmed().isEmpty() && (result.httpStatus < 200 || result.httpStatus >= 300)) {
        result.errorText = QString("Unexpected HTTP status %1").arg(result.httpStatus);
    }

    reply->deleteLater();
    return result;
}

QString firstProgramTitle(const QJsonObject &program)
{
    const QJsonArray titles = program.value("titles").toArray();
    for (const QJsonValue &titleValue : titles) {
        const QJsonObject titleObject = titleValue.toObject();
        const QString title = titleObject.value("title120").toString().trimmed();
        if (!title.isEmpty()) {
            return title;
        }
        const QString shortTitle = titleObject.value("title40").toString().trimmed();
        if (!shortTitle.isEmpty()) {
            return shortTitle;
        }
    }
    return {};
}

QString bestProgramDescription(const QJsonObject &program)
{
    const QJsonObject descriptions = program.value("descriptions").toObject();
    const QStringList preferredBuckets = {QStringLiteral("description1000"),
                                          QStringLiteral("description100"),
                                          QStringLiteral("description1000Lang"),
                                          QStringLiteral("description100Lang")};
    for (const QString &bucket : preferredBuckets) {
        const QJsonArray values = descriptions.value(bucket).toArray();
        for (const QJsonValue &value : values) {
            const QString description = value.toObject().value("description").toString().trimmed();
            if (!description.isEmpty()) {
                return description;
            }
        }
    }
    return {};
}

bool guideEntryMatchesScheduledSwitch(const QString &channelName,
                                      const TvGuideEntry &entry,
                                      const TvGuideScheduledSwitch &scheduledSwitch)
{
    return scheduledSwitch.channelName.trimmed() == channelName.trimmed()
           && scheduledSwitch.startUtc == entry.startUtc
           && scheduledSwitch.endUtc == entry.endUtc
           && scheduledSwitch.title.trimmed() == entry.title.trimmed();
}

bool scheduledSwitchesMatch(const TvGuideScheduledSwitch &left, const TvGuideScheduledSwitch &right)
{
    return left.channelName.trimmed() == right.channelName.trimmed()
           && left.startUtc == right.startUtc
           && left.endUtc == right.endUtc
           && left.title.trimmed() == right.title.trimmed();
}

bool scheduledSwitchListContains(const QList<TvGuideScheduledSwitch> &switches,
                                 const TvGuideScheduledSwitch &candidate)
{
    for (const TvGuideScheduledSwitch &scheduledSwitch : switches) {
        if (scheduledSwitchesMatch(scheduledSwitch, candidate)) {
            return true;
        }
    }
    return false;
}

bool scheduledSwitchesOverlap(const TvGuideScheduledSwitch &left, const TvGuideScheduledSwitch &right)
{
    return left.startUtc < right.endUtc && left.endUtc > right.startUtc;
}

QString autoFavoriteDismissalKey(const QString &cacheStamp, const TvGuideScheduledSwitch &scheduledSwitch)
{
    Q_UNUSED(cacheStamp);

    return QString("%1||%2||%3||%4")
        .arg(scheduledSwitch.channelName.trimmed(),
             QString::number(scheduledSwitch.startUtc.toMSecsSinceEpoch()),
             QString::number(scheduledSwitch.endUtc.toMSecsSinceEpoch()),
             normalizeFavoriteShowRule(scheduledSwitch.title));
}

bool isAutoFavoriteLockedIn(const QStringList &lockedSelections,
                            const QString &cacheStamp,
                            const TvGuideScheduledSwitch &scheduledSwitch)
{
    const QString selectionKey = autoFavoriteDismissalKey(cacheStamp, scheduledSwitch);
    if (selectionKey.isEmpty()) {
        return false;
    }
    return lockedSelections.contains(selectionKey);
}

bool isAutoFavoriteDismissed(const QStringList &dismissedCandidates,
                             const QString &cacheStamp,
                             const TvGuideScheduledSwitch &scheduledSwitch)
{
    const QString dismissalKey = autoFavoriteDismissalKey(cacheStamp, scheduledSwitch);
    if (dismissalKey.isEmpty()) {
        return false;
    }
    return dismissedCandidates.contains(dismissalKey);
}

void setAutoFavoriteDismissed(QStringList &dismissedCandidates,
                              const QString &cacheStamp,
                              const TvGuideScheduledSwitch &scheduledSwitch,
                              bool dismissed)
{
    const QString dismissalKey = autoFavoriteDismissalKey(cacheStamp, scheduledSwitch);
    if (dismissalKey.isEmpty()) {
        return;
    }

    dismissedCandidates.removeAll(dismissalKey);
    if (dismissed) {
        dismissedCandidates.append(dismissalKey);
    }
}

void setAutoFavoriteLockedIn(QStringList &lockedSelections,
                             const QString &cacheStamp,
                             const TvGuideScheduledSwitch &scheduledSwitch,
                             bool lockedIn)
{
    const QString selectionKey = autoFavoriteDismissalKey(cacheStamp, scheduledSwitch);
    if (selectionKey.isEmpty()) {
        return;
    }

    lockedSelections.removeAll(selectionKey);
    if (lockedIn) {
        lockedSelections.append(selectionKey);
    }
}

QStringList loadPersistedAutoFavoriteDecisionList(const QString &cacheStamp, const QString &settingKey)
{
    const QString trimmedStamp = cacheStamp.trimmed();
    if (trimmedStamp.isEmpty()) {
        return {};
    }

    QSettings settings("tv_tuner_gui", "watcher");
    const QString storedStamp = settings.value(kDismissedAutoFavoriteCandidatesStampSetting).toString().trimmed();
    if (storedStamp.isEmpty() || storedStamp != trimmedStamp) {
        return {};
    }

    QStringList values = settings.value(settingKey).toStringList();
    values.removeAll(QString());
    values.removeDuplicates();
    return values;
}

void savePersistedAutoFavoriteConflictState(const QString &cacheStamp,
                                            const QStringList &dismissedCandidates,
                                            const QStringList &lockedSelections)
{
    QSettings settings("tv_tuner_gui", "watcher");
    if (dismissedCandidates.isEmpty() && lockedSelections.isEmpty()) {
        settings.remove(kDismissedAutoFavoriteCandidatesSetting);
        settings.remove(kLockedAutoFavoriteSelectionsSetting);
        settings.remove(kDismissedAutoFavoriteCandidatesStampSetting);
        return;
    }

    QStringList dedupedDismissed = dismissedCandidates;
    dedupedDismissed.removeAll(QString());
    dedupedDismissed.removeDuplicates();
    QStringList dedupedLocked = lockedSelections;
    dedupedLocked.removeAll(QString());
    dedupedLocked.removeDuplicates();

    const QString trimmedStamp = cacheStamp.trimmed();
    if (trimmedStamp.isEmpty()) {
        settings.remove(kDismissedAutoFavoriteCandidatesStampSetting);
    } else {
        settings.setValue(kDismissedAutoFavoriteCandidatesStampSetting, trimmedStamp);
    }
    settings.setValue(kDismissedAutoFavoriteCandidatesSetting, dedupedDismissed);
    settings.setValue(kLockedAutoFavoriteSelectionsSetting, dedupedLocked);
}

QString scheduledSwitchLabel(const TvGuideScheduledSwitch &scheduledSwitch)
{
    const QString title = scheduledSwitch.title.trimmed().isEmpty()
                              ? scheduledSwitch.channelName.trimmed()
                              : scheduledSwitch.title.trimmed();
    return QString("%1 on %2 at %3")
        .arg(title,
             scheduledSwitch.channelName.trimmed(),
             scheduledSwitch.startUtc.toLocalTime().toString("ddd h:mm AP"));
}

QString scheduledSwitchListLabel(const TvGuideScheduledSwitch &scheduledSwitch)
{
    const QString title = scheduledSwitch.title.trimmed().isEmpty()
                              ? scheduledSwitch.channelName.trimmed()
                              : scheduledSwitch.title.trimmed();
    return QString("%1 | %2 | %3 - %4")
        .arg(title,
             scheduledSwitch.channelName.trimmed(),
             scheduledSwitch.startUtc.toLocalTime().toString("ddd h:mm AP"),
             scheduledSwitch.endUtc.toLocalTime().toString("h:mm AP"));
}

QString scheduledSwitchManagementLabel(const TvGuideScheduledSwitch &scheduledSwitch, bool lockedIn)
{
    QString label = scheduledSwitchListLabel(scheduledSwitch);
    if (lockedIn) {
        label += " | locked in";
    }
    return label;
}

QString scheduledSwitchDebugLabel(const TvGuideScheduledSwitch &scheduledSwitch)
{
    const QString title = scheduledSwitch.title.trimmed().isEmpty()
                              ? scheduledSwitch.channelName.trimmed()
                              : scheduledSwitch.title.trimmed();
    return QString("%1 | %2 | local %3 - %4 | utc %5 - %6")
        .arg(title,
             scheduledSwitch.channelName.trimmed(),
             scheduledSwitch.startUtc.toLocalTime().toString("ddd h:mm:ss AP"),
             scheduledSwitch.endUtc.toLocalTime().toString("h:mm:ss AP"),
             scheduledSwitch.startUtc.toString(Qt::ISODateWithMs),
             scheduledSwitch.endUtc.toString(Qt::ISODateWithMs));
}

QString summarizeScheduledSwitchesDebug(const QList<TvGuideScheduledSwitch> &switches, int maxItems = 12)
{
    if (switches.isEmpty()) {
        return "none";
    }

    QStringList parts;
    const int switchCount = static_cast<int>(switches.size());
    const int limit = maxItems < 0 ? switchCount : std::min(maxItems, switchCount);
    for (int index = 0; index < limit; ++index) {
        parts.append(scheduledSwitchDebugLabel(switches.at(index)));
    }
    if (switches.size() > limit) {
        parts.append(QString("... +%1 more").arg(switches.size() - limit));
    }
    return parts.join(" || ");
}

QString lockedScheduledSwitchKey(const TvGuideScheduledSwitch &scheduledSwitch)
{
    return QString("%1||%2||%3||%4")
        .arg(scheduledSwitch.channelName.trimmed(),
             QString::number(scheduledSwitch.startUtc.toMSecsSinceEpoch()),
             QString::number(scheduledSwitch.endUtc.toMSecsSinceEpoch()),
             normalizeFavoriteShowRule(scheduledSwitch.title));
}

struct ScheduledSwitchChoiceOption {
    TvGuideScheduledSwitch scheduledSwitch;
    bool isExisting{false};
    int existingIndex{-1};
};

QString scheduledSwitchChoiceDebugLabel(const ScheduledSwitchChoiceOption &choice)
{
    return QString("%1 | %2 | existingIndex=%3")
        .arg(scheduledSwitchDebugLabel(choice.scheduledSwitch),
             choice.isExisting ? "existing" : "candidate",
             QString::number(choice.existingIndex));
}

QJsonObject scheduledSwitchToJsonObject(const TvGuideScheduledSwitch &scheduledSwitch)
{
    QJsonObject object;
    object.insert("channelName", scheduledSwitch.channelName);
    object.insert("startUtc", scheduledSwitch.startUtc.toString(Qt::ISODateWithMs));
    object.insert("endUtc", scheduledSwitch.endUtc.toString(Qt::ISODateWithMs));
    object.insert("title", scheduledSwitch.title);
    if (!scheduledSwitch.episode.trimmed().isEmpty()) {
        object.insert("episode", scheduledSwitch.episode.trimmed());
    }
    object.insert("synopsis", scheduledSwitch.synopsis);
    return object;
}

QList<TvGuideScheduledSwitch> scheduledSwitchesFromJsonArray(const QJsonArray &array)
{
    QList<TvGuideScheduledSwitch> scheduledSwitches;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        TvGuideScheduledSwitch scheduledSwitch;
        scheduledSwitch.channelName = object.value("channelName").toString().trimmed();
        scheduledSwitch.startUtc = QDateTime::fromString(object.value("startUtc").toString(), Qt::ISODateWithMs);
        scheduledSwitch.endUtc = QDateTime::fromString(object.value("endUtc").toString(), Qt::ISODateWithMs);
        scheduledSwitch.title = object.value("title").toString().trimmed();
        scheduledSwitch.episode = object.value("episode").toString().trimmed();
        scheduledSwitch.synopsis = object.value("synopsis").toString().trimmed();

        if (scheduledSwitch.channelName.isEmpty()
            || !scheduledSwitch.startUtc.isValid()
            || !scheduledSwitch.endUtc.isValid()
            || scheduledSwitch.endUtc <= scheduledSwitch.startUtc) {
            continue;
        }

        bool duplicate = false;
        for (const TvGuideScheduledSwitch &existing : scheduledSwitches) {
            if (scheduledSwitchesMatch(existing, scheduledSwitch)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            scheduledSwitches.append(scheduledSwitch);
        }
    }

    std::sort(scheduledSwitches.begin(), scheduledSwitches.end(), [](const TvGuideScheduledSwitch &left,
                                                                     const TvGuideScheduledSwitch &right) {
        if (left.startUtc == right.startUtc) {
            return left.channelName < right.channelName;
        }
        return left.startUtc < right.startUtc;
    });
    return scheduledSwitches;
}

QScreen *screenForWidget(QWidget *widget)
{
    if (widget == nullptr) {
        return nullptr;
    }

    if (QScreen *screen = widget->screen()) {
        return screen;
    }

    const QPoint center = widget->mapToGlobal(widget->rect().center());
    if (QScreen *screen = QGuiApplication::screenAt(center)) {
        return screen;
    }

    if (QWidget *window = widget->window()) {
        return window->screen();
    }

    return nullptr;
}

QString describeScreen(const QScreen *screen)
{
    if (screen == nullptr) {
        return QStringLiteral("null");
    }

    const QRect geometry = screen->geometry();
    const QRect availableGeometry = screen->availableGeometry();
    const QString name = screen->name().trimmed().isEmpty() ? QStringLiteral("unnamed") : screen->name().trimmed();
    return QStringLiteral("%1 geom=%2,%3 %4x%5 avail=%6,%7 %8x%9")
        .arg(name)
        .arg(geometry.x())
        .arg(geometry.y())
        .arg(geometry.width())
        .arg(geometry.height())
        .arg(availableGeometry.x())
        .arg(availableGeometry.y())
        .arg(availableGeometry.width())
        .arg(availableGeometry.height());
}

QString eventTypeName(QEvent::Type type)
{
    switch (type) {
    case QEvent::Enter:
        return QStringLiteral("Enter");
    case QEvent::MouseMove:
        return QStringLiteral("MouseMove");
    case QEvent::MouseButtonPress:
        return QStringLiteral("MouseButtonPress");
    case QEvent::MouseButtonRelease:
        return QStringLiteral("MouseButtonRelease");
    case QEvent::MouseButtonDblClick:
        return QStringLiteral("MouseButtonDblClick");
    case QEvent::Wheel:
        return QStringLiteral("Wheel");
    case QEvent::HoverMove:
        return QStringLiteral("HoverMove");
    case QEvent::KeyPress:
        return QStringLiteral("KeyPress");
    default:
        return QStringLiteral("Type(%1)").arg(static_cast<int>(type));
    }
}

QString describeObjectForLog(QObject *object)
{
    if (object == nullptr) {
        return QStringLiteral("null");
    }

    const QString className = QString::fromLatin1(object->metaObject()->className());
    const QString objectName = object->objectName().trimmed();
    return objectName.isEmpty() ? className : QStringLiteral("%1(%2)").arg(className, objectName);
}

qint64 rectArea(const QRect &rect)
{
    if (rect.isEmpty()) {
        return 0;
    }

    return static_cast<qint64>(rect.width()) * static_cast<qint64>(rect.height());
}

QScreen *screenForGlobalRect(const QRect &globalRect)
{
    if (globalRect.isEmpty()) {
        return nullptr;
    }

    QScreen *bestScreen = nullptr;
    qint64 bestOverlapArea = -1;
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const qint64 overlapArea = rectArea(globalRect.intersected(screen->geometry()));
        if (overlapArea > bestOverlapArea) {
            bestOverlapArea = overlapArea;
            bestScreen = screen;
        }
    }

    if (bestScreen != nullptr && bestOverlapArea > 0) {
        return bestScreen;
    }

    return QGuiApplication::screenAt(globalRect.center());
}

QString tuneKey(const QString &frequency, const QString &program)
{
    const QString trimmedFrequency = frequency.trimmed();
    const QString trimmedProgram = program.trimmed();
    if (trimmedFrequency.isEmpty() || trimmedProgram.isEmpty()) {
        return {};
    }
    return trimmedFrequency + "|" + trimmedProgram;
}

QString tuneKeyForParts(const QStringList &parts)
{
    if (parts.size() < 6) {
        return {};
    }
    return tuneKey(parts[1], parts[5]);
}

bool loadChannelHintsFromJson(QHash<QString, QString> &numberByTuneKey,
                              QHash<QString, QString> &programByChannel,
                              QString *errorText = nullptr)
{
    const QString hintsPath = resolveChannelHintsJsonPath();
    if (hintsPath.isEmpty()) {
        if (errorText != nullptr) {
            *errorText = "Could not resolve the channel hints JSON path.";
        }
        return false;
    }

    QFile hintsFile(hintsPath);
    if (!hintsFile.exists()) {
        return false;
    }
    if (!hintsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorText != nullptr) {
            *errorText = QString("Could not open %1.").arg(hintsPath);
        }
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(hintsFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorText != nullptr) {
            *errorText = QString("Could not parse %1 (%2).").arg(hintsPath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject numberObject = root.value("numberByTuneKey").toObject();
    for (auto it = numberObject.begin(); it != numberObject.end(); ++it) {
        const QString key = it.key().trimmed();
        const QString value = it.value().toString().trimmed();
        if (!key.isEmpty() && !value.isEmpty()) {
            numberByTuneKey.insert(key, value);
        }
    }

    const QJsonObject programObject = root.value("programByChannel").toObject();
    for (auto it = programObject.begin(); it != programObject.end(); ++it) {
        const QString key = it.key().trimmed();
        const QString value = it.value().toString().trimmed();
        if (!key.isEmpty() && !value.isEmpty()) {
            programByChannel.insert(key, value);
        }
    }

    return !numberByTuneKey.isEmpty() || !programByChannel.isEmpty();
}

bool saveChannelHintsToJson(const QHash<QString, QString> &numberByTuneKey,
                            const QHash<QString, QString> &programByChannel,
                            QString *errorText = nullptr)
{
    const QString hintsPath = resolveChannelHintsJsonPath();
    if (hintsPath.isEmpty()) {
        if (errorText != nullptr) {
            *errorText = "Could not resolve the channel hints JSON path.";
        }
        return false;
    }

    QFileInfo hintsInfo(hintsPath);
    QDir hintsDir = hintsInfo.dir();
    if (!hintsDir.exists() && !hintsDir.mkpath(".")) {
        if (errorText != nullptr) {
            *errorText = QString("Could not create %1.").arg(hintsDir.path());
        }
        return false;
    }

    QJsonObject numberObject;
    for (auto it = numberByTuneKey.cbegin(); it != numberByTuneKey.cend(); ++it) {
        if (!it.key().trimmed().isEmpty() && !it.value().trimmed().isEmpty()) {
            numberObject.insert(it.key(), it.value());
        }
    }

    QJsonObject programObject;
    for (auto it = programByChannel.cbegin(); it != programByChannel.cend(); ++it) {
        if (!it.key().trimmed().isEmpty() && !it.value().trimmed().isEmpty()) {
            programObject.insert(it.key(), it.value());
        }
    }

    QJsonObject root;
    root.insert("version", 1);
    root.insert("generatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert("numberByTuneKey", numberObject);
    root.insert("programByChannel", programObject);

    QSaveFile hintsFile(hintsPath);
    if (!hintsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText != nullptr) {
            *errorText = QString("Could not open %1 for writing.").arg(hintsPath);
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (hintsFile.write(payload) != payload.size()) {
        hintsFile.cancelWriting();
        if (errorText != nullptr) {
            *errorText = QString("Could not write %1.").arg(hintsPath);
        }
        return false;
    }

    if (!hintsFile.commit()) {
        if (errorText != nullptr) {
            *errorText = QString("Could not finalize %1.").arg(hintsPath);
        }
        return false;
    }

    return true;
}

QStringList normalizedFavorites(const QStringList &favorites, int maxCount = -1)
{
    QStringList normalized;
    for (const QString &favorite : favorites) {
        const QString trimmedFavorite = favorite.trimmed();
        if (trimmedFavorite.isEmpty() || normalized.contains(trimmedFavorite)) {
            continue;
        }
        normalized.append(trimmedFavorite);
        if (maxCount > 0 && normalized.size() >= maxCount) {
            break;
        }
    }
    return normalized;
}

QStringList favoriteCandidatesFromTable(const QTableWidget *channelsTable, const QStringList &favorites)
{
    if (channelsTable == nullptr) {
        return {};
    }

    const QStringList normalizedExistingFavorites = normalizedFavorites(favorites);
    QStringList candidates;
    for (int row = 0; row < channelsTable->rowCount(); ++row) {
        const QTableWidgetItem *item = channelsTable->item(row, 1);
        if (item == nullptr) {
            continue;
        }

        const QString channelName = item->text().trimmed();
        if (channelName.isEmpty()
            || normalizedExistingFavorites.contains(channelName)
            || candidates.contains(channelName)) {
            continue;
        }
        candidates.append(channelName);
    }
    return candidates;
}

int firstAvailableFrontendForAdapter(int adapter, int preferredFrontend)
{
    const auto frontendPath = [adapter](int frontend) {
        return QString("/dev/dvb/adapter%1/frontend%2").arg(adapter).arg(frontend);
    };

    if (preferredFrontend >= 0 && QFileInfo::exists(frontendPath(preferredFrontend))) {
        return preferredFrontend;
    }

    for (int frontend = 0; frontend <= 7; ++frontend) {
        if (QFileInfo::exists(frontendPath(frontend))) {
            return frontend;
        }
    }

    return -1;
}

bool adapterHasGuideDevices(int adapter)
{
    return QFileInfo::exists(QString("/dev/dvb/adapter%1/demux0").arg(adapter))
           && QFileInfo::exists(QString("/dev/dvb/adapter%1/dvr0").arg(adapter));
}

int findPreferredGuideAdapter(int preferredFrontend, int &guideFrontend)
{
    const QList<int> preferredAdapters = {1, 0};
    for (int adapter : preferredAdapters) {
        if (!adapterHasGuideDevices(adapter)) {
            continue;
        }
        const int frontend = firstAvailableFrontendForAdapter(adapter, preferredFrontend);
        if (frontend >= 0) {
            guideFrontend = frontend;
            return adapter;
        }
    }

    for (int adapter = 0; adapter <= 32; ++adapter) {
        if (!adapterHasGuideDevices(adapter)) {
            continue;
        }
        const int frontend = firstAvailableFrontendForAdapter(adapter, preferredFrontend);
        if (frontend < 0) {
            continue;
        }
        guideFrontend = frontend;
        return adapter;
    }

    guideFrontend = -1;
    return -1;
}

int findPreferredSeparateGuideAdapter(int currentAdapter, int preferredFrontend, int &guideFrontend)
{
    const QList<int> preferredAdapters = {1, 0};
    for (int adapter : preferredAdapters) {
        if (adapter == currentAdapter || !adapterHasGuideDevices(adapter)) {
            continue;
        }
        const int frontend = firstAvailableFrontendForAdapter(adapter, preferredFrontend);
        if (frontend < 0) {
            continue;
        }
        guideFrontend = frontend;
        return adapter;
    }

    for (int adapter = 0; adapter <= 32; ++adapter) {
        if (adapter == currentAdapter || !adapterHasGuideDevices(adapter)) {
            continue;
        }
        const int frontend = firstAvailableFrontendForAdapter(adapter, preferredFrontend);
        if (frontend < 0) {
            continue;
        }
        guideFrontend = frontend;
        return adapter;
    }

    guideFrontend = -1;
    return -1;
}

bool findCurrentOrNextGuideEntry(const QList<TvGuideEntry> &entries,
                                 const QDateTime &momentUtc,
                                 TvGuideEntry &entry,
                                 bool &isCurrent)
{
    bool foundCurrent = false;
    bool foundNext = false;
    TvGuideEntry nextEntry;

    for (const TvGuideEntry &candidate : entries) {
        if (!candidate.startUtc.isValid() || !candidate.endUtc.isValid() || candidate.endUtc <= candidate.startUtc) {
            continue;
        }

        if (candidate.startUtc <= momentUtc && candidate.endUtc > momentUtc) {
            if (!foundCurrent || candidate.startUtc > entry.startUtc) {
                entry = candidate;
                foundCurrent = true;
            }
            continue;
        }

        if (candidate.startUtc > momentUtc) {
            if (!foundNext || candidate.startUtc < nextEntry.startUtc) {
                nextEntry = candidate;
                foundNext = true;
            }
        }
    }

    if (foundCurrent) {
        isCurrent = true;
        return true;
    }
    if (foundNext) {
        entry = nextEntry;
        isCurrent = false;
        return true;
    }
    return false;
}

QString guideEntryToolTipText(const TvGuideEntry &entry, const QHash<QString, int> &favoriteShowRatings)
{
    const GuideEntryDisplayParts parts = displayPartsForGuideEntry(entry);
    QString text = QString("%1\n%2 - %3")
        .arg(formatRatedShowTitle(parts.title, favoriteShowRatings),
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

QString summarizeGuideEntries(const QList<TvGuideEntry> &entries, int maxItems = 4)
{
    if (entries.isEmpty()) {
        return "none";
    }

    QStringList parts;
    const int limit = std::min(maxItems, static_cast<int>(entries.size()));
    for (int i = 0; i < limit; ++i) {
        const TvGuideEntry &entry = entries.at(i);
        const GuideEntryDisplayParts display = displayPartsForGuideEntry(entry);
        QString summary = QString("%1 [%2-%3]")
                              .arg(display.title,
                          entry.startUtc.toLocalTime().toString("h:mm AP"),
                          entry.endUtc.toLocalTime().toString("h:mm AP"));
        if (!display.episodeTitle.isEmpty()) {
            summary += QString(" {%1}").arg(display.episodeTitle);
        }
        parts << summary;
    }
    if (entries.size() > limit) {
        parts << QString("... +%1 more").arg(entries.size() - limit);
    }
    return parts.join(" | ");
}

struct GuideChannelInfo {
    QString name;
    QString baseName;
    QString channelNumber;
    qint64 frequencyHz{0};
    int serviceId{-1};
};

struct SchedulesDirectLocalChannelHint {
    QString name;
    qint64 frequencyHz{0};
    int serviceId{-1};
    int rfChannel{-1};
    int virtualMajor{-1};
    int virtualMinor{-1};
};

struct SchedulesDirectChannelPayload {
    QString channelLabel;
    int rfChannel{-1};
    int virtualMajor{-1};
    int virtualMinor{-1};
    QString stationName;
    QString callsign;
    QString affiliate;
    QList<TvGuideEntry> entries;
};

QString normalizeGuideMatchText(const QString &text)
{
    QString normalized = text.toUpper();
    normalized.remove(QRegularExpression("[^A-Z0-9]+"));
    return normalized;
}

int atscRfChannelForFrequencyHz(qint64 frequencyHz)
{
    if (frequencyHz <= 0) {
        return -1;
    }

    const int mhz = static_cast<int>(std::llround(static_cast<double>(frequencyHz) / 1000000.0));
    if (mhz >= 57 && mhz <= 81 && ((mhz - 57) % 6) == 0) {
        return 2 + ((mhz - 57) / 6);
    }
    if (mhz >= 177 && mhz <= 213 && ((mhz - 177) % 6) == 0) {
        return 7 + ((mhz - 177) / 6);
    }
    if (mhz >= 473 && mhz <= 695 && ((mhz - 473) % 6) == 0) {
        return 14 + ((mhz - 473) / 6);
    }
    return -1;
}

bool parseSchedulesDirectChannelLabel(const QString &channelLabel, int &major, int &minor)
{
    major = -1;
    minor = -1;

    const QStringList parts = channelLabel.trimmed().split('.');
    if (parts.size() != 2) {
        return false;
    }

    bool majorOk = false;
    bool minorOk = false;
    const int parsedMajor = parts.at(0).toInt(&majorOk);
    const int parsedMinor = parts.at(1).toInt(&minorOk);
    if (!majorOk || !minorOk || parsedMajor <= 0 || parsedMinor <= 0) {
        return false;
    }

    major = parsedMajor;
    minor = parsedMinor;
    return true;
}

bool parseLocalVirtualChannelHint(const QString &channelNumberHint,
                                  int serviceId,
                                  int &major,
                                  int &minor)
{
    major = -1;
    minor = -1;

    const QString trimmedHint = channelNumberHint.trimmed();
    if (!trimmedHint.isEmpty()) {
        const QStringList parts = normalizeChannelNumberHint(trimmedHint).split(':');
        if (parts.size() >= 2) {
            bool majorOk = false;
            bool minorOk = false;
            const int parsedMajor = parts.at(0).toInt(&majorOk);
            int parsedMinor = parts.at(1).toInt(&minorOk);
            if (majorOk && minorOk && parsedMajor > 0 && parsedMinor > 0) {
                if (parsedMinor >= 1024) {
                    parsedMinor -= 1024;
                }
                if (parsedMinor > 0) {
                    major = parsedMajor;
                    minor = parsedMinor;
                    return true;
                }
            }
        }
    }

    if (serviceId >= 1001 && serviceId < 2000) {
        minor = serviceId - 1000;
        return minor > 0;
    }

    return false;
}

QString schedulesDirectEpisodeForProgram(const QJsonObject &programObject)
{
    return programObject.value("episode").toString().trimmed().isEmpty()
               ? programObject.value("episodeTitle").toString().trimmed()
               : programObject.value("episode").toString().trimmed();
}

QString schedulesDirectSynopsisForProgram(const QJsonObject &programObject)
{
    return programObject.value("description").toString().trimmed();
}

QVector<SchedulesDirectChannelPayload> parseSchedulesDirectExportChannels(const QJsonObject &root)
{
    QVector<SchedulesDirectChannelPayload> channels;
    const QJsonArray lineups = root.value("lineups").toArray();
    for (const QJsonValue &lineupValue : lineups) {
        const QJsonArray lineupChannels = lineupValue.toObject().value("channels").toArray();
        for (const QJsonValue &channelValue : lineupChannels) {
            if (!channelValue.isObject()) {
                continue;
            }

            const QJsonObject channelObject = channelValue.toObject();
            SchedulesDirectChannelPayload channel;
            channel.channelLabel = channelObject.value("channel").toString().trimmed();
            channel.rfChannel = channelObject.value("uhfVhf").toInt(-1);
            parseSchedulesDirectChannelLabel(channel.channelLabel, channel.virtualMajor, channel.virtualMinor);

            const QJsonObject stationObject = channelObject.value("station").toObject();
            channel.stationName = stationObject.value("name").toString().trimmed();
            channel.callsign = stationObject.value("callsign").toString().trimmed();
            channel.affiliate = stationObject.value("affiliate").toString().trimmed();

            const QJsonArray scheduleDays = channelObject.value("schedule").toArray();
            for (const QJsonValue &scheduleDayValue : scheduleDays) {
                const QJsonArray programs = scheduleDayValue.toObject().value("programs").toArray();
                for (const QJsonValue &programValue : programs) {
                    const QJsonObject programObject = programValue.toObject();
                    const QString airDateTime = programObject.value("airDateTime").toString().trimmed();
                    if (airDateTime.isEmpty()) {
                        continue;
                    }

                    QDateTime startUtc = QDateTime::fromString(airDateTime, Qt::ISODate);
                    if (!startUtc.isValid()) {
                        startUtc = QDateTime::fromString(airDateTime, Qt::ISODateWithMs);
                    }
                    if (!startUtc.isValid()) {
                        continue;
                    }
                    startUtc = startUtc.toUTC();

                    const int durationSeconds = std::max(0, programObject.value("duration").toInt());
                    const QString title = programObject.value("title").toString().trimmed();
                    if (title.isEmpty() || durationSeconds <= 0) {
                        continue;
                    }

                    TvGuideEntry entry;
                    entry.startUtc = startUtc;
                    entry.endUtc = startUtc.addSecs(durationSeconds);
                    entry.title = title;
                    entry.episode = schedulesDirectEpisodeForProgram(programObject);
                    entry.synopsis = schedulesDirectSynopsisForProgram(programObject);
                    channel.entries.append(entry);
                }
            }

            channels.append(channel);
        }
    }

    return channels;
}

bool schedulesDirectLocalNameMatches(const QString &localName,
                                     const SchedulesDirectChannelPayload &candidate)
{
    const QString normalizedLocal = normalizeGuideMatchText(localName);
    if (normalizedLocal.isEmpty()) {
        return false;
    }

    const QString normalizedStation = normalizeGuideMatchText(candidate.stationName);
    const QString normalizedCallsign = normalizeGuideMatchText(candidate.callsign);
    const QString normalizedAffiliate = normalizeGuideMatchText(candidate.affiliate);

    return normalizedStation == normalizedLocal
           || normalizedCallsign == normalizedLocal
           || normalizedStation.contains(normalizedLocal)
           || normalizedCallsign.contains(normalizedLocal)
           || normalizedAffiliate == normalizedLocal;
}

struct RawGuideEvent {
    int serviceId{-1};
    int eventId{-1};
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
    QString synopsis;
};

struct RawGuideSection {
    int pid{-1};
    int tableId{-1};
    int tableIdExtension{-1};
    int versionNumber{-1};
    bool currentNextIndicator{false};
    int sectionNumber{-1};
    int lastSectionNumber{-1};
    int declaredLengthBytes{0};
    int repeatCount{1};
    QByteArray bytes;
};

QString atscEventTextKey(int serviceId, int eventId)
{
    if (serviceId <= 0 || eventId < 0) {
        return {};
    }
    return QString("%1|%2").arg(serviceId).arg(eventId);
}

QHash<QString, QList<TvGuideEntry>> mapGuideEntriesForFrequency(const QVector<GuideChannelInfo> &frequencyChannels,
                                                                const QList<RawGuideEvent> &frequencyEvents,
                                                                const QHash<int, int> &atscSourceToProgram,
                                                                int *mappedForFrequency = nullptr,
                                                                QSet<QString> *mappedChannelNamesForMux = nullptr)
{
    QHash<int, QStringList> namesByServiceId;
    for (const GuideChannelInfo &channel : frequencyChannels) {
        namesByServiceId[channel.serviceId].append(channel.name);
    }

    int mappedCount = 0;
    QSet<QString> mappedNames;
    QHash<QString, QList<TvGuideEntry>> mappedEntriesByChannel;
    for (const RawGuideEvent &event : frequencyEvents) {
        int mappedServiceId = event.serviceId;
        if (atscSourceToProgram.contains(mappedServiceId)) {
            mappedServiceId = atscSourceToProgram.value(mappedServiceId);
        }
        const QStringList mappedChannels = namesByServiceId.value(mappedServiceId);
        if (mappedChannels.isEmpty()) {
            continue;
        }

        TvGuideEntry entry;
        entry.startUtc = event.startUtc;
        entry.endUtc = event.endUtc;
        entry.title = event.title;
        entry.synopsis = event.synopsis;
        for (const QString &channelName : mappedChannels) {
            mappedEntriesByChannel[channelName].append(entry);
            mappedNames.insert(channelName);
            ++mappedCount;
        }
    }

    if (mappedForFrequency != nullptr) {
        *mappedForFrequency = mappedCount;
    }
    if (mappedChannelNamesForMux != nullptr) {
        *mappedChannelNamesForMux = mappedNames;
    }
    return mappedEntriesByChannel;
}

QList<TvGuideEntry> cleanGuideEntries(const QList<TvGuideEntry> &entries,
                                      const QDateTime &nowUtc,
                                      int pastRetentionHours,
                                      QDateTime *latestEndUtc = nullptr)
{
    QList<TvGuideEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const TvGuideEntry &a, const TvGuideEntry &b) {
        return a.startUtc < b.startUtc;
    });

    const bool limitPastEntries = pastRetentionHours > 0 && nowUtc.isValid();
    const QDateTime earliestEndUtc = limitPastEntries
                                         ? nowUtc.addSecs(-static_cast<qint64>(pastRetentionHours) * 3600)
                                         : QDateTime();
    QHash<QString, int> dedupeIndexByKey;
    QList<TvGuideEntry> cleaned;
    for (const TvGuideEntry &entry : sorted) {
        if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
            continue;
        }
        if ((limitPastEntries && entry.endUtc < earliestEndUtc)
            || (nowUtc.isValid() && entry.startUtc > nowUtc.addDays(2))) {
            continue;
        }

        const QString dedupeKey = QString("%1|%2|%3")
                                      .arg(entry.startUtc.toSecsSinceEpoch())
                                      .arg(entry.endUtc.toSecsSinceEpoch())
                                      .arg(entry.title);
        if (dedupeIndexByKey.contains(dedupeKey)) {
            TvGuideEntry &existingEntry = cleaned[dedupeIndexByKey.value(dedupeKey)];
            if (existingEntry.episode.trimmed().isEmpty() && !entry.episode.trimmed().isEmpty()) {
                existingEntry.episode = entry.episode.trimmed();
            }
            if (existingEntry.synopsis.trimmed().isEmpty() && !entry.synopsis.trimmed().isEmpty()) {
                existingEntry.synopsis = entry.synopsis.trimmed();
            }
            continue;
        }
        dedupeIndexByKey.insert(dedupeKey, cleaned.size());
        cleaned.append(entry);
        if (latestEndUtc != nullptr && entry.endUtc > *latestEndUtc) {
            *latestEndUtc = entry.endUtc;
        }
    }
    return cleaned;
}

QJsonObject guideEntryToJson(const TvGuideEntry &entry)
{
    QJsonObject object;
    object.insert("startUtc", entry.startUtc.toString(Qt::ISODateWithMs));
    object.insert("endUtc", entry.endUtc.toString(Qt::ISODateWithMs));
    object.insert("title", entry.title);
    if (!entry.episode.trimmed().isEmpty()) {
        object.insert("episode", entry.episode.trimmed());
    }
    if (!entry.synopsis.trimmed().isEmpty()) {
        object.insert("synopsis", entry.synopsis.trimmed());
    }
    return object;
}

QJsonArray guideEntriesToJsonArray(const QList<TvGuideEntry> &entries)
{
    QJsonArray array;
    for (const TvGuideEntry &entry : entries) {
        array.append(guideEntryToJson(entry));
    }
    return array;
}

QList<TvGuideEntry> guideEntriesFromJsonArray(const QJsonArray &array)
{
    QList<TvGuideEntry> entries;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();

        TvGuideEntry entry;
        entry.startUtc = QDateTime::fromString(object.value("startUtc").toString(), Qt::ISODateWithMs);
        entry.endUtc = QDateTime::fromString(object.value("endUtc").toString(), Qt::ISODateWithMs);
        entry.title = object.value("title").toString().trimmed();
        entry.episode = object.value("episode").toString().trimmed();
        entry.synopsis = object.value("synopsis").toString().trimmed();
        if (entry.episode.isEmpty()) {
            const GuideEntryDisplayParts parts = displayPartsForGuideEntry(entry);
            entry.episode = parts.episodeTitle;
            entry.synopsis = parts.synopsisBody;
        }
        if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.title.isEmpty()) {
            continue;
        }
        entries.append(entry);
    }
    return entries;
}

bool pruneGuideCacheFile(const QString &cachePath,
                         const QDateTime &nowUtc,
                         int retentionHours)
{
    if (cachePath.isEmpty()) {
        return false;
    }

    QFile cacheFile(cachePath);
    if (!cacheFile.exists() || !cacheFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(cacheFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject entriesObject = root.value("entriesByChannel").toObject();
    if (entriesObject.isEmpty()) {
        return false;
    }

    bool changed = false;
    QJsonObject trimmedEntriesObject;
    for (auto it = entriesObject.begin(); it != entriesObject.end(); ++it) {
        const QJsonArray originalArray = it.value().toArray();
        const QList<TvGuideEntry> trimmedEntries =
            cleanGuideEntries(guideEntriesFromJsonArray(originalArray), nowUtc, retentionHours);
        const QJsonArray trimmedArray = guideEntriesToJsonArray(trimmedEntries);
        if (trimmedArray != originalArray) {
            changed = true;
        }
        trimmedEntriesObject.insert(it.key(), trimmedArray);
    }

    if (!changed) {
        return false;
    }

    QJsonObject updatedRoot = root;
    updatedRoot.insert("entriesByChannel", trimmedEntriesObject);

    QSaveFile saveFile(cachePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(updatedRoot).toJson(QJsonDocument::Indented);
    if (saveFile.write(payload) != payload.size()) {
        saveFile.cancelWriting();
        return false;
    }
    return saveFile.commit();
}

QDateTime latestGuideEntryEndUtc(const QHash<QString, QList<TvGuideEntry>> &entriesByChannel)
{
    QDateTime latestEndUtc;
    for (auto it = entriesByChannel.cbegin(); it != entriesByChannel.cend(); ++it) {
        for (const TvGuideEntry &entry : it.value()) {
            if (!entry.endUtc.isValid()) {
                continue;
            }
            if (!latestEndUtc.isValid() || entry.endUtc > latestEndUtc) {
                latestEndUtc = entry.endUtc;
            }
        }
    }
    return latestEndUtc;
}

bool guideCacheLooksCurrentForStartup(const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                      const QDateTime &windowStartUtc,
                                      const QDateTime &nowUtc)
{
    if (entriesByChannel.isEmpty() || !windowStartUtc.isValid() || !nowUtc.isValid()) {
        return false;
    }

    const QDateTime latestEndUtc = latestGuideEntryEndUtc(entriesByChannel);
    if (!latestEndUtc.isValid()) {
        return false;
    }

    constexpr qint64 kStartupGuideCoverageSecs = 3 * 60 * 60;
    return windowStartUtc <= nowUtc && latestEndUtc >= nowUtc.addSecs(kStartupGuideCoverageSecs);
}

QString formatHexValue(quint64 value, int width)
{
    return QString("0x%1")
        .arg(QString::number(static_cast<qulonglong>(value), 16).toUpper().rightJustified(width, QChar('0')));
}

QString formatHexBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

QString guideTableName(int tableId)
{
    switch (tableId) {
    case 0xc7:
        return "MGT";
    case 0xc8:
        return "TVCT";
    case 0xc9:
        return "CVCT";
    case 0xcb:
        return "EIT";
    case 0xcc:
        return "ETT";
    case 0xcd:
        return "STT";
    default:
        break;
    }

    if (tableId >= 0x4e && tableId <= 0x6f) {
        return "DVB EIT";
    }
    return {};
}

QString decodeAtscMultipleString(const QByteArray &payload);
quint8 byteAt(const QByteArray &data, int index);

quint32 atscEtmIdFromSection(const QByteArray &section)
{
    if (section.size() < 13 || byteAt(section, 0) != 0xcc) {
        return 0;
    }

    return (static_cast<quint32>(byteAt(section, 9)) << 24)
           | (static_cast<quint32>(byteAt(section, 10)) << 16)
           | (static_cast<quint32>(byteAt(section, 11)) << 8)
           | static_cast<quint32>(byteAt(section, 12));
}

QString decodeAtscExtendedTextMessage(const QByteArray &section)
{
    if (section.size() < 17 || byteAt(section, 0) != 0xcc) {
        return {};
    }

    const int sectionLength = ((byteAt(section, 1) & 0x0f) << 8) | byteAt(section, 2);
    const int payloadEnd = 3 + sectionLength - 4; // Exclude CRC32
    if (payloadEnd <= 13 || payloadEnd > section.size()) {
        return {};
    }

    return decodeAtscMultipleString(section.mid(13, payloadEnd - 13)).trimmed();
}

QJsonObject rawGuideEventToJson(const RawGuideEvent &event, const QHash<int, int> &sourceToProgram)
{
    QJsonObject object;
    object.insert("serviceId", event.serviceId);
    if (event.eventId >= 0) {
        object.insert("eventId", event.eventId);
    }
    object.insert("mappedServiceId", sourceToProgram.value(event.serviceId, event.serviceId));
    object.insert("startUtc", event.startUtc.toString(Qt::ISODateWithMs));
    object.insert("endUtc", event.endUtc.toString(Qt::ISODateWithMs));
    object.insert("title", event.title);
    if (!event.synopsis.trimmed().isEmpty()) {
        object.insert("synopsis", event.synopsis.trimmed());
    }
    return object;
}

RawGuideSection makeRawGuideSection(int pid, const QByteArray &section)
{
    RawGuideSection rawSection;
    rawSection.pid = pid;
    rawSection.bytes = section;

    if (section.size() >= 1) {
        rawSection.tableId = byteAt(section, 0);
    }
    if (section.size() >= 3) {
        rawSection.declaredLengthBytes = 3 + (((byteAt(section, 1) & 0x0f) << 8) | byteAt(section, 2));
    }
    if (section.size() >= 5) {
        rawSection.tableIdExtension = (byteAt(section, 3) << 8) | byteAt(section, 4);
    }
    if (section.size() >= 6) {
        rawSection.versionNumber = (byteAt(section, 5) >> 1) & 0x1f;
        rawSection.currentNextIndicator = (byteAt(section, 5) & 0x01) != 0;
    }
    if (section.size() >= 7) {
        rawSection.sectionNumber = byteAt(section, 6);
    }
    if (section.size() >= 8) {
        rawSection.lastSectionNumber = byteAt(section, 7);
    }

    return rawSection;
}

void appendRawGuideSection(int pid,
                           const QByteArray &section,
                           QList<RawGuideSection> &rawSections,
                           QHash<QByteArray, int> &rawSectionIndexes)
{
    QByteArray key = QByteArray::number(pid);
    key.append(':');
    key.append(section);

    const auto existing = rawSectionIndexes.constFind(key);
    if (existing != rawSectionIndexes.cend()) {
        rawSections[*existing].repeatCount += 1;
        return;
    }

    rawSections.append(makeRawGuideSection(pid, section));
    rawSectionIndexes.insert(key, rawSections.size() - 1);
}

QJsonObject rawGuideSectionToJson(const RawGuideSection &section)
{
    QJsonObject object;
    object.insert("pid", section.pid);
    if (section.pid >= 0) {
        object.insert("pidHex", formatHexValue(section.pid, 4));
    }
    if (section.tableId >= 0) {
        object.insert("tableId", section.tableId);
        object.insert("tableIdHex", formatHexValue(section.tableId, 2));
    }

    const QString tableName = guideTableName(section.tableId);
    if (!tableName.isEmpty()) {
        object.insert("tableName", tableName);
    }
    if (section.tableIdExtension >= 0) {
        object.insert("tableIdExtension", section.tableIdExtension);
        object.insert("tableIdExtensionHex", formatHexValue(section.tableIdExtension, 4));
    }
    if (section.versionNumber >= 0) {
        object.insert("versionNumber", section.versionNumber);
    }
    object.insert("currentNextIndicator", section.currentNextIndicator);
    if (section.sectionNumber >= 0) {
        object.insert("sectionNumber", section.sectionNumber);
    }
    if (section.lastSectionNumber >= 0) {
        object.insert("lastSectionNumber", section.lastSectionNumber);
    }
    if (section.declaredLengthBytes > 0) {
        object.insert("declaredLengthBytes", section.declaredLengthBytes);
    }
    object.insert("capturedLengthBytes", section.bytes.size());
    object.insert("repeatCount", section.repeatCount);
    object.insert("sectionHex", formatHexBytes(section.bytes));
    if (section.bytes.size() >= 4) {
        object.insert("crc32Hex", formatHexBytes(section.bytes.right(4)));
    }

    if (section.tableId == 0xc7 && section.bytes.size() >= 11) {
        const int tablesDefined = (byteAt(section.bytes, 9) << 8) | byteAt(section.bytes, 10);
        object.insert("tablesDefined", tablesDefined);
    } else if ((section.tableId == 0xc8 || section.tableId == 0xc9) && section.bytes.size() >= 10) {
        object.insert("channelCount", byteAt(section.bytes, 9));
    } else if (section.tableId == 0xcb && section.bytes.size() >= 10) {
        object.insert("sourceId", section.tableIdExtension);
        object.insert("eventCount", byteAt(section.bytes, 9));
    } else if (section.tableId == 0xcc && section.bytes.size() >= 11) {
        const quint32 etmId = atscEtmIdFromSection(section.bytes);
        const bool isEventEtm = (etmId & 0x3u) == 0x2u;
        const bool isChannelEtm = (etmId & 0x3u) == 0x0u;
        const QString decodedText = decodeAtscExtendedTextMessage(section.bytes);

        object.insert("etmId", QString::number(etmId));
        object.insert("etmIdHex", formatHexValue(etmId, 8));
        object.insert("sourceId", static_cast<int>((etmId >> 16) & 0xffff));
        if (isEventEtm) {
            object.insert("etmKind", "event");
            object.insert("eventId", static_cast<int>((etmId >> 2) & 0x3fff));
        } else if (isChannelEtm) {
            object.insert("etmKind", "channel");
        } else {
            object.insert("etmKind", "unknown");
        }
        if (!decodedText.isEmpty()) {
            object.insert("decodedText", decodedText);
        }
    } else if (section.tableId >= 0x4e && section.tableId <= 0x6f) {
        object.insert("serviceId", section.tableIdExtension);
    }

    return object;
}

QJsonArray rawGuideSectionsToJsonArray(const QList<RawGuideSection> &rawSections)
{
    QJsonArray array;
    for (const RawGuideSection &rawSection : rawSections) {
        array.append(rawGuideSectionToJson(rawSection));
    }
    return array;
}

QJsonArray rawGuideEventsToJsonArray(const QList<RawGuideEvent> &events, const QHash<int, int> &sourceToProgram)
{
    QJsonArray array;
    for (const RawGuideEvent &event : events) {
        array.append(rawGuideEventToJson(event, sourceToProgram));
    }
    return array;
}

QJsonArray guideChannelsToJsonArray(const QVector<GuideChannelInfo> &channels)
{
    QJsonArray array;
    for (const GuideChannelInfo &channel : channels) {
        QJsonObject object;
        object.insert("name", channel.name);
        object.insert("frequencyHz", QString::number(channel.frequencyHz));
        object.insert("serviceId", channel.serviceId);
        array.append(object);
    }
    return array;
}

QJsonArray integerSetToJsonArray(const QSet<int> &values)
{
    QList<int> sortedValues = values.values();
    std::sort(sortedValues.begin(), sortedValues.end());

    QJsonArray array;
    for (int value : sortedValues) {
        array.append(value);
    }
    return array;
}

QJsonObject integerMapToJsonObject(const QHash<int, int> &values)
{
    QList<int> sortedKeys = values.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());

    QJsonObject object;
    for (int key : sortedKeys) {
        object.insert(QString::number(key), values.value(key));
    }
    return object;
}

QJsonArray stringSetToJsonArray(const QSet<QString> &values)
{
    QStringList sortedValues = values.values();
    std::sort(sortedValues.begin(), sortedValues.end());

    QJsonArray array;
    for (const QString &value : sortedValues) {
        array.append(value);
    }
    return array;
}

QString formatSourceToProgramMap(const QHash<int, int> &sourceToProgram)
{
    if (sourceToProgram.isEmpty()) {
        return "none";
    }

    QList<int> sourceIds = sourceToProgram.keys();
    std::sort(sourceIds.begin(), sourceIds.end());

    QStringList pairs;
    for (int sourceId : sourceIds) {
        pairs << QString("%1->%2").arg(sourceId).arg(sourceToProgram.value(sourceId));
    }
    return pairs.join(", ");
}

QString summarizeRawGuideEvents(const QList<RawGuideEvent> &events,
                                const QHash<int, int> &sourceToProgram,
                                int maxItems = 8)
{
    if (events.isEmpty()) {
        return "none";
    }

    QStringList parts;
    const int limit = std::min(maxItems, static_cast<int>(events.size()));
    for (int i = 0; i < limit; ++i) {
        const RawGuideEvent &event = events.at(i);
        const int mappedServiceId = sourceToProgram.value(event.serviceId, event.serviceId);
        parts << QString("%1->%2 %3 [%4-%5]")
                     .arg(event.serviceId)
                     .arg(mappedServiceId)
                     .arg(event.title,
                          event.startUtc.toLocalTime().toString("h:mm AP"),
                          event.endUtc.toLocalTime().toString("h:mm AP"));
    }
    if (events.size() > limit) {
        parts << QString("... +%1 more").arg(events.size() - limit);
    }
    return parts.join(" | ");
}

struct SectionAssembler {
    QByteArray bytes;
    int expectedLength{-1};
};

struct ParsedGuideData {
    QList<RawGuideEvent> events;
    QHash<int, int> atscSourceToProgram;
    QHash<QString, QString> atscEventSynopsisByKey;
    QSet<int> atscPsipTableIds;
    QList<RawGuideSection> rawSections;
};

struct DemuxSectionReader {
    int pid{-1};
    int fd{-1};
};

QSet<int> mappedProgramIdsFromParsedGuideData(const ParsedGuideData &parsed);
bool hasCoverageForAllExpectedServices(const ParsedGuideData &parsed, const QSet<int> &expectedServiceIds);
bool writeGuideCacheBetaFile(const QStringList &channelOrder,
                             const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                             const QDateTime &windowStartUtc,
                             int slotMinutes,
                             int slotCount,
                             const QString &statusText,
                             const QJsonArray &extractions);
void processGuideSectionForPid(int pid,
                               const QByteArray &section,
                               ParsedGuideData &parsed,
                               QSet<QString> &dedupe,
                               QSet<int> &atscPsipPids,
                               QHash<QByteArray, int> &rawSectionIndexes);

constexpr int kAtscPsipPid = 0x1ffb;
constexpr int kDvbEitPid = 0x0012;
constexpr qint64 kGpsEpochToUnixOffset = 315964800;
constexpr qint64 kDefaultGpsUtcOffset = 18;
constexpr int kTsPacketSize = 188;
constexpr int kGuideCaptureMinMs = 1000;
constexpr int kGuideCaptureMaxMs = 3600;
constexpr int kGuideLookupTotalMaxMs = 4000;
constexpr int kGuideProbeIntervalMs = 350;
constexpr int kGuideCapturePacketCount = 60000;
constexpr int kGuideCachePollIntervalMs = 5000;

quint8 byteAt(const QByteArray &data, int index)
{
    return static_cast<quint8>(data.at(index));
}

QString cleanGuideText(const QString &input)
{
    QString text = input;
    text.replace('\n', ' ');
    text.replace('\r', ' ');
    text.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")));
    return text.simplified();
}

QString decodeAtscMultipleString(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return {};
    }

    int offset = 0;
    const int numberStrings = byteAt(payload, offset++);
    QStringList fragments;

    for (int i = 0; i < numberStrings && offset < payload.size(); ++i) {
        if (offset + 4 > payload.size()) {
            break;
        }
        offset += 3; // ISO-639 language code
        const int segmentCount = byteAt(payload, offset++);
        QString composed;

        for (int segment = 0; segment < segmentCount && offset < payload.size(); ++segment) {
            if (offset + 3 > payload.size()) {
                offset = payload.size();
                break;
            }

            const int compressionType = byteAt(payload, offset++);
            const int mode = byteAt(payload, offset++);
            const int length = byteAt(payload, offset++);
            if (offset + length > payload.size()) {
                offset = payload.size();
                break;
            }

            const QByteArray segmentBytes = payload.mid(offset, length);
            offset += length;
            if (compressionType != 0) {
                continue;
            }

            QString decoded;
            if (mode == 0x00 || mode == 0x3f || mode == 0x3e) {
                decoded = QString::fromLatin1(segmentBytes);
            } else {
                decoded = QString::fromUtf8(segmentBytes);
                if (decoded.contains(QChar::ReplacementCharacter)) {
                    decoded = QString::fromLatin1(segmentBytes);
                }
            }
            composed += decoded;
        }

        composed = cleanGuideText(composed);
        if (!composed.isEmpty()) {
            fragments << composed;
        }
    }

    if (fragments.isEmpty()) {
        return cleanGuideText(QString::fromLatin1(payload));
    }
    return fragments.join(" / ");
}

int bcdToInt(quint8 value)
{
    return ((value >> 4) & 0x0f) * 10 + (value & 0x0f);
}

QDateTime atscGpsToUtc(quint32 gpsSeconds)
{
    const qint64 unixSeconds = static_cast<qint64>(gpsSeconds) + kGpsEpochToUnixOffset - kDefaultGpsUtcOffset;
    return QDateTime::fromSecsSinceEpoch(unixSeconds, QTimeZone::UTC);
}

QDate mjdToDate(int mjd)
{
    const int y = static_cast<int>((mjd - 15078.2) / 365.25);
    const int m = static_cast<int>((mjd - 14956.1 - static_cast<int>(y * 365.25)) / 30.6001);
    const int d = mjd - 14956 - static_cast<int>(y * 365.25) - static_cast<int>(m * 30.6001);
    const int k = (m == 14 || m == 15) ? 1 : 0;
    const int year = y + k + 1900;
    const int month = m - 1 - (k * 12);
    return QDate(year, month, d);
}

QDateTime decodeDvbUtcTime(const QByteArray &eventData)
{
    if (eventData.size() < 5) {
        return {};
    }
    if (byteAt(eventData, 0) == 0xff && byteAt(eventData, 1) == 0xff) {
        return {};
    }

    const int mjd = (byteAt(eventData, 0) << 8) | byteAt(eventData, 1);
    const QDate date = mjdToDate(mjd);
    const QTime time(bcdToInt(byteAt(eventData, 2)),
                     bcdToInt(byteAt(eventData, 3)),
                     bcdToInt(byteAt(eventData, 4)));
    if (!date.isValid() || !time.isValid()) {
        return {};
    }
    return QDateTime(date, time, QTimeZone::UTC);
}

QString decodeDvbText(const QByteArray &data)
{
    if (data.isEmpty()) {
        return {};
    }

    int offset = 0;
    if (byteAt(data, 0) < 0x20) {
        offset = 1;
    }
    const QByteArray payload = data.mid(offset);
    QString text = QString::fromUtf8(payload);
    if (text.contains(QChar::ReplacementCharacter)) {
        text = QString::fromLatin1(payload);
    }
    return cleanGuideText(text);
}

QString formatTableIdSet(const QSet<int> &tableIds)
{
    if (tableIds.isEmpty()) {
        return {};
    }

    QList<int> sorted = tableIds.values();
    std::sort(sorted.begin(), sorted.end());

    QStringList labels;
    for (int id : sorted) {
        labels << QString("0x%1").arg(id, 2, 16, QChar('0')).toUpper();
    }
    return labels.join(", ");
}

void appendAtscSourceMapFromVctSection(const QByteArray &section, QHash<int, int> &sourceToProgram)
{
    if (section.size() < 16) {
        return;
    }
    const quint8 tableId = byteAt(section, 0);
    if (tableId != 0xc8 && tableId != 0xc9) {
        return;
    }

    const int sectionLength = ((byteAt(section, 1) & 0x0f) << 8) | byteAt(section, 2);
    const int payloadEnd = 3 + sectionLength - 4; // Exclude CRC32
    if (payloadEnd <= 10 || payloadEnd > section.size()) {
        return;
    }

    const int numberChannels = byteAt(section, 9);
    int offset = 10;

    for (int i = 0; i < numberChannels; ++i) {
        if (offset + 42 > payloadEnd) {
            break;
        }

        const int programNumber = (byteAt(section, offset + 24) << 8) | byteAt(section, offset + 25);
        const int sourceId = (byteAt(section, offset + 28) << 8) | byteAt(section, offset + 29);
        const int descriptorsLength = ((byteAt(section, offset + 30) & 0x03) << 8) | byteAt(section, offset + 31);
        const int nextOffset = offset + 32 + descriptorsLength;
        if (nextOffset > payloadEnd) {
            break;
        }

        if (sourceId > 0 && programNumber > 0 && !sourceToProgram.contains(sourceId)) {
            sourceToProgram.insert(sourceId, programNumber);
        }
        offset = nextOffset;
    }
}

void appendAtscPsipPidsFromMgtSection(const QByteArray &section, QSet<int> &atscPsipPids)
{
    if (section.size() < 16 || byteAt(section, 0) != 0xc7) {
        return;
    }

    const int sectionLength = ((byteAt(section, 1) & 0x0f) << 8) | byteAt(section, 2);
    const int payloadEnd = 3 + sectionLength - 4; // Exclude CRC32
    if (payloadEnd <= 11 || payloadEnd > section.size()) {
        return;
    }

    const int tablesDefined = (byteAt(section, 9) << 8) | byteAt(section, 10);
    int offset = 11;

    for (int i = 0; i < tablesDefined; ++i) {
        if (offset + 11 > payloadEnd) {
            break;
        }

        const int tablePid = ((byteAt(section, offset + 2) & 0x1f) << 8) | byteAt(section, offset + 3);
        const int descriptorsLength = ((byteAt(section, offset + 9) & 0x0f) << 8) | byteAt(section, offset + 10);
        const int nextOffset = offset + 11 + descriptorsLength;
        if (nextOffset > payloadEnd) {
            break;
        }

        if (tablePid > 0 && tablePid < 8192) {
            atscPsipPids.insert(tablePid);
        }
        offset = nextOffset;
    }
}

void appendAtscEventTextFromSection(const QByteArray &section, QHash<QString, QString> &eventSynopsisByKey)
{
    if (section.size() < 17 || byteAt(section, 0) != 0xcc) {
        return;
    }

    const quint32 etmId = atscEtmIdFromSection(section);
    if ((etmId & 0x3u) != 0x2u) {
        return;
    }

    const int sourceId = static_cast<int>((etmId >> 16) & 0xffff);
    const int eventId = static_cast<int>((etmId >> 2) & 0x3fff);
    const QString key = atscEventTextKey(sourceId, eventId);
    if (key.isEmpty()) {
        return;
    }

    const QString synopsis = decodeAtscExtendedTextMessage(section);
    if (!synopsis.isEmpty()) {
        eventSynopsisByKey.insert(key, synopsis);
    }
}

void appendAtscEventsFromSection(const QByteArray &section, QList<RawGuideEvent> &events, QSet<QString> &dedupe)
{
    if (section.size() < 14 || byteAt(section, 0) != 0xcb) {
        return;
    }

    const int sectionLength = ((byteAt(section, 1) & 0x0f) << 8) | byteAt(section, 2);
    const int payloadEnd = 3 + sectionLength - 4; // Exclude CRC32
    if (payloadEnd <= 10 || payloadEnd > section.size()) {
        return;
    }

    const int sourceId = (byteAt(section, 3) << 8) | byteAt(section, 4);
    const int numberEvents = byteAt(section, 9);
    int offset = 10;

    for (int i = 0; i < numberEvents && offset + 12 <= payloadEnd; ++i) {
        const int eventId = ((byteAt(section, offset) & 0x3f) << 8) | byteAt(section, offset + 1);
        const quint32 startGps = (static_cast<quint32>(byteAt(section, offset + 2)) << 24)
                                 | (static_cast<quint32>(byteAt(section, offset + 3)) << 16)
                                 | (static_cast<quint32>(byteAt(section, offset + 4)) << 8)
                                 | static_cast<quint32>(byteAt(section, offset + 5));
        const int durationSeconds = ((byteAt(section, offset + 6) & 0x0f) << 16)
                                    | (byteAt(section, offset + 7) << 8)
                                    | byteAt(section, offset + 8);
        const int titleLength = byteAt(section, offset + 9);
        offset += 10;

        if (offset + titleLength > payloadEnd) {
            break;
        }
        const QString title = decodeAtscMultipleString(section.mid(offset, titleLength));
        offset += titleLength;

        if (offset + 2 > payloadEnd) {
            break;
        }
        const int descriptorsLength = ((byteAt(section, offset) & 0x0f) << 8) | byteAt(section, offset + 1);
        offset += 2;
        if (offset + descriptorsLength > payloadEnd) {
            break;
        }
        offset += descriptorsLength;

        const QDateTime startUtc = atscGpsToUtc(startGps);
        const QDateTime endUtc = startUtc.addSecs(durationSeconds);
        const QString cleanedTitle = cleanGuideText(title);
        if (!startUtc.isValid() || !endUtc.isValid() || endUtc <= startUtc || cleanedTitle.isEmpty()) {
            continue;
        }

        const QString key = QString("%1|%2|%3")
                                .arg(sourceId)
                                .arg(startUtc.toSecsSinceEpoch())
                                .arg(cleanedTitle);
        if (dedupe.contains(key)) {
            continue;
        }
        dedupe.insert(key);

        RawGuideEvent event;
        event.serviceId = sourceId;
        event.eventId = eventId;
        event.startUtc = startUtc;
        event.endUtc = endUtc;
        event.title = cleanedTitle;
        events.append(event);
    }
}

void appendDvbEventsFromSection(const QByteArray &section, QList<RawGuideEvent> &events, QSet<QString> &dedupe)
{
    if (section.size() < 18) {
        return;
    }

    const quint8 tableId = byteAt(section, 0);
    if (tableId < 0x4e || tableId > 0x6f) {
        return;
    }

    const int sectionLength = ((byteAt(section, 1) & 0x0f) << 8) | byteAt(section, 2);
    const int payloadEnd = 3 + sectionLength - 4; // Exclude CRC32
    if (payloadEnd <= 14 || payloadEnd > section.size()) {
        return;
    }

    const int serviceId = (byteAt(section, 3) << 8) | byteAt(section, 4);
    int offset = 14;

    while (offset + 12 <= payloadEnd) {
        const QDateTime startUtc = decodeDvbUtcTime(section.mid(offset + 2, 5));
        const int durationSeconds = bcdToInt(byteAt(section, offset + 7)) * 3600
                                    + bcdToInt(byteAt(section, offset + 8)) * 60
                                    + bcdToInt(byteAt(section, offset + 9));
        const int descriptorsLength = ((byteAt(section, offset + 10) & 0x0f) << 8) | byteAt(section, offset + 11);
        const int descriptorsStart = offset + 12;
        const int descriptorsEnd = descriptorsStart + descriptorsLength;
        if (descriptorsEnd > payloadEnd) {
            break;
        }

        QString title;
        QString synopsis;
        int descriptorOffset = descriptorsStart;
        while (descriptorOffset + 2 <= descriptorsEnd) {
            const quint8 descriptorTag = byteAt(section, descriptorOffset);
            const int descriptorLength = byteAt(section, descriptorOffset + 1);
            const int descriptorPayloadOffset = descriptorOffset + 2;
            const int descriptorEnd = descriptorPayloadOffset + descriptorLength;
            if (descriptorEnd > descriptorsEnd) {
                break;
            }

            if (descriptorTag == 0x4d && descriptorLength >= 5) {
                const int eventNameLength = byteAt(section, descriptorPayloadOffset + 3);
                const int eventNameOffset = descriptorPayloadOffset + 4;
                if (eventNameOffset + eventNameLength <= descriptorEnd) {
                    title = decodeDvbText(section.mid(eventNameOffset, eventNameLength));
                }
                const int eventTextLengthOffset = eventNameOffset + eventNameLength;
                if (eventTextLengthOffset < descriptorEnd) {
                    const int eventTextLength = byteAt(section, eventTextLengthOffset);
                    const int eventTextOffset = eventTextLengthOffset + 1;
                    if (eventTextOffset + eventTextLength <= descriptorEnd) {
                        synopsis = decodeDvbText(section.mid(eventTextOffset, eventTextLength));
                    }
                }
            } else if (descriptorTag == 0x4e && descriptorLength >= 6) {
                const int itemsLengthOffset = descriptorPayloadOffset + 3;
                if (itemsLengthOffset >= descriptorEnd) {
                    descriptorOffset = descriptorEnd;
                    continue;
                }
                const int itemsLength = byteAt(section, itemsLengthOffset);
                const int textLengthOffset = itemsLengthOffset + 1 + itemsLength;
                if (textLengthOffset < descriptorEnd) {
                    const int textLength = byteAt(section, textLengthOffset);
                    const int textOffset = textLengthOffset + 1;
                    if (textOffset + textLength <= descriptorEnd) {
                        const QString extendedText = decodeDvbText(section.mid(textOffset, textLength));
                        if (!extendedText.isEmpty()) {
                            synopsis = synopsis.isEmpty() ? extendedText : (synopsis + " " + extendedText);
                        }
                    }
                }
            }
            descriptorOffset = descriptorEnd;
        }

        if (!title.isEmpty() && startUtc.isValid() && durationSeconds > 0) {
            const QDateTime endUtc = startUtc.addSecs(durationSeconds);
            const QString key = QString("%1|%2|%3")
                                    .arg(serviceId)
                                    .arg(startUtc.toSecsSinceEpoch())
                                    .arg(title);
            if (!dedupe.contains(key)) {
                dedupe.insert(key);
                RawGuideEvent event;
                event.serviceId = serviceId;
                event.startUtc = startUtc;
                event.endUtc = endUtc;
                event.title = title;
                event.synopsis = synopsis.trimmed();
                events.append(event);
            }
        }

        offset = descriptorsEnd;
    }
}

void attachAtscEventSynopsisToParsedGuideData(ParsedGuideData &parsed)
{
    if (parsed.atscEventSynopsisByKey.isEmpty()) {
        return;
    }

    for (RawGuideEvent &event : parsed.events) {
        if (!event.synopsis.trimmed().isEmpty()) {
            continue;
        }
        const QString key = atscEventTextKey(event.serviceId, event.eventId);
        const QString synopsis = parsed.atscEventSynopsisByKey.value(key).trimmed();
        if (!synopsis.isEmpty()) {
            event.synopsis = synopsis;
        }
    }
}

ParsedGuideData parseGuideEventsFromTransport(const QByteArray &transport)
{
    ParsedGuideData parsed;
    QSet<QString> dedupe;
    QHash<int, SectionAssembler> sectionsByPid;
    QSet<int> atscPsipPids;
    QHash<QByteArray, int> rawSectionIndexes;
    atscPsipPids.insert(kAtscPsipPid);

    auto drainAssembler = [&](int pid, SectionAssembler &assembler) {
        while (true) {
            while (!assembler.bytes.isEmpty() && byteAt(assembler.bytes, 0) == 0xff) {
                assembler.bytes.remove(0, 1);
                assembler.expectedLength = -1;
            }

            if (assembler.bytes.size() < 3) {
                return;
            }

            if (assembler.expectedLength < 0) {
                const int sectionLength = ((byteAt(assembler.bytes, 1) & 0x0f) << 8)
                                          | byteAt(assembler.bytes, 2);
                assembler.expectedLength = 3 + sectionLength;
                if (assembler.expectedLength <= 0 || assembler.expectedLength > 4096) {
                    assembler.bytes.clear();
                    assembler.expectedLength = -1;
                    return;
                }
            }

            if (assembler.bytes.size() < assembler.expectedLength) {
                return;
            }

            const QByteArray section = assembler.bytes.left(assembler.expectedLength);
            assembler.bytes.remove(0, assembler.expectedLength);
            assembler.expectedLength = -1;
            processGuideSectionForPid(pid, section, parsed, dedupe, atscPsipPids, rawSectionIndexes);
        }
    };

    for (int offset = 0; offset + kTsPacketSize <= transport.size(); offset += kTsPacketSize) {
        const auto *packet = reinterpret_cast<const unsigned char *>(transport.constData() + offset);
        if (packet[0] != 0x47) {
            continue;
        }

        const int pid = ((packet[1] & 0x1f) << 8) | packet[2];
        if (pid != kDvbEitPid && !atscPsipPids.contains(pid)) {
            continue;
        }

        const bool payloadUnitStart = (packet[1] & 0x40) != 0;
        const int adaptationControl = (packet[3] >> 4) & 0x03;
        if (adaptationControl == 0 || adaptationControl == 2) {
            continue;
        }

        int payloadOffset = 4;
        if (adaptationControl == 3) {
            payloadOffset += 1 + packet[4];
        }
        if (payloadOffset >= kTsPacketSize) {
            continue;
        }

        QByteArray payload(reinterpret_cast<const char *>(packet + payloadOffset),
                           kTsPacketSize - payloadOffset);
        SectionAssembler &assembler = sectionsByPid[pid];
        if (payload.isEmpty()) {
            continue;
        }

        if (payloadUnitStart) {
            const int pointerField = byteAt(payload, 0);
            const int continuationLength = std::min(pointerField, static_cast<int>(payload.size()) - 1);
            if (continuationLength > 0) {
                assembler.bytes.append(payload.mid(1, continuationLength));
                drainAssembler(pid, assembler);
            }

            const int payloadStart = 1 + pointerField;
            if (payloadStart >= payload.size()) {
                continue;
            }
            payload = payload.mid(payloadStart);
            assembler.bytes.append(payload);
            drainAssembler(pid, assembler);
            continue;
        }

        assembler.bytes.append(payload);
        drainAssembler(pid, assembler);
    }

    attachAtscEventSynopsisToParsedGuideData(parsed);
    return parsed;
}

void processGuideSectionForPid(int pid,
                               const QByteArray &section,
                               ParsedGuideData &parsed,
                               QSet<QString> &dedupe,
                               QSet<int> &atscPsipPids,
                               QHash<QByteArray, int> &rawSectionIndexes)
{
    if (section.size() < 3) {
        return;
    }

    appendRawGuideSection(pid, section, parsed.rawSections, rawSectionIndexes);

    if (atscPsipPids.contains(pid)) {
        parsed.atscPsipTableIds.insert(byteAt(section, 0));
        appendAtscPsipPidsFromMgtSection(section, atscPsipPids);
        appendAtscSourceMapFromVctSection(section, parsed.atscSourceToProgram);
        appendAtscEventTextFromSection(section, parsed.atscEventSynopsisByKey);
        appendAtscEventsFromSection(section, parsed.events, dedupe);
    } else if (pid == kDvbEitPid) {
        appendDvbEventsFromSection(section, parsed.events, dedupe);
    }
}

int openDemuxSectionFilter(const QString &demuxPath, int pid, QString &errorText)
{
    const QByteArray pathBytes = QFile::encodeName(demuxPath);
    const int fd = ::open(pathBytes.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        errorText = QString("Failed to open %1 for PID 0x%2 (%3)")
                        .arg(demuxPath)
                        .arg(pid, 0, 16)
                        .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return -1;
    }

    const int bufferSize = 256 * 1024;
    ::ioctl(fd, DMX_SET_BUFFER_SIZE, bufferSize);

    dmx_sct_filter_params params{};
    params.pid = static_cast<__u16>(pid);
    params.timeout = 0;
    params.flags = DMX_IMMEDIATE_START;
    if (::ioctl(fd, DMX_SET_FILTER, &params) < 0) {
        errorText = QString("Failed to start demux section filter on %1 for PID 0x%2 (%3)")
                        .arg(demuxPath)
                        .arg(pid, 0, 16)
                        .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(fd);
        return -1;
    }

    return fd;
}

bool captureGuideEventsFromDemux(int adapter,
                                 const QString &contextName,
                                 const QSet<int> &expectedServiceIds,
                                 QList<RawGuideEvent> &events,
                                 QHash<int, int> &atscSourceToProgram,
                                 QSet<int> &atscPsipTableIds,
                                 QString &errorText,
                                 int maxCaptureMs = kGuideCaptureMaxMs,
                                 QList<RawGuideSection> *rawSections = nullptr)
{
    events.clear();
    atscSourceToProgram.clear();
    atscPsipTableIds.clear();
    errorText.clear();
    if (rawSections != nullptr) {
        rawSections->clear();
    }

    const int effectiveCaptureMaxMs = std::max(250, maxCaptureMs);
    const int captureMinMs = std::min(kGuideCaptureMinMs, effectiveCaptureMaxMs);

    const QString demuxPath = QString("/dev/dvb/adapter%1/demux0").arg(adapter);
    QList<DemuxSectionReader> readers;
    QSet<int> activePids;
    ParsedGuideData parsed;
    QSet<QString> dedupe;
    QSet<int> discoveredAtscPids{ kAtscPsipPid };
    QHash<QByteArray, int> rawSectionIndexes;
    int sectionsRead = 0;

    auto closeReaders = [&readers]() {
        for (const DemuxSectionReader &reader : readers) {
            if (reader.fd >= 0) {
                ::close(reader.fd);
            }
        }
    };

    auto ensureReader = [&](int pid) {
        if (activePids.contains(pid)) {
            return true;
        }
        QString openError;
        const int fd = openDemuxSectionFilter(demuxPath, pid, openError);
        if (fd < 0) {
            if (errorText.isEmpty()) {
                errorText = openError;
            }
            return false;
        }
        DemuxSectionReader reader;
        reader.pid = pid;
        reader.fd = fd;
        readers.append(reader);
        activePids.insert(pid);
        return true;
    };

    ensureReader(kAtscPsipPid);
    ensureReader(kDvbEitPid);

    QElapsedTimer captureTimer;
    captureTimer.start();
    qint64 lastProgressMs = 0;
    int bestMappedServiceCount = 0;

    while (captureTimer.elapsed() < effectiveCaptureMaxMs) {
        const QList<int> pendingAtscPids = discoveredAtscPids.values();
        for (int pid : pendingAtscPids) {
            ensureReader(pid);
        }
        if (readers.isEmpty()) {
            break;
        }

        std::vector<pollfd> pollFds;
        pollFds.reserve(static_cast<size_t>(readers.size()));
        for (const DemuxSectionReader &reader : readers) {
            pollfd pfd{};
            pfd.fd = reader.fd;
            pfd.events = POLLIN;
            pollFds.push_back(pfd);
        }

        const qint64 remainingMs = effectiveCaptureMaxMs - captureTimer.elapsed();
        const int pollTimeoutMs = static_cast<int>(std::max<qint64>(1, std::min<qint64>(kGuideProbeIntervalMs, remainingMs)));
        const int pollResult = ::poll(pollFds.data(),
                                      static_cast<nfds_t>(pollFds.size()),
                                      pollTimeoutMs);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            errorText = QString("Demux poll failed for %1 (%2)")
                            .arg(contextName, QString::fromLocal8Bit(std::strerror(errno)));
            break;
        }

        bool sawNewSection = false;
        if (pollResult > 0) {
            for (int i = 0; i < readers.size(); ++i) {
                if ((pollFds[static_cast<size_t>(i)].revents & POLLIN) == 0) {
                    continue;
                }

                while (true) {
                    char buffer[4096];
                    const ssize_t bytesRead = ::read(readers.at(i).fd, buffer, sizeof(buffer));
                    if (bytesRead <= 0) {
                        if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            errorText = QString("Demux read failed for %1 on PID 0x%2 (%3)")
                                            .arg(contextName)
                                            .arg(readers.at(i).pid, 0, 16)
                                            .arg(QString::fromLocal8Bit(std::strerror(errno)));
                        }
                        break;
                    }

                    ++sectionsRead;
                    sawNewSection = true;
                    processGuideSectionForPid(readers.at(i).pid,
                                              QByteArray(buffer, static_cast<int>(bytesRead)),
                                              parsed,
                                              dedupe,
                                              discoveredAtscPids,
                                              rawSectionIndexes);
                }
            }
        }

        const qint64 elapsedMs = captureTimer.elapsed();
        if (sawNewSection || elapsedMs >= captureMinMs) {
            const int mappedServiceCount = mappedProgramIdsFromParsedGuideData(parsed).size();
            if (mappedServiceCount > bestMappedServiceCount
                || !parsed.atscSourceToProgram.isEmpty()
                || !parsed.events.isEmpty()) {
                bestMappedServiceCount = std::max(bestMappedServiceCount, mappedServiceCount);
                lastProgressMs = elapsedMs;
            }

            if (!parsed.events.isEmpty()
                && !parsed.atscSourceToProgram.isEmpty()
                && elapsedMs >= 2200
                && elapsedMs - lastProgressMs >= 1600) {
                break;
            }
        }
    }

    closeReaders();

    attachAtscEventSynopsisToParsedGuideData(parsed);
    events = parsed.events;
    atscSourceToProgram = parsed.atscSourceToProgram;
    atscPsipTableIds = parsed.atscPsipTableIds;
    if (rawSections != nullptr) {
        *rawSections = parsed.rawSections;
    }

    if (sectionsRead == 0) {
        if (errorText.isEmpty()) {
            errorText = QString("No PSI/EIT sections read from %1 for %2").arg(demuxPath, contextName);
        }
        return false;
    }

    if (events.isEmpty()) {
        const QString tables = formatTableIdSet(atscPsipTableIds);
        errorText = QString("No EIT events decoded for %1 from demux0 (PSIP tables seen: %2)")
                        .arg(contextName, tables.isEmpty() ? "none" : tables);
    }

    return true;
}

QSet<int> mappedProgramIdsFromParsedGuideData(const ParsedGuideData &parsed)
{
    QSet<int> mappedPrograms;
    for (const RawGuideEvent &event : parsed.events) {
        int mappedServiceId = event.serviceId;
        if (parsed.atscSourceToProgram.contains(mappedServiceId)) {
            mappedServiceId = parsed.atscSourceToProgram.value(mappedServiceId);
        }
        if (mappedServiceId > 0) {
            mappedPrograms.insert(mappedServiceId);
        }
    }
    return mappedPrograms;
}

QVector<GuideChannelInfo> parseGuideChannels(const QStringList &channelLines,
                                            const QHash<QString, QString> *numberByTuneKey = nullptr)
{
    QVector<GuideChannelInfo> channels;
    QSet<QString> dedupe;

    for (const QString &line : channelLines) {
        const QStringList parts = line.split(':');
        if (parts.size() < 6) {
            continue;
        }

        const QString baseName = parts[0].trimmed();
        bool freqOk = false;
        bool serviceOk = false;
        const qint64 frequencyHz = parts[1].trimmed().toLongLong(&freqOk);
        const int serviceId = parts[5].trimmed().toInt(&serviceOk, 0);
        if (baseName.isEmpty() || !freqOk || frequencyHz <= 0 || !serviceOk || serviceId <= 0) {
            continue;
        }

        const QString channelNumber = channelNumberHintForParts(parts, numberByTuneKey);
        const QString displayName = channelDisplayLabel(baseName, channelNumber);

        const QString key = QString("%1|%2|%3").arg(baseName).arg(frequencyHz).arg(serviceId);
        if (dedupe.contains(key)) {
            continue;
        }
        dedupe.insert(key);

        GuideChannelInfo info;
        info.name = displayName.isEmpty() ? baseName : displayName;
        info.baseName = baseName;
        info.channelNumber = channelNumber;
        info.frequencyHz = frequencyHz;
        info.serviceId = serviceId;
        channels.append(info);
    }

    return channels;
}

bool captureGuideEventsFromDvr(const QString &dvrPath,
                               const QString &contextName,
                               const QSet<int> &expectedServiceIds,
                               bool dvrReadyObserved,
                               QList<RawGuideEvent> &events,
                               QHash<int, int> &atscSourceToProgram,
                               QSet<int> &atscPsipTableIds,
                               QString &errorText,
                               QList<RawGuideSection> *rawSections = nullptr)
{
    events.clear();
    atscSourceToProgram.clear();
    atscPsipTableIds.clear();
    errorText.clear();
    if (rawSections != nullptr) {
        rawSections->clear();
    }

    const QString timeoutExe = QStandardPaths::findExecutable("timeout");
    const QString ddExe = QStandardPaths::findExecutable("dd");
    if (timeoutExe.isEmpty() || ddExe.isEmpty()) {
        errorText = "The guide capture path requires timeout and dd in PATH.";
        return false;
    }

    QByteArray transport;
    transport.reserve(10 * 1024 * 1024);
    ParsedGuideData parsed;
    QProcess captureProcess;
    captureProcess.setProcessChannelMode(QProcess::SeparateChannels);
    QStringList captureArgs;
    captureArgs << QString::number(std::max(1, kGuideCaptureMaxMs / 1000)) + "s"
                << ddExe
                << QString("if=%1").arg(dvrPath)
                << QString("bs=%1").arg(kTsPacketSize)
                << QString("count=%1").arg(kGuideCapturePacketCount)
                << "status=none";
    captureProcess.start(timeoutExe, captureArgs);
    if (!captureProcess.waitForStarted(1200)) {
        errorText = QString("Failed to start capture process for %1 (%2)")
                        .arg(contextName, captureProcess.errorString());
        return false;
    }

    auto stopCapture = [&captureProcess]() {
        if (captureProcess.state() == QProcess::NotRunning) {
            return;
        }
        captureProcess.terminate();
        if (!captureProcess.waitForFinished(800)) {
            captureProcess.kill();
            captureProcess.waitForFinished(800);
        }
    };

    QElapsedTimer captureTimer;
    captureTimer.start();
    qint64 lastProbeMs = 0;
    qint64 lastProgressMs = 0;
    int bestMappedServiceCount = 0;
    while (captureTimer.elapsed() < kGuideCaptureMaxMs && captureProcess.state() != QProcess::NotRunning) {
        captureProcess.waitForReadyRead(220);
        const QByteArray chunk = captureProcess.readAllStandardOutput();
        if (!chunk.isEmpty()) {
            transport.append(chunk);
            if (transport.size() >= 28 * 1024 * 1024) {
                break;
            }
        }

        const qint64 elapsedMs = captureTimer.elapsed();
        if (elapsedMs >= kGuideCaptureMinMs
            && elapsedMs - lastProbeMs >= kGuideProbeIntervalMs
            && transport.size() >= 188 * 1200) {
            lastProbeMs = elapsedMs;
            parsed = parseGuideEventsFromTransport(transport);
            const int mappedServiceCount = mappedProgramIdsFromParsedGuideData(parsed).size();
            if (mappedServiceCount > bestMappedServiceCount || parsed.atscSourceToProgram.size() > 0) {
                bestMappedServiceCount = std::max(bestMappedServiceCount, mappedServiceCount);
                lastProgressMs = elapsedMs;
            }

            if (!parsed.events.isEmpty()
                && !parsed.atscSourceToProgram.isEmpty()
                && elapsedMs >= 2200
                && elapsedMs - lastProgressMs >= 1600) {
                break;
            }
        }
    }

    transport.append(captureProcess.readAllStandardOutput());
    const QString captureErrors = QString::fromUtf8(captureProcess.readAllStandardError());
    stopCapture();

    if (transport.isEmpty()) {
        errorText = QString("No transport data read from %1 for %2")
                        .arg(dvrPath, contextName);
        if (!captureErrors.trimmed().isEmpty()) {
            errorText += QString(" (%1)").arg(captureErrors.trimmed());
        }
        return false;
    }

    if (parsed.events.isEmpty() && parsed.atscSourceToProgram.isEmpty() && parsed.atscPsipTableIds.isEmpty()) {
        parsed = parseGuideEventsFromTransport(transport);
    }
    attachAtscEventSynopsisToParsedGuideData(parsed);
    events = parsed.events;
    atscSourceToProgram = parsed.atscSourceToProgram;
    atscPsipTableIds = parsed.atscPsipTableIds;
    if (rawSections != nullptr) {
        *rawSections = parsed.rawSections;
    }
    if (events.isEmpty()) {
        const QString tables = formatTableIdSet(atscPsipTableIds);
        errorText = dvrReadyObserved
                        ? QString("No EIT events decoded for %1 (PSIP tables seen: %2)")
                              .arg(contextName, tables.isEmpty() ? "none" : tables)
                        : QString("No EIT events decoded for %1 (DVR ready signal not observed)")
                              .arg(contextName);
    }
    return true;
}

bool captureGuideEventsForChannel(const QString &channelsFilePath,
                                  int adapter,
                                  int frontend,
                                  const QString &channelName,
                                  const QSet<int> &expectedServiceIds,
                                  QList<RawGuideEvent> &events,
                                  QHash<int, int> &atscSourceToProgram,
                                  QSet<int> &atscPsipTableIds,
                                  QString &errorText,
                                  int totalTimeoutMs = kGuideLookupTotalMaxMs,
                                  QList<RawGuideSection> *rawSections = nullptr)
{
    events.clear();
    atscSourceToProgram.clear();
    atscPsipTableIds.clear();
    errorText.clear();
    if (rawSections != nullptr) {
        rawSections->clear();
    }

    const int effectiveTotalTimeoutMs = std::max(1000, totalTimeoutMs);

    const QString zapExe = QStandardPaths::findExecutable("dvbv5-zap");
    if (zapExe.isEmpty()) {
        errorText = "dvbv5-zap is not available in PATH.";
        return false;
    }

    QElapsedTimer lookupTimer;
    lookupTimer.start();

    QProcess zapProcess;
    zapProcess.setProcessChannelMode(QProcess::SeparateChannels);
    QStringList args;
    args << "-I" << "ZAP"
         << "-c" << channelsFilePath
         << "-a" << QString::number(adapter)
         << "-f" << QString::number(frontend)
         << "-t" << QString::number(std::max(1, (effectiveTotalTimeoutMs + 999) / 1000))
         << channelName;
    zapProcess.start(zapExe, args);
    if (!zapProcess.waitForStarted(std::min(1500, effectiveTotalTimeoutMs))) {
        errorText = QString("Failed to start dvbv5-zap for %1 (%2)")
                        .arg(channelName, zapProcess.errorString());
        return false;
    }

    auto stopZap = [&zapProcess]() {
        zapProcess.terminate();
        if (!zapProcess.waitForFinished(1200)) {
            zapProcess.kill();
            zapProcess.waitForFinished(1200);
        }
    };

    QString zapErrors;

    const int remainingCaptureMs = std::max(0, effectiveTotalTimeoutMs - static_cast<int>(lookupTimer.elapsed()));
    if (remainingCaptureMs <= 0) {
        stopZap();
        errorText = QString("Stopped EIT lookup for %1 after %2 ms without receiving any guide sections.")
                        .arg(channelName)
                        .arg(effectiveTotalTimeoutMs);
        return false;
    }

    const bool captured = captureGuideEventsFromDemux(adapter,
                                                      channelName,
                                                      expectedServiceIds,
                                                      events,
                                                      atscSourceToProgram,
                                                      atscPsipTableIds,
                                                      errorText,
                                                      remainingCaptureMs,
                                                      rawSections);
    stopZap();
    zapErrors += QString::fromUtf8(zapProcess.readAllStandardError());
    if (!captured && !zapErrors.trimmed().isEmpty()) {
        errorText += QString(" (%1)").arg(zapErrors.trimmed());
    } else if (captured && events.isEmpty() && !zapErrors.trimmed().isEmpty() && errorText.isEmpty()) {
        const QString tables = formatTableIdSet(atscPsipTableIds);
        errorText = QString("No EIT events decoded for %1 within %2 ms (PSIP tables seen: %3; %4)")
                        .arg(channelName)
                        .arg(effectiveTotalTimeoutMs)
                        .arg(tables.isEmpty() ? "none" : tables)
                        .arg(zapErrors.trimmed());
    } else if (captured && events.isEmpty() && errorText.isEmpty()) {
        const QString tables = formatTableIdSet(atscPsipTableIds);
        errorText = QString("No EIT events decoded for %1 within %2 ms (PSIP tables seen: %3)")
                        .arg(channelName)
                        .arg(effectiveTotalTimeoutMs)
                        .arg(tables.isEmpty() ? "none" : tables);
    }
    return captured;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    qApp->installEventFilter(this);

    scanProcess_ = new QProcess(this);
    zapProcess_ = new QProcess(this);
    streamBridgeProcess_ = new QProcess(this);
    signalMonitorProcess_ = new QProcess(this);
    mediaPlayer_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    reconnectTimer_ = new QTimer(this);
    currentShowTimer_ = new QTimer(this);
    playbackAttachTimer_ = new QTimer(this);
    guideRefreshTimer_ = new QTimer(this);
    guideCachePollTimer_ = new QTimer(this);
    scheduledSwitchTimer_ = new QTimer(this);
    fullscreenCursorHideTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    currentShowTimer_->setSingleShot(true);
    playbackAttachTimer_->setSingleShot(true);
    scheduledSwitchTimer_->setSingleShot(true);
    fullscreenCursorHideTimer_->setSingleShot(true);
    fullscreenCursorHideTimer_->setInterval(5000);

    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setVideoOutput(videoWidget_);
    QSettings settings("tv_tuner_gui", "watcher");
    const int savedVolume = std::clamp(settings.value("volume_percent", 85).toInt(), 0, 100);
    obeyScheduledSwitches_ = settings.value(kObeyScheduledSwitchesSetting, true).toBool();
    autoFavoriteShowSchedulingEnabled_ = settings.value(kAutoFavoriteShowSchedulingSetting, true).toBool();
    favoriteShowRatingsOverrideEnabled_ = settings.value(kFavoriteShowRatingsOverrideSetting, false).toBool();
    autoPictureInPictureEnabled_ = settings.value(kAutoPictureInPictureSetting, true).toBool();
    lastAutoFavoriteScheduleStamp_ =
        settings.value(kDismissedAutoFavoriteCandidatesStampSetting).toString().trimmed();
    dismissedAutoFavoriteCandidates_ = settings.value(kDismissedAutoFavoriteCandidatesSetting).toStringList();
    lockedAutoFavoriteSelections_ = settings.value(kLockedAutoFavoriteSelectionsSetting).toStringList();
    lockedScheduledSwitches_ = settings.value(kLockedScheduledSwitchesSetting).toStringList();
    audioOutput_->setVolume(static_cast<float>(savedVolume) / 100.0f);
    volumeSlider_->setValue(savedVolume);
    if (hideNoEitChannelsCheckBox_ != nullptr) {
        const QSignalBlocker blocker(hideNoEitChannelsCheckBox_);
        hideNoEitChannelsCheckBox_->setChecked(settings.value(kHideNoEitChannelsSetting, false).toBool());
    }
    if (showFavoritesOnlyCheckBox_ != nullptr) {
        const QSignalBlocker blocker(showFavoritesOnlyCheckBox_);
        showFavoritesOnlyCheckBox_->setChecked(settings.value(kShowFavoritesOnlySetting, false).toBool());
    }
    if (obeyScheduledSwitchesCheckBox_ != nullptr) {
        const QSignalBlocker blocker(obeyScheduledSwitchesCheckBox_);
        obeyScheduledSwitchesCheckBox_->setChecked(obeyScheduledSwitches_);
    }
    if (autoFavoriteShowSchedulingCheckBox_ != nullptr) {
        const QSignalBlocker blocker(autoFavoriteShowSchedulingCheckBox_);
        autoFavoriteShowSchedulingCheckBox_->setChecked(autoFavoriteShowSchedulingEnabled_);
    }
    if (favoriteShowRatingsOverrideCheckBox_ != nullptr) {
        const QSignalBlocker blocker(favoriteShowRatingsOverrideCheckBox_);
        favoriteShowRatingsOverrideCheckBox_->setChecked(favoriteShowRatingsOverrideEnabled_);
    }
    if (autoPictureInPictureCheckBox_ != nullptr) {
        const QSignalBlocker blocker(autoPictureInPictureCheckBox_);
        autoPictureInPictureCheckBox_->setChecked(autoPictureInPictureEnabled_);
    }
    if (hideStartupSwitchSummaryCheckBox_ != nullptr) {
        const QSignalBlocker blocker(hideStartupSwitchSummaryCheckBox_);
        hideStartupSwitchSummaryCheckBox_->setChecked(
            settings.value(kHideStartupSwitchSummarySetting, false).toBool());
    }
    if (hideStartupSwitchSummaryCheckBox_ != nullptr) {
        const QSignalBlocker blocker(hideStartupSwitchSummaryCheckBox_);
        hideStartupSwitchSummaryCheckBox_->setChecked(
            settings.value(kHideStartupSwitchSummarySetting, false).toBool());
    }
    if (useSchedulesDirectGuideCheckBox_ != nullptr) {
        const QSignalBlocker blocker(useSchedulesDirectGuideCheckBox_);
        useSchedulesDirectGuideCheckBox_->setChecked(settings.value(kUseSchedulesDirectGuideSetting, false).toBool());
    }
    if (guideRefreshIntervalSpin_ != nullptr) {
        const QSignalBlocker blocker(guideRefreshIntervalSpin_);
        guideRefreshIntervalSpin_->setValue(settings.value(kGuideRefreshIntervalMinutesSetting,
                                                           kDefaultGuideRefreshIntervalMinutes)
                                                .toInt());
    }
    if (guideCacheRetentionSpin_ != nullptr) {
        const QSignalBlocker blocker(guideCacheRetentionSpin_);
        guideCacheRetentionSpin_->setValue(settings.value(kGuideCacheRetentionHoursSetting,
                                                          kDefaultGuideCacheRetentionHours)
                                               .toInt());
    }
    if (schedulesDirectUsernameEdit_ != nullptr) {
        const QSignalBlocker blocker(schedulesDirectUsernameEdit_);
        schedulesDirectUsernameEdit_->setText(settings.value(kSchedulesDirectUsernameSetting).toString().trimmed());
    }
    if (schedulesDirectPasswordEdit_ != nullptr) {
        const QSignalBlocker blocker(schedulesDirectPasswordEdit_);
        schedulesDirectPasswordEdit_->setText(settings.value(kSchedulesDirectPasswordSha1Setting).toString().trimmed());
    }
    if (schedulesDirectPostalCodeEdit_ != nullptr) {
        const QSignalBlocker blocker(schedulesDirectPostalCodeEdit_);
        schedulesDirectPostalCodeEdit_->setText(settings.value(kSchedulesDirectPostalCodeSetting).toString().trimmed());
    }
    applyGuideFilterSettings();
    applyGuideRefreshIntervalSetting();
    updateSchedulesDirectControls();

    connect(scanProcess_, &QProcess::readyReadStandardOutput, this, &MainWindow::handleStdOut);
    connect(scanProcess_, &QProcess::readyReadStandardError, this, &MainWindow::handleStdErr);
    connect(scanProcess_, &QProcess::finished, this, &MainWindow::processFinished);
    connect(zapProcess_, &QProcess::readyReadStandardError, this, &MainWindow::handleZapStdErr);
    connect(zapProcess_, &QProcess::finished, this, &MainWindow::handleZapFinished);
    connect(zapProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        appendLog(QString("zap: errorOccurred=%1 (%2)")
                      .arg(processErrorToString(error), zapProcess_->errorString()));
    });
    signalMonitorProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(signalMonitorProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        handleSignalMonitorOutput(QString::fromUtf8(signalMonitorProcess_->readAllStandardOutput()));
    });
    connect(signalMonitorProcess_, &QProcess::finished, this, &MainWindow::handleSignalMonitorFinished);
    connect(signalMonitorProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (currentChannelName_.isEmpty() || userStoppedWatching_) {
            return;
        }
        setSignalMonitorStatus("Signal: unavailable",
                               QString("dvb-fe-tool error: %1 (%2)")
                                   .arg(processErrorToString(error), signalMonitorProcess_->errorString()));
        appendLog(QString("signal: dvb-fe-tool errorOccurred=%1 (%2)")
                      .arg(processErrorToString(error), signalMonitorProcess_->errorString()));
    });
    connect(streamBridgeProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        appendLog(QString("ffmpeg bridge: errorOccurred=%1 (%2)")
                      .arg(processErrorToString(error), streamBridgeProcess_->errorString()));
    });
    connect(streamBridgeProcess_, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(streamBridgeProcess_->readAllStandardError()).trimmed();
        if (!err.isEmpty()) {
            const QStringList lines = err.split('\n');
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) {
                    const QString lowerTrimmed = trimmed.toLower();
                    if (lowerTrimmed.contains("could not find codec parameters")
                        || lowerTrimmed.contains("could not write header")
                        || lowerTrimmed.contains("sample rate not set")
                        || lowerTrimmed.contains("invalid frame dimensions 0x0")) {
                        bridgeSawCodecParameterFailure_ = true;
                    }
                    appendLog("ffmpeg: " + trimmed);
                }
            }
        }
    });
    connect(streamBridgeProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendLog(QString("ffmpeg bridge exited (code=%1, status=%2, error=%3)")
                      .arg(exitCode)
                      .arg(status == QProcess::NormalExit ? "normal" : "crash")
                      .arg(streamBridgeProcess_->errorString()));
        if (zapProcess_ != nullptr && zapProcess_->state() != QProcess::NotRunning) {
            suppressZapExitReconnect_ = true;
            stopProcess(zapProcess_, 1000);
            suppressZapExitReconnect_ = false;
        }
        if (!suppressBridgeExitReconnect_ && !userStoppedWatching_ && !currentChannelName_.isEmpty()) {
            if (tryDynamicBridgeFallback("Live stream bridge exited")) {
                return;
            }
            scheduleReconnect("Live stream bridge exited");
        }
    });
    connect(mediaPlayer_, &QMediaPlayer::mediaStatusChanged, this, &MainWindow::handleMediaStatusChanged);
    connect(mediaPlayer_, &QMediaPlayer::playbackStateChanged, this, [this]() {
        appendLog(QString("player: playbackStateChanged=%1").arg(static_cast<int>(mediaPlayer_->playbackState())));
        playbackStatusLabel_->setText(playbackStatusText());
        syncFullscreenOverlayState();
    });
    connect(mediaPlayer_, &QMediaPlayer::errorChanged, this, [this]() {
        if (mediaPlayer_->error() != QMediaPlayer::NoError) {
            appendLog(QString("player: errorChanged code=%1 text=%2")
                          .arg(static_cast<int>(mediaPlayer_->error()))
                          .arg(mediaPlayer_->errorString()));
            handlePlayerError(mediaPlayer_->errorString());
        }
    });
    connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
        appendLog(QString("player: errorOccurred code=%1 text=%2").arg(static_cast<int>(error)).arg(errorString));
    });
    connect(mediaPlayer_, &QMediaPlayer::hasVideoChanged, this, [this](bool hasVideo) {
        appendLog(QString("player: hasVideoChanged=%1").arg(hasVideo ? "true" : "false"));
    });
    connect(mediaPlayer_, &QMediaPlayer::hasAudioChanged, this, [this](bool hasAudio) {
        appendLog(QString("player: hasAudioChanged=%1").arg(hasAudio ? "true" : "false"));
    });
    connect(mediaPlayer_, &QMediaPlayer::bufferProgressChanged, this, [this](float progress) {
        appendLog(QString("player: bufferProgress=%1").arg(progress, 0, 'f', 3));
    });
    connect(reconnectTimer_, &QTimer::timeout, this, &MainWindow::triggerReconnect);
    connect(currentShowTimer_, &QTimer::timeout, this, &MainWindow::refreshCurrentShowStatus);
    connect(playbackAttachTimer_, &QTimer::timeout, this, [this]() {
        if (!waitingForDvrReady_ || pendingDvrPath_.isEmpty()) {
            return;
        }
        appendLog("player: DVR ready signal timeout; attempting playback anyway.");
        startPlaybackFromDvr(pendingDvrPath_);
    });
    connect(scheduledSwitchTimer_, &QTimer::timeout, this, &MainWindow::processScheduledSwitches);
    connect(fullscreenCursorHideTimer_, &QTimer::timeout, this, &MainWindow::hideFullscreenCursor);
    connect(guideRefreshTimer_, &QTimer::timeout, this, [this]() {
        appendLog("guide-bg: scheduled guide cache refresh triggered.");
        if (refreshGuideData(false, false)) {
            loadGuideCacheFile();
            applyCurrentShowStatusFromGuideCache();
            updateTvGuideDialogFromCurrentCache(false);
        }
    });
    guideCachePollTimer_->setInterval(kGuideCachePollIntervalMs);
    connect(guideCachePollTimer_, &QTimer::timeout, this, [this]() {
        const bool watchingLiveChannel = !currentChannelName_.isEmpty()
                                         && !currentChannelName_.startsWith("File: ")
                                         && !userStoppedWatching_;
        const bool guideDialogVisible = tvGuideDialog_ != nullptr && tvGuideDialog_->isVisible();
        if (!watchingLiveChannel && !guideDialogVisible) {
            return;
        }
        if (loadGuideCacheFile()) {
            if (watchingLiveChannel) {
                applyCurrentShowStatusFromGuideCache();
            }
            if (guideDialogVisible) {
                updateTvGuideDialogFromCurrentCache(false);
            }
        }
    });
    connect(volumeSlider_, &QSlider::valueChanged, this, &MainWindow::handleVolumeChanged);
    connect(muteButton_, &QPushButton::toggled, this, &MainWindow::handleMuteToggled);
    connect(contentSplitter_, &QSplitter::splitterMoved, this, [this](int, int) {
        saveChannelSidebarSizing();
    });

    logFilePath_ = resolveProjectLogPath();
    loadFavorites();
    loadFavoriteShowRules();
    loadTestingBugItems();
    loadXspfChannelHints();
    loadChannelsFileIfPresent();
    purgeExpiredGuideCacheFiles(true);
    loadScheduledSwitches();
    loadGuideCacheFile();
    refreshFavoriteShowRuleList();
    syncFavoriteShowRatingControls();
    refreshScheduledSwitchList();
    refreshQuickButtons();
    playbackStatusLabel_->setText(playbackStatusText());
    setSignalMonitorStatus("Signal: n/a");
    setCurrentShowStatus("NO EIT DATA");
    syncFullscreenOverlayState();
    updateTvGuideDialogFromCurrentCache(false);
    refreshScheduledSwitchTimer();
    guideCachePollTimer_->start();

    QTimer::singleShot(0, this, [this]() {
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        if (guideCacheLooksCurrentForStartup(guideEntriesCache_, lastGuideWindowStartUtc_, nowUtc)) {
            appendLog(QString("guide-bg: startup guide refresh skipped; cache already covers current time through %1")
                          .arg(latestGuideEntryEndUtc(guideEntriesCache_).toLocalTime().toString("ddd h:mm AP")));
        } else {
            appendLog("guide-bg: building initial guide cache at startup.");
            if (refreshGuideData(false, false)) {
                loadGuideCacheFile();
                applyCurrentShowStatusFromGuideCache();
            }
        }
        deferStartupAutoFavoriteScheduling_ = false;
        logInteraction("program",
                       "startup.favorite-show.auto-scan",
                       QString("guide-cache-loaded=%1 favorites=%2 queue-before=%3")
                           .arg(guideEntriesCache_.isEmpty() ? "false" : "true",
                                favoriteShowRules_.join(" | "),
                                summarizeScheduledSwitchesDebug(scheduledSwitches_)));
        autoScheduleFavoriteShowsFromGuideCache(false, true);
        guideRefreshTimer_->start();
        setStatusBarStateMessage(lastStatusBarMessage_);
        restoreChannelSidebarSizing();
        restoreLastPlayedChannel();
        processScheduledSwitches();
        showStartupSwitchSummary();
    });
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    exitFullscreen();
    userStoppedWatching_ = true;
    if (reconnectTimer_ != nullptr) {
        reconnectTimer_->stop();
    }
    if (currentShowTimer_ != nullptr) {
        currentShowTimer_->stop();
    }
    if (playbackAttachTimer_ != nullptr) {
        playbackAttachTimer_->stop();
    }
    if (guideRefreshTimer_ != nullptr) {
        guideRefreshTimer_->stop();
    }
    if (guideCachePollTimer_ != nullptr) {
        guideCachePollTimer_->stop();
    }
    if (scheduledSwitchTimer_ != nullptr) {
        scheduledSwitchTimer_->stop();
    }
    if (fullscreenCursorHideTimer_ != nullptr) {
        fullscreenCursorHideTimer_->stop();
    }
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl());
    }

    if (pipWindow_ != nullptr) {
        pipWindow_->hide();
    }

    suppressBridgeExitReconnect_ = true;
    stopProcess(streamBridgeProcess_, 1200);
    suppressBridgeExitReconnect_ = false;

    stopProcess(signalMonitorProcess_, 1000);

    suppressZapExitReconnect_ = true;
    stopProcess(zapProcess_, 1200);
    suppressZapExitReconnect_ = false;

    stopProcess(scanProcess_, 1200);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    appendLog("Application closing: releasing tuner resources.");
    if (pipWindow_ != nullptr) {
        pipWindow_->hide();
    }
    exitFullscreen();
    saveChannelSidebarSizing();
    stopWatching();
    stopProcess(scanProcess_, 1200);
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *watchedWidget = qobject_cast<QWidget *>(watched);
    const bool watchedInsideFullscreenWindow =
        fullscreenWindow_ != nullptr
        && watchedWidget != nullptr
        && (watchedWidget == fullscreenWindow_ || fullscreenWindow_->isAncestorOf(watchedWidget));
    const bool watchedInsideFullscreenOverlay =
        fullscreenOverlayContainer_ != nullptr
        && watchedWidget != nullptr
        && (watchedWidget == fullscreenOverlayContainer_
            || fullscreenOverlayContainer_->isAncestorOf(watchedWidget));
    const bool watchedInsideFullscreenRegion = watchedInsideFullscreenWindow || watchedInsideFullscreenOverlay;
    const bool fullscreenActivityEvent =
        fullscreenActive_
        && event != nullptr
        && (event->type() == QEvent::Enter
            || event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::Wheel
            || event->type() == QEvent::HoverMove);

    if (watched == videoWidget_ && event->type() == QEvent::MouseButtonDblClick) {
        enterFullscreen();
        return true;
    }

    if (watched == pipVideoWidget_ && event->type() == QEvent::MouseButtonDblClick) {
        if (tabs_ != nullptr && watchPage_ != nullptr) {
            tabs_->setCurrentWidget(watchPage_);
        } else {
            attachVideoFromPip();
        }
        return true;
    }

    if ((watchedInsideFullscreenRegion || watched == fullscreenVideoWidget_)
        && event->type() == QEvent::MouseButtonDblClick) {
        exitFullscreen();
        return true;
    }

    if (fullscreenActivityEvent) {
        const bool overlayHidden = fullscreenOverlayContainer_ != nullptr && !fullscreenOverlayContainer_->isVisible();
        const QRect overlayGeometry =
            fullscreenOverlayContainer_ != nullptr ? fullscreenOverlayContainer_->geometry() : QRect();
        const QRect fullscreenGeometry =
            fullscreenWindow_ != nullptr ? fullscreenWindow_->geometry() : QRect();

        if (watchedInsideFullscreenRegion) {
            if (fullscreenCursorHidden_ || overlayHidden) {
                appendLog(QStringLiteral("fullscreen-overlay: accepted activity type=%1 watched=%2 hiddenCursor=%3 overlayVisible=%4 overlayGeom=%5,%6 %7x%8 fullscreenGeom=%9,%10 %11x%12")
                              .arg(eventTypeName(event->type()),
                                   describeObjectForLog(watched),
                                   fullscreenCursorHidden_ ? QStringLiteral("true") : QStringLiteral("false"),
                                   overlayHidden ? QStringLiteral("false") : QStringLiteral("true"))
                              .arg(overlayGeometry.x())
                              .arg(overlayGeometry.y())
                              .arg(overlayGeometry.width())
                              .arg(overlayGeometry.height())
                              .arg(fullscreenGeometry.x())
                              .arg(fullscreenGeometry.y())
                              .arg(fullscreenGeometry.width())
                              .arg(fullscreenGeometry.height()));
            }
            showFullscreenCursor();
            restartFullscreenCursorHideTimer();
        } else if (fullscreenCursorHidden_ || overlayHidden) {
            static qint64 lastIgnoredActivityLogMs = 0;
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs - lastIgnoredActivityLogMs >= 300) {
                lastIgnoredActivityLogMs = nowMs;
                appendLog(QStringLiteral("fullscreen-overlay: ignored activity type=%1 watched=%2 watchedIsWidget=%3 hiddenCursor=%4 overlayVisible=%5 cursor=%6,%7 fullscreenGeom=%8,%9 %10x%11")
                              .arg(eventTypeName(event->type()),
                                   describeObjectForLog(watched),
                                   watchedWidget != nullptr ? QStringLiteral("true") : QStringLiteral("false"),
                                   fullscreenCursorHidden_ ? QStringLiteral("true") : QStringLiteral("false"),
                                   overlayHidden ? QStringLiteral("false") : QStringLiteral("true"))
                              .arg(QCursor::pos().x())
                              .arg(QCursor::pos().y())
                              .arg(fullscreenGeometry.x())
                              .arg(fullscreenGeometry.y())
                              .arg(fullscreenGeometry.width())
                              .arg(fullscreenGeometry.height()));
            }
        }
    }

    if (fullscreenActive_ && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            exitFullscreen();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void MainWindow::stopProcess(QProcess *process, int timeoutMs)
{
    if (process == nullptr || process->state() == QProcess::NotRunning) {
        return;
    }

    process->terminate();
    if (!process->waitForFinished(timeoutMs)) {
        process->kill();
        process->waitForFinished(timeoutMs);
    }
    process->close();
}

QWidget *MainWindow::modalDialogParent() const
{
    if (fullscreenActive_ && fullscreenWindow_ != nullptr) {
        return fullscreenWindow_;
    }
    return const_cast<MainWindow *>(this);
}

void MainWindow::prepareModalWindow(QWidget *window, const QString &reason)
{
    if (window == nullptr || !fullscreenActive_ || fullscreenWindow_ == nullptr) {
        return;
    }

    if (!reason.trimmed().isEmpty()) {
        appendLog(QString("fullscreen: presenting modal UI: %1").arg(reason));
    }

    showFullscreenCursor();
    if (fullscreenCursorHideTimer_ != nullptr) {
        fullscreenCursorHideTimer_->stop();
    }
    syncFullscreenOverlayState();
    positionFullscreenOverlay();
    if (fullscreenOverlayContainer_ != nullptr) {
        fullscreenOverlayContainer_->show();
        fullscreenOverlayContainer_->raise();
    }

    fullscreenWindow_->raise();
    fullscreenWindow_->activateWindow();
    window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window->winId();
    if (window->windowHandle() != nullptr && fullscreenWindow_->windowHandle() != nullptr) {
        window->windowHandle()->setTransientParent(fullscreenWindow_->windowHandle());
        if (QScreen *screen = fullscreenWindow_->windowHandle()->screen()) {
            window->windowHandle()->setScreen(screen);
        }
    }
}

void MainWindow::restoreAfterModalWindow()
{
    if (!fullscreenActive_) {
        return;
    }

    showFullscreenCursor();
    restartFullscreenCursorHideTimer();
    if (fullscreenWindow_ != nullptr) {
        fullscreenWindow_->raise();
        fullscreenWindow_->activateWindow();
    }
    if (fullscreenVideoWidget_ != nullptr) {
        fullscreenVideoWidget_->setFocus(Qt::ActiveWindowFocusReason);
    }
}

int MainWindow::execModalDialog(QDialog *dialog, const QString &reason)
{
    if (dialog == nullptr) {
        return QDialog::Rejected;
    }

    prepareModalWindow(dialog, reason);
    const int result = dialog->exec();
    restoreAfterModalWindow();
    return result;
}

void MainWindow::showAboutDialog(const QString &title, const QString &text)
{
    QMessageBox dialog(QMessageBox::Information, title, text, QMessageBox::Ok, modalDialogParent());
    dialog.setTextFormat(Qt::PlainText);
    execModalDialog(&dialog, title);
}

void MainWindow::showInformationDialog(const QString &title, const QString &text)
{
    QMessageBox dialog(QMessageBox::Information, title, text, QMessageBox::Ok, modalDialogParent());
    dialog.setTextFormat(Qt::PlainText);
    execModalDialog(&dialog, title);
}

void MainWindow::showWarningDialog(const QString &title, const QString &text)
{
    QMessageBox dialog(QMessageBox::Warning, title, text, QMessageBox::Ok, modalDialogParent());
    dialog.setTextFormat(Qt::PlainText);
    execModalDialog(&dialog, title);
}

void MainWindow::showCriticalDialog(const QString &title, const QString &text)
{
    QMessageBox dialog(QMessageBox::Critical, title, text, QMessageBox::Ok, modalDialogParent());
    dialog.setTextFormat(Qt::PlainText);
    execModalDialog(&dialog, title);
}

QString MainWindow::promptItemSelection(const QString &title,
                                        const QString &label,
                                        const QStringList &items,
                                        int current,
                                        bool editable)
{
    QInputDialog dialog(modalDialogParent());
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setComboBoxItems(items);
    dialog.setComboBoxEditable(editable);
    if (current >= 0 && current < items.size()) {
        dialog.setTextValue(items.at(current));
    }
    if (execModalDialog(&dialog, title) != QDialog::Accepted) {
        return {};
    }
    return dialog.textValue().trimmed();
}

void MainWindow::buildUi()
{
    setWindowTitle("Voncloft TV Tuner");
    resize(1320, 840);

    QMenu *fileMenu = menuBar()->addMenu("File");
    QAction *aboutAction = fileMenu->addAction("About");
    aboutAction->setShortcut(QKeySequence(Qt::Key_F1));
    aboutAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        showAboutDialog("About",
                        QString("Created by Voncloft\nVersion %1")
                            .arg(QStringLiteral(TV_TUNER_GUI_VERSION)));
    });

    auto *root = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(root);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    tabs_ = new QTabWidget(root);
    tabs_->setTabPosition(QTabWidget::North);
    tabs_->setDocumentMode(true);
    tabs_->setStyleSheet("QTabWidget::pane { border: 0; }");

    watchPage_ = new QWidget(tabs_);
    auto *watchLayout = new QVBoxLayout(watchPage_);
    watchLayout->setContentsMargins(0, 0, 0, 0);
    watchLayout->setSpacing(6);

    auto *tuningPage = new QWidget(tabs_);
    auto *tuningLayout = new QVBoxLayout(tuningPage);

    configPage_ = new QWidget(tabs_);
    auto *configLayout = new QVBoxLayout(configPage_);

    auto *metaManagementPage = new QWidget(tabs_);
    auto *metaManagementLayout = new QVBoxLayout(metaManagementPage);

    auto *logsPage = new QWidget(tabs_);
    auto *logsLayout = new QVBoxLayout(logsPage);

    auto *testingBugsPage = new QWidget(tabs_);
    auto *testingBugsLayout = new QVBoxLayout(testingBugsPage);

    auto *scanGroup = new QGroupBox("Scan Settings", tuningPage);
    auto *form = new QFormLayout(scanGroup);

    frontendTypeCombo_ = new QComboBox(scanGroup);
    frontendTypeCombo_->addItem("Terrestrial (DVB-T/T2)", "t");
    frontendTypeCombo_->addItem("Terrestrial DVB-T only", "t1");
    frontendTypeCombo_->addItem("Terrestrial DVB-T2 only", "t2");
    frontendTypeCombo_->addItem("ATSC", "a");
    frontendTypeCombo_->addItem("Cable", "c");
    frontendTypeCombo_->setCurrentIndex(0);

    countryEdit_ = new QLineEdit(scanGroup);
    countryEdit_->setText("US");
    countryEdit_->setMaxLength(2);
    countryEdit_->setPlaceholderText("US");

    adapterSpin_ = new QSpinBox(scanGroup);
    adapterSpin_->setRange(0, 32);
    adapterSpin_->setValue(0);

    frontendSpin_ = new QSpinBox(scanGroup);
    frontendSpin_->setRange(0, 32);
    frontendSpin_->setValue(0);

    outputFormatCombo_ = new QComboBox(scanGroup);
    outputFormatCombo_->addItem("xine/tzap/czap", "X");
    outputFormatCombo_->addItem("VLC playlist", "L");
    outputFormatCombo_->addItem("MPlayer", "M");
    outputFormatCombo_->addItem("DVBv5", "5");
    outputFormatCombo_->addItem("XML", "Z");
    outputFormatCombo_->setCurrentIndex(0);

    form->addRow("Frontend type:", frontendTypeCombo_);
    form->addRow("Country code:", countryEdit_);
    form->addRow("Adapter:", adapterSpin_);
    form->addRow("Frontend:", frontendSpin_);
    form->addRow("Output format:", outputFormatCombo_);

    auto *scanActionsRow = new QHBoxLayout();
    startButton_ = new QPushButton("Start Scan", tuningPage);
    stopButton_ = new QPushButton("Stop Scan", tuningPage);
    scanActionsRow->addWidget(startButton_);
    scanActionsRow->addWidget(stopButton_);
    scanActionsRow->addStretch(1);
    stopButton_->setEnabled(false);

    tuningLayout->addWidget(scanGroup);
    tuningLayout->addLayout(scanActionsRow);
    tuningLayout->addStretch(1);

    auto *guideOptionsGroup = new QGroupBox("TV Guide", configPage_);
    auto *guideOptionsLayout = new QVBoxLayout(guideOptionsGroup);
    useSchedulesDirectGuideCheckBox_ = new QCheckBox("Use Schedules Direct OTA data for guide refreshes", guideOptionsGroup);
    hideNoEitChannelsCheckBox_ = new QCheckBox("Hide channels without EIT data", guideOptionsGroup);
    showFavoritesOnlyCheckBox_ = new QCheckBox("Show only favorites in TV Guide", guideOptionsGroup);
    obeyScheduledSwitchesCheckBox_ = new QCheckBox("Obey scheduled tuner switches", guideOptionsGroup);
    autoFavoriteShowSchedulingCheckBox_ = new QCheckBox("Automatically schedule favorite show matches", guideOptionsGroup);
    favoriteShowRatingsOverrideCheckBox_ =
        new QCheckBox("Override conflicts with favorite show priority ratings", guideOptionsGroup);
    guideOptionsLayout->addWidget(useSchedulesDirectGuideCheckBox_);
    guideOptionsLayout->addWidget(hideNoEitChannelsCheckBox_);
    guideOptionsLayout->addWidget(showFavoritesOnlyCheckBox_);
    guideOptionsLayout->addWidget(obeyScheduledSwitchesCheckBox_);
    guideOptionsLayout->addWidget(autoFavoriteShowSchedulingCheckBox_);
    guideOptionsLayout->addWidget(favoriteShowRatingsOverrideCheckBox_);

    auto *playbackOptionsGroup = new QGroupBox("Playback", configPage_);
    auto *playbackOptionsLayout = new QVBoxLayout(playbackOptionsGroup);
    autoPictureInPictureCheckBox_ = new QCheckBox("Pop video out when leaving the Video tab", playbackOptionsGroup);
    hideStartupSwitchSummaryCheckBox_ =
        new QCheckBox("Hide the scheduled switches summary at startup", playbackOptionsGroup);
    playbackOptionsLayout->addWidget(autoPictureInPictureCheckBox_);
    playbackOptionsLayout->addWidget(hideStartupSwitchSummaryCheckBox_);

    auto *cacheOptionsGroup = new QGroupBox("Guide Cache", configPage_);
    auto *cacheOptionsForm = new QFormLayout(cacheOptionsGroup);
    guideRefreshIntervalSpin_ = new QSpinBox(cacheOptionsGroup);
    guideRefreshIntervalSpin_->setRange(5, 1440);
    guideRefreshIntervalSpin_->setSuffix(" min");
    guideCacheRetentionSpin_ = new QSpinBox(cacheOptionsGroup);
    guideCacheRetentionSpin_->setRange(0, 168);
    guideCacheRetentionSpin_->setSpecialValueText("Never");
    guideCacheRetentionSpin_->setSuffix(" hr");
    cacheOptionsForm->addRow("Refresh guide JSON every:", guideRefreshIntervalSpin_);
    cacheOptionsForm->addRow("Delete guide cache after:", guideCacheRetentionSpin_);

    auto *schedulesDirectGroup = new QGroupBox("Schedules Direct", configPage_);
    auto *schedulesDirectLayout = new QVBoxLayout(schedulesDirectGroup);
    auto *schedulesDirectForm = new QFormLayout();
    schedulesDirectUsernameEdit_ = new QLineEdit(schedulesDirectGroup);
    schedulesDirectUsernameEdit_->setPlaceholderText("voncloft");
    schedulesDirectPasswordEdit_ = new QLineEdit(schedulesDirectGroup);
    schedulesDirectPasswordEdit_->setEchoMode(QLineEdit::Password);
    schedulesDirectPasswordEdit_->setPlaceholderText("Saved as SHA1 for background refreshes");
    schedulesDirectPostalCodeEdit_ = new QLineEdit(schedulesDirectGroup);
    schedulesDirectPostalCodeEdit_->setPlaceholderText("46825");
    schedulesDirectPostalCodeEdit_->setMaxLength(16);
    schedulesDirectForm->addRow("Username:", schedulesDirectUsernameEdit_);
    schedulesDirectForm->addRow("Password:", schedulesDirectPasswordEdit_);
    schedulesDirectForm->addRow("ZIP / Postal code:", schedulesDirectPostalCodeEdit_);
    exportSchedulesDirectButton_ = new QPushButton("Download schedules_direct.org.json", schedulesDirectGroup);
    exportSchedulesDirectButton_->setEnabled(false);
    schedulesDirectStatusLabel_ = new QLabel("Downloads OTA lineup, schedule, and program data to a JSON file in the app data folder. When the guide checkbox above is enabled, background guide refreshes use this source instead of live EIT.", schedulesDirectGroup);
    schedulesDirectStatusLabel_->setWordWrap(true);
    schedulesDirectStatusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    schedulesDirectLayout->addLayout(schedulesDirectForm);
    schedulesDirectLayout->addWidget(exportSchedulesDirectButton_, 0);
    schedulesDirectLayout->addWidget(schedulesDirectStatusLabel_, 0);

    auto *favoriteShowsGroup = new QGroupBox("Favorite Shows", metaManagementPage);
    auto *favoriteShowsLayout = new QVBoxLayout(favoriteShowsGroup);
    auto *favoriteShowsAddRow = new QHBoxLayout();
    favoriteShowRuleEdit_ = new QLineEdit(favoriteShowsGroup);
    favoriteShowRuleEdit_->setPlaceholderText("Add favorite show name");
    addFavoriteShowRuleButton_ = new QPushButton("Add Favorite Show", favoriteShowsGroup);
    addFavoriteShowRuleButton_->setEnabled(false);
    favoriteShowsAddRow->addWidget(favoriteShowRuleEdit_, 1);
    favoriteShowsAddRow->addWidget(addFavoriteShowRuleButton_, 0);
    favoriteShowRulesList_ = new QListWidget(favoriteShowsGroup);
    favoriteShowRulesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    favoriteShowRulesList_->setAlternatingRowColors(true);
    auto *favoriteShowRatingRow = new QHBoxLayout();
    auto *favoriteShowRatingLabel = new QLabel("Priority rating:", favoriteShowsGroup);
    favoriteShowRatingSpin_ = new QSpinBox(favoriteShowsGroup);
    favoriteShowRatingSpin_->setRange(kDefaultFavoriteShowRating, kMaxFavoriteShowRating);
    favoriteShowRatingSpin_->setEnabled(false);
    auto *favoriteShowRatingHelpLabel =
        new QLabel("1 = don't care, 5 = keep at all costs", favoriteShowsGroup);
    favoriteShowRatingHelpLabel->setWordWrap(true);
    favoriteShowRatingRow->addWidget(favoriteShowRatingLabel, 0);
    favoriteShowRatingRow->addWidget(favoriteShowRatingSpin_, 0);
    favoriteShowRatingRow->addWidget(favoriteShowRatingHelpLabel, 1);
    removeFavoriteShowRuleButton_ = new QPushButton("Remove Selected Favorite Show", favoriteShowsGroup);
    removeFavoriteShowRuleButton_->setEnabled(false);
    favoriteShowsLayout->addLayout(favoriteShowsAddRow);
    favoriteShowsLayout->addWidget(favoriteShowRulesList_, 1);
    favoriteShowsLayout->addLayout(favoriteShowRatingRow);
    favoriteShowsLayout->addWidget(removeFavoriteShowRuleButton_, 0);

    auto *scheduledSwitchesGroup = new QGroupBox("Scheduled Switches", metaManagementPage);
    auto *scheduledSwitchesLayout = new QVBoxLayout(scheduledSwitchesGroup);
    scheduledSwitchesList_ = new QListWidget(scheduledSwitchesGroup);
    scheduledSwitchesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    scheduledSwitchesList_->setAlternatingRowColors(true);
    removeScheduledSwitchButton_ = new QPushButton("Remove Selected Switch", scheduledSwitchesGroup);
    removeScheduledSwitchButton_->setEnabled(false);
    scheduledSwitchesLayout->addWidget(scheduledSwitchesList_, 1);
    scheduledSwitchesLayout->addWidget(removeScheduledSwitchButton_, 0);

    configLayout->addWidget(guideOptionsGroup);
    configLayout->addWidget(playbackOptionsGroup);
    configLayout->addWidget(cacheOptionsGroup);
    configLayout->addWidget(schedulesDirectGroup);
    configLayout->addStretch(1);

    metaManagementLayout->addWidget(favoriteShowsGroup, 1);
    metaManagementLayout->addWidget(scheduledSwitchesGroup, 1);

    auto *testingBugsGroup = new QGroupBox("Testing / Bugs", testingBugsPage);
    auto *testingBugsGroupLayout = new QVBoxLayout(testingBugsGroup);
    auto *testingBugsEntryRow = new QHBoxLayout();
    testingBugItemEdit_ = new QLineEdit(testingBugsGroup);
    testingBugItemEdit_->setPlaceholderText("Add something to test or watch for");
    saveTestingBugItemButton_ = new QPushButton("Save Item", testingBugsGroup);
    saveTestingBugItemButton_->setEnabled(false);
    testingBugsEntryRow->addWidget(testingBugItemEdit_, 1);
    testingBugsEntryRow->addWidget(saveTestingBugItemButton_, 0);
    testingBugItemsList_ = new QListWidget(testingBugsGroup);
    testingBugItemsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    testingBugItemsList_->setAlternatingRowColors(true);
    removeTestingBugItemButton_ = new QPushButton("Delete Selected Item", testingBugsGroup);
    removeTestingBugItemButton_->setEnabled(false);
    testingBugsGroupLayout->addLayout(testingBugsEntryRow);
    testingBugsGroupLayout->addWidget(testingBugItemsList_, 1);
    testingBugsGroupLayout->addWidget(removeTestingBugItemButton_, 0);
    testingBugsLayout->addWidget(testingBugsGroup, 1);

    watchControlsContainer_ = new QWidget(watchPage_);
    auto *watchControlsRow = new QHBoxLayout(watchControlsContainer_);
    watchControlsRow->setContentsMargins(0, 0, 0, 0);
    watchControlsRow->setSpacing(8);
    watchButton_ = new QPushButton("Watch Selected", watchPage_);
    stopWatchButton_ = new QPushButton("Stop Watching", watchPage_);
    openFileButton_ = new QPushButton("Open File", watchPage_);
    fullscreenButton_ = new QPushButton("Fullscreen", watchPage_);
    muteButton_ = new QPushButton("Mute", watchPage_);
    volumeSlider_ = new QSlider(Qt::Horizontal, watchPage_);
    playbackStatusLabel_ = new QLabel("Idle", watchPage_);
    signalMonitorLabel_ = new QLabel("Signal: n/a", watchPage_);
    currentShowLabel_ = new QLabel("NO EIT DATA", watchPage_);
    currentShowSynopsisLabel_ = new QLabel(watchPage_);
    addFavoriteButton_ = new QPushButton("Add Favorite", watchPage_);
    removeFavoriteButton_ = new QPushButton("Remove Favorite", watchPage_);

    stopButton_->setEnabled(false);
    stopWatchButton_->setEnabled(false);
    muteButton_->setCheckable(true);
    const auto setStableButtonWidth = [](QPushButton *button, const QStringList &labels) {
        if (button == nullptr) {
            return;
        }

        const QFontMetrics metrics(button->font());
        int width = button->sizeHint().width();
        for (const QString &label : labels) {
            width = std::max(width, metrics.horizontalAdvance(label) + 24);
        }
        button->setFixedWidth(width);
    };
    setStableButtonWidth(watchButton_, {"Watch Selected"});
    setStableButtonWidth(stopWatchButton_, {"Stop Watching"});
    setStableButtonWidth(openFileButton_, {"Open File"});
    setStableButtonWidth(fullscreenButton_, {"Fullscreen", "Exit Fullscreen"});
    setStableButtonWidth(muteButton_, {"Mute", "Unmute"});
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(85);
    volumeSlider_->setMinimumWidth(120);
    volumeSlider_->setMaximumWidth(220);
    playbackStatusLabel_->setMinimumWidth(100);
    playbackStatusLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    signalMonitorLabel_->setMinimumWidth(150);
    signalMonitorLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    signalMonitorLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentShowLabel_->setMinimumWidth(220);
    currentShowLabel_->setWordWrap(true);
    currentShowLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    currentShowLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    currentShowLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentShowSynopsisLabel_->setWordWrap(true);
    currentShowSynopsisLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    currentShowSynopsisLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentShowSynopsisLabel_->setVisible(false);
    currentShowSynopsisLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    currentShowSynopsisLabel_->setStyleSheet("QLabel { color: #d4d4d4; }");

    watchControlsRow->addWidget(watchButton_);
    watchControlsRow->addWidget(stopWatchButton_);
    watchControlsRow->addWidget(openFileButton_);
    watchControlsRow->addWidget(fullscreenButton_);
    watchControlsRow->addSpacing(12);
    watchControlsRow->addWidget(new QLabel("Volume:", watchPage_));
    watchControlsRow->addWidget(volumeSlider_);
    watchControlsRow->addWidget(muteButton_);
    watchControlsRow->addStretch(1);

    contentSplitter_ = new QSplitter(Qt::Horizontal, watchPage_);
    videoWidget_ = new QVideoWidget(contentSplitter_);
    videoWidget_->setMinimumHeight(240);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget_->setStyleSheet("background: #000;");
    videoDetachedPlaceholderLabel_ = new QLabel("Video is playing in the floating mini-player.", watchPage_);
    videoDetachedPlaceholderLabel_->setAlignment(Qt::AlignCenter);
    videoDetachedPlaceholderLabel_->setWordWrap(true);
    videoDetachedPlaceholderLabel_->setStyleSheet("QLabel { background: #000000; color: #b8b8b8; padding: 24px; }");
    videoDetachedPlaceholderLabel_->hide();

    channelsTable_ = new QTableWidget(contentSplitter_);
    channelsTable_->setColumnCount(3);
    channelsTable_->setHorizontalHeaderLabels({"Number", "Channel", "Raw line"});
    channelsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    channelsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    channelsTable_->setColumnHidden(2, true);
    channelsTable_->setAlternatingRowColors(true);
    channelsTable_->setShowGrid(false);
    channelsTable_->verticalHeader()->setVisible(false);
    channelsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    channelsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelsTable_->setMinimumWidth(260);
    channelsTable_->setSortingEnabled(true);
    channelsTable_->sortByColumn(0, Qt::AscendingOrder);
    contentSplitter_->setChildrenCollapsible(false);
    contentSplitter_->setStretchFactor(0, 6);
    contentSplitter_->setStretchFactor(1, 2);

    favoritesContainer_ = new QWidget(watchPage_);
    auto *favoritesLayout = new QVBoxLayout(favoritesContainer_);
    favoritesLayout->setContentsMargins(0, 0, 0, 0);
    favoritesLayout->setSpacing(6);
    auto *favoritesControlsRow = new QHBoxLayout();
    favoritesControlsRow->setSpacing(8);
    favoritesControlsRow->addWidget(addFavoriteButton_);
    favoritesControlsRow->addWidget(removeFavoriteButton_);
    favoritesControlsRow->addStretch(1);
    auto *favoritesLabel = new QLabel("Favorites:", watchPage_);

    auto *favoritesButtonsGrid = new QGridLayout();
    favoritesButtonsGrid->setContentsMargins(0, 0, 0, 0);
    favoritesButtonsGrid->setHorizontalSpacing(8);
    favoritesButtonsGrid->setVerticalSpacing(6);
    for (int i = 0; i < kQuickFavoriteCount; ++i) {
        quickFavoriteButtons_[i] = new QPushButton(QString::number(i + 1), watchPage_);
        quickFavoriteButtons_[i]->setEnabled(false);
        quickFavoriteButtons_[i]->setMinimumWidth(150);
        quickFavoriteButtons_[i]->setMinimumHeight(32);
        quickFavoriteButtons_[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        favoritesButtonsGrid->addWidget(quickFavoriteButtons_[i], i / 5, i % 5);
        connect(quickFavoriteButtons_[i], &QPushButton::clicked, this, &MainWindow::triggerQuickFavorite);
    }

    statusContainer_ = new QWidget(watchPage_);
    auto *statusRow = new QHBoxLayout(statusContainer_);
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(12);
    statusRow->addWidget(playbackStatusLabel_, 0, Qt::AlignTop);
    statusRow->addWidget(signalMonitorLabel_, 0, Qt::AlignTop);
    statusRow->addWidget(currentShowSynopsisLabel_, 6);
    statusRow->addWidget(currentShowLabel_, 4);

    favoritesLayout->addLayout(favoritesControlsRow);
    favoritesLayout->addWidget(favoritesLabel);
    favoritesLayout->addLayout(favoritesButtonsGrid);

    watchLayout->addWidget(watchControlsContainer_);
    watchLayout->addWidget(contentSplitter_, 1);
    watchLayout->addWidget(videoDetachedPlaceholderLabel_);
    watchLayout->addWidget(favoritesContainer_);
    watchLayout->addWidget(statusContainer_);

    pipWindow_ = new QWidget(this, Qt::Tool | Qt::WindowStaysOnTopHint);
    pipWindow_->setWindowTitle("Voncloft TV Mini Player");
    pipWindow_->resize(480, 270);
    pipWindow_->setMinimumSize(320, 180);
    pipWindow_->setStyleSheet("background: #000;");
    pipWindowLayout_ = new QVBoxLayout(pipWindow_);
    pipWindowLayout_->setContentsMargins(0, 0, 0, 0);
    pipWindowLayout_->setSpacing(0);
    pipVideoWidget_ = new QVideoWidget(pipWindow_);
    pipVideoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    pipVideoWidget_->setStyleSheet("background: #000;");
    pipWindowLayout_->addWidget(pipVideoWidget_, 1);
    pipWindow_->hide();

    logOutput_ = new QPlainTextEdit(logsPage);
    logOutput_->setReadOnly(true);
    logOutput_->setMaximumBlockCount(4000);
    logOutput_->setPlaceholderText("w_scan2 and tuning output will appear here...");
    logsLayout->addWidget(logOutput_);

    tvGuideDialog_ = new TvGuideDialog(tabs_);
    tabs_->addTab(watchPage_, "Video");
    tabs_->addTab(tvGuideDialog_, "TV Guide");
    tabs_->addTab(metaManagementPage, "Meta Management");
    tabs_->addTab(tuningPage, "Tuning");
    tabs_->addTab(configPage_, "Config");
    tabs_->addTab(testingBugsPage, "Testing/bugs");
    tabs_->addTab(logsPage, "Logs");
    mainLayout->addWidget(tabs_, 1);
    setCentralWidget(root);

    fullscreenWindow_ = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
    fullscreenWindow_->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    fullscreenWindow_->setStyleSheet("background: #000;");
    fullscreenWindow_->setMouseTracking(true);
    auto *fullscreenLayout = new QGridLayout(fullscreenWindow_);
    fullscreenLayout->setContentsMargins(0, 0, 0, 0);
    fullscreenLayout->setSpacing(0);
    fullscreenVideoWidget_ = new QVideoWidget(fullscreenWindow_);
    fullscreenVideoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fullscreenVideoWidget_->setStyleSheet("background: #000;");
    fullscreenVideoWidget_->setAspectRatioMode(Qt::IgnoreAspectRatio);
    fullscreenVideoWidget_->setMouseTracking(true);
    fullscreenLayout->addWidget(fullscreenVideoWidget_, 0, 0);

    fullscreenOverlayContainer_ =
        new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    fullscreenOverlayContainer_->setObjectName("fullscreenOverlayContainer");
    fullscreenOverlayContainer_->setAttribute(Qt::WA_StyledBackground, true);
    fullscreenOverlayContainer_->setAttribute(Qt::WA_TranslucentBackground, true);
    fullscreenOverlayContainer_->setAttribute(Qt::WA_ShowWithoutActivating, true);
    fullscreenOverlayContainer_->setMouseTracking(true);
    fullscreenOverlayContainer_->setStyleSheet(
        "QWidget#fullscreenOverlayContainer { background: rgba(110, 110, 110, 179); }"
        "QPushButton { min-height: 34px; }"
        "QLabel { color: #f3f3f3; }");
    auto *fullscreenOverlayLayout = new QVBoxLayout(fullscreenOverlayContainer_);
    fullscreenOverlayLayout->setContentsMargins(18, 14, 18, 14);
    fullscreenOverlayLayout->setSpacing(10);

    auto *fullscreenControlsRow = new QHBoxLayout();
    fullscreenControlsRow->setContentsMargins(0, 0, 0, 0);
    fullscreenControlsRow->setSpacing(8);
    fullscreenWatchButton_ = new QPushButton("Watch Selected", fullscreenOverlayContainer_);
    fullscreenStopWatchButton_ = new QPushButton("Stop Watching", fullscreenOverlayContainer_);
    fullscreenMuteButton_ = new QPushButton("Mute", fullscreenOverlayContainer_);
    fullscreenMuteButton_->setCheckable(true);
    fullscreenVolumeSlider_ = new QSlider(Qt::Horizontal, fullscreenOverlayContainer_);
    fullscreenVolumeSlider_->setRange(0, 100);
    fullscreenVolumeSlider_->setMinimumWidth(160);
    fullscreenVolumeSlider_->setMaximumWidth(260);
    fullscreenPlaybackStatusLabel_ = new QLabel("Idle", fullscreenOverlayContainer_);
    fullscreenSignalMonitorLabel_ = new QLabel("Signal: n/a", fullscreenOverlayContainer_);
    fullscreenPlaybackStatusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fullscreenPlaybackStatusLabel_->setMinimumWidth(220);
    fullscreenPlaybackStatusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fullscreenSignalMonitorLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fullscreenSignalMonitorLabel_->setMinimumWidth(180);
    fullscreenSignalMonitorLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fullscreenControlsRow->addWidget(fullscreenWatchButton_);
    fullscreenControlsRow->addWidget(fullscreenStopWatchButton_);
    fullscreenControlsRow->addSpacing(12);
    fullscreenControlsRow->addWidget(new QLabel("Volume:", fullscreenOverlayContainer_));
    fullscreenControlsRow->addWidget(fullscreenVolumeSlider_);
    fullscreenControlsRow->addWidget(fullscreenMuteButton_);
    fullscreenControlsRow->addStretch(1);
    fullscreenControlsRow->addWidget(fullscreenSignalMonitorLabel_, 0, Qt::AlignRight);
    fullscreenControlsRow->addWidget(fullscreenPlaybackStatusLabel_, 0, Qt::AlignRight);

    auto *fullscreenInfoRow = new QHBoxLayout();
    fullscreenInfoRow->setContentsMargins(0, 0, 0, 0);
    fullscreenInfoRow->setSpacing(14);
    fullscreenCurrentShowSynopsisLabel_ = new QLabel(fullscreenOverlayContainer_);
    fullscreenCurrentShowSynopsisLabel_->setWordWrap(true);
    fullscreenCurrentShowSynopsisLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    fullscreenCurrentShowSynopsisLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fullscreenCurrentShowSynopsisLabel_->setStyleSheet("QLabel { color: #d7d7d7; }");
    fullscreenCurrentShowLabel_ = new QLabel("NO EIT DATA", fullscreenOverlayContainer_);
    fullscreenCurrentShowLabel_->setWordWrap(true);
    fullscreenCurrentShowLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    fullscreenCurrentShowLabel_->setMinimumWidth(340);
    fullscreenCurrentShowLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fullscreenInfoRow->addWidget(fullscreenCurrentShowSynopsisLabel_, 6);
    fullscreenInfoRow->addWidget(fullscreenCurrentShowLabel_, 4);

    fullscreenOverlayLayout->addLayout(fullscreenControlsRow);
    fullscreenOverlayLayout->addLayout(fullscreenInfoRow);
    fullscreenOverlayContainer_->hide();
    fullscreenWindow_->hide();

    videoWidget_->installEventFilter(this);
    pipVideoWidget_->installEventFilter(this);
    fullscreenWindow_->installEventFilter(this);
    fullscreenVideoWidget_->installEventFilter(this);
    fullscreenOverlayContainer_->installEventFilter(this);

    QTimer::singleShot(0, this, [this]() {
        if (fullscreenWindow_ == nullptr) {
            appendLog(QStringLiteral("fullscreen: setup skipped because fullscreenWindow_ is null."));
            return;
        }

        fullscreenWindow_->winId();
        if (windowHandle() == nullptr || fullscreenWindow_->windowHandle() == nullptr) {
            appendLog(QStringLiteral("fullscreen: setup deferred because handles are not ready. main=%1 fullscreen=%2")
                          .arg(windowHandle() != nullptr ? QStringLiteral("ready") : QStringLiteral("null"),
                               fullscreenWindow_->windowHandle() != nullptr ? QStringLiteral("ready")
                                                                            : QStringLiteral("null")));
            return;
        }

        fullscreenWindow_->windowHandle()->setTransientParent(windowHandle());
        fullscreenOverlayContainer_->winId();
        if (fullscreenOverlayContainer_->windowHandle() != nullptr) {
            fullscreenOverlayContainer_->windowHandle()->setTransientParent(fullscreenWindow_->windowHandle());
            if (QScreen *screen = fullscreenWindow_->windowHandle()->screen()) {
                fullscreenOverlayContainer_->windowHandle()->setScreen(screen);
            }
        }
        appendLog(QStringLiteral("fullscreen: setup prepared fullscreen window handle. main-window-screen=%1 fullscreen-screen=%2")
                      .arg(describeScreen(windowHandle()->screen()),
                           describeScreen(fullscreenWindow_->windowHandle()->screen())));
        connect(fullscreenWindow_->windowHandle(), &QWindow::screenChanged, this, [this](QScreen *screen) {
            appendLog(QStringLiteral("fullscreen: fullscreen window screen changed to %1")
                          .arg(describeScreen(screen)));
            if (screen != nullptr && fullscreenOverlayContainer_ != nullptr
                && fullscreenOverlayContainer_->windowHandle() != nullptr) {
                fullscreenOverlayContainer_->windowHandle()->setScreen(screen);
                if (fullscreenActive_) {
                    positionFullscreenOverlay();
                }
            }
        });
    });

    setStableButtonWidth(fullscreenWatchButton_, {"Watch Selected"});
    setStableButtonWidth(fullscreenStopWatchButton_, {"Stop Watching"});
    setStableButtonWidth(fullscreenMuteButton_, {"Mute", "Unmute"});
    connect(fullscreenWatchButton_, &QPushButton::clicked, this, &MainWindow::watchSelectedChannel);
    connect(fullscreenStopWatchButton_, &QPushButton::clicked, this, &MainWindow::stopWatching);
    connect(fullscreenMuteButton_, &QPushButton::toggled, this, [this](bool checked) {
        if (muteButton_ == nullptr) {
            return;
        }
        if (muteButton_->isChecked() != checked) {
            muteButton_->setChecked(checked);
        } else {
            handleMuteToggled(checked);
        }
    });
    connect(fullscreenVolumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (volumeSlider_ == nullptr) {
            return;
        }
        if (volumeSlider_->value() != value) {
            volumeSlider_->setValue(value);
        } else {
            handleVolumeChanged(value);
        }
    });

    setStatusBarStateMessage(QString());
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &message) {
        if (!message.trimmed().isEmpty()) {
            return;
        }
        QTimer::singleShot(0, this, [this]() {
            if (statusBar() == nullptr || !statusBar()->currentMessage().trimmed().isEmpty()) {
                return;
            }
            const QString fallback = lastStatusBarMessage_.trimmed();
            setStatusBarStateMessage(fallback);
        });
    });

    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopScan);
    connect(watchButton_, &QPushButton::clicked, this, &MainWindow::watchSelectedChannel);
    connect(stopWatchButton_, &QPushButton::clicked, this, &MainWindow::stopWatching);
    connect(openFileButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
    connect(addFavoriteButton_, &QPushButton::clicked, this, &MainWindow::addSelectedFavorite);
    connect(removeFavoriteButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedFavorite);
    connect(tvGuideDialog_, &TvGuideDialog::refreshRequested, this, &MainWindow::refreshTvGuide);
    connect(tvGuideDialog_, &TvGuideDialog::scheduleSwitchRequested, this, &MainWindow::handleGuideScheduleToggle);
    connect(tvGuideDialog_, &TvGuideDialog::searchScheduleRequested, this, &MainWindow::handleSearchScheduleRequested);
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::handleCurrentTabChanged);
    connect(hideNoEitChannelsCheckBox_, &QCheckBox::toggled, this, &MainWindow::handleGuideHideNoEitToggled);
    connect(showFavoritesOnlyCheckBox_, &QCheckBox::toggled, this, &MainWindow::handleGuideShowFavoritesOnlyToggled);
    connect(obeyScheduledSwitchesCheckBox_, &QCheckBox::toggled, this, &MainWindow::handleObeyScheduledSwitchesChanged);
    connect(autoFavoriteShowSchedulingCheckBox_,
            &QCheckBox::toggled,
            this,
            &MainWindow::handleAutoFavoriteShowSchedulingToggled);
    connect(favoriteShowRatingsOverrideCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        favoriteShowRatingsOverrideEnabled_ = checked;
        QSettings settings("tv_tuner_gui", "watcher");
        settings.setValue(kFavoriteShowRatingsOverrideSetting, favoriteShowRatingsOverrideEnabled_);
    });
    connect(autoPictureInPictureCheckBox_, &QCheckBox::toggled, this, &MainWindow::handleAutoPictureInPictureToggled);
    connect(hideStartupSwitchSummaryCheckBox_,
            &QCheckBox::toggled,
            this,
            &MainWindow::handleHideStartupSwitchSummaryToggled);
    connect(guideRefreshIntervalSpin_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &MainWindow::handleGuideRefreshIntervalChanged);
    connect(guideCacheRetentionSpin_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &MainWindow::handleGuideCacheRetentionChanged);
    connect(useSchedulesDirectGuideCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings("tv_tuner_gui", "watcher");
        settings.setValue(kUseSchedulesDirectGuideSetting, checked);
        updateSchedulesDirectControls();
        const bool dialogVisible = tvGuideDialog_ != nullptr && tvGuideDialog_->isVisible();
        if (refreshGuideData(false, dialogVisible)) {
            loadGuideCacheFile();
            applyCurrentShowStatusFromGuideCache();
        }
        if (!dialogVisible) {
            updateTvGuideDialogFromCurrentCache(false);
        }
    });
    connect(schedulesDirectUsernameEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        QSettings settings("tv_tuner_gui", "watcher");
        settings.setValue(kSchedulesDirectUsernameSetting, text.trimmed());
        updateSchedulesDirectControls();
    });
    connect(schedulesDirectPostalCodeEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        QSettings settings("tv_tuner_gui", "watcher");
        settings.setValue(kSchedulesDirectPostalCodeSetting, text.trimmed());
        updateSchedulesDirectControls();
    });
    connect(schedulesDirectPasswordEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        QSettings settings("tv_tuner_gui", "watcher");
        const QString trimmedText = text.trimmed();
        if (trimmedText.isEmpty()) {
            settings.remove(kSchedulesDirectPasswordSha1Setting);
        } else if (looksLikeSha1Hex(trimmedText)) {
            settings.setValue(kSchedulesDirectPasswordSha1Setting, trimmedText.toLower());
        } else {
            const QString passwordSha1 =
                QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
            settings.setValue(kSchedulesDirectPasswordSha1Setting, passwordSha1);
        }
        updateSchedulesDirectControls();
    });
    connect(exportSchedulesDirectButton_, &QPushButton::clicked, this, &MainWindow::exportSchedulesDirectJson);
    connect(channelsTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        watchSelectedChannel();
    });
    connect(favoriteShowRulesList_, &QListWidget::itemSelectionChanged, this, [this]() {
        if (removeFavoriteShowRuleButton_ != nullptr) {
            removeFavoriteShowRuleButton_->setEnabled(favoriteShowRulesList_ != nullptr
                                                      && favoriteShowRulesList_->currentRow() >= 0);
        }
        syncFavoriteShowRatingControls();
    });
    connect(favoriteShowRatingSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (favoriteShowRulesList_ == nullptr) {
            return;
        }
        const int row = favoriteShowRulesList_->currentRow();
        if (row < 0 || row >= favoriteShowRules_.size()) {
            return;
        }
        setFavoriteShowRating(favoriteShowRules_.at(row), value);
    });
    connect(favoriteShowRuleEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (addFavoriteShowRuleButton_ != nullptr) {
            addFavoriteShowRuleButton_->setEnabled(!text.simplified().isEmpty());
        }
    });
    const auto addManualFavoriteShowRule = [this]() {
        if (favoriteShowRuleEdit_ == nullptr) {
            return;
        }

        const QString showName = favoriteShowRuleEdit_->text().simplified();
        if (showName.isEmpty()) {
            return;
        }

        if (!addFavoriteShowRule(showName)) {
            showInformationDialog("Favorite show already saved",
                                  QString("%1 is already in your favorite show list.").arg(showName));
            return;
        }

        favoriteShowRuleEdit_->clear();
        if (autoFavoriteShowSchedulingEnabled_) {
            autoScheduleFavoriteShowsFromGuideCache(false, true);
        }
    };
    connect(addFavoriteShowRuleButton_, &QPushButton::clicked, this, addManualFavoriteShowRule);
    connect(favoriteShowRuleEdit_, &QLineEdit::returnPressed, this, addManualFavoriteShowRule);
    connect(scheduledSwitchesList_, &QListWidget::itemSelectionChanged, this, [this]() {
        if (removeScheduledSwitchButton_ != nullptr) {
            removeScheduledSwitchButton_->setEnabled(scheduledSwitchesList_ != nullptr
                                                     && scheduledSwitchesList_->currentRow() >= 0);
        }
    });
    connect(testingBugItemEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (saveTestingBugItemButton_ != nullptr) {
            saveTestingBugItemButton_->setEnabled(!text.simplified().isEmpty());
        }
    });
    connect(testingBugItemEdit_, &QLineEdit::returnPressed, this, &MainWindow::addTestingBugItem);
    connect(saveTestingBugItemButton_, &QPushButton::clicked, this, &MainWindow::addTestingBugItem);
    connect(testingBugItemsList_, &QListWidget::itemSelectionChanged, this, [this]() {
        if (removeTestingBugItemButton_ != nullptr) {
            removeTestingBugItemButton_->setEnabled(testingBugItemsList_ != nullptr
                                                    && testingBugItemsList_->currentRow() >= 0);
        }
    });
    connect(testingBugItemsList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
        saveTestingBugItems();
    });
    connect(removeTestingBugItemButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedTestingBugItem);
    connect(removeFavoriteShowRuleButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedFavoriteShowRule);
    connect(removeScheduledSwitchButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedScheduledSwitch);
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    updateSchedulesDirectControls();
}

void MainWindow::handleGuideHideNoEitToggled(bool checked)
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kHideNoEitChannelsSetting, checked);
    applyGuideFilterSettings();
}

void MainWindow::handleGuideShowFavoritesOnlyToggled(bool checked)
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kShowFavoritesOnlySetting, checked);
    applyGuideFilterSettings();
}

void MainWindow::handleAutoFavoriteShowSchedulingToggled(bool checked)
{
    autoFavoriteShowSchedulingEnabled_ = checked;
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kAutoFavoriteShowSchedulingSetting, autoFavoriteShowSchedulingEnabled_);
    if (autoFavoriteShowSchedulingCheckBox_ != nullptr
        && autoFavoriteShowSchedulingCheckBox_->isChecked() != autoFavoriteShowSchedulingEnabled_) {
        autoFavoriteShowSchedulingCheckBox_->setChecked(autoFavoriteShowSchedulingEnabled_);
    }
    if (autoFavoriteShowSchedulingEnabled_) {
        autoScheduleFavoriteShowsFromGuideCache(false, true);
    }
}

void MainWindow::handleObeyScheduledSwitchesChanged(bool obey)
{
    obeyScheduledSwitches_ = obey;
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kObeyScheduledSwitchesSetting, obeyScheduledSwitches_);
    if (obeyScheduledSwitchesCheckBox_ != nullptr && obeyScheduledSwitchesCheckBox_->isChecked() != obeyScheduledSwitches_) {
        obeyScheduledSwitchesCheckBox_->setChecked(obeyScheduledSwitches_);
    }
    updateTvGuideDialogFromCurrentCache(false);
    refreshScheduledSwitchTimer();
    if (obeyScheduledSwitches_) {
        processScheduledSwitches();
    }
}

void MainWindow::handleAutoPictureInPictureToggled(bool checked)
{
    autoPictureInPictureEnabled_ = checked;
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kAutoPictureInPictureSetting, autoPictureInPictureEnabled_);

    if (!autoPictureInPictureEnabled_) {
        attachVideoFromPip();
        if (pipWindow_ != nullptr) {
            pipWindow_->hide();
        }
        return;
    }

    if (tabs_ != nullptr) {
        handleCurrentTabChanged(tabs_->currentIndex());
    }
}

void MainWindow::handleHideStartupSwitchSummaryToggled(bool checked)
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kHideStartupSwitchSummarySetting, checked);
}

void MainWindow::handleGuideRefreshIntervalChanged(int minutes)
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kGuideRefreshIntervalMinutesSetting, minutes);
    applyGuideRefreshIntervalSetting();
    setStatusBarStateMessage(lastStatusBarMessage_);
}

void MainWindow::handleGuideCacheRetentionChanged(int hours)
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kGuideCacheRetentionHoursSetting, hours);
    setStatusBarStateMessage("Purging old cached data");
    const bool removed = purgeExpiredGuideCacheFiles(true);
    loadGuideCacheFile();
    applyCurrentShowStatusFromGuideCache();
    updateTvGuideDialogFromCurrentCache(false);
    setStatusBarStateMessage(removed ? "Old cached data purged"
                                     : "Guide cache retention updated");
}

bool MainWindow::useSchedulesDirectGuideSource() const
{
    if (useSchedulesDirectGuideCheckBox_ != nullptr) {
        return useSchedulesDirectGuideCheckBox_->isChecked();
    }
    QSettings settings("tv_tuner_gui", "watcher");
    return settings.value(kUseSchedulesDirectGuideSetting, false).toBool();
}

void MainWindow::updateSchedulesDirectControls()
{
    const QString username = schedulesDirectUsernameEdit_ != nullptr
                                 ? schedulesDirectUsernameEdit_->text().trimmed()
                                 : QString();
    const QString password = schedulesDirectPasswordEdit_ != nullptr
                                 ? schedulesDirectPasswordEdit_->text().trimmed()
                                 : QString();
    const QString postalCode = schedulesDirectPostalCodeEdit_ != nullptr
                                   ? schedulesDirectPostalCodeEdit_->text().trimmed()
                                   : QString();
    const bool ready = !username.isEmpty() && !password.isEmpty() && !postalCode.isEmpty();
    if (exportSchedulesDirectButton_ != nullptr) {
        exportSchedulesDirectButton_->setEnabled(ready);
    }

    if (schedulesDirectStatusLabel_ == nullptr) {
        return;
    }

    const bool hasCachedJson = QFileInfo::exists(resolveSchedulesDirectExportPath())
                               || QFileInfo::exists(resolveLegacySchedulesDirectExportPath());
    if (useSchedulesDirectGuideSource()) {
        if (ready) {
            schedulesDirectStatusLabel_->setText("Background guide refreshes will download OTA JSON from Schedules Direct and use it instead of live EIT.");
        } else if (hasCachedJson) {
            schedulesDirectStatusLabel_->setText("Background guide refreshes are set to Schedules Direct. Cached OTA JSON will be used until username, password, and ZIP are filled in again.");
        } else {
            schedulesDirectStatusLabel_->setText("Background guide refreshes are set to Schedules Direct. Enter username, password, and ZIP/postal code to let the timer download OTA JSON.");
        }
        return;
    }

    if (ready) {
        schedulesDirectStatusLabel_->setText("Ready to download schedules_direct.org.json. The password field can contain your plain password or the saved 40-character SHA1 hash.");
    } else if (hasCachedJson) {
        schedulesDirectStatusLabel_->setText("A cached Schedules Direct OTA JSON file is available. Enter username, password, and ZIP/postal code to refresh it.");
    } else {
        schedulesDirectStatusLabel_->setText("Enter username, password, and ZIP/postal code to download OTA JSON. The config stores the password as a SHA1 hash because sdJSON accepts the hash directly.");
    }
}

bool MainWindow::ensureSchedulesDirectJson(bool allowCachedExport,
                                           bool *usedCachedExport,
                                           QString *summary,
                                           QString *errorText)
{
    if (usedCachedExport != nullptr) {
        *usedCachedExport = false;
    }
    if (summary != nullptr) {
        summary->clear();
    }
    if (errorText != nullptr) {
        errorText->clear();
    }

    QSettings settings("tv_tuner_gui", "watcher");
    const QString username = schedulesDirectUsernameEdit_ != nullptr
                                 ? schedulesDirectUsernameEdit_->text().trimmed()
                                 : settings.value(kSchedulesDirectUsernameSetting).toString().trimmed();
    const QString postalCode = schedulesDirectPostalCodeEdit_ != nullptr
                                   ? schedulesDirectPostalCodeEdit_->text().trimmed()
                                   : settings.value(kSchedulesDirectPostalCodeSetting).toString().trimmed();
    QString passwordInput = schedulesDirectPasswordEdit_ != nullptr
                                ? schedulesDirectPasswordEdit_->text().trimmed()
                                : settings.value(kSchedulesDirectPasswordSha1Setting).toString().trimmed();
    const QString exportPath = resolveSchedulesDirectExportPath();
    const QString legacyExportPath = resolveLegacySchedulesDirectExportPath();
    const QString cachedExportPath = QFileInfo::exists(exportPath)
                                         ? exportPath
                                         : (QFileInfo::exists(legacyExportPath) ? legacyExportPath : QString());

    const auto finishWithCachedExport = [&](const QString &reason) {
        if (usedCachedExport != nullptr) {
            *usedCachedExport = true;
        }
        const QString message = QString("%1 Using cached Schedules Direct JSON from %2.")
                                    .arg(reason.trimmed(), cachedExportPath);
        if (summary != nullptr) {
            *summary = message;
        }
        appendLog(QString("schedules-direct: %1").arg(message));
        if (schedulesDirectStatusLabel_ != nullptr) {
            schedulesDirectStatusLabel_->setText(message);
        }
        setStatusBarStateMessage("Using cached Schedules Direct JSON");
        return true;
    };

    if (username.isEmpty() || passwordInput.isEmpty() || postalCode.isEmpty()) {
        if (allowCachedExport && !cachedExportPath.isEmpty()) {
            return finishWithCachedExport("Schedules Direct credentials are incomplete.");
        }
        const QString message = "Enter your Schedules Direct username, password, and ZIP/postal code first.";
        if (errorText != nullptr) {
            *errorText = message;
        }
        appendLog(QString("schedules-direct: %1").arg(message));
        if (schedulesDirectStatusLabel_ != nullptr) {
            schedulesDirectStatusLabel_->setText(message);
        }
        setStatusBarStateMessage("Schedules Direct download not configured");
        return false;
    }

    if (exportPath.isEmpty()) {
        const QString message = "Could not resolve the app data folder for schedules_direct.org.json.";
        if (errorText != nullptr) {
            *errorText = message;
        }
        if (schedulesDirectStatusLabel_ != nullptr) {
            schedulesDirectStatusLabel_->setText(message);
        }
        setStatusBarStateMessage("Schedules Direct download failed");
        return false;
    }

    QFileInfo exportInfo(exportPath);
    QDir exportDir = exportInfo.dir();
    if (!exportDir.exists() && !exportDir.mkpath(".")) {
        const QString message = QString("Could not create the export folder for %1.").arg(exportPath);
        if (errorText != nullptr) {
            *errorText = message;
        }
        if (schedulesDirectStatusLabel_ != nullptr) {
            schedulesDirectStatusLabel_->setText(message);
        }
        setStatusBarStateMessage("Schedules Direct download failed");
        return false;
    }

    QString passwordSha1 = passwordInput;
    if (!looksLikeSha1Hex(passwordSha1)) {
        passwordSha1 = QString::fromLatin1(QCryptographicHash::hash(passwordInput.toUtf8(), QCryptographicHash::Sha1).toHex());
    }
    passwordSha1 = passwordSha1.toLower();
    settings.setValue(kSchedulesDirectUsernameSetting, username);
    settings.setValue(kSchedulesDirectPasswordSha1Setting, passwordSha1);
    settings.setValue(kSchedulesDirectPostalCodeSetting, postalCode);

    const auto updateStatus = [this](const QString &text, const QString &statusBarText = QString()) {
        if (schedulesDirectStatusLabel_ != nullptr) {
            schedulesDirectStatusLabel_->setText(text);
        }
        setStatusBarStateMessage(statusBarText.isEmpty() ? text : statusBarText);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    };

    updateStatus("Connecting to Schedules Direct...", "Schedules Direct download in progress");
    logInteraction("program",
                   "schedules-direct.export.start",
                   QString("username=%1 postal=%2 source=%3")
                       .arg(username,
                            postalCode,
                            useSchedulesDirectGuideSource() ? "guide-timer" : "manual"));

    const QString userAgent = QString("tv_tuner_gui/%1").arg(QStringLiteral(TV_TUNER_GUI_VERSION));
    QNetworkAccessManager networkManager;

    const auto failExport = [&](const QString &failureSummary, const QString &details) {
        QString message = failureSummary.trimmed();
        if (!details.trimmed().isEmpty()) {
            message += QString(" %1").arg(details.trimmed());
        }
        message.replace('\n', ' ');
        appendLog(QString("schedules-direct: %1").arg(message));
        logInteraction("program",
                       "schedules-direct.export.failed",
                       message);
        if (allowCachedExport && !cachedExportPath.isEmpty()) {
            return finishWithCachedExport(message);
        }
        if (schedulesDirectStatusLabel_ != nullptr) {
            schedulesDirectStatusLabel_->setText(failureSummary);
        }
        if (errorText != nullptr) {
            *errorText = message;
        }
        setStatusBarStateMessage("Schedules Direct export failed");
        return false;
    };

    QJsonObject tokenRequestBody;
    tokenRequestBody.insert("username", username);
    tokenRequestBody.insert("password", passwordSha1);
    const JsonRequestResult tokenResult =
        performJsonRequest(networkManager,
                           userAgent,
                           QUrl("https://json.schedulesdirect.org/20141201/token"),
                           QString(),
                           QJsonDocument(tokenRequestBody));
    if (!tokenResult.ok() || !tokenResult.document.isObject()) {
        return failExport("Schedules Direct login failed.", tokenResult.errorText);
    }

    const QJsonObject tokenObject = tokenResult.document.object();
    const QString token = tokenObject.value("token").toString().trimmed();
    if (token.isEmpty()) {
        return failExport("Schedules Direct login failed.", "The token response did not include a usable token.");
    }

    updateStatus("Fetching account status...");

    const JsonRequestResult statusResult =
        performJsonRequest(networkManager,
                           userAgent,
                           QUrl("https://json.schedulesdirect.org/20141201/status"),
                           token);
    if (!statusResult.ok() || !statusResult.document.isObject()) {
        return failExport("Schedules Direct status lookup failed.", statusResult.errorText);
    }

    updateStatus(QString("Fetching headends for %1...").arg(postalCode));

    QUrl headendsUrl("https://json.schedulesdirect.org/20141201/headends");
    QUrlQuery headendsQuery;
    headendsQuery.addQueryItem("country", "USA");
    headendsQuery.addQueryItem("postalcode", postalCode);
    headendsUrl.setQuery(headendsQuery);
    const JsonRequestResult headendsResult =
        performJsonRequest(networkManager,
                           userAgent,
                           headendsUrl,
                           token);
    if (!headendsResult.ok() || !headendsResult.document.isArray()) {
        return failExport("Schedules Direct headend lookup failed.", headendsResult.errorText);
    }

    const QJsonArray headends = headendsResult.document.array();
    QJsonArray otaHeadends;
    QStringList otaLineupIds;
    for (const QJsonValue &value : headends) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject headend = value.toObject();
        if (headend.value("transport").toString().trimmed().compare("Antenna", Qt::CaseInsensitive) != 0) {
            continue;
        }

        otaHeadends.append(headend);
        const QJsonArray lineups = headend.value("lineups").toArray();
        for (const QJsonValue &lineupValue : lineups) {
            const QString lineupId = lineupValue.toObject().value("lineup").toString().trimmed();
            if (!lineupId.isEmpty() && !otaLineupIds.contains(lineupId)) {
                otaLineupIds.append(lineupId);
            }
        }
    }
    if (otaLineupIds.isEmpty()) {
        return failExport(QString("No OTA lineup was found for %1.").arg(postalCode),
                          "Schedules Direct returned no Antenna lineups for the current ZIP/postal code.");
    }

    QSet<QString> accountLineupIds;
    const QJsonArray statusLineups = statusResult.document.object().value("lineups").toArray();
    for (const QJsonValue &value : statusLineups) {
        const QString lineupId = value.toObject().value("lineup").toString().trimmed();
        if (!lineupId.isEmpty()) {
            accountLineupIds.insert(lineupId);
        }
    }

    QJsonArray addedLineups;
    QJsonArray exportedLineups;
    int totalChannels = 0;
    int totalScheduleEntries = 0;
    int totalResolvedPrograms = 0;

    constexpr int kSchedulesDirectTimeoutMs = 120000;
    constexpr int kProgramChunkSize = 500;

    for (int index = 0; index < otaLineupIds.size(); ++index) {
        const QString &lineupId = otaLineupIds.at(index);
        const QString encodedLineupId = QString::fromUtf8(QUrl::toPercentEncoding(lineupId));

        if (!accountLineupIds.contains(lineupId)) {
            updateStatus(QString("Adding OTA lineup %1/%2...")
                             .arg(index + 1)
                             .arg(otaLineupIds.size()));

            const JsonRequestResult addLineupResult =
                performJsonRequest(networkManager,
                                   userAgent,
                                   QUrl(QString("https://json.schedulesdirect.org/20141201/lineups/%1")
                                            .arg(encodedLineupId)),
                                   token,
                                   QJsonDocument(),
                                   "PUT",
                                   kSchedulesDirectTimeoutMs);
            if (!addLineupResult.ok() || !addLineupResult.document.isObject()) {
                return failExport(QString("Could not add OTA lineup %1 to the Schedules Direct account.").arg(lineupId),
                                  addLineupResult.errorText);
            }

            addedLineups.append(addLineupResult.document.object());
            accountLineupIds.insert(lineupId);
        }

        updateStatus(QString("Fetching OTA lineup %1/%2...")
                         .arg(index + 1)
                         .arg(otaLineupIds.size()));

        const JsonRequestResult lineupResult =
            performJsonRequest(networkManager,
                               userAgent,
                               QUrl(QString("https://json.schedulesdirect.org/20141201/lineups/%1")
                                        .arg(encodedLineupId)),
                               token,
                               QJsonDocument(),
                               QByteArray(),
                               kSchedulesDirectTimeoutMs);
        if (!lineupResult.ok() || !lineupResult.document.isObject()) {
            return failExport(QString("Could not fetch OTA lineup %1.").arg(lineupId), lineupResult.errorText);
        }

        const QJsonObject lineupObject = lineupResult.document.object();
        const QJsonArray stationMap = lineupObject.value("map").toArray();
        const QJsonArray stations = lineupObject.value("stations").toArray();
        if (stationMap.isEmpty()) {
            return failExport(QString("OTA lineup %1 returned no channel map.").arg(lineupId),
                              "The lineup request succeeded but did not include any mapped OTA stations.");
        }

        QHash<QString, QJsonObject> stationById;
        for (const QJsonValue &stationValue : stations) {
            const QJsonObject stationObject = stationValue.toObject();
            const QString stationId = stationObject.value("stationID").toString().trimmed();
            if (!stationId.isEmpty()) {
                stationById.insert(stationId, stationObject);
            }
        }

        QSet<QString> stationIdSet;
        QJsonArray schedulesRequestArray;
        for (const QJsonValue &mapValue : stationMap) {
            const QString stationId = mapValue.toObject().value("stationID").toString().trimmed();
            if (stationId.isEmpty() || stationIdSet.contains(stationId)) {
                continue;
            }
            stationIdSet.insert(stationId);
            QJsonObject scheduleRequestObject;
            scheduleRequestObject.insert("stationID", stationId);
            schedulesRequestArray.append(scheduleRequestObject);
        }

        updateStatus(QString("Fetching OTA schedules %1/%2...")
                         .arg(index + 1)
                         .arg(otaLineupIds.size()));

        const JsonRequestResult schedulesResult =
            performJsonRequest(networkManager,
                               userAgent,
                               QUrl("https://json.schedulesdirect.org/20141201/schedules"),
                               token,
                               QJsonDocument(schedulesRequestArray),
                               QByteArray(),
                               kSchedulesDirectTimeoutMs);
        if (!schedulesResult.ok() || !schedulesResult.document.isArray()) {
            return failExport(QString("Could not fetch OTA schedules for %1.").arg(lineupId),
                              schedulesResult.errorText);
        }

        const QJsonArray scheduleDays = schedulesResult.document.array();
        QHash<QString, QJsonArray> scheduleDaysByStation;
        QStringList uniqueProgramIds;
        QSet<QString> seenProgramIds;
        int lineupScheduleEntries = 0;
        for (const QJsonValue &scheduleDayValue : scheduleDays) {
            if (!scheduleDayValue.isObject()) {
                continue;
            }

            const QJsonObject scheduleDayObject = scheduleDayValue.toObject();
            const QString stationId = scheduleDayObject.value("stationID").toString().trimmed();
            if (stationId.isEmpty()) {
                continue;
            }

            QJsonArray stationScheduleDays = scheduleDaysByStation.value(stationId);
            stationScheduleDays.append(scheduleDayObject);
            scheduleDaysByStation.insert(stationId, stationScheduleDays);

            const QJsonArray dailyPrograms = scheduleDayObject.value("programs").toArray();
            for (const QJsonValue &programValue : dailyPrograms) {
                const QString programId = programValue.toObject().value("programID").toString().trimmed();
                if (!programId.isEmpty() && !seenProgramIds.contains(programId)) {
                    seenProgramIds.insert(programId);
                    uniqueProgramIds.append(programId);
                }
                if (!programId.isEmpty()) {
                    ++lineupScheduleEntries;
                }
            }
        }

        QJsonObject programsById;
        for (int start = 0; start < uniqueProgramIds.size(); start += kProgramChunkSize) {
            const int chunkNumber = (start / kProgramChunkSize) + 1;
            const int chunkCount = std::max(1, static_cast<int>(std::ceil(uniqueProgramIds.size()
                                                                          / static_cast<double>(kProgramChunkSize))));
            updateStatus(QString("Fetching OTA program details %1/%2 for %3...")
                             .arg(chunkNumber)
                             .arg(chunkCount)
                             .arg(lineupId));

            QJsonArray programsRequestArray;
            const int end = std::min(start + kProgramChunkSize, static_cast<int>(uniqueProgramIds.size()));
            for (int programIndex = start; programIndex < end; ++programIndex) {
                programsRequestArray.append(uniqueProgramIds.at(programIndex));
            }

            const JsonRequestResult programsResult =
                performJsonRequest(networkManager,
                                   userAgent,
                                   QUrl("https://json.schedulesdirect.org/20141201/programs"),
                                   token,
                                   QJsonDocument(programsRequestArray),
                                   QByteArray(),
                                   kSchedulesDirectTimeoutMs);
            if (!programsResult.ok() || !programsResult.document.isArray()) {
                return failExport(QString("Could not fetch OTA program details for %1.").arg(lineupId),
                                  programsResult.errorText);
            }

            for (const QJsonValue &programValue : programsResult.document.array()) {
                if (!programValue.isObject()) {
                    continue;
                }
                const QJsonObject programObject = programValue.toObject();
                const QString programId = programObject.value("programID").toString().trimmed();
                if (!programId.isEmpty()) {
                    programsById.insert(programId, programObject);
                }
            }
        }

        QJsonArray channels;
        for (const QJsonValue &mapValue : stationMap) {
            const QJsonObject mapObject = mapValue.toObject();
            const QString stationId = mapObject.value("stationID").toString().trimmed();

            QJsonObject channelObject = mapObject;
            if (stationById.contains(stationId)) {
                channelObject.insert("station", stationById.value(stationId));
            }

            QJsonArray enrichedScheduleDays;
            int channelScheduleEntries = 0;
            const QJsonArray stationScheduleDays = scheduleDaysByStation.value(stationId);
            for (const QJsonValue &scheduleDayValue : stationScheduleDays) {
                const QJsonObject scheduleDayObject = scheduleDayValue.toObject();
                QJsonObject enrichedScheduleDay = scheduleDayObject;
                QJsonArray enrichedPrograms;

                const QJsonArray dailyPrograms = scheduleDayObject.value("programs").toArray();
                for (const QJsonValue &programValue : dailyPrograms) {
                    QJsonObject enrichedProgram = programValue.toObject();
                    const QString programId = enrichedProgram.value("programID").toString().trimmed();
                    const QJsonObject programObject = programsById.value(programId).toObject();
                    if (!programObject.isEmpty()) {
                        const QString title = firstProgramTitle(programObject);
                        const QString episodeTitle = programObject.value("episodeTitle150").toString().trimmed();
                        const QString description = bestProgramDescription(programObject);
                        const QString showType = programObject.value("showType").toString().trimmed();
                        const QString entityType = programObject.value("entityType").toString().trimmed();
                        const QString originalAirDate = programObject.value("originalAirDate").toString().trimmed();

                        if (!title.isEmpty()) {
                            enrichedProgram.insert("title", title);
                        }
                        if (!episodeTitle.isEmpty()) {
                            enrichedProgram.insert("episode", episodeTitle);
                            enrichedProgram.insert("episodeTitle", episodeTitle);
                        }
                        if (!description.isEmpty()) {
                            enrichedProgram.insert("description", description);
                        }
                        if (!showType.isEmpty()) {
                            enrichedProgram.insert("showType", showType);
                        }
                        if (!entityType.isEmpty()) {
                            enrichedProgram.insert("entityType", entityType);
                        }
                        if (!originalAirDate.isEmpty()) {
                            enrichedProgram.insert("originalAirDate", originalAirDate);
                        }

                        const QJsonArray genres = programObject.value("genres").toArray();
                        if (!genres.isEmpty()) {
                            enrichedProgram.insert("genres", genres);
                        }
                    }

                    enrichedPrograms.append(enrichedProgram);
                    ++channelScheduleEntries;
                }

                enrichedScheduleDay.insert("programs", enrichedPrograms);
                enrichedScheduleDays.append(enrichedScheduleDay);
            }

            channelObject.insert("schedule", enrichedScheduleDays);
            channelObject.insert("scheduleEntryCount", channelScheduleEntries);
            channels.append(channelObject);
            ++totalChannels;
            totalScheduleEntries += channelScheduleEntries;
        }

        totalResolvedPrograms += programsById.size();

        QJsonObject exportedLineup;
        exportedLineup.insert("lineup", lineupId);
        exportedLineup.insert("metadata", lineupObject.value("metadata").toObject());
        exportedLineup.insert("stationCount", stations.size());
        exportedLineup.insert("channelCount", channels.size());
        exportedLineup.insert("scheduleDayCount", scheduleDays.size());
        exportedLineup.insert("scheduleEntryCount", lineupScheduleEntries);
        exportedLineup.insert("programCount", programsById.size());
        exportedLineup.insert("channels", channels);
        exportedLineups.append(exportedLineup);
    }

    QJsonObject exportObject;
    exportObject.insert("version", 2);
    exportObject.insert("generatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    exportObject.insert("source", "Schedules Direct sdJSON");
    exportObject.insert("transport", "Antenna");
    exportObject.insert("country", "USA");
    exportObject.insert("postalCode", postalCode);
    exportObject.insert("username", username);
    exportObject.insert("tokenExpires", tokenObject.value("tokenExpires").toInt());
    exportObject.insert("status", statusResult.document.object());
    exportObject.insert("otaHeadends", otaHeadends);
    exportObject.insert("addedLineups", addedLineups);
    exportObject.insert("lineups", exportedLineups);
    exportObject.insert("totalChannels", totalChannels);
    exportObject.insert("totalScheduleEntries", totalScheduleEntries);
    exportObject.insert("totalProgramRecords", totalResolvedPrograms);

    QSaveFile exportFile(exportPath);
    if (!exportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failExport("Schedules Direct export failed.",
                          QString("Could not open %1 for writing.").arg(exportPath));
    }

    const QByteArray payload = QJsonDocument(exportObject).toJson(QJsonDocument::Indented);
    if (exportFile.write(payload) != payload.size()) {
        exportFile.cancelWriting();
        return failExport("Schedules Direct export failed.",
                          QString("Could not write the JSON payload to %1.").arg(exportPath));
    }
    if (!exportFile.commit()) {
        return failExport("Schedules Direct export failed.",
                          QString("Could not finalize %1.").arg(exportPath));
    }

    QHash<QString, QList<TvGuideEntry>> guideLikeEntries;
    QStringList importedChannels;
    QStringList unmatchedChannels;
    QStringList skippedChannels;
    int importedEntryCount = 0;
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const int retentionHours = guideCacheRetentionHoursValue(guideCacheRetentionSpin_);
    QDateTime latestEndUtc = nowUtc.addSecs(6 * 3600);
    applySchedulesDirectGuideFallback(guideLikeEntries,
                                      retentionHours,
                                      nowUtc,
                                      &latestEndUtc,
                                      &importedChannels,
                                      &unmatchedChannels,
                                      &skippedChannels,
                                      &importedEntryCount);

    QStringList channelOrder;
    QSet<QString> seenChannelNames;
    const QVector<GuideChannelInfo> localChannels = parseGuideChannels(channelLines_, &xspfNumberByTuneKey_);
    for (const GuideChannelInfo &channel : localChannels) {
        if (!channel.name.trimmed().isEmpty() && !seenChannelNames.contains(channel.name)) {
            seenChannelNames.insert(channel.name);
            channelOrder.append(channel.name);
        }
    }

    QDateTime windowStartUtc = nowUtc;
    windowStartUtc = windowStartUtc.addSecs(-windowStartUtc.time().second());
    windowStartUtc = windowStartUtc.addMSecs(-windowStartUtc.time().msec());
    windowStartUtc = windowStartUtc.addSecs(-(windowStartUtc.time().minute() % 30) * 60);

    constexpr int slotMinutes = 30;
    const qint64 minimumWindowSeconds = 6 * 3600;
    const qint64 requestedWindowSeconds =
        std::max(minimumWindowSeconds, windowStartUtc.secsTo(latestEndUtc) + slotMinutes * 60);
    int slotCount = static_cast<int>(std::ceil(static_cast<double>(requestedWindowSeconds) / (slotMinutes * 60.0)));
    slotCount = std::clamp(slotCount, 12, 32);

    QJsonArray channelOrderArray;
    for (const QString &channelName : channelOrder) {
        channelOrderArray.append(channelName);
    }

    QJsonObject entriesByChannelObject;
    int guideLikeChannelCount = 0;
    for (const QString &channelName : channelOrder) {
        const QList<TvGuideEntry> entries = guideLikeEntries.value(channelName);
        if (!entries.isEmpty()) {
            ++guideLikeChannelCount;
        }
        entriesByChannelObject.insert(channelName, guideEntriesToJsonArray(entries));
    }

    QString guideLikeStatusText = QString("Schedules Direct OTA guide: %1 entries mapped for %2/%3 local channels.")
                                      .arg(importedEntryCount)
                                      .arg(guideLikeChannelCount)
                                      .arg(channelOrder.size());
    if (!unmatchedChannels.isEmpty()) {
        guideLikeStatusText += QString("\nNo guide data: %1").arg(unmatchedChannels.join(", "));
    }
    if (!skippedChannels.isEmpty()) {
        guideLikeStatusText += QString("\nSkipped duplicate local channel names: %1").arg(skippedChannels.join(", "));
    }

    exportObject.insert("version", 3);
    exportObject.insert("windowStartUtc", windowStartUtc.toString(Qt::ISODateWithMs));
    exportObject.insert("slotMinutes", slotMinutes);
    exportObject.insert("slotCount", slotCount);
    exportObject.insert("statusText", guideLikeStatusText);
    exportObject.insert("channelOrder", channelOrderArray);
    exportObject.insert("entriesByChannel", entriesByChannelObject);
    exportObject.insert("mappedChannels", QJsonArray::fromStringList(importedChannels));
    exportObject.insert("unmatchedChannels", QJsonArray::fromStringList(unmatchedChannels));
    exportObject.insert("skippedChannels", QJsonArray::fromStringList(skippedChannels));

    QSaveFile guideLikeExportFile(exportPath);
    if (!guideLikeExportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failExport("Schedules Direct export failed.",
                          QString("Could not reopen %1 for guide-shaped JSON output.").arg(exportPath));
    }

    const QByteArray guideLikePayload = QJsonDocument(exportObject).toJson(QJsonDocument::Indented);
    if (guideLikeExportFile.write(guideLikePayload) != guideLikePayload.size()) {
        guideLikeExportFile.cancelWriting();
        return failExport("Schedules Direct export failed.",
                          QString("Could not write the guide-shaped JSON payload to %1.").arg(exportPath));
    }
    if (!guideLikeExportFile.commit()) {
        return failExport("Schedules Direct export failed.",
                          QString("Could not finalize the guide-shaped JSON payload at %1.").arg(exportPath));
    }

    const QString summaryText = QString("Saved %1 OTA lineup%2 with %3 channels and %4 show entries to %5")
                                .arg(exportedLineups.size())
                                .arg(exportedLineups.size() == 1 ? QString() : QString("s"))
                                .arg(totalChannels)
                                .arg(totalScheduleEntries)
                                .arg(exportPath);
    appendLog(QString("schedules-direct: %1").arg(summaryText));
    logInteraction("program",
                   "schedules-direct.export.complete",
                   QString("postal=%1 ota-lineups=%2 channels=%3 schedule-entries=%4 path=%5")
                       .arg(postalCode)
                       .arg(exportedLineups.size())
                       .arg(totalChannels)
                       .arg(totalScheduleEntries)
                       .arg(exportPath));
    if (schedulesDirectStatusLabel_ != nullptr) {
        schedulesDirectStatusLabel_->setText(summaryText);
    }
    setStatusBarStateMessage("Schedules Direct export complete");
    if (summary != nullptr) {
        *summary = summaryText;
    }
    return true;
}

void MainWindow::exportSchedulesDirectJson()
{
    if (exportSchedulesDirectButton_ != nullptr) {
        exportSchedulesDirectButton_->setEnabled(false);
    }
    struct ExportGuard {
        std::function<void()> onExit;
        ~ExportGuard()
        {
            if (onExit) {
                onExit();
            }
        }
    } exportGuard{[this]() {
        if (exportSchedulesDirectButton_ != nullptr) {
            const bool ready = schedulesDirectUsernameEdit_ != nullptr
                               && schedulesDirectPasswordEdit_ != nullptr
                               && schedulesDirectPostalCodeEdit_ != nullptr
                               && !schedulesDirectUsernameEdit_->text().trimmed().isEmpty()
                               && !schedulesDirectPasswordEdit_->text().trimmed().isEmpty()
                               && !schedulesDirectPostalCodeEdit_->text().trimmed().isEmpty();
            exportSchedulesDirectButton_->setEnabled(ready);
        }
    }};

    QString summaryText;
    QString errorText;
    bool usedCachedExport = false;
    const bool ok = ensureSchedulesDirectJson(false, &usedCachedExport, &summaryText, &errorText);
    if (!ok) {
        return;
    }
}

bool MainWindow::applySchedulesDirectGuideFallback(QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                                   int retentionHours,
                                                   const QDateTime &nowUtc,
                                                   QDateTime *latestEndUtc,
                                                   QStringList *importedChannels,
                                                   QStringList *unmatchedChannels,
                                                   QStringList *skippedChannels,
                                                   int *importedEntryCount)
{
    if (importedChannels != nullptr) {
        importedChannels->clear();
    }
    if (unmatchedChannels != nullptr) {
        unmatchedChannels->clear();
    }
    if (skippedChannels != nullptr) {
        skippedChannels->clear();
    }
    if (importedEntryCount != nullptr) {
        *importedEntryCount = 0;
    }

    auto appendUnique = [](QStringList *list, const QString &value) {
        if (list == nullptr) {
            return;
        }
        const QString trimmedValue = value.trimmed();
        if (!trimmedValue.isEmpty() && !list->contains(trimmedValue)) {
            list->append(trimmedValue);
        }
    };

    if (channelLines_.isEmpty()) {
        return false;
    }

    QString exportPath = resolveSchedulesDirectExportPath();
    if (exportPath.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(exportPath)) {
        const QString legacyExportPath = resolveLegacySchedulesDirectExportPath();
        if (QFileInfo::exists(legacyExportPath)) {
            exportPath = legacyExportPath;
        }
    }

    QFile exportFile(exportPath);
    if (!exportFile.exists() || !exportFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(exportFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendLog(QString("guide-sd: could not parse %1 (%2)")
                      .arg(exportPath, parseError.errorString()));
        return false;
    }

    const QJsonObject rootObject = document.object();
    const QJsonObject embeddedEntriesObject = rootObject.value("entriesByChannel").toObject();
    if (!embeddedEntriesObject.isEmpty()) {
        int importedEntries = 0;
        for (auto it = embeddedEntriesObject.begin(); it != embeddedEntriesObject.end(); ++it) {
            const QString channelName = it.key().trimmed();
            if (channelName.isEmpty()) {
                continue;
            }

            const QList<TvGuideEntry> cleanedEntries =
                cleanGuideEntries(guideEntriesFromJsonArray(it.value().toArray()),
                                  nowUtc,
                                  retentionHours,
                                  latestEndUtc);
            entriesByChannel.insert(channelName, cleanedEntries);
            if (!cleanedEntries.isEmpty()) {
                importedEntries += cleanedEntries.size();
                appendUnique(importedChannels, channelName);
            }
        }

        const auto appendArrayToList = [&appendUnique](const QJsonArray &array, QStringList *list) {
            for (const QJsonValue &value : array) {
                appendUnique(list, value.toString());
            }
        };
        appendArrayToList(rootObject.value("unmatchedChannels").toArray(), unmatchedChannels);
        appendArrayToList(rootObject.value("skippedChannels").toArray(), skippedChannels);

        if (importedEntryCount != nullptr) {
            *importedEntryCount = importedEntries;
        }
        if (importedChannels != nullptr && !importedChannels->isEmpty()) {
            appendLog(QString("guide-sd: loaded %1 entries for %2 local channel%3 from embedded guide data in %4")
                          .arg(importedEntries)
                          .arg(importedChannels->size())
                          .arg(importedChannels->size() == 1 ? QString() : QString("s"))
                          .arg(exportPath));
            return true;
        }
        return false;
    }

    const QVector<SchedulesDirectChannelPayload> sdChannels =
        parseSchedulesDirectExportChannels(rootObject);
    if (sdChannels.isEmpty()) {
        appendLog(QString("guide-sd: %1 did not contain OTA schedule entries.").arg(exportPath));
        return false;
    }

    const QVector<GuideChannelInfo> localChannels = parseGuideChannels(channelLines_, &xspfNumberByTuneKey_);
    if (localChannels.isEmpty()) {
        return false;
    }

    QHash<QString, int> localNameCounts;
    for (const GuideChannelInfo &channel : localChannels) {
        localNameCounts[channel.name] += 1;
    }

    int importedEntries = 0;
    int matchedChannels = 0;
    for (const GuideChannelInfo &channel : localChannels) {
        const QString localName = channel.name.trimmed();
        const QString localMatchName = channel.baseName.trimmed().isEmpty() ? localName : channel.baseName.trimmed();
        if (localName.isEmpty() || localMatchName.isEmpty()) {
            continue;
        }

        if (localNameCounts.value(localName) > 1) {
            appendUnique(skippedChannels, localName);
            continue;
        }

        if (!entriesByChannel.value(localName).isEmpty()) {
            continue;
        }

        SchedulesDirectLocalChannelHint hint;
        hint.name = localName;
        hint.frequencyHz = channel.frequencyHz;
        hint.serviceId = channel.serviceId;
        hint.rfChannel = atscRfChannelForFrequencyHz(channel.frequencyHz);

        const QString xspfNumberHint =
            xspfNumberByTuneKey_.value(tuneKey(QString::number(channel.frequencyHz),
                                              QString::number(channel.serviceId))).trimmed();
        parseLocalVirtualChannelHint(xspfNumberHint, channel.serviceId, hint.virtualMajor, hint.virtualMinor);

        const SchedulesDirectChannelPayload *matchedChannel = nullptr;

        if (hint.virtualMajor > 0 && hint.virtualMinor > 0) {
            for (const SchedulesDirectChannelPayload &candidate : sdChannels) {
                if (candidate.virtualMajor == hint.virtualMajor
                    && candidate.virtualMinor == hint.virtualMinor) {
                    matchedChannel = &candidate;
                    break;
                }
            }
        }

        if (matchedChannel == nullptr && hint.rfChannel > 0 && hint.virtualMinor > 0) {
            const SchedulesDirectChannelPayload *uniqueRfMinorMatch = nullptr;
            for (const SchedulesDirectChannelPayload &candidate : sdChannels) {
                if (candidate.rfChannel != hint.rfChannel || candidate.virtualMinor != hint.virtualMinor) {
                    continue;
                }
                if (uniqueRfMinorMatch != nullptr) {
                    uniqueRfMinorMatch = nullptr;
                    break;
                }
                uniqueRfMinorMatch = &candidate;
            }
            matchedChannel = uniqueRfMinorMatch;
        }

        if (matchedChannel == nullptr && hint.rfChannel > 0) {
            const SchedulesDirectChannelPayload *uniqueRfNameMatch = nullptr;
            for (const SchedulesDirectChannelPayload &candidate : sdChannels) {
                if (candidate.rfChannel != hint.rfChannel
                    || !schedulesDirectLocalNameMatches(localMatchName, candidate)) {
                    continue;
                }
                if (uniqueRfNameMatch != nullptr) {
                    uniqueRfNameMatch = nullptr;
                    break;
                }
                uniqueRfNameMatch = &candidate;
            }
            matchedChannel = uniqueRfNameMatch;
        }

        if (matchedChannel == nullptr && hint.virtualMajor > 0) {
            const SchedulesDirectChannelPayload *uniqueMajorNameMatch = nullptr;
            for (const SchedulesDirectChannelPayload &candidate : sdChannels) {
                if (candidate.virtualMajor != hint.virtualMajor
                    || !schedulesDirectLocalNameMatches(localMatchName, candidate)) {
                    continue;
                }
                if (uniqueMajorNameMatch != nullptr) {
                    uniqueMajorNameMatch = nullptr;
                    break;
                }
                uniqueMajorNameMatch = &candidate;
            }
            matchedChannel = uniqueMajorNameMatch;
        }

        if (matchedChannel == nullptr && hint.rfChannel > 0) {
            const SchedulesDirectChannelPayload *onlyRfMatch = nullptr;
            for (const SchedulesDirectChannelPayload &candidate : sdChannels) {
                if (candidate.rfChannel != hint.rfChannel) {
                    continue;
                }
                if (onlyRfMatch != nullptr) {
                    onlyRfMatch = nullptr;
                    break;
                }
                onlyRfMatch = &candidate;
            }
            matchedChannel = onlyRfMatch;
        }

        if (matchedChannel == nullptr) {
            appendUnique(unmatchedChannels, localName);
            continue;
        }

        const QList<TvGuideEntry> cleanedEntries =
            cleanGuideEntries(matchedChannel->entries, nowUtc, retentionHours, latestEndUtc);
        if (cleanedEntries.isEmpty()) {
            appendUnique(unmatchedChannels, localName);
            continue;
        }

        entriesByChannel.insert(localName, cleanedEntries);
        importedEntries += cleanedEntries.size();
        ++matchedChannels;
        appendUnique(importedChannels, localName);
        appendLog(QString("guide-sd: mapped %1 -> %2 (%3 entries)")
                      .arg(localName, matchedChannel->channelLabel)
                      .arg(cleanedEntries.size()));
    }

    if (importedEntryCount != nullptr) {
        *importedEntryCount = importedEntries;
    }

    if (matchedChannels > 0) {
        appendLog(QString("guide-sd: imported %1 entries for %2 local channel%3 from %4")
                      .arg(importedEntries)
                      .arg(matchedChannels)
                      .arg(matchedChannels == 1 ? QString() : QString("s"))
                      .arg(exportPath));
    }
    if (skippedChannels != nullptr && !skippedChannels->isEmpty()) {
        appendLog(QString("guide-sd: skipped duplicate local channel name%1: %2")
                      .arg(skippedChannels->size() == 1 ? QString() : QString("s"))
                      .arg(skippedChannels->join(", ")));
    }

    return matchedChannels > 0;
}

void MainWindow::applyGuideFilterSettings()
{
    if (tvGuideDialog_ == nullptr) {
        return;
    }

    const bool hideNoEit = hideNoEitChannelsCheckBox_ != nullptr && hideNoEitChannelsCheckBox_->isChecked();
    const bool showFavoritesOnly = showFavoritesOnlyCheckBox_ != nullptr && showFavoritesOnlyCheckBox_->isChecked();
    tvGuideDialog_->setGuideFilters(hideNoEit, showFavoritesOnly);
}

void MainWindow::applyGuideRefreshIntervalSetting()
{
    if (guideRefreshTimer_ == nullptr) {
        return;
    }

    const int minutes = std::clamp(guideRefreshIntervalSpin_ != nullptr ? guideRefreshIntervalSpin_->value()
                                                                        : kDefaultGuideRefreshIntervalMinutes,
                                   5,
                                   1440);
    guideRefreshTimer_->setInterval(minutes * 60 * 1000);
    if (guideRefreshTimer_->isActive()) {
        guideRefreshTimer_->start();
    }
}

void MainWindow::clearLoadedGuideCache()
{
    guideEntriesCache_.clear();
    lastGuideChannelOrder_.clear();
    lastGuideWindowStartUtc_ = QDateTime();
    lastGuideSlotMinutes_ = 30;
    lastGuideSlotCount_ = 12;
    lastGuideCacheGeneratedUtc_.clear();
    lastGuideStatusText_ = "Guide cache expired and was removed.";
    // Preserve auto-favorite conflict choices so a cache reload with the same
    // guide stamp does not re-prompt the user for the same overlap.
    showTransientStatusBarMessage("Guide cache expired and was removed.", 4000);
    noAutoCurrentShowLookupChannels_.clear();
}

void MainWindow::loadFavoriteShowRules()
{
    favoriteShowRules_.clear();
    favoriteShowRatings_.clear();

    QSettings settings("tv_tuner_gui", "watcher");
    const QStringList storedRules = settings.value(kFavoriteShowRulesSetting).toStringList();
    QSet<QString> seenRules;
    for (const QString &storedRule : storedRules) {
        const QString trimmedRule = storedRule.simplified();
        if (trimmedRule.isEmpty()) {
            continue;
        }
        const QString normalizedRule = normalizeFavoriteShowRule(trimmedRule);
        if (seenRules.contains(normalizedRule)) {
            continue;
        }
        seenRules.insert(normalizedRule);
        favoriteShowRules_.append(trimmedRule);
    }

    const QString serializedRatings = settings.value(kFavoriteShowRatingsSetting).toString().trimmed();
    if (!serializedRatings.isEmpty()) {
        QJsonParseError parseError{};
        const QJsonDocument ratingsDocument =
            QJsonDocument::fromJson(serializedRatings.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && ratingsDocument.isObject()) {
            const QJsonObject ratingsObject = ratingsDocument.object();
            for (auto it = ratingsObject.begin(); it != ratingsObject.end(); ++it) {
                favoriteShowRatings_.insert(it.key(),
                                            std::clamp(it.value().toInt(kDefaultFavoriteShowRating),
                                                       kDefaultFavoriteShowRating,
                                                       kMaxFavoriteShowRating));
            }
        }
    }

    std::sort(favoriteShowRules_.begin(),
              favoriteShowRules_.end(),
              [](const QString &left, const QString &right) {
                  return left.localeAwareCompare(right) < 0;
              });

    for (const QString &rule : favoriteShowRules_) {
        const QString normalizedRule = normalizeFavoriteShowRule(rule);
        if (!favoriteShowRatings_.contains(normalizedRule)) {
            favoriteShowRatings_.insert(normalizedRule, kDefaultFavoriteShowRating);
        }
    }
}

void MainWindow::saveFavoriteShowRules() const
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kFavoriteShowRulesSetting, favoriteShowRules_);

    QJsonObject ratingsObject;
    for (const QString &rule : favoriteShowRules_) {
        ratingsObject.insert(normalizeFavoriteShowRule(rule), favoriteShowRating(rule));
    }
    if (ratingsObject.isEmpty()) {
        settings.remove(kFavoriteShowRatingsSetting);
    } else {
        settings.setValue(kFavoriteShowRatingsSetting,
                          QString::fromUtf8(QJsonDocument(ratingsObject).toJson(QJsonDocument::Compact)));
    }
}

void MainWindow::refreshFavoriteShowRuleList()
{
    if (favoriteShowRulesList_ == nullptr) {
        return;
    }

    const QListWidgetItem *currentItem = favoriteShowRulesList_->currentItem();
    const QString previouslySelectedRule =
        currentItem != nullptr ? currentItem->data(Qt::UserRole).toString() : QString();
    favoriteShowRulesList_->clear();
    for (const QString &rule : favoriteShowRules_) {
        auto *item = new QListWidgetItem(favoriteShowRuleDisplayLabel(rule), favoriteShowRulesList_);
        item->setData(Qt::UserRole, rule);
    }
    if (!previouslySelectedRule.isEmpty()) {
        for (int index = 0; index < favoriteShowRulesList_->count(); ++index) {
            QListWidgetItem *item = favoriteShowRulesList_->item(index);
            if (item != nullptr && item->data(Qt::UserRole).toString() == previouslySelectedRule) {
                favoriteShowRulesList_->setCurrentRow(index);
                break;
            }
        }
    }
    if (removeFavoriteShowRuleButton_ != nullptr) {
        removeFavoriteShowRuleButton_->setEnabled(favoriteShowRulesList_->currentRow() >= 0);
    }
    syncFavoriteShowRatingControls();
}

QString MainWindow::favoriteShowRuleDisplayLabel(const QString &title) const
{
    const QString trimmedTitle = title.simplified();
    if (trimmedTitle.isEmpty()) {
        return QString();
    }

    return QString("%1 | priority %2").arg(trimmedTitle).arg(favoriteShowRating(trimmedTitle));
}

int MainWindow::favoriteShowRating(const QString &title) const
{
    const QString normalizedTitle = normalizeFavoriteShowRule(title);
    if (normalizedTitle.isEmpty()) {
        return kDefaultFavoriteShowRating;
    }

    return std::clamp(favoriteShowRatings_.value(normalizedTitle, kDefaultFavoriteShowRating),
                      kDefaultFavoriteShowRating,
                      kMaxFavoriteShowRating);
}

void MainWindow::setFavoriteShowRating(const QString &title, int rating)
{
    const QString normalizedTitle = normalizeFavoriteShowRule(title);
    if (normalizedTitle.isEmpty()) {
        return;
    }

    const int clampedRating = std::clamp(rating, kDefaultFavoriteShowRating, kMaxFavoriteShowRating);
    if (favoriteShowRatings_.value(normalizedTitle, kDefaultFavoriteShowRating) == clampedRating) {
        return;
    }

    favoriteShowRatings_.insert(normalizedTitle, clampedRating);
    saveFavoriteShowRules();
    refreshFavoriteShowRuleList();
}

void MainWindow::syncFavoriteShowRatingControls()
{
    if (favoriteShowRulesList_ == nullptr || favoriteShowRatingSpin_ == nullptr) {
        return;
    }

    const int currentRow = favoriteShowRulesList_->currentRow();
    const bool hasSelection = currentRow >= 0 && currentRow < favoriteShowRules_.size();
    favoriteShowRatingSpin_->setEnabled(hasSelection);

    const QSignalBlocker blocker(favoriteShowRatingSpin_);
    favoriteShowRatingSpin_->setValue(hasSelection ? favoriteShowRating(favoriteShowRules_.at(currentRow))
                                                   : kDefaultFavoriteShowRating);
}

void MainWindow::refreshScheduledSwitchList()
{
    if (scheduledSwitchesList_ == nullptr) {
        return;
    }

    scheduledSwitchesList_->clear();
    for (const TvGuideScheduledSwitch &scheduledSwitch : scheduledSwitches_) {
        const QString displayTitle = scheduledSwitch.title.trimmed().isEmpty()
                                         ? scheduledSwitch.channelName.trimmed()
                                         : scheduledSwitch.title.simplified();
        QString label = QString("%1 (%2) | %3 | %4")
                            .arg(displayTitle)
                            .arg(favoriteShowRating(displayTitle))
                            .arg(scheduledSwitch.channelName.trimmed())
                            .arg(scheduledSwitch.startUtc.toLocalTime().toString("ddd h:mm AP"));
        if (isScheduledSwitchLockedIn(scheduledSwitch)) {
            label += " | locked in";
        }
        scheduledSwitchesList_->addItem(label);
    }
    if (removeScheduledSwitchButton_ != nullptr) {
        removeScheduledSwitchButton_->setEnabled(false);
    }
}

void MainWindow::loadTestingBugItems()
{
    if (testingBugItemsList_ == nullptr) {
        return;
    }

    const QSignalBlocker blocker(testingBugItemsList_);
    testingBugItemsList_->clear();

    QSettings settings("tv_tuner_gui", "watcher");
    const QString serializedItems = settings.value(kTestingBugItemsSetting).toString().trimmed();
    if (serializedItems.isEmpty()) {
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(serializedItems.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return;
    }

    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        addTestingBugItemEntry(object.value("text").toString(), object.value("checked").toBool(false));
    }
    if (removeTestingBugItemButton_ != nullptr) {
        removeTestingBugItemButton_->setEnabled(false);
    }
}

void MainWindow::saveTestingBugItems() const
{
    if (testingBugItemsList_ == nullptr) {
        return;
    }

    QJsonArray itemsArray;
    for (int index = 0; index < testingBugItemsList_->count(); ++index) {
        QListWidgetItem *item = testingBugItemsList_->item(index);
        if (item == nullptr) {
            continue;
        }

        const QString text = item->text().simplified();
        if (text.isEmpty()) {
            continue;
        }

        QJsonObject object;
        object.insert("text", text);
        object.insert("checked", item->checkState() == Qt::Checked);
        itemsArray.append(object);
    }

    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kTestingBugItemsSetting,
                      QString::fromUtf8(QJsonDocument(itemsArray).toJson(QJsonDocument::Compact)));
}

bool MainWindow::addTestingBugItemEntry(const QString &text, bool checked)
{
    if (testingBugItemsList_ == nullptr) {
        return false;
    }

    const QString trimmedText = text.simplified();
    if (trimmedText.isEmpty()) {
        return false;
    }

    for (int index = 0; index < testingBugItemsList_->count(); ++index) {
        QListWidgetItem *existingItem = testingBugItemsList_->item(index);
        if (existingItem != nullptr && existingItem->text().compare(trimmedText, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }

    auto *item = new QListWidgetItem(trimmedText, testingBugItemsList_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return true;
}

void MainWindow::addTestingBugItem()
{
    if (testingBugItemEdit_ == nullptr) {
        return;
    }

    const QString text = testingBugItemEdit_->text().simplified();
    if (text.isEmpty()) {
        return;
    }

    if (!addTestingBugItemEntry(text, false)) {
        showInformationDialog("Item already saved",
                              QString("%1 is already in your Testing/bugs list.").arg(text));
        return;
    }

    testingBugItemEdit_->clear();
    saveTestingBugItems();
}

void MainWindow::removeSelectedTestingBugItem()
{
    if (testingBugItemsList_ == nullptr) {
        return;
    }

    const int row = testingBugItemsList_->currentRow();
    if (row < 0) {
        return;
    }

    delete testingBugItemsList_->takeItem(row);
    if (removeTestingBugItemButton_ != nullptr) {
        removeTestingBugItemButton_->setEnabled(false);
    }
    saveTestingBugItems();
}

bool MainWindow::addFavoriteShowRule(const QString &title)
{
    const QString trimmedTitle = title.simplified();
    if (trimmedTitle.isEmpty()) {
        return false;
    }

    const QString normalizedTitle = normalizeFavoriteShowRule(trimmedTitle);
    for (const QString &existingRule : favoriteShowRules_) {
        if (normalizeFavoriteShowRule(existingRule) == normalizedTitle) {
            return false;
        }
    }

    favoriteShowRules_.append(trimmedTitle);
    std::sort(favoriteShowRules_.begin(),
              favoriteShowRules_.end(),
              [](const QString &left, const QString &right) {
                  return left.localeAwareCompare(right) < 0;
              });
    favoriteShowRatings_.insert(normalizeFavoriteShowRule(trimmedTitle), kDefaultFavoriteShowRating);
    saveFavoriteShowRules();
    refreshFavoriteShowRuleList();
    appendLog(QString("favorite-show: added %1").arg(trimmedTitle));
    return true;
}

bool MainWindow::isScheduledSwitchLockedIn(const TvGuideScheduledSwitch &scheduledSwitch) const
{
    if (lockedScheduledSwitches_.contains(lockedScheduledSwitchKey(scheduledSwitch))) {
        return true;
    }

    const QString cacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                      lastGuideWindowStartUtc_,
                                                      lastGuideSlotMinutes_,
                                                      lastGuideSlotCount_);
    return isAutoFavoriteLockedIn(lockedAutoFavoriteSelections_, cacheStamp, scheduledSwitch);
}

void MainWindow::saveLockedScheduledSwitches() const
{
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kLockedScheduledSwitchesSetting, lockedScheduledSwitches_);
}

void MainWindow::setScheduledSwitchLockedIn(const TvGuideScheduledSwitch &scheduledSwitch, bool lockedIn)
{
    const QString key = lockedScheduledSwitchKey(scheduledSwitch);
    if (key.isEmpty()) {
        return;
    }

    const bool alreadyLocked = lockedScheduledSwitches_.contains(key);
    if (alreadyLocked == lockedIn) {
        return;
    }

    lockedScheduledSwitches_.removeAll(key);
    if (lockedIn) {
        lockedScheduledSwitches_.append(key);
    }
    saveLockedScheduledSwitches();
}

void MainWindow::pruneLockedScheduledSwitches()
{
    QStringList prunedLocks;
    prunedLocks.reserve(lockedScheduledSwitches_.size());
    for (const QString &lockedKey : lockedScheduledSwitches_) {
        if (!lockedKey.trimmed().isEmpty() && !prunedLocks.contains(lockedKey)) {
            prunedLocks.append(lockedKey);
        }
    }

    if (prunedLocks == lockedScheduledSwitches_) {
        return;
    }

    lockedScheduledSwitches_ = prunedLocks;
    saveLockedScheduledSwitches();
}

void MainWindow::handleCurrentTabChanged(int index)
{
    if (tabs_ == nullptr) {
        return;
    }

    if (tabs_->widget(index) == tvGuideDialog_ && tvGuideDialog_ != nullptr) {
        tvGuideDialog_->syncToCurrentTime();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const bool cacheLooksCurrent =
            guideCacheLooksCurrentForStartup(guideEntriesCache_, lastGuideWindowStartUtc_, nowUtc);
        const bool cacheMatchesSelectedSource =
            useSchedulesDirectGuideSource()
                ? lastGuideStatusText_.contains("Schedules Direct", Qt::CaseInsensitive)
                : !lastGuideStatusText_.contains("Schedules Direct", Qt::CaseInsensitive);
        if (!guideRefreshInProgress_ && (!cacheLooksCurrent || !cacheMatchesSelectedSource)) {
            refreshGuideData(false, true);
        } else {
            updateTvGuideDialogFromCurrentCache(false);
        }
    }

    if (tabs_->widget(index) == watchPage_) {
        attachVideoFromPip();
        return;
    }

    if (fullscreenActive_) {
        exitFullscreen();
    }

    if (shouldDetachVideoForCurrentTab(index)) {
        detachVideoToPip();
    } else if (videoDetachedToPip_) {
        attachVideoFromPip();
    }
}

bool MainWindow::shouldDetachVideoForCurrentTab(int index) const
{
    if (!autoPictureInPictureEnabled_
        || tabs_ == nullptr
        || watchPage_ == nullptr
        || currentChannelName_.trimmed().isEmpty()
        || fullscreenActive_) {
        return false;
    }

    return index >= 0 && tabs_->widget(index) != watchPage_;
}

void MainWindow::detachVideoToPip()
{
    if (videoDetachedToPip_
        || videoWidget_ == nullptr
        || pipVideoWidget_ == nullptr
        || pipWindowLayout_ == nullptr
        || pipWindow_ == nullptr) {
        return;
    }

    if (videoDetachedPlaceholderLabel_ != nullptr) {
        videoDetachedPlaceholderLabel_->show();
    }
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->setVideoOutput(pipVideoWidget_);
    }
    pipVideoWidget_->show();
    videoDetachedToPip_ = true;

    if (pipWindow_->pos() == QPoint(0, 0)) {
        if (QScreen *targetScreen = screenForWidget(this)) {
            const QRect available = targetScreen->availableGeometry();
            pipWindow_->move(available.right() - pipWindow_->width() - 24, available.top() + 24);
        }
    }

    pipWindow_->show();
    pipWindow_->raise();
}

void MainWindow::attachVideoFromPip()
{
    if (!videoDetachedToPip_
        || videoWidget_ == nullptr
        || pipVideoWidget_ == nullptr) {
        return;
    }

    if (videoDetachedPlaceholderLabel_ != nullptr) {
        videoDetachedPlaceholderLabel_->hide();
    }
    if (pipWindow_ != nullptr) {
        pipWindow_->hide();
    }
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->setVideoOutput(videoWidget_);
    }
    videoWidget_->show();
    videoDetachedToPip_ = false;
}

bool MainWindow::purgeExpiredGuideCacheFiles(bool clearLoadedState)
{
    const int retentionHours = guideCacheRetentionHoursValue(guideCacheRetentionSpin_);
    if (retentionHours <= 0) {
        return false;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    bool updatedPrimaryCache = false;
    bool removedAny = false;
    const QString primaryCachePath = resolveGuideCachePath();
    if (pruneGuideCacheFile(primaryCachePath, nowUtc, retentionHours)) {
        updatedPrimaryCache = true;
        removedAny = true;
    }

    const QString betaCachePath = resolveGuideCacheBetaPath();
    if (pruneGuideCacheFile(betaCachePath, nowUtc, retentionHours)) {
        removedAny = true;
    }

    if (updatedPrimaryCache && clearLoadedState) {
        clearLoadedGuideCache();
    }
    return removedAny;
}

bool MainWindow::saveScheduledSwitches() const
{
    const QString schedulePath = resolveGuideSchedulePath();
    if (schedulePath.isEmpty()) {
        return false;
    }

    QFileInfo scheduleInfo(schedulePath);
    QDir scheduleDir = scheduleInfo.dir();
    if (!scheduleDir.exists() && !scheduleDir.mkpath(".")) {
        return false;
    }

    QJsonObject root;
    root.insert("version", 1);
    root.insert("savedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QJsonArray switchesArray;
    for (const TvGuideScheduledSwitch &scheduledSwitch : scheduledSwitches_) {
        switchesArray.append(scheduledSwitchToJsonObject(scheduledSwitch));
    }
    root.insert("switches", switchesArray);

    QSaveFile scheduleFile(schedulePath);
    if (!scheduleFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (scheduleFile.write(payload) != payload.size()) {
        scheduleFile.cancelWriting();
        return false;
    }
    return scheduleFile.commit();
}

bool MainWindow::loadScheduledSwitches()
{
    scheduledSwitches_.clear();
    refreshScheduledSwitchList();

    const QString schedulePath = resolveGuideSchedulePath();
    if (schedulePath.isEmpty()) {
        return false;
    }

    QFile scheduleFile(schedulePath);
    if (!scheduleFile.exists()) {
        return false;
    }
    if (!scheduleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(scheduleFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    scheduledSwitches_ = scheduledSwitchesFromJsonArray(document.object().value("switches").toArray());
    appendLog(QString("schedule: loaded %1 switch(es) from disk -> %2")
                  .arg(scheduledSwitches_.size())
                  .arg(summarizeScheduledSwitchesDebug(scheduledSwitches_)));
    const bool pruned = pruneExpiredScheduledSwitches();
    if (pruned) {
        saveScheduledSwitches();
        appendLog(QString("schedule: pruned expired/duplicate switches after load -> %1")
                      .arg(summarizeScheduledSwitchesDebug(scheduledSwitches_)));
    }
    refreshScheduledSwitchList();
    return !scheduledSwitches_.isEmpty();
}

bool MainWindow::pruneExpiredScheduledSwitches()
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QList<TvGuideScheduledSwitch> cleaned;
    bool changed = false;

    std::sort(scheduledSwitches_.begin(), scheduledSwitches_.end(), [](const TvGuideScheduledSwitch &left,
                                                                       const TvGuideScheduledSwitch &right) {
        if (left.startUtc == right.startUtc) {
            return left.channelName < right.channelName;
        }
        return left.startUtc < right.startUtc;
    });

    for (const TvGuideScheduledSwitch &scheduledSwitch : scheduledSwitches_) {
        if (scheduledSwitch.channelName.trimmed().isEmpty()
            || !scheduledSwitch.startUtc.isValid()
            || !scheduledSwitch.endUtc.isValid()
            || scheduledSwitch.endUtc <= scheduledSwitch.startUtc
            || scheduledSwitch.endUtc <= nowUtc) {
            changed = true;
            continue;
        }

        bool duplicate = false;
        for (const TvGuideScheduledSwitch &existing : cleaned) {
            if (scheduledSwitchesMatch(existing, scheduledSwitch)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            changed = true;
            continue;
        }
        cleaned.append(scheduledSwitch);
    }

    if (cleaned.size() != scheduledSwitches_.size()) {
        changed = true;
    }
    scheduledSwitches_ = cleaned;
    pruneLockedScheduledSwitches();
    return changed;
}

void MainWindow::refreshScheduledSwitchTimer()
{
    if (scheduledSwitchTimer_ == nullptr) {
        return;
    }

    const bool changed = pruneExpiredScheduledSwitches();
    if (changed) {
        saveScheduledSwitches();
        refreshScheduledSwitchList();
        updateTvGuideDialogFromCurrentCache(false);
    }

    scheduledSwitchTimer_->stop();
    if (!obeyScheduledSwitches_ || scheduledSwitches_.isEmpty()) {
        appendLog(QString("schedule: timer idle obey=%1 queue=%2")
                      .arg(obeyScheduledSwitches_ ? "true" : "false",
                           summarizeScheduledSwitchesDebug(scheduledSwitches_)));
        return;
    }

    const QDateTime nextStartUtc = scheduledSwitches_.first().startUtc;
    const qint64 delayMs = std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(nextStartUtc));
    scheduledSwitchTimer_->start(static_cast<int>(std::min<qint64>(delayMs, std::numeric_limits<int>::max())));
    appendLog(QString("schedule: timer armed next=%1 delayMs=%2 queue=%3")
                  .arg(nextStartUtc.toLocalTime().toString("ddd h:mm:ss AP"))
                  .arg(delayMs)
                  .arg(summarizeScheduledSwitchesDebug(scheduledSwitches_)));
}

void MainWindow::processScheduledSwitches()
{
    const bool changed = pruneExpiredScheduledSwitches();
    if (changed) {
        saveScheduledSwitches();
        refreshScheduledSwitchList();
        updateTvGuideDialogFromCurrentCache(false);
    }

    if (!obeyScheduledSwitches_ || scheduledSwitches_.isEmpty()) {
        appendLog(QString("schedule: process skipped obey=%1 queue=%2")
                      .arg(obeyScheduledSwitches_ ? "true" : "false",
                           summarizeScheduledSwitchesDebug(scheduledSwitches_)));
        refreshScheduledSwitchTimer();
        return;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    appendLog(QString("schedule: process begin now=%1 queue=%2")
                  .arg(nowUtc.toLocalTime().toString("ddd h:mm:ss AP"))
                  .arg(summarizeScheduledSwitchesDebug(scheduledSwitches_)));
    showTransientStatusBarMessage("Checking scheduled switches", 3000);
    QList<TvGuideScheduledSwitch> activeSwitches;
    for (const TvGuideScheduledSwitch &scheduledSwitch : scheduledSwitches_) {
        if (scheduledSwitch.startUtc > nowUtc) {
            appendLog(QString("schedule: next pending switch is still in the future -> %1")
                          .arg(scheduledSwitchDebugLabel(scheduledSwitch)));
            break;
        }
        if (scheduledSwitch.endUtc <= nowUtc) {
            appendLog(QString("schedule: skipping already-ended switch during process -> %1")
                          .arg(scheduledSwitchDebugLabel(scheduledSwitch)));
            continue;
        }
        activeSwitches.append(scheduledSwitch);
    }

    if (activeSwitches.isEmpty()) {
        refreshScheduledSwitchTimer();
        return;
    }

    if (scanProcess_ != nullptr && scanProcess_->state() != QProcess::NotRunning) {
        appendLog(QString("schedule: %1 active switch(es) are due, but a scan is still running. Retrying in 30 seconds.")
                      .arg(activeSwitches.size()));
        showTransientStatusBarMessage(QString("Scheduled switch waiting for scan: %1")
                                          .arg(activeSwitches.first().channelName),
                                      4000);
        scheduledSwitchTimer_->start(30000);
        return;
    }

    if (activeSwitches.size() > 1) {
        appendLog(QString("schedule: runtime conflict for active switches -> %1")
                      .arg(summarizeScheduledSwitchesDebug(activeSwitches)));
        const bool resolved = resolveScheduledSwitchChoices(activeSwitches,
                                                            "runtime schedule:",
                                                            !favoriteShowRatingsOverrideEnabled_);
        if (!resolved) {
            appendLog("schedule: runtime conflict left unresolved; retrying in 30 seconds.");
            showTransientStatusBarMessage("Runtime schedule conflict still needs a choice", 4000);
            qint64 retryMs = 30000;
            for (const TvGuideScheduledSwitch &activeSwitch : activeSwitches) {
                retryMs = std::min<qint64>(retryMs,
                                           std::max<qint64>(1000, nowUtc.msecsTo(activeSwitch.endUtc)));
            }
            scheduledSwitchTimer_->start(static_cast<int>(retryMs));
            return;
        }
        processScheduledSwitches();
        return;
    }

    const TvGuideScheduledSwitch scheduledSwitch = activeSwitches.first();
    int scheduledSwitchIndex = -1;
    for (int index = 0; index < scheduledSwitches_.size(); ++index) {
        if (scheduledSwitchesMatch(scheduledSwitches_.at(index), scheduledSwitch)) {
            scheduledSwitchIndex = index;
            break;
        }
    }
    if (scheduledSwitchIndex < 0) {
        refreshScheduledSwitchTimer();
        return;
    }

    if (currentChannelName_ == scheduledSwitch.channelName) {
        appendLog(QString("schedule: already tuned for %1; removing completed switch.")
                      .arg(scheduledSwitchLabel(scheduledSwitch)));
        showTransientStatusBarMessage(QString("Scheduled switch already satisfied: %1")
                                          .arg(scheduledSwitch.channelName),
                                      3000);
        scheduledSwitches_.removeAt(scheduledSwitchIndex);
        saveScheduledSwitches();
        refreshScheduledSwitchList();
        updateTvGuideDialogFromCurrentCache(false);
        refreshScheduledSwitchTimer();
        return;
    }

    appendLog(QString("schedule: obeying %1").arg(scheduledSwitchLabel(scheduledSwitch)));
    if (tabs_ != nullptr && watchPage_ != nullptr) {
        tabs_->setCurrentWidget(watchPage_);
    }

    if (startWatchingChannel(scheduledSwitch.channelName, false)) {
        scheduledSwitches_.removeAt(scheduledSwitchIndex);
        saveScheduledSwitches();
        refreshScheduledSwitchList();
        updateTvGuideDialogFromCurrentCache(false);
        setStatusBarStateMessage("Applying scheduled switch");
        refreshScheduledSwitchTimer();
        return;
    }

    appendLog(QString("schedule: failed to switch to %1; will retry while the program is still airing.")
                  .arg(scheduledSwitch.channelName));
    showTransientStatusBarMessage(QString("Scheduled switch retrying: %1").arg(scheduledSwitch.channelName),
                                  4000);
    scheduledSwitchTimer_->start(static_cast<int>(std::min<qint64>(30000,
                                                                   std::max<qint64>(1000,
                                                                                    nowUtc.msecsTo(scheduledSwitch.endUtc)))));
}

bool MainWindow::resolveScheduledSwitchChoices(const QList<TvGuideScheduledSwitch> &candidates,
                                               const QString &sourceDescription,
                                               bool promptForConflict)
{
    QList<TvGuideScheduledSwitch> uniqueCandidates;
    for (const TvGuideScheduledSwitch &candidate : candidates) {
        if (candidate.channelName.trimmed().isEmpty()
            || !candidate.startUtc.isValid()
            || !candidate.endUtc.isValid()
            || candidate.endUtc <= candidate.startUtc) {
            continue;
        }

        bool duplicateCandidate = false;
        for (const TvGuideScheduledSwitch &existingCandidate : uniqueCandidates) {
            if (scheduledSwitchesMatch(existingCandidate, candidate)) {
                duplicateCandidate = true;
                break;
            }
        }
        if (!duplicateCandidate) {
            uniqueCandidates.append(candidate);
        }
    }

    if (uniqueCandidates.isEmpty()) {
        return false;
    }

    appendLog(QString("%1 resolve begin prompt=%2 candidates=%3 existing=%4")
                  .arg(sourceDescription,
                       promptForConflict ? "true" : "false",
                       summarizeScheduledSwitchesDebug(uniqueCandidates),
                       summarizeScheduledSwitchesDebug(scheduledSwitches_)));

    QList<ScheduledSwitchChoiceOption> choices;
    QSet<int> relevantExistingIndexSet;
    for (int index = 0; index < scheduledSwitches_.size(); ++index) {
        const TvGuideScheduledSwitch &existing = scheduledSwitches_.at(index);
        bool relevant = false;
        for (const TvGuideScheduledSwitch &candidate : uniqueCandidates) {
            if (scheduledSwitchesMatch(existing, candidate) || scheduledSwitchesOverlap(existing, candidate)) {
                relevant = true;
                break;
            }
        }
        if (!relevant) {
            continue;
        }

        relevantExistingIndexSet.insert(index);
        choices.append(ScheduledSwitchChoiceOption{existing, true, index});
    }

    for (const TvGuideScheduledSwitch &candidate : uniqueCandidates) {
        bool represented = false;
        for (const ScheduledSwitchChoiceOption &choice : choices) {
            if (scheduledSwitchesMatch(choice.scheduledSwitch, candidate)) {
                represented = true;
                break;
            }
        }
        if (!represented) {
            choices.append(ScheduledSwitchChoiceOption{candidate, false, -1});
        }
    }

    if (choices.isEmpty()) {
        return false;
    }

    QList<int> choiceRatings;
    choiceRatings.reserve(choices.size());
    for (const ScheduledSwitchChoiceOption &choice : choices) {
        choiceRatings.append(favoriteShowRating(choice.scheduledSwitch.title));
    }

    int lockedChoiceIndex = -1;
    for (int index = 0; index < choices.size(); ++index) {
        const ScheduledSwitchChoiceOption &choice = choices.at(index);
        if (!choice.isExisting || !isScheduledSwitchLockedIn(choice.scheduledSwitch)) {
            continue;
        }
        lockedChoiceIndex = index;
        break;
    }
    if (lockedChoiceIndex < 0) {
        for (int index = 0; index < choices.size(); ++index) {
            if (isScheduledSwitchLockedIn(choices.at(index).scheduledSwitch)) {
                lockedChoiceIndex = index;
                break;
            }
        }
    }

    const bool ratingOverrideActive = favoriteShowRatingsOverrideEnabled_ && choices.size() > 1;
    const bool showChoiceRatings = ratingOverrideActive;
    int ratingChosenIndex = -1;
    bool ratingTiePrompt = false;
    bool choicesTrimmedByRating = false;
    if (ratingOverrideActive) {
        int highestRating = kDefaultFavoriteShowRating;
        QList<int> topRatedIndexes;
        for (int index = 0; index < choiceRatings.size(); ++index) {
            const int rating = choiceRatings.at(index);
            if (topRatedIndexes.isEmpty() || rating > highestRating) {
                highestRating = rating;
                topRatedIndexes = {index};
            } else if (rating == highestRating) {
                topRatedIndexes.append(index);
            }
        }

        if (topRatedIndexes.size() == 1) {
            ratingChosenIndex = topRatedIndexes.first();
        } else if (topRatedIndexes.size() > 1) {
            QList<ScheduledSwitchChoiceOption> topRatedChoices;
            QList<int> topRatedChoiceRatings;
            int remappedLockedChoiceIndex = -1;
            for (int sourceIndex : topRatedIndexes) {
                if (sourceIndex == lockedChoiceIndex) {
                    remappedLockedChoiceIndex = topRatedChoices.size();
                }
                topRatedChoices.append(choices.at(sourceIndex));
                topRatedChoiceRatings.append(choiceRatings.at(sourceIndex));
            }
            choicesTrimmedByRating = topRatedChoices.size() != choices.size();
            if (choicesTrimmedByRating) {
                appendLog(QString("%1 rating override dropped lower-rated choices and kept only priority %2 ties")
                              .arg(sourceDescription)
                              .arg(highestRating));
            }
            choices = topRatedChoices;
            choiceRatings = topRatedChoiceRatings;
            lockedChoiceIndex = remappedLockedChoiceIndex;
            ratingTiePrompt = true;
        }
    }

    auto choiceLogLabel = [this, &choiceRatings, showChoiceRatings](const ScheduledSwitchChoiceOption &choice,
                                                                    int choiceIndex) {
        QString label = scheduledSwitchChoiceDebugLabel(choice);
        if (showChoiceRatings && choiceIndex >= 0 && choiceIndex < choiceRatings.size()) {
            label += QString(" | priority=%1").arg(choiceRatings.at(choiceIndex));
        }
        return label;
    };

    QStringList choiceLabels;
    for (int index = 0; index < choices.size(); ++index) {
        choiceLabels.append(choiceLogLabel(choices.at(index), index));
    }
    appendLog(QString("%1 resolve choices=%2")
                  .arg(sourceDescription, choiceLabels.join(" || ")));
    const bool shouldPrompt = choices.size() > 1 && (promptForConflict || ratingTiePrompt);
    if (shouldPrompt) {
        logInteraction("program",
                       "schedule.conflict-dialog.show",
                       QString("source=%1 choices=%2")
                           .arg(sourceDescription, choiceLabels.join(" || ")));
    } else if (ratingChosenIndex >= 0 && choices.size() > 1) {
        appendLog(QString("%1 resolve using rating override -> %2")
                      .arg(sourceDescription, choiceLogLabel(choices.at(ratingChosenIndex), ratingChosenIndex)));
        showTransientStatusBarMessage(QString("Rating override kept %1 (priority %2)")
                                          .arg(choices.at(ratingChosenIndex).scheduledSwitch.title.trimmed().isEmpty()
                                                   ? choices.at(ratingChosenIndex).scheduledSwitch.channelName
                                                   : choices.at(ratingChosenIndex).scheduledSwitch.title.trimmed())
                                          .arg(choiceRatings.at(ratingChosenIndex)),
                                      4000);
    } else if (!shouldPrompt && choices.size() > 1 && lockedChoiceIndex >= 0) {
        appendLog(QString("%1 resolve using locked choice without prompting -> %2")
                      .arg(sourceDescription, choiceLogLabel(choices.at(lockedChoiceIndex), lockedChoiceIndex)));
        showTransientStatusBarMessage("Keeping locked scheduled switch", 4000);
    }

    int chosenIndex = -1;
    if (choices.size() == 1) {
        chosenIndex = 0;
    } else if (ratingChosenIndex >= 0) {
        chosenIndex = ratingChosenIndex;
    } else if (!shouldPrompt) {
        chosenIndex = lockedChoiceIndex;
        if (chosenIndex < 0) {
            for (int index = 0; index < choices.size(); ++index) {
                if (choices.at(index).isExisting) {
                    chosenIndex = index;
                    break;
                }
            }
        }
        if (chosenIndex < 0) {
            chosenIndex = 0;
        }
    } else {
        QDialog dialog(modalDialogParent());
        dialog.setWindowTitle("Schedule conflict");
        dialog.setWindowModality(Qt::WindowModal);

        auto *layout = new QVBoxLayout(&dialog);
        auto *titleLabel = new QLabel(ratingTiePrompt
                                          ? "Top-rated shows are tied. Choose the one to keep scheduled."
                                          : "Overlapping shows were found. Choose the one to keep scheduled.",
                                      &dialog);
        titleLabel->setWordWrap(true);
        layout->addWidget(titleLabel);

        QString detailText;
        if (ratingTiePrompt) {
            detailText = QString("These shows all have priority rating %1. Pick the winner for this airing.")
                             .arg(choiceRatings.isEmpty() ? kDefaultFavoriteShowRating : choiceRatings.first());
            if (choicesTrimmedByRating) {
                detailText += " Lower-rated conflicting shows will be removed automatically.";
            }
        } else {
            detailText = "Only entries that still overlap your selection will be removed.";
        }
        auto *detailLabel = new QLabel(detailText, &dialog);
        detailLabel->setWordWrap(true);
        layout->addWidget(detailLabel);

        auto *choicesList = new QListWidget(&dialog);
        int defaultRow = -1;
        int widestChoiceText = 0;
        const QFontMetrics choiceMetrics(choicesList->font());
        for (int index = 0; index < choices.size(); ++index) {
            const ScheduledSwitchChoiceOption &choice = choices.at(index);
            QString label = scheduledSwitchListLabel(choice.scheduledSwitch);
            if (showChoiceRatings) {
                label += QString(" | priority %1").arg(choiceRatings.at(index));
            }
            if (choice.isExisting) {
                label += " | existing";
            }
            if (isScheduledSwitchLockedIn(choice.scheduledSwitch)) {
                label += " | locked in";
            }
            widestChoiceText = std::max(widestChoiceText, choiceMetrics.horizontalAdvance(label));
            choicesList->addItem(label);
        }

        defaultRow = lockedChoiceIndex;
        if (defaultRow < 0) {
            for (int index = 0; index < choices.size(); ++index) {
                if (choices.at(index).isExisting) {
                    defaultRow = index;
                    break;
                }
            }
        }
        if (defaultRow < 0) {
            defaultRow = 0;
        }
        choicesList->setCurrentRow(defaultRow);
        choicesList->setSelectionMode(QAbstractItemView::SingleSelection);
        choicesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        layout->addWidget(choicesList, 1);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QPushButton *keepSelectedButton = buttons->button(QDialogButtonBox::Ok);
        QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel);
        if (keepSelectedButton != nullptr) {
            keepSelectedButton->setText("Keep Selected");
        }
        if (cancelButton != nullptr) {
            cancelButton->setText(relevantExistingIndexSet.isEmpty() ? "Leave Unchanged" : "Keep Existing");
        }
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(choicesList, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem *) {
            dialog.accept();
        });

        const QWidget *dialogParent = modalDialogParent();
        const QScreen *screen =
            dialogParent != nullptr && dialogParent->screen() != nullptr
                ? dialogParent->screen()
                : QGuiApplication::primaryScreen();
        const int availableWidth = screen != nullptr ? screen->availableGeometry().width() : 1280;
        const int availableHeight = screen != nullptr ? screen->availableGeometry().height() : 800;
        const int targetWidth = std::clamp(widestChoiceText + 140, 620, std::max(620, availableWidth - 120));
        const int visibleRows = std::clamp(static_cast<int>(choices.size()), 3, 8);
        const int targetHeight = std::clamp((visibleRows * choicesList->sizeHintForRow(0)) + 180,
                                            260,
                                            std::max(260, availableHeight - 120));
        choicesList->setMinimumWidth(targetWidth - 80);
        dialog.resize(targetWidth, targetHeight);

        if (execModalDialog(&dialog, "Schedule conflict") != QDialog::Accepted
            || choicesList->currentRow() < 0) {
            appendLog(QString("%1 left overlapping scheduled shows unchanged.").arg(sourceDescription));
            return false;
        }

        chosenIndex = choicesList->currentRow();
    }

    if (chosenIndex < 0 || chosenIndex >= choices.size()) {
        return false;
    }

    const ScheduledSwitchChoiceOption chosen = choices.at(chosenIndex);
    appendLog(QString("%1 resolve selected row=%2 -> %3")
                  .arg(sourceDescription)
                  .arg(chosenIndex)
                  .arg(choiceLogLabel(chosen, chosenIndex)));
    const QString cacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                      lastGuideWindowStartUtc_,
                                                      lastGuideSlotMinutes_,
                                                      lastGuideSlotCount_);
    const bool persistConflictChoice = shouldPrompt && choices.size() > 1 && !cacheStamp.isEmpty();
    bool conflictDecisionStateChanged = false;
    bool scheduledLockStateChanged = false;
    if (shouldPrompt && choices.size() > 1) {
        const bool chosenWasLockedIn = isScheduledSwitchLockedIn(chosen.scheduledSwitch);
        setScheduledSwitchLockedIn(chosen.scheduledSwitch, true);
        if (!chosenWasLockedIn) {
            scheduledLockStateChanged = true;
        }

        for (int index = 0; index < choices.size(); ++index) {
            if (index == chosenIndex) {
                continue;
            }

            const TvGuideScheduledSwitch &other = choices.at(index).scheduledSwitch;
            if (!scheduledSwitchesMatch(other, chosen.scheduledSwitch)
                && !scheduledSwitchesOverlap(other, chosen.scheduledSwitch)) {
                continue;
            }

            const bool otherWasLockedIn = isScheduledSwitchLockedIn(other);
            setScheduledSwitchLockedIn(other, false);
            if (otherWasLockedIn) {
                scheduledLockStateChanged = true;
            }
        }
    }
    if (persistConflictChoice) {
        const bool chosenWasLocked = isAutoFavoriteLockedIn(lockedAutoFavoriteSelections_,
                                                            cacheStamp,
                                                            chosen.scheduledSwitch);
        const bool chosenWasDismissed = isAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_,
                                                                cacheStamp,
                                                                chosen.scheduledSwitch);
        setAutoFavoriteLockedIn(lockedAutoFavoriteSelections_, cacheStamp, chosen.scheduledSwitch, true);
        setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, cacheStamp, chosen.scheduledSwitch, false);
        if (!chosenWasLocked || chosenWasDismissed) {
            conflictDecisionStateChanged = true;
        }

        for (int index = 0; index < choices.size(); ++index) {
            if (index == chosenIndex) {
                continue;
            }

            const TvGuideScheduledSwitch &other = choices.at(index).scheduledSwitch;
            if (!scheduledSwitchesMatch(other, chosen.scheduledSwitch)
                && !scheduledSwitchesOverlap(other, chosen.scheduledSwitch)) {
                continue;
            }

            const bool otherWasLocked = isAutoFavoriteLockedIn(lockedAutoFavoriteSelections_,
                                                               cacheStamp,
                                                               other);
            const bool otherWasDismissed = isAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_,
                                                                   cacheStamp,
                                                                   other);
            setAutoFavoriteLockedIn(lockedAutoFavoriteSelections_, cacheStamp, other, false);
            setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, cacheStamp, other, true);
            if (otherWasLocked || !otherWasDismissed) {
                conflictDecisionStateChanged = true;
            }
        }
    }
    bool changed = false;

    QList<int> relevantExistingIndexes = relevantExistingIndexSet.values();
    std::sort(relevantExistingIndexes.begin(), relevantExistingIndexes.end(), std::greater<int>());
    for (const int index : relevantExistingIndexes) {
        if (chosen.isExisting && index == chosen.existingIndex) {
            continue;
        }
        if (index < 0 || index >= scheduledSwitches_.size()) {
            continue;
        }
        const TvGuideScheduledSwitch &existingSwitch = scheduledSwitches_.at(index);
        if (!scheduledSwitchesMatch(existingSwitch, chosen.scheduledSwitch)
            && !scheduledSwitchesOverlap(existingSwitch, chosen.scheduledSwitch)) {
            appendLog(QString("%1 kept non-overlapping switch %2 while resolving %3")
                          .arg(sourceDescription,
                               scheduledSwitchLabel(existingSwitch),
                               scheduledSwitchLabel(chosen.scheduledSwitch)));
            continue;
        }
        appendLog(QString("%1 removed conflicting switch %2")
                      .arg(sourceDescription, scheduledSwitchLabel(existingSwitch)));
        scheduledSwitches_.removeAt(index);
        changed = true;
    }

    if (!chosen.isExisting) {
        bool alreadyScheduled = false;
        for (const TvGuideScheduledSwitch &existing : scheduledSwitches_) {
            if (scheduledSwitchesMatch(existing, chosen.scheduledSwitch)) {
                alreadyScheduled = true;
                break;
            }
        }
        if (!alreadyScheduled) {
            scheduledSwitches_.append(chosen.scheduledSwitch);
            changed = true;
        }
    }

    if (!changed) {
        if (persistConflictChoice) {
            savePersistedAutoFavoriteConflictState(cacheStamp,
                                                   dismissedAutoFavoriteCandidates_,
                                                   lockedAutoFavoriteSelections_);
            if (conflictDecisionStateChanged || scheduledLockStateChanged) {
                refreshScheduledSwitchList();
                updateTvGuideDialogFromCurrentCache(false);
            }
        }
        if (!persistConflictChoice && scheduledLockStateChanged) {
            refreshScheduledSwitchList();
            updateTvGuideDialogFromCurrentCache(false);
        }
        appendLog(QString("%1 kept %2").arg(sourceDescription, scheduledSwitchLabel(chosen.scheduledSwitch)));
        appendLog(QString("%1 resolve queue unchanged -> %2")
                      .arg(sourceDescription, summarizeScheduledSwitchesDebug(scheduledSwitches_)));
        return true;
    }

    std::sort(scheduledSwitches_.begin(), scheduledSwitches_.end(), [](const TvGuideScheduledSwitch &left,
                                                                       const TvGuideScheduledSwitch &right) {
        if (left.startUtc == right.startUtc) {
            return left.channelName < right.channelName;
        }
        return left.startUtc < right.startUtc;
    });
    pruneLockedScheduledSwitches();
    if (persistConflictChoice) {
        savePersistedAutoFavoriteConflictState(cacheStamp,
                                               dismissedAutoFavoriteCandidates_,
                                               lockedAutoFavoriteSelections_);
    }
    saveScheduledSwitches();
    refreshScheduledSwitchList();
    updateTvGuideDialogFromCurrentCache(false);
    refreshScheduledSwitchTimer();
    appendLog(QString("%1 kept %2").arg(sourceDescription, scheduledSwitchLabel(chosen.scheduledSwitch)));
    appendLog(QString("%1 resolve queue after -> %2")
                  .arg(sourceDescription, summarizeScheduledSwitchesDebug(scheduledSwitches_)));
    return true;
}

bool MainWindow::addScheduledSwitchCandidate(const TvGuideScheduledSwitch &candidate,
                                             const QString &sourceDescription,
                                             bool promptForConflict)
{
    Q_UNUSED(promptForConflict);

    if (candidate.channelName.trimmed().isEmpty()
        || !candidate.startUtc.isValid()
        || !candidate.endUtc.isValid()
        || candidate.endUtc <= candidate.startUtc) {
        return false;
    }

    if (scheduledSwitchListContains(scheduledSwitches_, candidate)) {
        appendLog(QString("%1 skipped duplicate queued switch %2")
                      .arg(sourceDescription, scheduledSwitchLabel(candidate)));
        return false;
    }

    scheduledSwitches_.append(candidate);
    std::sort(scheduledSwitches_.begin(), scheduledSwitches_.end(), [](const TvGuideScheduledSwitch &left,
                                                                       const TvGuideScheduledSwitch &right) {
        if (left.startUtc == right.startUtc) {
            return left.channelName < right.channelName;
        }
        return left.startUtc < right.startUtc;
    });
    saveScheduledSwitches();
    refreshScheduledSwitchList();
    updateTvGuideDialogFromCurrentCache(false);
    refreshScheduledSwitchTimer();
    appendLog(QString("%1 queued %2").arg(sourceDescription, scheduledSwitchLabel(candidate)));
    appendLog(QString("%1 queue after -> %2")
                  .arg(sourceDescription, summarizeScheduledSwitchesDebug(scheduledSwitches_)));
    return true;
}

void MainWindow::scheduleMatchingGuideEntriesForTitle(const QString &favoriteShowTitle,
                                                      const TvGuideScheduledSwitch &seedCandidate,
                                                      const QString &sourceDescription,
                                                      bool promptForConflict)
{
    const QString normalizedTitle = normalizeFavoriteShowRule(favoriteShowTitle);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QList<TvGuideScheduledSwitch> matchingCandidates;
    const auto appendCandidate = [&matchingCandidates](const TvGuideScheduledSwitch &candidate) {
        if (candidate.channelName.trimmed().isEmpty()
            || !candidate.startUtc.isValid()
            || !candidate.endUtc.isValid()
            || candidate.endUtc <= candidate.startUtc) {
            return;
        }

        for (const TvGuideScheduledSwitch &existingCandidate : matchingCandidates) {
            if (scheduledSwitchesMatch(existingCandidate, candidate)) {
                return;
            }
        }

        matchingCandidates.append(candidate);
    };

    if (seedCandidate.startUtc > nowUtc
        && normalizeFavoriteShowRule(seedCandidate.title) == normalizedTitle) {
        appendCandidate(seedCandidate);
    }

    if (!normalizedTitle.isEmpty()) {
        QStringList orderedChannels = lastGuideChannelOrder_;
        for (auto it = guideEntriesCache_.cbegin(); it != guideEntriesCache_.cend(); ++it) {
            if (!orderedChannels.contains(it.key())) {
                orderedChannels.append(it.key());
            }
        }

        for (const QString &channelName : orderedChannels) {
            const QList<TvGuideEntry> entries = guideEntriesCache_.value(channelName);
            for (const TvGuideEntry &entry : entries) {
                if (!entry.startUtc.isValid()
                    || !entry.endUtc.isValid()
                    || entry.endUtc <= entry.startUtc
                    || entry.startUtc <= nowUtc
                    || normalizeFavoriteShowRule(entry.title) != normalizedTitle) {
                    continue;
                }

                TvGuideScheduledSwitch candidate;
                candidate.channelName = channelName.trimmed();
                candidate.startUtc = entry.startUtc;
                candidate.endUtc = entry.endUtc;
                candidate.title = entry.title.trimmed();
                candidate.episode = entry.episode.trimmed();
                candidate.synopsis = entry.synopsis.trimmed();
                appendCandidate(candidate);
            }
        }
    }

    std::sort(matchingCandidates.begin(), matchingCandidates.end(), [](const TvGuideScheduledSwitch &left,
                                                                       const TvGuideScheduledSwitch &right) {
        if (left.startUtc == right.startUtc) {
            if (left.endUtc == right.endUtc) {
                return left.channelName.localeAwareCompare(right.channelName) < 0;
            }
            return left.endUtc < right.endUtc;
        }
        return left.startUtc < right.startUtc;
    });

    const QString showTitle = !favoriteShowTitle.simplified().isEmpty()
                                  ? favoriteShowTitle.simplified()
                                  : (seedCandidate.title.trimmed().isEmpty() ? "<untitled>" : seedCandidate.title.trimmed());
    appendLog(QString("%1 matched %2 guide entr%3 for %4")
                  .arg(sourceDescription)
                  .arg(matchingCandidates.size())
                  .arg(matchingCandidates.size() == 1 ? "y" : "ies")
                  .arg(showTitle));

    const QString cacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                      lastGuideWindowStartUtc_,
                                                      lastGuideSlotMinutes_,
                                                      lastGuideSlotCount_);
    for (const TvGuideScheduledSwitch &candidate : matchingCandidates) {
        addScheduledSwitchCandidate(candidate, sourceDescription, promptForConflict);
        if (scheduledSwitchListContains(scheduledSwitches_, candidate)) {
            setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, cacheStamp, candidate, false);
        }
    }

    savePersistedAutoFavoriteConflictState(cacheStamp,
                                           dismissedAutoFavoriteCandidates_,
                                           lockedAutoFavoriteSelections_);
}

void MainWindow::handleGuideScheduleToggle(const QString &channelName, const TvGuideEntry &entry, bool enabled)
{
    const QString trimmedChannelName = channelName.trimmed();
    if (trimmedChannelName.isEmpty()
        || !entry.startUtc.isValid()
        || !entry.endUtc.isValid()
        || entry.endUtc <= entry.startUtc) {
        updateTvGuideDialogFromCurrentCache(false);
        return;
    }

    TvGuideScheduledSwitch candidate;
    candidate.channelName = trimmedChannelName;
    candidate.startUtc = entry.startUtc;
    candidate.endUtc = entry.endUtc;
    candidate.title = entry.title.trimmed();
    candidate.episode = entry.episode.trimmed();
    candidate.synopsis = entry.synopsis.trimmed();
    logInteraction("user",
                   "schedule.guide-toggle",
                   QString("enabled=%1 candidate=%2 queue-before=%3")
                       .arg(enabled ? "true" : "false",
                            scheduledSwitchDebugLabel(candidate),
                            summarizeScheduledSwitchesDebug(scheduledSwitches_)));

    if (enabled) {
        if (candidate.startUtc <= QDateTime::currentDateTimeUtc()) {
            showInformationDialog("Only future shows",
                                  "Only future TV Guide entries can be scheduled for automatic tuning.");
            updateTvGuideDialogFromCurrentCache(false);
            return;
        }
        addFavoriteShowRule(candidate.title);
        scheduleMatchingGuideEntriesForTitle(candidate.title, candidate, "schedule:", false);
    } else {
        const QString cacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                          lastGuideWindowStartUtc_,
                                                          lastGuideSlotMinutes_,
                                                          lastGuideSlotCount_);
        bool removed = false;
        for (int index = scheduledSwitches_.size() - 1; index >= 0; --index) {
            if (!guideEntryMatchesScheduledSwitch(trimmedChannelName, entry, scheduledSwitches_.at(index))) {
                continue;
            }
            scheduledSwitches_.removeAt(index);
            removed = true;
        }
        if (removed) {
            setAutoFavoriteLockedIn(lockedAutoFavoriteSelections_, cacheStamp, candidate, false);
            setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, cacheStamp, candidate, true);
            savePersistedAutoFavoriteConflictState(cacheStamp,
                                                   dismissedAutoFavoriteCandidates_,
                                                   lockedAutoFavoriteSelections_);
            saveScheduledSwitches();
            refreshScheduledSwitchList();
            appendLog(QString("schedule: removed %1").arg(scheduledSwitchLabel(candidate)));
        }
    }

    updateTvGuideDialogFromCurrentCache(false);
    refreshScheduledSwitchTimer();
    logInteraction("program",
                   "schedule.guide-toggle.complete",
                   QString("queue-after=%1").arg(summarizeScheduledSwitchesDebug(scheduledSwitches_)));
}

void MainWindow::handleSearchScheduleRequested(const QString &favoriteShowTitle,
                                               const QString &channelName,
                                               const TvGuideEntry &entry)
{
    const QString trimmedChannelName = channelName.trimmed();
    const QString trimmedFavoriteTitle = favoriteShowTitle.simplified();
    if (trimmedChannelName.isEmpty()
        || trimmedFavoriteTitle.isEmpty()
        || !entry.startUtc.isValid()
        || !entry.endUtc.isValid()
        || entry.endUtc <= entry.startUtc) {
        return;
    }

    const bool addedFavoriteRule = addFavoriteShowRule(trimmedFavoriteTitle);
    const bool entryIsFuture = entry.startUtc > QDateTime::currentDateTimeUtc();
    TvGuideScheduledSwitch candidate;
    candidate.channelName = trimmedChannelName;
    candidate.startUtc = entry.startUtc;
    candidate.endUtc = entry.endUtc;
    candidate.title = entry.title.trimmed();
    candidate.episode = entry.episode.trimmed();
    candidate.synopsis = entry.synopsis.trimmed();

    if (entryIsFuture) {
        addScheduledSwitchCandidate(candidate, "favorite-show:", false);
        const QString cacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                          lastGuideWindowStartUtc_,
                                                          lastGuideSlotMinutes_,
                                                          lastGuideSlotCount_);
        if (scheduledSwitchListContains(scheduledSwitches_, candidate)) {
            setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, cacheStamp, candidate, false);
            savePersistedAutoFavoriteConflictState(cacheStamp,
                                                   dismissedAutoFavoriteCandidates_,
                                                   lockedAutoFavoriteSelections_);
        }
    } else {
        appendLog(QString("favorite-show: saved %1 without scheduling %2 because the selected airing is not in the future.")
                      .arg(trimmedFavoriteTitle, scheduledSwitchLabel(candidate)));
    }

    if (autoFavoriteShowSchedulingEnabled_) {
        autoScheduleFavoriteShowsFromGuideCache(false, true);
    } else if (addedFavoriteRule && entryIsFuture) {
        updateTvGuideDialogFromCurrentCache(false);
    }
}

void MainWindow::removeSelectedFavoriteShowRule()
{
    if (favoriteShowRulesList_ == nullptr) {
        return;
    }

    const int row = favoriteShowRulesList_->currentRow();
    if (row < 0 || row >= favoriteShowRules_.size()) {
        return;
    }

    const QString removedRule = favoriteShowRules_.at(row);
    favoriteShowRules_.removeAt(row);
    favoriteShowRatings_.remove(normalizeFavoriteShowRule(removedRule));
    saveFavoriteShowRules();
    refreshFavoriteShowRuleList();
    appendLog(QString("favorite-show: removed %1").arg(removedRule));
}

void MainWindow::removeSelectedScheduledSwitch()
{
    if (scheduledSwitchesList_ == nullptr) {
        return;
    }

    const int row = scheduledSwitchesList_->currentRow();
    if (row < 0 || row >= scheduledSwitches_.size()) {
        return;
    }

    const TvGuideScheduledSwitch removedSwitch = scheduledSwitches_.at(row);
    scheduledSwitches_.removeAt(row);
    const QString cacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                      lastGuideWindowStartUtc_,
                                                      lastGuideSlotMinutes_,
                                                      lastGuideSlotCount_);
    setAutoFavoriteLockedIn(lockedAutoFavoriteSelections_, cacheStamp, removedSwitch, false);
    setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, cacheStamp, removedSwitch, true);
    savePersistedAutoFavoriteConflictState(cacheStamp,
                                           dismissedAutoFavoriteCandidates_,
                                           lockedAutoFavoriteSelections_);
    saveScheduledSwitches();
    refreshScheduledSwitchList();
    updateTvGuideDialogFromCurrentCache(false);
    refreshScheduledSwitchTimer();
    appendLog(QString("schedule: removed %1").arg(scheduledSwitchLabel(removedSwitch)));
}

void MainWindow::autoScheduleFavoriteShowsFromGuideCache(bool promptForConflict, bool forceCurrentCacheSearch)
{
    if ((!autoFavoriteShowSchedulingEnabled_ && !forceCurrentCacheSearch)
        || favoriteShowRules_.isEmpty()
        || guideEntriesCache_.isEmpty()) {
        return;
    }

    const QString currentCacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                             lastGuideWindowStartUtc_,
                                                             lastGuideSlotMinutes_,
                                                             lastGuideSlotCount_);
    if (currentCacheStamp != lastAutoFavoriteScheduleStamp_) {
        dismissedAutoFavoriteCandidates_ =
            loadPersistedAutoFavoriteDecisionList(currentCacheStamp,
                                                  kDismissedAutoFavoriteCandidatesSetting);
        lockedAutoFavoriteSelections_ =
            loadPersistedAutoFavoriteDecisionList(currentCacheStamp,
                                                  kLockedAutoFavoriteSelectionsSetting);
        appendLog(QString("favorite-show auto: restored persisted conflict decisions for stamp %1 dismissed=%2 locked=%3")
                      .arg(currentCacheStamp)
                      .arg(dismissedAutoFavoriteCandidates_.size())
                      .arg(lockedAutoFavoriteSelections_.size()));
    }
    logInteraction("program",
                   "favorite-show.auto.begin",
                   QString("prompt=%1 force=%2 stamp=%3 favorites=%4 existing=%5")
                       .arg(promptForConflict ? "true" : "false",
                            forceCurrentCacheSearch ? "true" : "false",
                            currentCacheStamp,
                            favoriteShowRules_.join(" | "),
                            summarizeScheduledSwitchesDebug(scheduledSwitches_)));

    if (!forceCurrentCacheSearch
        && !currentCacheStamp.isEmpty()
        && currentCacheStamp == lastAutoFavoriteScheduleStamp_) {
        appendLog(QString("favorite-show auto: skipped because stamp %1 already processed").arg(currentCacheStamp));
        return;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QStringList orderedChannels = lastGuideChannelOrder_;
    for (auto it = guideEntriesCache_.cbegin(); it != guideEntriesCache_.cend(); ++it) {
        if (!orderedChannels.contains(it.key())) {
            orderedChannels.append(it.key());
        }
    }

    QList<TvGuideScheduledSwitch> matchedCandidates;
    for (const QString &favoriteRule : favoriteShowRules_) {
        const QString normalizedRule = normalizeFavoriteShowRule(favoriteRule);
        if (normalizedRule.isEmpty()) {
            continue;
        }

        for (const QString &channelName : orderedChannels) {
            const QList<TvGuideEntry> entries = guideEntriesCache_.value(channelName);
            for (const TvGuideEntry &entry : entries) {
                if (!entry.startUtc.isValid()
                    || !entry.endUtc.isValid()
                    || entry.endUtc <= entry.startUtc
                    || entry.startUtc <= nowUtc
                    || normalizeFavoriteShowRule(entry.title) != normalizedRule) {
                    continue;
                }

                TvGuideScheduledSwitch candidate;
                candidate.channelName = channelName.trimmed();
                candidate.startUtc = entry.startUtc;
                candidate.endUtc = entry.endUtc;
                candidate.title = entry.title.trimmed();
                candidate.episode = entry.episode.trimmed();
                candidate.synopsis = entry.synopsis.trimmed();
                if (isAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_, currentCacheStamp, candidate)) {
                    appendLog(QString("favorite-show auto: skipped dismissed candidate rule=%1 -> %2")
                                  .arg(favoriteRule, scheduledSwitchDebugLabel(candidate)));
                    continue;
                }

                bool duplicateCandidate = false;
                for (const TvGuideScheduledSwitch &existingCandidate : matchedCandidates) {
                    if (scheduledSwitchesMatch(existingCandidate, candidate)) {
                        duplicateCandidate = true;
                        break;
                    }
                }
                if (!duplicateCandidate) {
                    matchedCandidates.append(candidate);
                    appendLog(QString("favorite-show auto: matched rule=%1 -> %2")
                                  .arg(favoriteRule, scheduledSwitchDebugLabel(candidate)));
                }
            }
        }
    }

    std::sort(matchedCandidates.begin(), matchedCandidates.end(), [](const TvGuideScheduledSwitch &left,
                                                                     const TvGuideScheduledSwitch &right) {
        if (left.startUtc == right.startUtc) {
            if (left.endUtc == right.endUtc) {
                if (left.channelName == right.channelName) {
                    return left.title.localeAwareCompare(right.title) < 0;
                }
                return left.channelName.localeAwareCompare(right.channelName) < 0;
            }
            return left.endUtc < right.endUtc;
        }
        return left.startUtc < right.startUtc;
    });
    appendLog(QString("favorite-show auto: sorted candidates=%1")
                  .arg(summarizeScheduledSwitchesDebug(matchedCandidates)));

    for (const TvGuideScheduledSwitch &candidate : matchedCandidates) {
        appendLog(QString("favorite-show auto: queueing candidate=%1 queue-before=%2")
                      .arg(scheduledSwitchDebugLabel(candidate),
                           summarizeScheduledSwitchesDebug(scheduledSwitches_)));
        addScheduledSwitchCandidate(candidate, "favorite-show auto:", promptForConflict);
        setAutoFavoriteDismissed(dismissedAutoFavoriteCandidates_,
                                 currentCacheStamp,
                                 candidate,
                                 !scheduledSwitchListContains(scheduledSwitches_, candidate));
        appendLog(QString("favorite-show auto: candidate-result kept=%1 candidate=%2 queue-after=%3")
                      .arg(scheduledSwitchListContains(scheduledSwitches_, candidate) ? "true" : "false",
                           scheduledSwitchDebugLabel(candidate),
                           summarizeScheduledSwitchesDebug(scheduledSwitches_)));
    }

    lastAutoFavoriteScheduleStamp_ = currentCacheStamp;
    savePersistedAutoFavoriteConflictState(currentCacheStamp,
                                           dismissedAutoFavoriteCandidates_,
                                           lockedAutoFavoriteSelections_);
    if (!promptForConflict && !guideRefreshInProgress_) {
        const QString statusText = matchedCandidates.isEmpty()
                                       ? "Background favorite-switch scan found no new matches"
                                       : QString("Background favorite-switch scan checked %1 match%2")
                                             .arg(matchedCandidates.size())
                                             .arg(matchedCandidates.size() == 1 ? QString() : QString("es"));
        showTransientStatusBarMessage(statusText, 4000);
    }
    logInteraction("program",
                   "favorite-show.auto.complete",
                   QString("stamp=%1 final-queue=%2")
                       .arg(currentCacheStamp, summarizeScheduledSwitchesDebug(scheduledSwitches_)));
}

void MainWindow::showStartupSwitchSummary()
{
    const bool hideStartupSwitchSummary =
        hideStartupSwitchSummaryCheckBox_ != nullptr
            ? hideStartupSwitchSummaryCheckBox_->isChecked()
            : QSettings("tv_tuner_gui", "watcher").value(kHideStartupSwitchSummarySetting, false).toBool();
    if (startupSwitchSummaryShown_ || !obeyScheduledSwitches_ || hideStartupSwitchSummary) {
        return;
    }
    startupSwitchSummaryShown_ = true;

    QString message;
    if (scheduledSwitches_.isEmpty()) {
        message = "No scheduled switches are currently saved.";
    } else {
        QStringList lines;
        for (const TvGuideScheduledSwitch &scheduledSwitch : scheduledSwitches_) {
            lines.append(scheduledSwitchManagementLabel(scheduledSwitch,
                                                       isScheduledSwitchLockedIn(scheduledSwitch)));
        }
        message = lines.join('\n');
    }

    const QString loggedMessage = QString(message).replace('\n', "\\n");
    logInteraction("program",
                   "startup.switch-summary",
                   loggedMessage);
    QMessageBox messageBox(QMessageBox::Information,
                           "Scheduled switches at startup",
                           message,
                           QMessageBox::Ok,
                           modalDialogParent());
    messageBox.setTextFormat(Qt::PlainText);
    if (QLabel *label = messageBox.findChild<QLabel *>("qt_msgbox_label")) {
        label->setWordWrap(false);
    }
    if (auto *layout = qobject_cast<QGridLayout *>(messageBox.layout())) {
        layout->addItem(new QSpacerItem(520, 0, QSizePolicy::Minimum, QSizePolicy::Expanding),
                        layout->rowCount(),
                        0,
                        1,
                        layout->columnCount());
    }
    execModalDialog(&messageBox, "Scheduled switches at startup");
}

QStringList MainWindow::makeArguments() const
{
    QStringList args;
    args << "-f" << frontendTypeCombo_->currentData().toString();

    const QString country = countryEdit_->text().trimmed().toUpper();
    if (!country.isEmpty()) {
        args << "-c" << country;
    }

    const QString dvbPath = QString("/dev/dvb/adapter%1/frontend%2")
                                .arg(adapterSpin_->value())
                                .arg(frontendSpin_->value());
    args << "-a" << dvbPath;

    const QString outputSwitch = outputFormatCombo_->currentData().toString();
    if (!outputSwitch.isEmpty()) {
        args << "-" + outputSwitch;
    }

    return args;
}

void MainWindow::startScan()
{
    if (scanProcess_->state() != QProcess::NotRunning) {
        return;
    }

    stopWatching();
    channelsTable_->setRowCount(0);
    logOutput_->clear();
    partialStdOut_.clear();
    partialStdErr_.clear();
    channelLines_.clear();
    pendingScanChannelNumbersByName_.clear();
    channelHintsDirty_ = false;

    const QString program = "w_scan2";
    const QStringList args = makeArguments();

    appendLog("Starting: " + program + " " + args.join(' '));
    scanProcess_->start(program, args);

    if (!scanProcess_->waitForStarted(2000)) {
        showCriticalDialog("Failed to start", "Could not launch w_scan2. Check that it is in your PATH.");
        appendLog("Failed to start w_scan2.");
        return;
    }

    setScanningState(true);
    setStatusBarStateMessage("Scanning...");
}

void MainWindow::stopScan()
{
    if (scanProcess_->state() == QProcess::NotRunning) {
        return;
    }

    appendLog("Stopping scan...");
    scanProcess_->terminate();
}

void MainWindow::handleStdOut()
{
    partialStdOut_.append(QString::fromUtf8(scanProcess_->readAllStandardOutput()));
    const QStringList lines = partialStdOut_.split('\n');
    partialStdOut_ = lines.back();

    for (int i = 0; i < lines.size() - 1; ++i) {
        const QString line = lines[i].trimmed();
        if (!line.isEmpty()) {
            appendLog(line);
            parseAndStoreLine(line);
        }
    }
}

void MainWindow::handleStdErr()
{
    partialStdErr_.append(QString::fromUtf8(scanProcess_->readAllStandardError()));
    const QStringList lines = partialStdErr_.split('\n');
    partialStdErr_ = lines.back();

    static const QRegularExpression channelNumberPattern(
        QStringLiteral("Channel number:\\s*(\\d+[:.-]\\d+)\\.\\s*Name:\\s*'([^']+)'"),
        QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < lines.size() - 1; ++i) {
        const QString line = lines[i].trimmed();
        if (!line.isEmpty()) {
            appendLog("stderr: " + line);
            const QRegularExpressionMatch match = channelNumberPattern.match(line);
            if (match.hasMatch()) {
                const QString channelNumber = normalizeChannelNumberHint(match.captured(1));
                const QString channelName = match.captured(2).trimmed();
                if (!channelNumber.isEmpty() && !channelName.isEmpty()) {
                    pendingScanChannelNumbersByName_[channelName].append(channelNumber);
                }
            }
        }
    }
}

void MainWindow::processFinished(int exitCode)
{
    if (!partialStdOut_.trimmed().isEmpty()) {
        const QString line = partialStdOut_.trimmed();
        appendLog(line);
        parseAndStoreLine(line);
        partialStdOut_.clear();
    }
    if (!partialStdErr_.trimmed().isEmpty()) {
        appendLog("stderr: " + partialStdErr_.trimmed());
        partialStdErr_.clear();
    }

    setScanningState(false);
    persistChannelsFile();
    if (channelHintsDirty_) {
        QString saveError;
        if (!saveChannelHintsToJson(xspfNumberByTuneKey_, xspfProgramByChannel_, &saveError)
            && !saveError.trimmed().isEmpty()) {
            appendLog(saveError);
        }
    }
    pendingScanChannelNumbersByName_.clear();
    channelHintsDirty_ = false;
    const int rowCount = channelsTable_->rowCount();
    const QString endMsg = QString("Scan finished (exit=%1). Channels parsed: %2").arg(exitCode).arg(rowCount);
    appendLog(endMsg);
    setStatusBarStateMessage(endMsg);
}

void MainWindow::setScanningState(bool running)
{
    startButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
    watchButton_->setEnabled(!running);
    addFavoriteButton_->setEnabled(!running);
    removeFavoriteButton_->setEnabled(!running);
    for (auto *button : quickFavoriteButtons_) {
        if (button != nullptr) {
            button->setEnabled(!running && !button->property("channelName").toString().isEmpty());
        }
    }
    frontendTypeCombo_->setEnabled(!running);
    countryEdit_->setEnabled(!running);
    adapterSpin_->setEnabled(!running);
    frontendSpin_->setEnabled(!running);
    outputFormatCombo_->setEnabled(!running);
    syncFullscreenOverlayState();
}

void MainWindow::appendLog(const QString &line)
{
    const QString entry = QString("[%1] %2")
                              .arg(QDateTime::currentDateTime().toString("MM/dd/yyyy HH:mm:ss"), line);

    logOutput_->appendPlainText(entry);
    QTextCursor cursor = logOutput_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logOutput_->setTextCursor(cursor);

    if (!logFilePath_.isEmpty()) {
        QFile logFile(logFilePath_);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            logFile.write((entry + '\n').toUtf8());
        }
    }
}

void MainWindow::logInteraction(const QString &actor, const QString &action, const QString &details)
{
    QString line = QString("interaction: actor=%1 action=%2")
                       .arg(actor.simplified().isEmpty() ? "unknown" : actor.simplified(),
                            action.simplified().isEmpty() ? "unspecified" : action.simplified());
    const QString trimmedDetails = details.trimmed();
    if (!trimmedDetails.isEmpty()) {
        line += " details=" + trimmedDetails;
    }
    appendLog(line);
}

void MainWindow::parseAndStoreLine(const QString &line)
{
    if (line.startsWith('#') || line.startsWith("stderr:")) {
        return;
    }

    const QString normalizedLine = normalizeZapLine(line);
    const QStringList parts = normalizedLine.split(':');
    if (parts.size() < 3) {
        return;
    }

    const QString channelName = parts[0].trimmed();
    if (channelName.isEmpty()) {
        return;
    }

    if (!channelLines_.contains(normalizedLine)) {
        channelLines_.append(normalizedLine);
    }

    const QString tuneKeyValue = tuneKeyForParts(parts);
    QString channelNumberHint = normalizeChannelNumberHint(xspfNumberByTuneKey_.value(tuneKeyValue));
    if (channelNumberHint.isEmpty()) {
        QStringList &pendingNumbers = pendingScanChannelNumbersByName_[channelName];
        if (!pendingNumbers.isEmpty()) {
            channelNumberHint = normalizeChannelNumberHint(pendingNumbers.takeFirst());
            if (!channelNumberHint.isEmpty() && !tuneKeyValue.isEmpty()) {
                xspfNumberByTuneKey_.insert(tuneKeyValue, channelNumberHint);
                channelHintsDirty_ = true;
            }
        }
        if (pendingNumbers.isEmpty()) {
            pendingScanChannelNumbersByName_.remove(channelName);
        }
    }
    if (channelNumberHint.isEmpty()) {
        channelNumberHint = channelNumberHintForParts(parts, &xspfNumberByTuneKey_);
    }
    const QString channelNumber = displayChannelNumber(channelNumberHint).isEmpty()
                                      ? parts[5].trimmed()
                                      : displayChannelNumber(channelNumberHint);
    const QString displayLabel = channelDisplayLabel(channelName, channelNumberHint).isEmpty()
                                     ? channelName
                                     : channelDisplayLabel(channelName, channelNumberHint);
    const bool sortingEnabled = channelsTable_->isSortingEnabled();
    const int sortColumn = channelsTable_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = channelsTable_->horizontalHeader()->sortIndicatorOrder();

    if (sortingEnabled) {
        channelsTable_->setSortingEnabled(false);
    }

    const int row = channelsTable_->rowCount();
    channelsTable_->insertRow(row);
    channelsTable_->setItem(row, 0, new QTableWidgetItem(channelNumber));
    channelsTable_->setItem(row, 1, new QTableWidgetItem(displayLabel));
    channelsTable_->setItem(row, 2, new QTableWidgetItem(normalizedLine));

    xspfProgramByChannel_.insert(channelName, parts[5].trimmed());
    if (displayLabel != channelName) {
        xspfProgramByChannel_.insert(displayLabel, parts[5].trimmed());
    }

    if (sortingEnabled) {
        channelsTable_->setSortingEnabled(true);
        channelsTable_->sortItems(sortColumn >= 0 ? sortColumn : 0, sortOrder);
    }
}

bool MainWindow::persistChannelsFile()
{
    if (channelLines_.isEmpty()) {
        return false;
    }

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appendLog("Could not resolve app data directory for channels list.");
        return false;
    }

    QDir dir(appDataPath);
    if (!dir.exists() && !dir.mkpath(".")) {
        appendLog("Could not create app data directory: " + appDataPath);
        return false;
    }

    channelsFilePath_ = dir.filePath("channels.conf");
    QFile file(channelsFilePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendLog("Could not write channels file: " + channelsFilePath_);
        return false;
    }

    for (const QString &entry : channelLines_) {
        file.write(entry.toUtf8());
        file.write("\n");
    }
    file.close();
    appendLog("Channels saved: " + channelsFilePath_);
    return true;
}

QString MainWindow::selectedChannelNameFromTable() const
{
    const auto rows = channelsTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return {};
    }
    const int row = rows.first().row();
    const auto *item = channelsTable_->item(row, 1);
    if (item == nullptr) {
        return {};
    }
    return item->text().trimmed();
}

QString MainWindow::selectedChannelLineFromTable() const
{
    const auto rows = channelsTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return {};
    }
    const int row = rows.first().row();
    const auto *item = channelsTable_->item(row, 2);
    if (item == nullptr) {
        return {};
    }
    return normalizeZapLine(item->text()).trimmed();
}

QString MainWindow::firstChannelLineForName(const QString &channelName) const
{
    const QString trimmedChannelName = channelName.trimmed();
    if (trimmedChannelName.isEmpty()) {
        return {};
    }

    for (const QString &line : channelLines_) {
        const QString normalizedLine = normalizeZapLine(line).trimmed();
        const QString baseName = channelNameFromZapLine(normalizedLine).trimmed();
        const QString displayLabel = channelDisplayLabelForLine(normalizedLine, &xspfNumberByTuneKey_).trimmed();
        if (trimmedChannelName == baseName || trimmedChannelName == displayLabel) {
            return normalizedLine;
        }
    }

    QFile file(channelsFilePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    while (!file.atEnd()) {
        const QString normalizedLine = normalizeZapLine(QString::fromUtf8(file.readLine())).trimmed();
        const QString baseName = channelNameFromZapLine(normalizedLine).trimmed();
        const QString displayLabel = channelDisplayLabelForLine(normalizedLine, &xspfNumberByTuneKey_).trimmed();
        if (trimmedChannelName == baseName || trimmedChannelName == displayLabel) {
            return normalizedLine;
        }
    }

    return {};
}

bool MainWindow::highlightChannelInTable(const QString &channelName)
{
    if (channelsTable_ == nullptr || channelName.trimmed().isEmpty()) {
        return false;
    }

    for (int row = 0; row < channelsTable_->rowCount(); ++row) {
        QTableWidgetItem *channelItem = channelsTable_->item(row, 1);
        if (channelItem == nullptr || channelItem->text().trimmed() != channelName.trimmed()) {
            continue;
        }

        channelsTable_->selectRow(row);
        channelsTable_->setCurrentCell(row, 1);
        channelsTable_->scrollToItem(channelItem, QAbstractItemView::PositionAtCenter);
        return true;
    }

    return false;
}

bool MainWindow::highlightChannelLineInTable(const QString &channelLine)
{
    if (channelsTable_ == nullptr || channelLine.trimmed().isEmpty()) {
        return false;
    }

    const QString normalizedChannelLine = normalizeZapLine(channelLine).trimmed();
    for (int row = 0; row < channelsTable_->rowCount(); ++row) {
        QTableWidgetItem *rawLineItem = channelsTable_->item(row, 2);
        if (rawLineItem == nullptr || normalizeZapLine(rawLineItem->text()).trimmed() != normalizedChannelLine) {
            continue;
        }

        channelsTable_->selectRow(row);
        channelsTable_->setCurrentCell(row, 1);
        channelsTable_->scrollToItem(rawLineItem, QAbstractItemView::PositionAtCenter);
        return true;
    }

    return false;
}

QString MainWindow::programIdForChannel(const QString &channelName) const
{
    if (channelName.isEmpty()) {
        return {};
    }

    const QString trimmedChannelName = channelName.trimmed();
    if (xspfProgramByChannel_.contains(trimmedChannelName)) {
        return xspfProgramByChannel_.value(trimmedChannelName);
    }

    for (const QString &line : channelLines_) {
        const QString normalizedLine = normalizeZapLine(line).trimmed();
        const QString baseName = channelNameFromZapLine(normalizedLine);
        const QString displayLabel = channelDisplayLabelForLine(normalizedLine, &xspfNumberByTuneKey_);
        if (trimmedChannelName == baseName || trimmedChannelName == displayLabel) {
            const QString programId = programIdFromZapLine(normalizedLine);
            if (!programId.isEmpty()) {
                return programId;
            }
        }
    }

    QFile file(channelsFilePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    while (!file.atEnd()) {
        const QString normalizedLine = normalizeZapLine(QString::fromUtf8(file.readLine())).trimmed();
        const QString baseName = channelNameFromZapLine(normalizedLine);
        const QString displayLabel = channelDisplayLabelForLine(normalizedLine, &xspfNumberByTuneKey_);
        if (trimmedChannelName == baseName || trimmedChannelName == displayLabel) {
            const QString programId = programIdFromZapLine(normalizedLine);
            if (!programId.isEmpty()) {
                return programId;
            }
        }
    }
    return {};
}

void MainWindow::watchSelectedChannel()
{
    if (scanProcess_->state() != QProcess::NotRunning) {
        showWarningDialog("Scan in progress", "Stop scanning before starting live viewing.");
        return;
    }

    const QString channelLine = selectedChannelLineFromTable();
    const QString channelName = channelNameFromZapLine(channelLine);
    if (channelName.isEmpty()) {
        showInformationDialog("Select a channel", "Select a channel row first.");
        return;
    }

    startWatchingChannel(channelName, false, channelLine);
}

void MainWindow::openMediaFile()
{
    QFileDialog dialog(modalDialogParent(),
                       "Open Media File",
                       QDir::homePath(),
                       "Video Files (*.mp4 *.mkv *.webm *.avi *.mov *.ts *.m2ts);;All Files (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    if (fullscreenActive_) {
        dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    }
    if (execModalDialog(&dialog, "Open Media File") != QDialog::Accepted) {
        return;
    }
    const QStringList selectedFiles = dialog.selectedFiles();
    const QString filePath = selectedFiles.isEmpty() ? QString() : selectedFiles.first();
    if (filePath.isEmpty()) {
        return;
    }

    stopWatching();
    userStoppedWatching_ = true;
    setSignalMonitorStatus("Signal: local file", "Signal monitoring is only available during live tuner playback.");
    currentChannelName_ = "File: " + QFileInfo(filePath).fileName();
    setCurrentShowStatus(QFileInfo(filePath).fileName());

    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setSource(QUrl::fromLocalFile(filePath));
    mediaPlayer_->play();

    appendLog("player: Opened local media file: " + filePath);
    stopWatchButton_->setEnabled(true);
    playbackStatusLabel_->setText(playbackStatusText());
    syncFullscreenOverlayState();
    setStatusBarStateMessage("Opening media file");
}

bool MainWindow::refreshGuideData(bool interactive, bool updateDialog)
{
    const bool useSchedulesDirect = useSchedulesDirectGuideSource();
    if (!useSchedulesDirect
        && scanProcess_ != nullptr
        && scanProcess_->state() != QProcess::NotRunning) {
        if (interactive) {
            showWarningDialog("Scan in progress", "Stop scanning before opening the TV guide.");
        } else {
            appendLog("guide-bg: skipped guide refresh while a scan is in progress.");
        }
        return false;
    }

    if (useSchedulesDirect) {
        return refreshGuideDataFromSchedulesDirect(interactive, updateDialog);
    }

    if (channelLines_.isEmpty()) {
        loadChannelsFileIfPresent();
    }
    if (channelLines_.isEmpty()) {
        if (interactive) {
            showInformationDialog("No channels", "No channels are loaded yet. Run a scan first.");
        } else {
            appendLog("guide-bg: no channels are loaded yet; skipping background guide refresh.");
        }
        return false;
    }

    if (!persistChannelsFile() && channelsFilePath_.isEmpty()) {
        if (interactive) {
            showWarningDialog("Missing channels file",
                              "Could not prepare channels.conf for guide collection.");
        } else {
            appendLog("guide-bg: could not prepare channels.conf for background guide collection.");
        }
        return false;
    }

    const QVector<GuideChannelInfo> channels = parseGuideChannels(channelLines_, &xspfNumberByTuneKey_);
    if (channels.isEmpty()) {
        if (interactive) {
            showWarningDialog("Unsupported channel format",
                              "Could not parse channels.conf entries for guide mapping.");
        } else {
            appendLog("guide-bg: could not parse channels.conf entries for guide mapping.");
        }
        return false;
    }

    QHash<qint64, QVector<GuideChannelInfo>> channelsByFrequency;
    QStringList channelOrder;
    QSet<QString> channelSeen;
    for (const GuideChannelInfo &channel : channels) {
        channelsByFrequency[channel.frequencyHz].append(channel);
        if (!channelSeen.contains(channel.name)) {
            channelSeen.insert(channel.name);
            channelOrder.append(channel.name);
        }
    }

    const bool livePlaybackActive = zapProcess_ != nullptr
                                    && zapProcess_->state() != QProcess::NotRunning
                                    && !currentChannelName_.isEmpty()
                                    && !currentChannelName_.startsWith("File: ");
    const int playbackAdapter = adapterSpin_->value();
    int guideFrontend = -1;
    int guideAdapter = findPreferredGuideAdapter(frontendSpin_->value(), guideFrontend);
    if (guideAdapter < 0) {
        if (interactive) {
            showWarningDialog("Guide unavailable", "No tuner is available for EIT/guide collection.");
        } else {
            appendLog("guide-bg: no tuner is available for hidden guide collection.");
        }
        return false;
    }
    bool usingAlternateGuideAdapter = false;
    if (livePlaybackActive) {
        int alternateFrontend = -1;
        const int alternateAdapter = findPreferredSeparateGuideAdapter(playbackAdapter,
                                                                       frontendSpin_->value(),
                                                                       alternateFrontend);
        if (alternateAdapter >= 0) {
            guideAdapter = alternateAdapter;
            guideFrontend = alternateFrontend;
            usingAlternateGuideAdapter = true;
            appendLog(QString("guide: using adapter%1/frontend%2 while playback continues on adapter%3")
                          .arg(guideAdapter)
                          .arg(guideFrontend)
                          .arg(playbackAdapter));
        }
    }
    qint64 liveFrequencyHz = -1;
    bool currentProgramOk = false;
    const int currentProgramId = currentProgramId_.toInt(&currentProgramOk, 0);
    if (livePlaybackActive && guideAdapter == playbackAdapter && !usingAlternateGuideAdapter) {
        for (const GuideChannelInfo &channel : channels) {
            if (channel.name == currentChannelName_) {
                liveFrequencyHz = channel.frequencyHz;
                break;
            }
        }
        if (liveFrequencyHz <= 0 && currentProgramOk) {
            for (const GuideChannelInfo &channel : channels) {
                if (channel.serviceId == currentProgramId) {
                    liveFrequencyHz = channel.frequencyHz;
                    break;
                }
            }
        }
        if (liveFrequencyHz <= 0) {
            if (interactive) {
                showWarningDialog("Guide unavailable",
                                  "Could not determine the currently tuned multiplex while live playback is active.");
            } else {
                appendLog("guide-bg: could not determine the currently tuned multiplex during hidden guide refresh.");
            }
            return false;
        }
    }

    const auto setGuideRefreshInProgress = [this](bool active) {
        guideRefreshInProgress_ = active;
        setStatusBarStateMessage(lastStatusBarMessage_);
    };
    struct GuideRefreshGuard {
        std::function<void()> onExit;
        ~GuideRefreshGuard()
        {
            if (onExit) {
                onExit();
            }
        }
    };

    const bool liveMuxOnlyRefresh = livePlaybackActive && guideAdapter == playbackAdapter && liveFrequencyHz > 0;
    if (guideEntriesCache_.isEmpty()) {
        loadGuideCacheFile();
    }
    const bool hadGuideCache = !guideEntriesCache_.isEmpty();
    setGuideRefreshInProgress(true);
    GuideRefreshGuard guideRefreshGuard{[&setGuideRefreshInProgress]() {
        setGuideRefreshInProgress(false);
    }};

    QString loadingMessage = "Collecting OTA schedule data from EIT...";
    QString statusMessage = "Collecting OTA guide data...";
    if (usingAlternateGuideAdapter) {
        loadingMessage = QString("Collecting OTA schedule data on adapter%1 while %2 continues playing on adapter%3...")
                             .arg(guideAdapter)
                             .arg(currentChannelName_)
                             .arg(playbackAdapter);
        statusMessage = QString("Collecting OTA guide data on adapter%1...").arg(guideAdapter);
    } else if (liveMuxOnlyRefresh) {
        loadingMessage = QString("Collecting OTA schedule data from the current multiplex while %1 continues playing...")
                             .arg(currentChannelName_);
        statusMessage = "Collecting OTA guide data from the current multiplex...";
    }
    if (updateDialog && tvGuideDialog_ != nullptr) {
        tvGuideDialog_->setLoadingState(loadingMessage);
    }
    if (interactive) {
        setStatusBarStateMessage(statusMessage);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    } else {
        appendLog(QString("guide-bg: starting hidden guide refresh (%1)").arg(statusMessage));
        setStatusBarStateMessage(statusMessage);
    }

    QList<qint64> frequencies = channelsByFrequency.keys();
    if (liveMuxOnlyRefresh) {
        frequencies = { liveFrequencyHz };
    }
    const auto progressStatusMessage = [&statusMessage, &frequencies](int completedFrequencies) {
        const int totalFrequencies = frequencies.size();
        if (totalFrequencies <= 0) {
            return statusMessage;
        }
        const int boundedCompleted = std::clamp(completedFrequencies, 0, totalFrequencies);
        const int percent = static_cast<int>(std::lround((100.0 * boundedCompleted) / totalFrequencies));
        return QString("%1 %2%").arg(statusMessage).arg(percent);
    };

    QProgressDialog *progress = nullptr;
    if (interactive) {
        progress = new QProgressDialog(liveMuxOnlyRefresh
                                           ? "Collecting OTA EIT schedule data from the current multiplex..."
                                           : "Collecting OTA EIT schedule data...",
                                       "Cancel",
                                       0,
                                       frequencies.size(),
                                       modalDialogParent());
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        prepareModalWindow(progress, statusMessage);
        progress->show();
    }

    QHash<QString, QList<TvGuideEntry>> entriesByChannel = guideEntriesCache_;
    QStringList errors;
    int frequenciesWithData = 0;
    int decodedEvents = 0;
    QSet<int> observedPsipTableIds;
    QJsonArray betaExtractions;
    QStringList muxSummaryLines;
    QStringList missingChannelNames;

    if (interactive) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
    }
    setStatusBarStateMessage(progressStatusMessage(0));
    for (int i = 0; i < frequencies.size(); ++i) {
        const qint64 frequencyHz = frequencies.at(i);
        const QVector<GuideChannelInfo> frequencyChannels = channelsByFrequency.value(frequencyHz);
        if (frequencyChannels.isEmpty()) {
            continue;
        }
        const bool reuseCurrentTunedMux = liveMuxOnlyRefresh && frequencyHz == liveFrequencyHz;

        if (progress != nullptr) {
            progress->setValue(i);
            progress->setLabelText(QString("%1 %2 MHz (%3/%4) via %5")
                                       .arg(reuseCurrentTunedMux ? "Reading" : "Tuning")
                                       .arg(QString::number(static_cast<double>(frequencyHz) / 1000000.0, 'f', 1))
                                       .arg(i + 1)
                                       .arg(frequencies.size())
                                       .arg(frequencyChannels.first().name));
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        if (progress != nullptr && progress->wasCanceled()) {
            break;
        }

        QList<RawGuideEvent> frequencyEvents;
        QList<RawGuideSection> rawSections;
        QHash<int, int> atscSourceToProgram;
        QSet<int> psipTableIds;
        QString errorText;
        QSet<int> expectedServiceIds;
        for (const GuideChannelInfo &channel : frequencyChannels) {
            expectedServiceIds.insert(channel.serviceId);
        }
        if (reuseCurrentTunedMux) {
            captureGuideEventsFromDemux(guideAdapter,
                                        frequencyChannels.first().name,
                                        expectedServiceIds,
                                        frequencyEvents,
                                        atscSourceToProgram,
                                        psipTableIds,
                                        errorText,
                                        kGuideCaptureMaxMs,
                                        &rawSections);
        } else {
            captureGuideEventsForChannel(channelsFilePath_,
                                         guideAdapter,
                                         guideFrontend,
                                         frequencyChannels.first().name,
                                         expectedServiceIds,
                                         frequencyEvents,
                                         atscSourceToProgram,
                                         psipTableIds,
                                         errorText,
                                         kGuideLookupTotalMaxMs,
                                         &rawSections);
        }
        appendLog(QString("guide: mux %1 tables=%2 decoded=%3 source-map=%4")
                      .arg(frequencyChannels.first().name,
                           formatTableIdSet(psipTableIds).isEmpty() ? "none" : formatTableIdSet(psipTableIds))
                      .arg(frequencyEvents.size())
                      .arg(atscSourceToProgram.size()));
        observedPsipTableIds.unite(psipTableIds);
        if (!errorText.isEmpty()) {
            errors.append(errorText);
        }

        if (frequencyEvents.isEmpty()) {
            QStringList missingForMux;
            for (const GuideChannelInfo &channel : frequencyChannels) {
                missingForMux.append(channel.name);
                missingChannelNames.append(channel.name);
            }
            muxSummaryLines.append(QString("%1 MHz: 0/%2 channels missing %3")
                                       .arg(QString::number(static_cast<double>(frequencyHz) / 1000000.0, 'f', 1))
                                       .arg(frequencyChannels.size())
                                       .arg(missingForMux.join(", ")));
            continue;
        }
        ++frequenciesWithData;
        decodedEvents += frequencyEvents.size();

        int mappedForFrequency = 0;
        QSet<QString> mappedChannelNamesForMux;
        const QHash<QString, QList<TvGuideEntry>> mappedEntriesForMux =
            mapGuideEntriesForFrequency(frequencyChannels,
                                        frequencyEvents,
                                        atscSourceToProgram,
                                        &mappedForFrequency,
                                        &mappedChannelNamesForMux);
        if (!mappedEntriesForMux.isEmpty()) {
            for (const GuideChannelInfo &channel : frequencyChannels) {
                QList<TvGuideEntry> mergedEntries = entriesByChannel.value(channel.name);
                mergedEntries.append(mappedEntriesForMux.value(channel.name));
                entriesByChannel.insert(channel.name, mergedEntries);
            }
        }
        appendLog(QString("guide: mux %1 mapped=%2").arg(frequencyChannels.first().name).arg(mappedForFrequency));

        QStringList missingForMux;
        for (const GuideChannelInfo &channel : frequencyChannels) {
            if (!mappedChannelNamesForMux.contains(channel.name)) {
                missingForMux.append(channel.name);
                missingChannelNames.append(channel.name);
            }
        }

        QString muxSummary = QString("%1 MHz: %2/%3 channels")
                                 .arg(QString::number(static_cast<double>(frequencyHz) / 1000000.0, 'f', 1))
                                 .arg(mappedChannelNamesForMux.size())
                                 .arg(frequencyChannels.size());
        if (!missingForMux.isEmpty()) {
            muxSummary += QString(" missing %1").arg(missingForMux.join(", "));
        }
        muxSummaryLines.append(muxSummary);

        QJsonObject extractionObject;
        extractionObject.insert("frequencyHz", QString::number(frequencyHz));
        extractionObject.insert("frequencyMHz",
                                QString::number(static_cast<double>(frequencyHz) / 1000000.0, 'f', 1));
        extractionObject.insert("channels", guideChannelsToJsonArray(frequencyChannels));
        extractionObject.insert("usedCurrentTunedMux", reuseCurrentTunedMux);
        extractionObject.insert("decodedEventCount", frequencyEvents.size());
        extractionObject.insert("mappedEventCount", mappedForFrequency);
        extractionObject.insert("mappedChannelNames", stringSetToJsonArray(mappedChannelNamesForMux));
        extractionObject.insert("psipTableIds", integerSetToJsonArray(psipTableIds));
        extractionObject.insert("sourceToProgram", integerMapToJsonObject(atscSourceToProgram));
        extractionObject.insert("rawEvents", rawGuideEventsToJsonArray(frequencyEvents, atscSourceToProgram));
        extractionObject.insert("rawSectionCount", rawSections.size());
        extractionObject.insert("rawSections", rawGuideSectionsToJsonArray(rawSections));
        if (!errorText.trimmed().isEmpty()) {
            extractionObject.insert("errorText", errorText.trimmed());
        }
        betaExtractions.append(extractionObject);
        setStatusBarStateMessage(progressStatusMessage(i + 1));
    }
    if (progress != nullptr) {
        progress->setValue(frequencies.size());
        delete progress;
        restoreAfterModalWindow();
    }
    if (interactive) {
        QApplication::restoreOverrideCursor();
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const int retentionHours = guideCacheRetentionHoursValue(guideCacheRetentionSpin_);
    int mappedEntries = 0;
    QDateTime latestEndUtc = nowUtc.addSecs(6 * 3600);

    for (const QString &channelName : channelOrder) {
        const QList<TvGuideEntry> cleaned = cleanGuideEntries(entriesByChannel.value(channelName),
                                                              nowUtc,
                                                              retentionHours,
                                                              &latestEndUtc);
        mappedEntries += cleaned.size();
        entriesByChannel.insert(channelName, cleaned);
        if (cleaned.isEmpty()) {
            noAutoCurrentShowLookupChannels_.insert(channelName);
        } else {
            noAutoCurrentShowLookupChannels_.remove(channelName);
        }
    }

    QDateTime windowStartUtc = nowUtc;
    windowStartUtc = windowStartUtc.addSecs(-windowStartUtc.time().second());
    windowStartUtc = windowStartUtc.addMSecs(-windowStartUtc.time().msec());
    windowStartUtc = windowStartUtc.addSecs(-(windowStartUtc.time().minute() % 30) * 60);

    constexpr int slotMinutes = 30;
    const qint64 minimumWindowSeconds = 6 * 3600;
    const qint64 requestedWindowSeconds = std::max(minimumWindowSeconds, windowStartUtc.secsTo(latestEndUtc) + slotMinutes * 60);
    int slotCount = static_cast<int>(std::ceil(static_cast<double>(requestedWindowSeconds) / (slotMinutes * 60.0)));
    slotCount = std::clamp(slotCount, 12, 32);

    QString statusText = QString("Guide events: %1 mapped from %2 decoded across %3/%4 multiplexes.")
                             .arg(mappedEntries)
                             .arg(decodedEvents)
                             .arg(frequenciesWithData)
                             .arg(frequencies.size());
    if (mappedEntries == 0) {
        statusText = QString("No EIT schedule entries mapped. Checked %1 multiplexes.")
                         .arg(frequencies.size());
        if (observedPsipTableIds.contains(0xc8) && !observedPsipTableIds.contains(0xcb)) {
            statusText += " ATSC PSIP was present, but EIT (table 0xCB) was not broadcast.";
        }
        if (observedPsipTableIds.contains(0xcb) && mappedEntries == 0) {
            statusText += " EIT was detected, but did not map to current channel ids.";
        }
    }
    if (!errors.isEmpty()) {
        appendLog("guide: " + errors.first());
        statusText += " Some multiplexes returned no schedule data.";
    }
    if (!muxSummaryLines.isEmpty()) {
        statusText += "\n" + muxSummaryLines.join("\n");
    }
    missingChannelNames.removeDuplicates();
    if (!missingChannelNames.isEmpty()) {
        statusText += QString("\nNo guide data: %1").arg(missingChannelNames.join(", "));
    }

    if (usingAlternateGuideAdapter) {
        QString liveStatus = QString("Live playback active: full guide refreshed on adapter%1 while %2 continued playing on adapter%3.")
                                 .arg(guideAdapter)
                                 .arg(currentChannelName_)
                                 .arg(playbackAdapter);
        statusText = liveStatus + "\n" + statusText;
    } else if (liveMuxOnlyRefresh) {
        QString liveStatus = QString("Live playback active: refreshed guide from the current multiplex only for %1.")
                                 .arg(currentChannelName_);
        liveStatus += " Full all-channel guide collection still requires retuning or a second tuner.";
        if (hadGuideCache) {
            liveStatus += " Cached guide rows are kept for channels that were not refreshed.";
        }
        statusText = liveStatus + "\n" + statusText;
    }

    lastGuideChannelOrder_ = channelOrder;
    lastGuideWindowStartUtc_ = windowStartUtc;
    lastGuideSlotMinutes_ = slotMinutes;
    lastGuideSlotCount_ = slotCount;
    lastGuideStatusText_ = statusText;
    guideEntriesCache_ = entriesByChannel;
    if (!writeGuideCacheFile(channelOrder, entriesByChannel, windowStartUtc, slotMinutes, slotCount, statusText)) {
        appendLog("guide: failed to write guide cache file.");
    } else if (!loadGuideCacheFile()) {
        appendLog("guide: failed to reload guide cache file after refresh; using in-memory cache.");
        guideEntriesCache_ = entriesByChannel;
        lastGuideChannelOrder_ = channelOrder;
        lastGuideWindowStartUtc_ = windowStartUtc;
        lastGuideSlotMinutes_ = slotMinutes;
        lastGuideSlotCount_ = slotCount;
        lastGuideStatusText_ = statusText;
    }

    if (!writeGuideCacheBetaFile(channelOrder,
                                 entriesByChannel,
                                 windowStartUtc,
                                 slotMinutes,
                                 slotCount,
                                 statusText,
                                 betaExtractions)) {
        appendLog("guide: failed to write beta guide cache file.");
    }
    applyCurrentShowStatusFromGuideCache();
    if (updateDialog && tvGuideDialog_ != nullptr) {
        tvGuideDialog_->setGuideData(lastGuideChannelOrder_,
                                     favorites_,
                                     favoriteShowRatings_,
                                     guideEntriesCache_,
                                     lastGuideWindowStartUtc_,
                                     lastGuideSlotMinutes_,
                                     lastGuideSlotCount_,
                                     scheduledSwitches_,
                                     lastGuideStatusText_);
    }
    setStatusBarStateMessage(statusText.section('\n', 0, 0));
    return true;
}

void MainWindow::refreshTvGuide()
{
    if (!refreshGuideData(true, true)) {
        loadGuideCacheFile();
        updateTvGuideDialogFromCurrentCache(true);
    }
}

bool MainWindow::writeGuideCacheFile(const QStringList &channelOrder,
                                     const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                                     const QDateTime &windowStartUtc,
                                     int slotMinutes,
                                     int slotCount,
                                     const QString &statusText)
{
    const QString cachePath = resolveGuideCachePath();
    if (cachePath.isEmpty()) {
        return false;
    }

    QFileInfo cacheInfo(cachePath);
    QDir cacheDir = cacheInfo.dir();
    if (!cacheDir.exists() && !cacheDir.mkpath(".")) {
        return false;
    }

    QJsonObject root;
    root.insert("version", 1);
    root.insert("generatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert("windowStartUtc", windowStartUtc.toString(Qt::ISODateWithMs));
    root.insert("slotMinutes", slotMinutes);
    root.insert("slotCount", slotCount);
    root.insert("statusText", statusText);

    QJsonArray channelOrderArray;
    for (const QString &channelName : channelOrder) {
        channelOrderArray.append(channelName);
    }
    root.insert("channelOrder", channelOrderArray);

    QJsonObject entriesObject;
    for (auto it = entriesByChannel.cbegin(); it != entriesByChannel.cend(); ++it) {
        entriesObject.insert(it.key(), guideEntriesToJsonArray(it.value()));
    }
    root.insert("entriesByChannel", entriesObject);

    QSaveFile cacheFile(cachePath);
    if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (cacheFile.write(payload) != payload.size()) {
        cacheFile.cancelWriting();
        return false;
    }
    return cacheFile.commit();
}

namespace {
bool writeGuideCacheBetaFile(const QStringList &channelOrder,
                             const QHash<QString, QList<TvGuideEntry>> &entriesByChannel,
                             const QDateTime &windowStartUtc,
                             int slotMinutes,
                             int slotCount,
                             const QString &statusText,
                             const QJsonArray &extractions)
{
    const QString cachePath = resolveGuideCacheBetaPath();
    if (cachePath.isEmpty()) {
        return false;
    }

    QFileInfo cacheInfo(cachePath);
    QDir cacheDir = cacheInfo.dir();
    if (!cacheDir.exists() && !cacheDir.mkpath(".")) {
        return false;
    }

    QJsonObject root;
    root.insert("version", 2);
    root.insert("generatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert("windowStartUtc", windowStartUtc.toString(Qt::ISODateWithMs));
    root.insert("slotMinutes", slotMinutes);
    root.insert("slotCount", slotCount);
    root.insert("statusText", statusText);

    QJsonArray channelOrderArray;
    for (const QString &channelName : channelOrder) {
        channelOrderArray.append(channelName);
    }
    root.insert("channelOrder", channelOrderArray);

    QJsonObject entriesObject;
    for (auto it = entriesByChannel.cbegin(); it != entriesByChannel.cend(); ++it) {
        entriesObject.insert(it.key(), guideEntriesToJsonArray(it.value()));
    }
    root.insert("entriesByChannel", entriesObject);
    root.insert("extractions", extractions);

    QSaveFile cacheFile(cachePath);
    if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (cacheFile.write(payload) != payload.size()) {
        cacheFile.cancelWriting();
        return false;
    }
    return cacheFile.commit();
}
}

bool MainWindow::refreshGuideDataFromSchedulesDirect(bool interactive, bool updateDialog)
{
    if (guideEntriesCache_.isEmpty()) {
        loadGuideCacheFile();
    }

    const auto setGuideRefreshInProgress = [this](bool active) {
        guideRefreshInProgress_ = active;
        setStatusBarStateMessage(lastStatusBarMessage_);
    };
    struct GuideRefreshGuard {
        std::function<void()> onExit;
        ~GuideRefreshGuard()
        {
            if (onExit) {
                onExit();
            }
        }
    };

    setGuideRefreshInProgress(true);
    GuideRefreshGuard guideRefreshGuard{[&setGuideRefreshInProgress]() {
        setGuideRefreshInProgress(false);
    }};

    const QString loadingMessage = "Downloading OTA schedule data from Schedules Direct...";
    if (updateDialog && tvGuideDialog_ != nullptr) {
        tvGuideDialog_->setLoadingState(loadingMessage);
    }
    setStatusBarStateMessage("Downloading Schedules Direct guide data...");
    if (!interactive) {
        appendLog("guide-bg: starting Schedules Direct guide refresh.");
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString exportSummary;
    QString exportError;
    bool usedCachedExport = false;
    if (!ensureSchedulesDirectJson(true, &usedCachedExport, &exportSummary, &exportError)) {
        const QString statusText = exportError.trimmed().isEmpty()
                                       ? QString("Schedules Direct guide refresh failed.")
                                       : exportError.trimmed();
        appendLog(QString("guide-sd: %1").arg(statusText));
        if (updateDialog && tvGuideDialog_ != nullptr) {
            tvGuideDialog_->setLoadingState(statusText);
        }
        setStatusBarStateMessage(statusText.section('\n', 0, 0));
        return false;
    }

    QString exportPath = resolveSchedulesDirectExportPath();
    if (!QFileInfo::exists(exportPath)) {
        const QString legacyExportPath = resolveLegacySchedulesDirectExportPath();
        if (QFileInfo::exists(legacyExportPath)) {
            exportPath = legacyExportPath;
        }
    }

    QFile exportFile(exportPath);
    if (!exportFile.exists() || !exportFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString statusText = QString("Could not open %1 for Schedules Direct guide import.").arg(exportPath);
        appendLog(QString("guide-sd: %1").arg(statusText));
        if (updateDialog && tvGuideDialog_ != nullptr) {
            tvGuideDialog_->setLoadingState(statusText);
        }
        setStatusBarStateMessage("Schedules Direct guide import failed");
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(exportFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QString statusText = QString("Could not parse %1 (%2).").arg(exportPath, parseError.errorString());
        appendLog(QString("guide-sd: %1").arg(statusText));
        if (updateDialog && tvGuideDialog_ != nullptr) {
            tvGuideDialog_->setLoadingState(statusText);
        }
        setStatusBarStateMessage("Schedules Direct guide import failed");
        return false;
    }

    const QJsonObject root = document.object();
    QStringList channelOrder;
    const QJsonArray channelOrderArray = root.value("channelOrder").toArray();
    for (const QJsonValue &value : channelOrderArray) {
        const QString channelName = value.toString().trimmed();
        if (!channelName.isEmpty() && !channelOrder.contains(channelName)) {
            channelOrder.append(channelName);
        }
    }
    if (channelOrder.isEmpty()) {
        const QVector<GuideChannelInfo> localChannels = parseGuideChannels(channelLines_, &xspfNumberByTuneKey_);
        QSet<QString> seenChannelNames;
        for (const GuideChannelInfo &channel : localChannels) {
            if (!channel.name.trimmed().isEmpty() && !seenChannelNames.contains(channel.name)) {
                seenChannelNames.insert(channel.name);
                channelOrder.append(channel.name);
            }
        }
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const int retentionHours = guideCacheRetentionHoursValue(guideCacheRetentionSpin_);
    QDateTime latestEndUtc = nowUtc.addSecs(6 * 3600);
    QHash<QString, QList<TvGuideEntry>> entriesByChannel;
    const QJsonObject entriesObject = root.value("entriesByChannel").toObject();
    if (!entriesObject.isEmpty()) {
        for (auto it = entriesObject.begin(); it != entriesObject.end(); ++it) {
            entriesByChannel.insert(it.key(),
                                    cleanGuideEntries(guideEntriesFromJsonArray(it.value().toArray()),
                                                      nowUtc,
                                                      retentionHours,
                                                      &latestEndUtc));
        }
    } else {
        QStringList importedChannels;
        QStringList unmatchedChannels;
        QStringList skippedChannels;
        int importedEntryCount = 0;
        applySchedulesDirectGuideFallback(entriesByChannel,
                                          retentionHours,
                                          nowUtc,
                                          &latestEndUtc,
                                          &importedChannels,
                                          &unmatchedChannels,
                                          &skippedChannels,
                                          &importedEntryCount);
    }

    int mappedEntries = 0;
    QStringList missingChannelNames;
    for (const QString &channelName : channelOrder) {
        const QList<TvGuideEntry> cleaned =
            cleanGuideEntries(entriesByChannel.value(channelName), nowUtc, retentionHours, &latestEndUtc);
        entriesByChannel.insert(channelName, cleaned);
        mappedEntries += cleaned.size();
        if (cleaned.isEmpty()) {
            noAutoCurrentShowLookupChannels_.insert(channelName);
            missingChannelNames.append(channelName);
        } else {
            noAutoCurrentShowLookupChannels_.remove(channelName);
        }
    }

    QDateTime windowStartUtc = QDateTime::fromString(root.value("windowStartUtc").toString(), Qt::ISODateWithMs);
    if (!windowStartUtc.isValid()) {
        windowStartUtc = nowUtc;
        windowStartUtc = windowStartUtc.addSecs(-windowStartUtc.time().second());
        windowStartUtc = windowStartUtc.addMSecs(-windowStartUtc.time().msec());
        windowStartUtc = windowStartUtc.addSecs(-(windowStartUtc.time().minute() % 30) * 60);
    }

    const int slotMinutes = std::clamp(root.value("slotMinutes").toInt(30), 15, 120);
    int slotCount = root.value("slotCount").toInt(12);
    if (slotCount <= 0) {
        const qint64 minimumWindowSeconds = 6 * 3600;
        const qint64 requestedWindowSeconds =
            std::max(minimumWindowSeconds, windowStartUtc.secsTo(latestEndUtc) + slotMinutes * 60);
        slotCount = static_cast<int>(std::ceil(static_cast<double>(requestedWindowSeconds) / (slotMinutes * 60.0)));
        slotCount = std::clamp(slotCount, 12, 32);
    }

    QString statusText = root.value("statusText").toString().trimmed();
    if (statusText.isEmpty()) {
        statusText = QString("Schedules Direct OTA guide: %1 entries loaded for %2/%3 local channels.")
                         .arg(mappedEntries)
                         .arg(channelOrder.size() - missingChannelNames.size())
                         .arg(channelOrder.size());
    }
    if (!exportSummary.trimmed().isEmpty()) {
        statusText = exportSummary.trimmed() + "\n" + statusText;
    }
    if (!missingChannelNames.isEmpty()
        && !statusText.contains(QString("No guide data: %1").arg(missingChannelNames.join(", ")))) {
        statusText += QString("\nNo guide data: %1").arg(missingChannelNames.join(", "));
    }
    if (usedCachedExport) {
        statusText = "Using cached Schedules Direct JSON.\n" + statusText;
    }

    lastGuideChannelOrder_ = channelOrder;
    lastGuideWindowStartUtc_ = windowStartUtc;
    lastGuideSlotMinutes_ = slotMinutes;
    lastGuideSlotCount_ = slotCount;
    lastGuideStatusText_ = statusText;
    guideEntriesCache_ = entriesByChannel;
    if (!writeGuideCacheFile(channelOrder, entriesByChannel, windowStartUtc, slotMinutes, slotCount, statusText)) {
        appendLog("guide-sd: failed to write guide cache file.");
    } else if (!loadGuideCacheFile()) {
        appendLog("guide-sd: failed to reload guide cache file after refresh; using in-memory cache.");
        guideEntriesCache_ = entriesByChannel;
        lastGuideChannelOrder_ = channelOrder;
        lastGuideWindowStartUtc_ = windowStartUtc;
        lastGuideSlotMinutes_ = slotMinutes;
        lastGuideSlotCount_ = slotCount;
        lastGuideStatusText_ = statusText;
    }

    QJsonArray betaExtractions;
    QJsonObject extractionObject;
    extractionObject.insert("source", "Schedules Direct sdJSON");
    extractionObject.insert("exportPath", exportPath);
    extractionObject.insert("usedCachedExport", usedCachedExport);
    extractionObject.insert("mappedEntryCount", mappedEntries);
    extractionObject.insert("channelCount", channelOrder.size());
    extractionObject.insert("statusText", statusText);
    betaExtractions.append(extractionObject);
    if (!writeGuideCacheBetaFile(channelOrder,
                                 entriesByChannel,
                                 windowStartUtc,
                                 slotMinutes,
                                 slotCount,
                                 statusText,
                                 betaExtractions)) {
        appendLog("guide-sd: failed to write beta guide cache file.");
    }

    applyCurrentShowStatusFromGuideCache();
    if (updateDialog && tvGuideDialog_ != nullptr) {
        tvGuideDialog_->setGuideData(lastGuideChannelOrder_,
                                     favorites_,
                                     favoriteShowRatings_,
                                     guideEntriesCache_,
                                     lastGuideWindowStartUtc_,
                                     lastGuideSlotMinutes_,
                                     lastGuideSlotCount_,
                                     scheduledSwitches_,
                                     lastGuideStatusText_);
    }
    setStatusBarStateMessage(statusText.section('\n', 0, 0));
    return mappedEntries > 0;
}

bool MainWindow::loadGuideCacheFile()
{
    purgeExpiredGuideCacheFiles(true);

    const QString cachePath = resolveGuideCachePath();
    if (cachePath.isEmpty()) {
        return false;
    }

    QFile cacheFile(cachePath);
    if (!cacheFile.exists()) {
        return false;
    }
    if (!cacheFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(cacheFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QString previousCacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                              lastGuideWindowStartUtc_,
                                                              lastGuideSlotMinutes_,
                                                              lastGuideSlotCount_);
    const QJsonObject root = document.object();
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QStringList storedChannelOrder = [&root]() {
        QStringList order;
        for (const QJsonValue &value : root.value("channelOrder").toArray()) {
            const QString channelName = value.toString().trimmed();
            if (!channelName.isEmpty() && !order.contains(channelName)) {
                order.append(channelName);
            }
        }
        return order;
    }();

    QHash<QString, QList<TvGuideEntry>> loadedEntries;
    const QJsonObject entriesObject = root.value("entriesByChannel").toObject();
    const int retentionHours = guideCacheRetentionHoursValue(guideCacheRetentionSpin_);
    for (auto it = entriesObject.begin(); it != entriesObject.end(); ++it) {
        const QList<TvGuideEntry> cleaned =
            cleanGuideEntries(guideEntriesFromJsonArray(it.value().toArray()), nowUtc, retentionHours);
        loadedEntries.insert(it.key(), cleaned);
    }

    guideEntriesCache_ = loadedEntries;
    lastGuideChannelOrder_ = storedChannelOrder;
    lastGuideCacheGeneratedUtc_ = root.value("generatedUtc").toString().trimmed();
    lastGuideWindowStartUtc_ = QDateTime::fromString(root.value("windowStartUtc").toString(), Qt::ISODateWithMs);
    lastGuideSlotMinutes_ = root.value("slotMinutes").toInt(30);
    lastGuideSlotCount_ = root.value("slotCount").toInt(12);
    lastGuideStatusText_ = root.value("statusText").toString().trimmed();
    const QString loadedCacheStamp = currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                            lastGuideWindowStartUtc_,
                                                            lastGuideSlotMinutes_,
                                                            lastGuideSlotCount_);

    for (const QString &channelName : storedChannelOrder) {
        if (guideEntriesCache_.value(channelName).isEmpty()) {
            noAutoCurrentShowLookupChannels_.insert(channelName);
        } else {
            noAutoCurrentShowLookupChannels_.remove(channelName);
        }
    }
    if (deferStartupAutoFavoriteScheduling_) {
        logInteraction("program",
                       "startup.favorite-show.auto-scan.defer",
                       QString("cache stamp=%1 favorites=%2")
                           .arg(currentGuideCacheStamp(lastGuideCacheGeneratedUtc_,
                                                       lastGuideWindowStartUtc_,
                                                       lastGuideSlotMinutes_,
                                                       lastGuideSlotCount_),
                                favoriteShowRules_.join(" | ")));
    } else {
        autoScheduleFavoriteShowsFromGuideCache(false, false);
    }
    if (!guideRefreshInProgress_ && !loadedCacheStamp.isEmpty() && loadedCacheStamp != previousCacheStamp) {
        showTransientStatusBarMessage(previousCacheStamp.isEmpty() ? "Guide cache loaded"
                                                                   : "Guide cache updated in background",
                                     4000);
    }
    return true;
}

void MainWindow::setCurrentShowStatus(const QString &text,
                                      const QString &toolTip,
                                      const QString &synopsisText)
{
    if (currentShowLabel_ == nullptr) {
        return;
    }

    currentShowOverlayToolTip_ = toolTip.trimmed();
    currentShowLabel_->setText(text);
    currentShowLabel_->setToolTip(QString());
    if (currentShowSynopsisLabel_ != nullptr) {
        const QString trimmedSynopsis = synopsisText.trimmed();
        currentShowSynopsisLabel_->setText(trimmedSynopsis);
        currentShowSynopsisLabel_->setToolTip(QString());
        currentShowSynopsisLabel_->setVisible(!trimmedSynopsis.isEmpty());
        currentShowSynopsisLabel_->updateGeometry();
    }
    syncFullscreenOverlayState();
}

void MainWindow::scheduleCurrentShowRefresh(const QDateTime &refreshUtc)
{
    if (currentShowTimer_ == nullptr) {
        return;
    }

    currentShowTimer_->stop();
    if (!refreshUtc.isValid()) {
        return;
    }

    const qint64 delayMs = QDateTime::currentDateTimeUtc().msecsTo(refreshUtc);
    if (delayMs <= 0) {
        return;
    }

    constexpr qint64 kMaxShowRefreshDelayMs = 24LL * 60LL * 60LL * 1000LL;
    currentShowTimer_->start(static_cast<int>(std::min(delayMs, kMaxShowRefreshDelayMs)));
}

bool MainWindow::applyCurrentShowStatusFromGuideCache()
{
    if (currentChannelName_.isEmpty() || currentChannelName_.startsWith("File: ")) {
        if (currentShowTimer_ != nullptr) {
            currentShowTimer_->stop();
        }
        return false;
    }

    const QList<TvGuideEntry> entries = guideEntriesCache_.value(currentChannelName_);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    TvGuideEntry currentEntry;
    TvGuideEntry nextEntry;
    bool foundCurrent = false;
    bool foundNext = false;

    for (const TvGuideEntry &entry : entries) {
        if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
            continue;
        }
        if (entry.startUtc <= nowUtc && entry.endUtc > nowUtc) {
            if (!foundCurrent || entry.startUtc > currentEntry.startUtc) {
                currentEntry = entry;
                foundCurrent = true;
            }
            continue;
        }
        if (entry.startUtc > nowUtc && (!foundNext || entry.startUtc < nextEntry.startUtc)) {
            nextEntry = entry;
            foundNext = true;
        }
    }

    if (!foundCurrent && !foundNext) {
        if (currentShowTimer_ != nullptr) {
            currentShowTimer_->stop();
        }
        return false;
    }

    QStringList lines;
    QStringList detailLines;
    QStringList toolTips;
    QString detailText;
    QDateTime refreshUtc;

    if (foundCurrent) {
        const GuideEntryDisplayParts currentParts = displayPartsForGuideEntry(currentEntry);
        const QString ratedCurrentTitle = formatRatedShowTitle(currentParts.title, favoriteShowRatings_);
        lines << QString("Current: %1").arg(ratedCurrentTitle);
        if (!currentParts.episodeTitle.isEmpty()) {
            lines << QString("Episode: %1").arg(currentParts.episodeTitle);
        }
        if (!ratedCurrentTitle.isEmpty()) {
            detailLines << ratedCurrentTitle;
        }
        if (!currentParts.episodeTitle.isEmpty()) {
            detailLines << QString("Episode: %1").arg(currentParts.episodeTitle);
        }
        if (!currentParts.synopsisBody.isEmpty()) {
            detailLines << QString("Synopsis: %1").arg(currentParts.synopsisBody);
        }
        toolTips << guideEntryToolTipText(currentEntry, favoriteShowRatings_);
        detailText = detailLines.join('\n');
        refreshUtc = currentEntry.endUtc.addSecs(1);
    } else {
        lines << "Current: NO EIT DATA";
    }

    if (foundNext) {
        const GuideEntryDisplayParts nextParts = displayPartsForGuideEntry(nextEntry);
        const QString ratedNextTitle = formatRatedShowTitle(nextParts.title, favoriteShowRatings_);
        lines << QString("Next (%1): %2")
                     .arg(nextEntry.startUtc.toLocalTime().toString("h:mm AP"),
                          ratedNextTitle);
        if (!nextParts.episodeTitle.isEmpty()) {
            lines << QString("Next Episode: %1").arg(nextParts.episodeTitle);
        }
        toolTips << guideEntryToolTipText(nextEntry, favoriteShowRatings_);
        if (detailText.isEmpty()) {
            if (!ratedNextTitle.isEmpty()) {
                detailLines << ratedNextTitle;
            }
            if (!nextParts.episodeTitle.isEmpty()) {
                detailLines << QString("Episode: %1").arg(nextParts.episodeTitle);
            }
            if (!nextParts.synopsisBody.isEmpty()) {
                detailLines << QString("Synopsis: %1").arg(nextParts.synopsisBody);
            }
            detailText = detailLines.join('\n');
        }
        if (!refreshUtc.isValid()) {
            refreshUtc = nextEntry.startUtc.addSecs(1);
        }
    }

    setCurrentShowStatus(lines.join('\n'),
                         toolTips.join("\n\n"),
                         detailText);
    scheduleCurrentShowRefresh(refreshUtc);
    return true;
}

void MainWindow::probeCurrentShowAfterTune(const QString &channelName,
                                           int lookupSerial)
{
    currentShowTimer_->stop();
    const auto lookupStillCurrent = [this, &channelName, lookupSerial]() {
        return lookupSerial == currentShowLookupSerial_
               && currentChannelName_ == channelName
               && !userStoppedWatching_;
    };

    appendLog(QString("guide-bg: probe-start channel=%1 serial=%2")
                  .arg(channelName)
                  .arg(lookupSerial)
                  );
    showTransientStatusBarMessage(QString("Checking guide cache for %1").arg(channelName), 3000);

    if (!lookupStillCurrent()) {
        appendLog(QString("guide-bg: discarding stale guide cache read for %1").arg(channelName));
        return;
    }

    if (loadGuideCacheFile()) {
        appendLog(QString("guide-bg: reloaded guide cache file for %1").arg(channelName));
        showTransientStatusBarMessage(QString("Guide cache reloaded for %1").arg(channelName), 3000);
    }

    if (applyCurrentShowStatusFromGuideCache()) {
        noAutoCurrentShowLookupChannels_.remove(channelName);
        appendLog(QString("guide-bg: updated current show from guide cache file for %1").arg(channelName));
        return;
    }

    noAutoCurrentShowLookupChannels_.insert(channelName);
    currentShowTimer_->stop();
    setCurrentShowStatus("NO EIT DATA",
                         QString("No guide cache data is currently available for %1.").arg(channelName));
    appendLog(QString("guide-bg: no cached guide data available for %1").arg(channelName));
    showTransientStatusBarMessage(QString("No guide cache data for %1").arg(channelName), 4000);
}

void MainWindow::refreshCurrentShowStatus()
{
    if (currentChannelName_.isEmpty()) {
        currentShowTimer_->stop();
        setCurrentShowStatus("NO EIT DATA");
        return;
    }
    if (currentChannelName_.startsWith("File: ")) {
        currentShowTimer_->stop();
        setCurrentShowStatus("Local File");
        return;
    }
    if (zapProcess_ == nullptr || zapProcess_->state() == QProcess::NotRunning) {
        currentShowTimer_->stop();
        setCurrentShowStatus("NO EIT DATA");
        return;
    }

    loadGuideCacheFile();
    if (applyCurrentShowStatusFromGuideCache()) {
        return;
    }
    currentShowTimer_->stop();
    if (noAutoCurrentShowLookupChannels_.contains(currentChannelName_)) {
        setCurrentShowStatus("NO EIT DATA", "The guide cache does not currently contain data for this channel.");
        return;
    }
    setCurrentShowStatus("NO EIT DATA", "No guide cache data is currently available for this channel.");
}

bool MainWindow::startWatchingChannel(const QString &channelName,
                                      bool reconnectAttempt,
                                      const QString &channelLine)
{
    if (channelName.isEmpty()) {
        return false;
    }

    if (!persistChannelsFile()) {
        showWarningDialog("No channel list", "No saved channels are available yet. Run a scan first.");
        return false;
    }

    const QString zapExe = QStandardPaths::findExecutable("dvbv5-zap");
    if (zapExe.isEmpty()) {
        showCriticalDialog("Missing dependency", "dvbv5-zap was not found in PATH.");
        return false;
    }

    const QString requestedChannelName = channelName.trimmed();
    QString activeChannelLine = normalizeZapLine(channelLine).trimmed();
    if (activeChannelLine.isEmpty()) {
        activeChannelLine = firstChannelLineForName(requestedChannelName);
    }

    const QString resolvedChannelName = !activeChannelLine.isEmpty()
                                            ? channelDisplayLabelForLine(activeChannelLine, &xspfNumberByTuneKey_)
                                            : requestedChannelName;

    QString tuneChannelsPath = channelsFilePath_;
    if (!activeChannelLine.isEmpty()) {
        const QString activeTunePath = resolveActiveTuneChannelPath();
        if (!activeTunePath.isEmpty()) {
            QFileInfo activeTuneInfo(activeTunePath);
            QDir activeTuneDir = activeTuneInfo.dir();
            if (activeTuneDir.exists() || activeTuneDir.mkpath(".")) {
                QSaveFile activeTuneFile(activeTunePath);
                if (activeTuneFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                    const QByteArray tunePayload = activeChannelLine.toUtf8() + '\n';
                    if (activeTuneFile.write(tunePayload) == tunePayload.size() && activeTuneFile.commit()) {
                        tuneChannelsPath = activeTunePath;
                    } else {
                        activeTuneFile.cancelWriting();
                        appendLog(QString("player: failed to write single-channel tune file %1; falling back to %2")
                                      .arg(activeTunePath, channelsFilePath_));
                    }
                } else {
                    appendLog(QString("player: could not open %1 for single-channel tuning; falling back to %2")
                                  .arg(activeTunePath, channelsFilePath_));
                }
            } else {
                appendLog(QString("player: could not create %1 for single-channel tuning; falling back to %2")
                              .arg(activeTuneDir.path(), channelsFilePath_));
            }
        }
    }

    userStoppedWatching_ = false;
    reconnectTimer_->stop();
    if (playbackAttachTimer_ != nullptr) {
        playbackAttachTimer_->stop();
    }
    waitingForDvrReady_ = false;
    pendingDvrPath_.clear();
    stopSignalMonitor();
    ++playbackStartSerial_;
    if (!reconnectAttempt) {
        reconnectAttemptCount_ = 0;
        useResilientBridgeMode_ = false;
        resilientBridgeTried_ = false;
        useVideoOnlyBridgeMode_ = false;
        videoOnlyBridgeTried_ = false;
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1200);
        suppressBridgeExitReconnect_ = false;
    }

    if (zapProcess_->state() != QProcess::NotRunning) {
        suppressZapExitReconnect_ = true;
        stopProcess(zapProcess_, 1000);
        suppressZapExitReconnect_ = false;
    }

    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl());

    currentChannelName_ = resolvedChannelName.isEmpty() ? requestedChannelName : resolvedChannelName;
    currentChannelLine_ = activeChannelLine;
    currentProgramId_ = !currentChannelLine_.isEmpty()
                            ? programIdFromZapLine(currentChannelLine_)
                            : programIdForChannel(currentChannelName_);
    currentShowTimer_->stop();
    ++currentShowLookupSerial_;
    noAutoCurrentShowLookupChannels_.remove(channelName);
    loadGuideCacheFile();
    if (applyCurrentShowStatusFromGuideCache()) {
        appendLog(QString("current-show: showing cached guide data while tuning %1").arg(channelName));
    } else {
        setCurrentShowStatus("Current: Detecting...\nNext: ...",
                             QString("Loading %1, then reading the hidden TV Guide cache.").arg(channelName));
    }

    QStringList args;
    args << "-I" << "ZAP"
         << "-c" << tuneChannelsPath
         << "-a" << QString::number(adapterSpin_->value())
         << "-f" << QString::number(frontendSpin_->value())
         << "-r"
         << "-P"
         << "-p";

    const QString zapChannelName = channelNameFromZapLine(activeChannelLine).trimmed();
    args << (zapChannelName.isEmpty() ? requestedChannelName : zapChannelName);
    appendLog(QString("Tuning channel: %1 (program=%2)")
                  .arg(currentChannelName_, currentProgramId_.isEmpty() ? "unknown" : currentProgramId_));
    appendLog("zap: launch " + formatCommandLine(zapExe, args));
    zapProcess_->start(zapExe, args);
    if (!zapProcess_->waitForStarted(2000)) {
        appendLog(QString("Failed to start dvbv5-zap for %1 (%2)")
                      .arg(currentChannelName_, zapProcess_->errorString()));
        scheduleReconnect("Failed to start tuner process");
        return false;
    }
    startSignalMonitor(adapterSpin_->value(), frontendSpin_->value());

    pendingDvrPath_ = QString("/dev/dvb/adapter%1/dvr0").arg(adapterSpin_->value());
    waitingForDvrReady_ = true;
    appendLog(QString("player: Waiting for DVR device readiness on %1.").arg(pendingDvrPath_));
    if (playbackAttachTimer_ != nullptr) {
        playbackAttachTimer_->start(3500);
    }

    if (!currentChannelLine_.isEmpty()) {
        highlightChannelLineInTable(currentChannelLine_);
    } else {
    highlightChannelInTable(currentChannelName_);
    }

    QSettings settings("tv_tuner_gui", "watcher");
    const QString persistedChannelName = currentChannelName_.isEmpty() ? requestedChannelName : currentChannelName_;
    settings.setValue(kLastPlayedChannelSetting, persistedChannelName);
    const QString persistedChannelLine = !currentChannelLine_.isEmpty()
                                             ? currentChannelLine_
                                             : firstChannelLineForName(persistedChannelName);
    if (!persistedChannelLine.isEmpty()) {
        settings.setValue(kLastPlayedChannelLineSetting, persistedChannelLine);
    } else {
        settings.remove(kLastPlayedChannelLineSetting);
    }

    stopWatchButton_->setEnabled(true);
    playbackStatusLabel_->setText(playbackStatusText());
    syncFullscreenOverlayState();
    setStatusBarStateMessage("Buffering channel");
    return true;
}

void MainWindow::stopWatching()
{
    exitFullscreen();
    attachVideoFromPip();
    if (pipWindow_ != nullptr) {
        pipWindow_->hide();
    }
    userStoppedWatching_ = true;
    reconnectTimer_->stop();
    currentShowTimer_->stop();
    stopSignalMonitor();
    if (playbackAttachTimer_ != nullptr) {
        playbackAttachTimer_->stop();
    }
    ++currentShowLookupSerial_;
    ++playbackStartSerial_;
    waitingForDvrReady_ = false;
    pendingDvrPath_.clear();
    reconnectAttemptCount_ = 0;
    useResilientBridgeMode_ = false;
    resilientBridgeTried_ = false;
    useVideoOnlyBridgeMode_ = false;
    videoOnlyBridgeTried_ = false;
    bridgeSawCodecParameterFailure_ = false;
    currentChannelName_.clear();
    currentChannelLine_.clear();
    currentProgramId_.clear();
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl());
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1200);
        suppressBridgeExitReconnect_ = false;
    }

    if (zapProcess_ != nullptr && zapProcess_->state() != QProcess::NotRunning) {
        suppressZapExitReconnect_ = true;
        stopProcess(zapProcess_, 1200);
        suppressZapExitReconnect_ = false;
    }

    stopWatchButton_->setEnabled(false);
    playbackStatusLabel_->setText(playbackStatusText());
    setSignalMonitorStatus("Signal: n/a");
    setCurrentShowStatus("NO EIT DATA");
    syncFullscreenOverlayState();
    if (scanProcess_->state() == QProcess::NotRunning) {
        setStatusBarStateMessage(QString());
    }
}

void MainWindow::handleZapStdErr()
{
    const QString output = QString::fromUtf8(zapProcess_->readAllStandardError()).trimmed();
    if (output.isEmpty()) {
        return;
    }

    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            appendLog("zap: " + trimmed);
            if (waitingForDvrReady_
                && trimmed.contains("DVR interface")
                && trimmed.contains("can now be opened")) {
                QString detectedPath = pendingDvrPath_;
                const QRegularExpression re("'([^']+/dvr0)'");
                const auto match = re.match(trimmed);
                if (match.hasMatch()) {
                    detectedPath = match.captured(1);
                }
                appendLog("player: DVR ready path detected: " + detectedPath);
                startPlaybackFromDvr(detectedPath);
            }
        }
    }
}

void MainWindow::startPlaybackFromDvr(const QString &dvrPath)
{
    if (dvrPath.isEmpty()) {
        return;
    }
    if (playbackAttachTimer_ != nullptr) {
        playbackAttachTimer_->stop();
    }
    waitingForDvrReady_ = false;
    pendingDvrPath_.clear();

    const QString ffmpegExe = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpegExe.isEmpty()) {
        appendLog("player: ffmpeg not found in PATH for live DVB bridge.");
        scheduleReconnect("Missing ffmpeg for live stream");
        return;
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1000);
        suppressBridgeExitReconnect_ = false;
    }

    QStringList ffmpegArgs;
    bridgeSawCodecParameterFailure_ = false;
    if (useVideoOnlyBridgeMode_) {
        ffmpegArgs << "-hide_banner"
                   << "-nostdin"
                   << "-loglevel" << "warning"
                   << "-fflags" << "+genpts+discardcorrupt"
                   << "-err_detect" << "ignore_err"
                   << "-analyzeduration" << "12M"
                   << "-probesize" << "12M"
                   << "-f" << "mpegts"
                   << "-i" << dvrPath;
        if (!currentProgramId_.isEmpty()) {
            ffmpegArgs << "-map" << QString("0:p:%1?").arg(currentProgramId_);
        } else {
            ffmpegArgs << "-map" << "0:v:0?";
        }
        ffmpegArgs << "-an"
                   << "-sn"
                   << "-dn"
                   << "-c:v" << "mpeg2video"
                   << "-q:v" << "3"
                   << "-mpegts_flags" << "+resend_headers+pat_pmt_at_frames"
                   << "-flush_packets" << "1"
                   << "-f" << "mpegts"
                   << QString("udp://127.0.0.1:%1?pkt_size=1316").arg(23000 + adapterSpin_->value());
    } else if (useResilientBridgeMode_) {
        ffmpegArgs << "-hide_banner"
                   << "-nostdin"
                   << "-loglevel" << "warning"
                   << "-fflags" << "+genpts+discardcorrupt"
                   << "-err_detect" << "ignore_err"
                   << "-analyzeduration" << "4M"
                   << "-probesize" << "4M"
                   << "-f" << "mpegts"
                   << "-i" << dvrPath;
        if (!currentProgramId_.isEmpty()) {
            ffmpegArgs << "-map" << QString("0:p:%1?").arg(currentProgramId_);
        } else {
            ffmpegArgs << "-map" << "0:v:0?"
                       << "-map" << "0:a:0?";
        }
        ffmpegArgs << "-sn"
                   << "-dn"
                   << "-c:v" << "mpeg2video"
                   << "-q:v" << "3"
                   << "-c:a" << "mp2"
                   << "-b:a" << "192k"
                   << "-mpegts_flags" << "+resend_headers+pat_pmt_at_frames"
                   << "-flush_packets" << "1"
                   << "-f" << "mpegts"
                   << QString("udp://127.0.0.1:%1?pkt_size=1316").arg(23000 + adapterSpin_->value());
    } else {
        ffmpegArgs << "-hide_banner"
                   << "-nostdin"
                   << "-loglevel" << "warning"
                   << "-fflags" << "+genpts"
                   << "-analyzeduration" << "2M"
                   << "-probesize" << "2M"
                   << "-f" << "mpegts"
                   << "-i" << dvrPath;
        if (!currentProgramId_.isEmpty()) {
            ffmpegArgs << "-map" << QString("0:p:%1?").arg(currentProgramId_);
        } else {
            ffmpegArgs << "-map" << "0";
        }
        ffmpegArgs
                   << "-c" << "copy"
                   << "-mpegts_flags" << "+resend_headers+pat_pmt_at_frames"
                   << "-flush_packets" << "1"
                   << "-f" << "mpegts"
                   << QString("udp://127.0.0.1:%1?pkt_size=1316").arg(23000 + adapterSpin_->value());
    }

    appendLog("ffmpeg bridge: launch " + formatCommandLine(ffmpegExe, ffmpegArgs));
    streamBridgeProcess_->setProgram(ffmpegExe);
    streamBridgeProcess_->setArguments(ffmpegArgs);
    streamBridgeProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    streamBridgeProcess_->start();
    if (!streamBridgeProcess_->waitForStarted(2000)) {
        appendLog(QString("player: Failed to start ffmpeg bridge for %1 (%2)")
                      .arg(dvrPath, streamBridgeProcess_->errorString()));
        scheduleReconnect("Could not start ffmpeg bridge");
        return;
    }

    const int attachSerial = playbackStartSerial_;
    const int udpPort = 23000 + adapterSpin_->value();
    const QUrl liveUrl(QString("udp://127.0.0.1:%1").arg(udpPort));
    appendLog(QString("player: Starting playback from DVR %1 via %2 (mode=%3, program=%4)")
                  .arg(dvrPath,
                       liveUrl.toString(),
                       useVideoOnlyBridgeMode_ ? "video-only" : (useResilientBridgeMode_ ? "resilient" : "normal"),
                       currentProgramId_.isEmpty() ? "unknown" : currentProgramId_));
    mediaPlayer_->setAudioOutput(audioOutput_);
    currentShowTimer_->stop();
    if (applyCurrentShowStatusFromGuideCache()) {
        // Cached guide data already resolved the current show.
    } else {
        setCurrentShowStatus("Current: Detecting...\nNext: ...",
                             QString("Loading %1, then reading the hidden TV Guide cache.").arg(currentChannelName_));
    }
    const int lookupSerial = currentShowLookupSerial_;
    const QString lookupChannelName = currentChannelName_;
    if (!lookupChannelName.isEmpty() && !lookupChannelName.startsWith("File: ")) {
        QTimer::singleShot(1200, this, [this, attachSerial, lookupSerial, lookupChannelName]() {
            if (attachSerial != playbackStartSerial_
                || lookupSerial != currentShowLookupSerial_
                || currentChannelName_ != lookupChannelName
                || userStoppedWatching_) {
                return;
            }
            probeCurrentShowAfterTune(lookupChannelName, lookupSerial);
        });
    }
    QTimer::singleShot(450, this, [this, liveUrl, attachSerial]() {
        if (attachSerial != playbackStartSerial_) {
            return;
        }
        if (streamBridgeProcess_ == nullptr || streamBridgeProcess_->state() != QProcess::Running) {
            appendLog("player: ffmpeg bridge exited before media attach.");
            return;
        }
        appendLog("player: Attaching UDP live stream to media player.");
        mediaPlayer_->setSource(liveUrl);
        mediaPlayer_->play();
    });
}

void MainWindow::addSelectedFavorite()
{
    const QString selectedChannelName = selectedChannelNameFromTable().trimmed();
    const QString currentWatchedChannel =
        (!currentChannelName_.startsWith("File: ") && !currentChannelName_.isEmpty()) ? currentChannelName_.trimmed() : QString();

    if (favorites_.size() >= kQuickFavoriteCount) {
        const QString candidateName = !selectedChannelName.isEmpty() ? selectedChannelName : currentWatchedChannel;
        showInformationDialog("Favorites full",
                              QString("You can save up to %1 favorites. Remove one first%2.")
                                  .arg(kQuickFavoriteCount)
                                  .arg(candidateName.isEmpty() ? QString() : QString(" to add %1").arg(candidateName)));
        return;
    }

    QString channelName;
    if (!selectedChannelName.isEmpty() && !favorites_.contains(selectedChannelName)) {
        channelName = selectedChannelName;
    } else if (!currentWatchedChannel.isEmpty() && !favorites_.contains(currentWatchedChannel)) {
        channelName = currentWatchedChannel;
    } else {
        const QStringList candidates = favoriteCandidatesFromTable(channelsTable_, favorites_);
        if (candidates.isEmpty()) {
            showInformationDialog("No channels available",
                                  "There are no additional channels available to add to favorites.");
            return;
        }

        const QString pickedChannel =
            promptItemSelection("Add Favorite", "Choose a channel to add:", candidates, 0, false);
        if (pickedChannel.isEmpty()) {
            return;
        }
        channelName = pickedChannel.trimmed();
    }

    if (channelName.isEmpty() || favorites_.contains(channelName)) {
        showInformationDialog("Already a favorite", QString("%1 is already in favorites.").arg(channelName));
        return;
    }

    favorites_.append(channelName);
    saveFavorites();
    refreshQuickButtons();
    const QString previousStatusMessage = lastStatusBarMessage_;
    if (statusBar() != nullptr) {
        statusBar()->showMessage(QString("Added %1 to favorites.").arg(channelName), 3000);
    }
    QTimer::singleShot(3100, this, [this, previousStatusMessage]() {
        if (statusBar() == nullptr || !statusBar()->currentMessage().trimmed().isEmpty()) {
            return;
        }
        setStatusBarStateMessage(previousStatusMessage);
    });
}

void MainWindow::removeSelectedFavorite()
{
    QString name = selectedChannelNameFromTable();
    if (name.isEmpty() && !currentChannelName_.startsWith("File: ") && !currentChannelName_.isEmpty()) {
        name = currentChannelName_;
    }
    if (name.isEmpty()) {
        return;
    }

    if (!favorites_.contains(name)) {
        return;
    }

    favorites_.removeAll(name);
    saveFavorites();
    refreshQuickButtons();
}

void MainWindow::watchFavoriteItem(QListWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }
    startWatchingChannel(item->text(), false);
}

void MainWindow::handleZapFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    stopSignalMonitor();
    if (!userStoppedWatching_ && !currentChannelName_.isEmpty()) {
        setSignalMonitorStatus("Signal: unavailable", "The tuner process exited, so live signal stats stopped updating.");
    }
    if (exitStatus != QProcess::NormalExit) {
        appendLog(QString("zap: tuner process crashed (code=%1, error=%2)")
                      .arg(exitCode)
                      .arg(zapProcess_->errorString()));
    }
    if (!suppressZapExitReconnect_ && exitCode != 0 && !userStoppedWatching_ && !currentChannelName_.isEmpty()) {
        scheduleReconnect(QString("Tuner process exited (%1)").arg(exitCode));
    }
}

void MainWindow::handleSignalMonitorFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    partialSignalMonitorOutput_.clear();
    if (userStoppedWatching_ || currentChannelName_.isEmpty()) {
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        setSignalMonitorStatus("Signal: unavailable",
                               QString("dvb-fe-tool exited (%1, %2).")
                                   .arg(exitCode)
                                   .arg(signalMonitorProcess_->errorString()));
        appendLog(QString("signal: dvb-fe-tool exited (code=%1, status=%2, error=%3)")
                      .arg(exitCode)
                      .arg(exitStatus == QProcess::NormalExit ? "normal" : "crash")
                      .arg(signalMonitorProcess_->errorString()));
        return;
    }

    if (signalMonitorLabel_ != nullptr
        && signalMonitorLabel_->text().trimmed() == "Signal: monitoring...") {
        setSignalMonitorStatus("Signal: unavailable",
                               "dvb-fe-tool exited before reporting any frontend statistics.");
    }
}

QString MainWindow::playbackStatusText() const
{
    if (currentChannelName_.isEmpty()) {
        return "Idle";
    }

    const QString stateText = (mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) ? "Playing" : "Buffering";
    return QString("%1: %2").arg(stateText, currentChannelName_);
}

void MainWindow::startSignalMonitor(int adapter, int frontend)
{
    stopSignalMonitor();

    const QString femonExe = QStandardPaths::findExecutable("dvb-fe-tool");
    if (femonExe.isEmpty()) {
        setSignalMonitorStatus("Signal: unavailable", "dvb-fe-tool was not found in PATH.");
        return;
    }

    partialSignalMonitorOutput_.clear();
    setSignalMonitorStatus("Signal: monitoring...",
                           QString("Monitoring adapter%1/frontend%2 with dvb-fe-tool --femon.")
                               .arg(adapter)
                               .arg(frontend));

    QStringList args;
    args << "-m"
         << "-a" << QString::number(adapter)
         << "-f" << QString::number(frontend);
    signalMonitorProcess_->start(femonExe, args);
    if (!signalMonitorProcess_->waitForStarted(1200)) {
        setSignalMonitorStatus("Signal: unavailable",
                               QString("Failed to start dvb-fe-tool (%1).")
                                   .arg(signalMonitorProcess_->errorString()));
        appendLog(QString("signal: failed to start dvb-fe-tool (%1)")
                      .arg(signalMonitorProcess_->errorString()));
        return;
    }

    appendLog(QString("signal: launch %1").arg(formatCommandLine(femonExe, args)));
}

void MainWindow::stopSignalMonitor()
{
    partialSignalMonitorOutput_.clear();
    if (signalMonitorProcess_ != nullptr && signalMonitorProcess_->state() != QProcess::NotRunning) {
        stopProcess(signalMonitorProcess_, 1000);
    }
}

void MainWindow::handleSignalMonitorOutput(const QString &chunk)
{
    if (chunk.isEmpty()) {
        return;
    }

    partialSignalMonitorOutput_ += chunk;
    partialSignalMonitorOutput_.replace('\r', '\n');

    int newlineIndex = partialSignalMonitorOutput_.indexOf('\n');
    while (newlineIndex >= 0) {
        const QString line = partialSignalMonitorOutput_.left(newlineIndex).trimmed();
        partialSignalMonitorOutput_.remove(0, newlineIndex + 1);

        if (!line.isEmpty()) {
            const SignalMonitorReading reading = parseSignalMonitorLine(line);
            if (reading.isValid()) {
                setSignalMonitorStatus(signalMonitorDisplayText(reading),
                                       signalMonitorToolTip(reading,
                                                            QString("Monitoring %1")
                                                                .arg(currentChannelName_.trimmed())));
            } else if (line.contains("error", Qt::CaseInsensitive)
                       || line.contains("fail", Qt::CaseInsensitive)
                       || line.contains("could not", Qt::CaseInsensitive)) {
                setSignalMonitorStatus("Signal: unavailable", line);
                appendLog("signal: " + line);
            }
        }

        newlineIndex = partialSignalMonitorOutput_.indexOf('\n');
    }
}

void MainWindow::setSignalMonitorStatus(const QString &text, const QString &toolTip)
{
    signalMonitorOverlayToolTip_ = toolTip.trimmed();
    if (signalMonitorLabel_ != nullptr) {
        signalMonitorLabel_->setText(text);
        signalMonitorLabel_->setToolTip(QString());
    }
    syncFullscreenOverlayState();
}

void MainWindow::setStatusBarStateMessage(const QString &text)
{
    lastStatusBarMessage_ = text;

    QString message = text.trimmed();
    if (guideRefreshInProgress_) {
        if (!message.isEmpty()) {
            message += " | ";
        }
        message += useSchedulesDirectGuideSource()
                       ? "Getting latest Schedules Direct guide data..."
                       : "Getting latest EIT data...";
    }

    if (guideRefreshTimer_ != nullptr && guideRefreshTimer_->isActive()) {
        const int remainingMs = guideRefreshTimer_->remainingTime();
        if (remainingMs >= 0) {
            const QDateTime nextRefreshUtc = QDateTime::currentDateTimeUtc().addMSecs(remainingMs);
            if (!message.isEmpty()) {
                message += " | ";
            }
            message += QString("Next JSON update %1")
                           .arg(nextRefreshUtc.toLocalTime().toString("h:mm AP"));
        }
    }

    if (statusBar() != nullptr) {
        statusBar()->showMessage(message);
    }
}

void MainWindow::showTransientStatusBarMessage(const QString &text, int timeoutMs)
{
    const QString message = text.trimmed();
    if (message.isEmpty() || statusBar() == nullptr) {
        return;
    }
    // Background activity should temporarily override the idle
    // "Next JSON update ..." status without replacing the persistent state.
    statusBar()->showMessage(message, std::max(0, timeoutMs));
}

void MainWindow::updateTvGuideDialogFromCurrentCache(bool showStatusMessage)
{
    if (tvGuideDialog_ == nullptr) {
        return;
    }

    const bool hasGuideCache =
        !lastGuideChannelOrder_.isEmpty() || !guideEntriesCache_.isEmpty() || !lastGuideStatusText_.trimmed().isEmpty();
    const QString statusText = hasGuideCache
                                   ? lastGuideStatusText_
                                   : (useSchedulesDirectGuideSource()
                                          ? QString("No cached guide data loaded yet. Background Schedules Direct retrieval will update this window automatically.")
                                          : QString("No cached guide data loaded yet. Background EIT retrieval will update this window automatically."));

    tvGuideDialog_->setGuideData(lastGuideChannelOrder_,
                                 favorites_,
                                 favoriteShowRatings_,
                                 guideEntriesCache_,
                                 lastGuideWindowStartUtc_,
                                 lastGuideSlotMinutes_,
                                 lastGuideSlotCount_,
                                 scheduledSwitches_,
                                 statusText);

    if (showStatusMessage) {
        setStatusBarStateMessage(hasGuideCache ? "Guide cache reloaded"
                                               : "No cached guide data");
    }
}

void MainWindow::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    appendLog(QString("player: mediaStatusChanged=%1").arg(static_cast<int>(status)));
    playbackStatusLabel_->setText(playbackStatusText());
    syncFullscreenOverlayState();
    if (!currentChannelName_.isEmpty()) {
        const bool isLocalFile = currentChannelName_.startsWith("File: ");
        if (status == QMediaPlayer::InvalidMedia) {
            setStatusBarStateMessage(isLocalFile ? "Local file error" : "Playback error");
        } else if (mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) {
            setStatusBarStateMessage(QString());
        } else {
            setStatusBarStateMessage(isLocalFile ? "Opening media file" : "Buffering channel");
        }
    }
    if (!currentChannelName_.isEmpty() && status == QMediaPlayer::InvalidMedia && !userStoppedWatching_) {
        if (tryDynamicBridgeFallback("Media stream became invalid")) {
            return;
        }
        scheduleReconnect("Media stream became invalid");
    }
    if (!currentChannelName_.isEmpty() && status == QMediaPlayer::EndOfMedia && !userStoppedWatching_) {
        if (tryDynamicBridgeFallback("Media reached unexpected end")) {
            return;
        }
        scheduleReconnect("Media stream became invalid");
    }
}

void MainWindow::handlePlayerError(const QString &errorText)
{
    if (errorText.trimmed().isEmpty()) {
        return;
    }
    appendLog("player: " + errorText.trimmed());
    if (!userStoppedWatching_ && !currentChannelName_.isEmpty()) {
        if (tryDynamicBridgeFallback(QString("Player error: %1").arg(errorText.trimmed()))) {
            return;
        }
        scheduleReconnect("Player error");
    }
}

bool MainWindow::tryDynamicBridgeFallback(const QString &reason)
{
    if (userStoppedWatching_ || currentChannelName_.isEmpty()) {
        return false;
    }

    const bool shouldTryVideoOnly =
        bridgeSawCodecParameterFailure_
        || reason.contains("Could not open file", Qt::CaseInsensitive)
        || useResilientBridgeMode_;

    if (shouldTryVideoOnly && !videoOnlyBridgeTried_ && !useVideoOnlyBridgeMode_) {
        videoOnlyBridgeTried_ = true;
        useVideoOnlyBridgeMode_ = true;
        useResilientBridgeMode_ = false;
        reconnectTimer_->stop();
        reconnectAttemptCount_ = 0;
        appendLog(QString("player: %1; retrying with video-only bridge mode").arg(reason));
        startWatchingChannel(currentChannelName_, true, currentChannelLine_);
        return true;
    }

    if (resilientBridgeTried_ || useResilientBridgeMode_) {
        return false;
    }
    resilientBridgeTried_ = true;
    useResilientBridgeMode_ = true;
    useVideoOnlyBridgeMode_ = false;
    reconnectTimer_->stop();
    reconnectAttemptCount_ = 0;
    appendLog(QString("player: %1; retrying with resilient bridge mode").arg(reason));
    startWatchingChannel(currentChannelName_, true, currentChannelLine_);
    return true;
}

void MainWindow::triggerQuickFavorite()
{
    auto *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr) {
        return;
    }
    const QString channel = button->property("channelName").toString();
    if (channel.isEmpty()) {
        return;
    }
    startWatchingChannel(channel, false);
}

void MainWindow::scheduleReconnect(const QString &reason)
{
    if (currentChannelName_.isEmpty() || userStoppedWatching_) {
        return;
    }
    if (reconnectAttemptCount_ >= maxReconnectAttempts_) {
        appendLog("Reconnect failed after maximum attempts.");
        setStatusBarStateMessage("Reconnect failed");
        return;
    }

    ++reconnectAttemptCount_;
    const int delayMs = 800 + (reconnectAttemptCount_ * 900);
    appendLog(QString("Reconnect attempt %1/%2 in %3 ms (%4)")
                  .arg(reconnectAttemptCount_)
                  .arg(maxReconnectAttempts_)
                  .arg(delayMs)
                  .arg(reason));
    setStatusBarStateMessage("Reconnecting...");
    reconnectTimer_->start(delayMs);
}

void MainWindow::triggerReconnect()
{
    if (currentChannelName_.isEmpty() || userStoppedWatching_) {
        return;
    }
    startWatchingChannel(currentChannelName_, true, currentChannelLine_);
}

void MainWindow::handleMuteToggled(bool checked)
{
    if (checked) {
        audioOutput_->setVolume(0.0f);
    } else {
        audioOutput_->setVolume(static_cast<float>(volumeSlider_->value()) / 100.0f);
    }
    muteButton_->setText(checked ? "Unmute" : "Mute");
    syncFullscreenOverlayState();
}

void MainWindow::handleVolumeChanged(int value)
{
    if (muteButton_ != nullptr && muteButton_->isChecked()) {
        audioOutput_->setVolume(0.0f);
    } else {
        audioOutput_->setVolume(static_cast<float>(value) / 100.0f);
    }
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue("volume_percent", value);
    syncFullscreenOverlayState();
}

void MainWindow::saveChannelSidebarSizing()
{
    if (contentSplitter_ == nullptr || channelsTable_ == nullptr || fullscreenActive_ || !channelsTable_->isVisible()) {
        return;
    }

    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue(kChannelSidebarSplitterStateSetting, contentSplitter_->saveState());
}

void MainWindow::restoreChannelSidebarSizing()
{
    if (contentSplitter_ == nullptr) {
        return;
    }

    QSettings settings("tv_tuner_gui", "watcher");
    const QByteArray splitterState = settings.value(kChannelSidebarSplitterStateSetting).toByteArray();
    if (!splitterState.isEmpty()) {
        contentSplitter_->restoreState(splitterState);
    }
}

void MainWindow::restoreLastPlayedChannel()
{
    if (channelsTable_ == nullptr || channelsTable_->rowCount() == 0) {
        return;
    }

    QSettings settings("tv_tuner_gui", "watcher");
    const QString savedChannelLine = normalizeZapLine(
                                         settings.value(kLastPlayedChannelLineSetting).toString())
                                         .trimmed();
    QString restoredChannelLine;
    if (!savedChannelLine.isEmpty()) {
        for (const QString &line : channelLines_) {
            if (normalizeZapLine(line).trimmed() == savedChannelLine) {
                restoredChannelLine = savedChannelLine;
                break;
            }
        }
    }

    QString restoredChannelName;
    if (!restoredChannelLine.isEmpty()) {
        restoredChannelName = channelDisplayLabelForLine(restoredChannelLine, &xspfNumberByTuneKey_).trimmed();
        if (restoredChannelName.isEmpty()) {
            restoredChannelName = channelNameFromZapLine(restoredChannelLine).trimmed();
        }
    }

    const QString lastChannelName = settings.value(kLastPlayedChannelSetting).toString().trimmed();
    if (restoredChannelLine.isEmpty() && !lastChannelName.isEmpty()) {
        restoredChannelLine = firstChannelLineForName(lastChannelName);
        if (!restoredChannelLine.isEmpty()) {
            restoredChannelName = channelDisplayLabelForLine(restoredChannelLine, &xspfNumberByTuneKey_).trimmed();
            if (restoredChannelName.isEmpty()) {
                restoredChannelName = channelNameFromZapLine(restoredChannelLine).trimmed();
            }
        }
    }

    if (!restoredChannelLine.isEmpty() && !restoredChannelName.isEmpty()) {
        if (!highlightChannelLineInTable(restoredChannelLine)) {
            appendLog("Saved last channel line was not highlighted in the current table: " + restoredChannelLine);
        }
        if (startWatchingChannel(restoredChannelName, false, restoredChannelLine)) {
            return;
        }
    }

    if (lastChannelName.isEmpty()) {
        return;
    }

    if (!highlightChannelInTable(lastChannelName)) {
        appendLog("Saved last channel label was not highlighted in the current table: " + lastChannelName);
    }

    if (startWatchingChannel(lastChannelName, false)) {
        return;
    }

    appendLog("Saved last channel could not be restored: " + lastChannelName);
}

void MainWindow::toggleFullscreen()
{
    if (fullscreenActive_) {
        exitFullscreen();
    } else {
        enterFullscreen();
    }
}

void MainWindow::handleFullscreenChanged(bool fullScreen)
{
    fullscreenButton_->setText(fullScreen ? "Exit Fullscreen" : "Fullscreen");
}

void MainWindow::syncFullscreenOverlayState()
{
    if (fullscreenOverlayContainer_ == nullptr) {
        return;
    }

    if (fullscreenWatchButton_ != nullptr && watchButton_ != nullptr) {
        fullscreenWatchButton_->setEnabled(watchButton_->isEnabled());
    }
    if (fullscreenStopWatchButton_ != nullptr) {
        const bool stopEnabled = stopWatchButton_ != nullptr
                                     ? stopWatchButton_->isEnabled()
                                     : !currentChannelName_.trimmed().isEmpty();
        fullscreenStopWatchButton_->setEnabled(stopEnabled);
    }
    if (fullscreenMuteButton_ != nullptr && muteButton_ != nullptr) {
        const QSignalBlocker blocker(fullscreenMuteButton_);
        fullscreenMuteButton_->setChecked(muteButton_->isChecked());
        fullscreenMuteButton_->setText(muteButton_->text());
        fullscreenMuteButton_->setEnabled(muteButton_->isEnabled());
    }
    if (fullscreenVolumeSlider_ != nullptr && volumeSlider_ != nullptr) {
        const QSignalBlocker blocker(fullscreenVolumeSlider_);
        fullscreenVolumeSlider_->setValue(volumeSlider_->value());
        fullscreenVolumeSlider_->setEnabled(volumeSlider_->isEnabled());
    }
    if (fullscreenPlaybackStatusLabel_ != nullptr && playbackStatusLabel_ != nullptr) {
        fullscreenPlaybackStatusLabel_->setText(playbackStatusLabel_->text());
        fullscreenPlaybackStatusLabel_->setToolTip(QString());
    }
    if (fullscreenSignalMonitorLabel_ != nullptr && signalMonitorLabel_ != nullptr) {
        fullscreenSignalMonitorLabel_->setText(signalMonitorLabel_->text());
        fullscreenSignalMonitorLabel_->setToolTip(signalMonitorOverlayToolTip_);
    }
    if (fullscreenCurrentShowLabel_ != nullptr && currentShowLabel_ != nullptr) {
        fullscreenCurrentShowLabel_->setText(currentShowLabel_->text());
        fullscreenCurrentShowLabel_->setToolTip(currentShowOverlayToolTip_);
    }
    if (fullscreenCurrentShowSynopsisLabel_ != nullptr && currentShowSynopsisLabel_ != nullptr) {
        const QString synopsis = currentShowSynopsisLabel_->text().trimmed();
        fullscreenCurrentShowSynopsisLabel_->setText(synopsis);
        fullscreenCurrentShowSynopsisLabel_->setToolTip(currentShowOverlayToolTip_);
        fullscreenCurrentShowSynopsisLabel_->setVisible(!synopsis.isEmpty());
    }
}

void MainWindow::positionFullscreenOverlay()
{
    if (fullscreenOverlayContainer_ == nullptr || fullscreenWindow_ == nullptr) {
        return;
    }

    QRect anchorRect = fullscreenWindow_->geometry();
    if (fullscreenWindow_->windowHandle() != nullptr && fullscreenWindow_->windowHandle()->screen() != nullptr) {
        anchorRect = fullscreenWindow_->windowHandle()->screen()->geometry();
    }
    if (anchorRect.isEmpty()) {
        return;
    }

    constexpr int kOverlayMargin = 24;
    const int overlayWidth = std::max(320, anchorRect.width() - (kOverlayMargin * 2));
    fullscreenOverlayContainer_->setMinimumWidth(overlayWidth);
    fullscreenOverlayContainer_->setMaximumWidth(overlayWidth);
    if (fullscreenOverlayContainer_->layout() != nullptr) {
        fullscreenOverlayContainer_->layout()->activate();
    }

    const int overlayHeight = std::min(anchorRect.height() - (kOverlayMargin * 2),
                                       std::max(fullscreenOverlayContainer_->sizeHint().height(), 120));
    const QRect overlayRect(anchorRect.x() + kOverlayMargin,
                            anchorRect.y() + anchorRect.height() - overlayHeight - kOverlayMargin,
                            overlayWidth,
                            overlayHeight);
    fullscreenOverlayContainer_->setGeometry(overlayRect);
}

void MainWindow::showFullscreenCursor()
{
    if (fullscreenOverlayContainer_ != nullptr) {
        if (fullscreenActive_) {
            const bool overlayWasVisible = fullscreenOverlayContainer_->isVisible();
            const QRect overlayGeometryBefore = fullscreenOverlayContainer_->geometry();
            syncFullscreenOverlayState();
            positionFullscreenOverlay();
            fullscreenOverlayContainer_->show();
            fullscreenOverlayContainer_->raise();
            if (!overlayWasVisible || overlayGeometryBefore.isEmpty()) {
                appendLog(QStringLiteral("fullscreen-overlay: show requested visible=%1 geom=%2,%3 %4x%5 sizeHint=%6x%7")
                              .arg(fullscreenOverlayContainer_->isVisible() ? QStringLiteral("true")
                                                                             : QStringLiteral("false"))
                              .arg(fullscreenOverlayContainer_->geometry().x())
                              .arg(fullscreenOverlayContainer_->geometry().y())
                              .arg(fullscreenOverlayContainer_->geometry().width())
                              .arg(fullscreenOverlayContainer_->geometry().height())
                              .arg(fullscreenOverlayContainer_->sizeHint().width())
                              .arg(fullscreenOverlayContainer_->sizeHint().height()));
            }
        } else {
            fullscreenOverlayContainer_->hide();
        }
    }

    if (fullscreenCursorHidden_) {
        if (fullscreenWindow_ != nullptr) {
            fullscreenWindow_->unsetCursor();
        }
        if (fullscreenVideoWidget_ != nullptr) {
            fullscreenVideoWidget_->unsetCursor();
        }
        fullscreenCursorHidden_ = false;
    }
}

void MainWindow::hideFullscreenCursor()
{
    if (!fullscreenActive_) {
        return;
    }

    if (fullscreenOverlayContainer_ != nullptr) {
        if (fullscreenOverlayContainer_->isVisible()) {
            appendLog(QStringLiteral("fullscreen-overlay: hide requested geom=%1,%2 %3x%4")
                          .arg(fullscreenOverlayContainer_->geometry().x())
                          .arg(fullscreenOverlayContainer_->geometry().y())
                          .arg(fullscreenOverlayContainer_->geometry().width())
                          .arg(fullscreenOverlayContainer_->geometry().height()));
        }
        fullscreenOverlayContainer_->hide();
    }

    if (!fullscreenCursorHidden_) {
        if (fullscreenWindow_ != nullptr) {
            fullscreenWindow_->setCursor(Qt::BlankCursor);
        }
        if (fullscreenVideoWidget_ != nullptr) {
            fullscreenVideoWidget_->setCursor(Qt::BlankCursor);
        }
        fullscreenCursorHidden_ = true;
    }
}

void MainWindow::restartFullscreenCursorHideTimer()
{
    if (!fullscreenActive_ || fullscreenCursorHideTimer_ == nullptr) {
        return;
    }

    fullscreenCursorHideTimer_->start();
}

void MainWindow::enterFullscreen()
{
    if (fullscreenActive_ || fullscreenWindow_ == nullptr || fullscreenVideoWidget_ == nullptr) {
        return;
    }

    const QRect mainFrameRect = frameGeometry();
    QRect videoGlobalRect;
    if (videoWidget_ != nullptr) {
        videoGlobalRect = QRect(videoWidget_->mapToGlobal(QPoint(0, 0)), videoWidget_->size());
    }

    QScreen *targetScreen = nullptr;
    QString targetReason = QStringLiteral("unknown");
    if (!mainFrameRect.isEmpty()) {
        targetScreen = screenForGlobalRect(mainFrameRect);
        if (targetScreen != nullptr) {
            targetReason = QStringLiteral("main-window-frame-overlap");
        }
    }
    if (targetScreen == nullptr && !videoGlobalRect.isEmpty()) {
        targetScreen = screenForGlobalRect(videoGlobalRect);
        if (targetScreen != nullptr) {
            targetReason = QStringLiteral("video-widget-overlap");
        }
    }
    if (targetScreen == nullptr && windowHandle() != nullptr) {
        targetScreen = windowHandle()->screen();
        if (targetScreen != nullptr) {
            targetReason = QStringLiteral("main-window-handle");
        }
    }
    if (targetScreen == nullptr) {
        targetScreen = screenForWidget(this);
        if (targetScreen != nullptr) {
            targetReason = QStringLiteral("main-widget");
        }
    }
    if (targetScreen == nullptr && videoWidget_ != nullptr) {
        targetScreen = screenForWidget(videoWidget_);
        if (targetScreen != nullptr) {
            targetReason = QStringLiteral("video-widget");
        }
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
        if (targetScreen != nullptr) {
            targetReason = QStringLiteral("primary-screen-fallback");
        }
    }
    if (targetScreen == nullptr) {
        appendLog(QStringLiteral("fullscreen: enter aborted because no target screen could be resolved."));
        return;
    }

    appendLog(QStringLiteral("fullscreen: enter requested. reason=%1 target=%2 main-window-screen=%3 main-widget-screen=%4 video-screen=%5 fullscreen-screen-before=%6 cursor=%7,%8")
                  .arg(targetReason,
                       describeScreen(targetScreen),
                       describeScreen(windowHandle() != nullptr ? windowHandle()->screen() : nullptr),
                       describeScreen(screenForWidget(this)),
                       describeScreen(videoWidget_ != nullptr ? screenForWidget(videoWidget_) : nullptr),
                       describeScreen(fullscreenWindow_->windowHandle() != nullptr
                                          ? fullscreenWindow_->windowHandle()->screen()
                                          : nullptr))
                  .arg(QCursor::pos().x())
                  .arg(QCursor::pos().y()));
    appendLog(QStringLiteral("fullscreen: frame rect=%1,%2 %3x%4 video rect=%5,%6 %7x%8")
                  .arg(mainFrameRect.x())
                  .arg(mainFrameRect.y())
                  .arg(mainFrameRect.width())
                  .arg(mainFrameRect.height())
                  .arg(videoGlobalRect.x())
                  .arg(videoGlobalRect.y())
                  .arg(videoGlobalRect.width())
                  .arg(videoGlobalRect.height()));

    if (pipWindow_ != nullptr) {
        pipWindow_->hide();
    }

    // Force the native handle to exist so the target screen is honored
    // on the first fullscreen open as well.
    fullscreenWindow_->winId();
    if (fullscreenWindow_->windowHandle() == nullptr) {
        appendLog(QStringLiteral("fullscreen: window handle is null even after winId(); proceeding without explicit screen binding."));
    } else {
        if (windowHandle() != nullptr) {
            fullscreenWindow_->windowHandle()->setTransientParent(windowHandle());
        }
        appendLog(QStringLiteral("fullscreen: before setScreen fullscreen-handle-screen=%1")
                      .arg(describeScreen(fullscreenWindow_->windowHandle()->screen())));
        fullscreenWindow_->windowHandle()->setScreen(targetScreen);
        appendLog(QStringLiteral("fullscreen: after setScreen fullscreen-handle-screen=%1")
                      .arg(describeScreen(fullscreenWindow_->windowHandle()->screen())));
    }
    fullscreenOverlayContainer_->winId();
    if (fullscreenOverlayContainer_ != nullptr && fullscreenOverlayContainer_->windowHandle() != nullptr) {
        if (fullscreenWindow_->windowHandle() != nullptr) {
            fullscreenOverlayContainer_->windowHandle()->setTransientParent(fullscreenWindow_->windowHandle());
        }
        fullscreenOverlayContainer_->windowHandle()->setScreen(targetScreen);
    }
    fullscreenWindow_->setGeometry(targetScreen->geometry());
    appendLog(QStringLiteral("fullscreen: geometry set to target screen rect=%1,%2 %3x%4")
                  .arg(targetScreen->geometry().x())
                  .arg(targetScreen->geometry().y())
                  .arg(targetScreen->geometry().width())
                  .arg(targetScreen->geometry().height()));

    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->setVideoOutput(fullscreenVideoWidget_);
    }
    fullscreenVideoWidget_->setAspectRatioMode(Qt::IgnoreAspectRatio);
    fullscreenWindow_->showFullScreen();
    appendLog(QStringLiteral("fullscreen: showFullScreen called. fullscreen-screen-immediate=%1")
                  .arg(describeScreen(fullscreenWindow_->windowHandle() != nullptr
                                          ? fullscreenWindow_->windowHandle()->screen()
                                          : nullptr)));
    fullscreenWindow_->raise();
    fullscreenWindow_->activateWindow();
    fullscreenVideoWidget_->setFocus(Qt::ActiveWindowFocusReason);
    fullscreenActive_ = true;
    syncFullscreenOverlayState();
    showFullscreenCursor();
    restartFullscreenCursorHideTimer();
    handleFullscreenChanged(true);

    const QString requestedScreenDescription = describeScreen(targetScreen);
    QTimer::singleShot(0, this, [this, requestedScreenDescription]() {
        appendLog(QStringLiteral("fullscreen: post-show(0ms) requested=%1 actual=%2")
                      .arg(requestedScreenDescription,
                           describeScreen(fullscreenWindow_ != nullptr && fullscreenWindow_->windowHandle() != nullptr
                                              ? fullscreenWindow_->windowHandle()->screen()
                                              : nullptr)));
    });
    QTimer::singleShot(250, this, [this, requestedScreenDescription]() {
        appendLog(QStringLiteral("fullscreen: post-show(250ms) requested=%1 actual=%2")
                      .arg(requestedScreenDescription,
                           describeScreen(fullscreenWindow_ != nullptr && fullscreenWindow_->windowHandle() != nullptr
                                              ? fullscreenWindow_->windowHandle()->screen()
                                              : nullptr)));
    });
}

void MainWindow::exitFullscreen()
{
    if (!fullscreenActive_) {
        return;
    }

    fullscreenActive_ = false;
    if (fullscreenCursorHideTimer_ != nullptr) {
        fullscreenCursorHideTimer_->stop();
    }
    if (fullscreenOverlayContainer_ != nullptr) {
        fullscreenOverlayContainer_->hide();
    }
    showFullscreenCursor();
    if (fullscreenWindow_ != nullptr) {
        fullscreenWindow_->hide();
    }

    if (mediaPlayer_ != nullptr) {
        if (videoDetachedToPip_ && pipVideoWidget_ != nullptr) {
            mediaPlayer_->setVideoOutput(pipVideoWidget_);
        } else {
            mediaPlayer_->setVideoOutput(videoWidget_);
        }
    }

    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    videoWidget_->updateGeometry();
    videoWidget_->update();
    if (videoDetachedToPip_ && pipWindow_ != nullptr && !currentChannelName_.trimmed().isEmpty()) {
        pipWindow_->show();
        pipWindow_->raise();
    }
    raise();
    activateWindow();
    if (videoDetachedToPip_) {
        if (pipVideoWidget_ != nullptr) {
            pipVideoWidget_->setFocus(Qt::ActiveWindowFocusReason);
        }
    } else if (videoWidget_ != nullptr) {
        videoWidget_->setFocus(Qt::ActiveWindowFocusReason);
    }
    handleFullscreenChanged(false);
}

void MainWindow::refreshQuickButtons()
{
    for (int i = 0; i < kQuickFavoriteCount; ++i) {
        auto *button = quickFavoriteButtons_[i];
        if (button == nullptr) {
            continue;
        }
        if (i < favorites_.size()) {
            const QString channel = favorites_.at(i);
            button->setText(QString("%1 %2").arg(i + 1).arg(channel));
            button->setToolTip(QString());
            button->setProperty("channelName", channel);
            button->setEnabled(scanProcess_ == nullptr || scanProcess_->state() == QProcess::NotRunning);
        } else {
            button->setText(QString::number(i + 1));
            button->setToolTip(QString());
            button->setProperty("channelName", QString());
            button->setEnabled(false);
        }
    }
}

void MainWindow::saveFavorites()
{
    favorites_ = normalizedFavorites(favorites_, kQuickFavoriteCount);
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue("favorites", favorites_);
}

void MainWindow::loadFavorites()
{
    QSettings settings("tv_tuner_gui", "watcher");
    const QStringList storedFavorites = settings.value("favorites").toStringList();
    favorites_ = normalizedFavorites(storedFavorites, kQuickFavoriteCount);
    if (favorites_ != storedFavorites) {
        settings.setValue("favorites", favorites_);
    }
}

void MainWindow::loadXspfChannelHints()
{
    xspfNumberByTuneKey_.clear();
    xspfProgramByChannel_.clear();

    QString jsonError;
    const bool loadedJsonHints = loadChannelHintsFromJson(xspfNumberByTuneKey_,
                                                          xspfProgramByChannel_,
                                                          &jsonError);
    if (loadedJsonHints) {
        appendLog(QString("Loaded %1 saved channel hint mapping%2 from %3")
                      .arg(xspfProgramByChannel_.size())
                      .arg(xspfProgramByChannel_.size() == 1 ? QString() : QString("s"))
                      .arg(resolveChannelHintsJsonPath()));
    } else if (!jsonError.trimmed().isEmpty()) {
        appendLog(jsonError);
    }

    const QString xspfPath = QDir::home().filePath("Desktop/tv.xspf");
    QFile file(xspfPath);
    if (!file.exists()) {
        if (!loadedJsonHints) {
            appendLog("No saved channel hint JSON or XSPF playlist found; using channels.conf metadata.");
        }
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog("Could not open XSPF playlist: " + xspfPath);
        return;
    }

    QXmlStreamReader xml(&file);
    QString currentTitle;
    QString currentProgram;
    QString currentFrequency;
    bool inTrack = false;
    bool inVlcOption = false;
    QString optionText;

    auto flushTrack = [this, &currentTitle, &currentProgram, &currentFrequency]() {
        if (currentTitle.isEmpty() || currentProgram.isEmpty()) {
            return;
        }
        const int firstSpace = currentTitle.indexOf(' ');
        if (firstSpace <= 0 || firstSpace >= currentTitle.size() - 1) {
            return;
        }
        const QString channelNumber = currentTitle.left(firstSpace).trimmed();
        const QString channelName = currentTitle.mid(firstSpace + 1).trimmed();
        if (!channelName.isEmpty()) {
            const QString normalizedChannelNumber = normalizeChannelNumberHint(channelNumber);
            if (!normalizedChannelNumber.isEmpty() && !currentFrequency.isEmpty()) {
                xspfNumberByTuneKey_.insert(tuneKey(currentFrequency, currentProgram), normalizedChannelNumber);
            }
            xspfProgramByChannel_.insert(channelName, currentProgram);
            const QString displayLabel = channelDisplayLabel(channelName, normalizedChannelNumber);
            if (!displayLabel.isEmpty() && displayLabel != channelName) {
                xspfProgramByChannel_.insert(displayLabel, currentProgram);
            }
        }
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == u"track") {
                inTrack = true;
                currentTitle.clear();
                currentProgram.clear();
                currentFrequency.clear();
            } else if (inTrack && name == u"title") {
                currentTitle = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            } else if (inTrack && name == u"location") {
                const QString location = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("frequency=(\\d+)")).match(location);
                currentFrequency = match.hasMatch() ? match.captured(1) : QString();
            } else if (inTrack && name == u"option" && xml.namespaceUri().toString().contains("videolan.org")) {
                inVlcOption = true;
                optionText.clear();
            }
        } else if (xml.isCharacters() && inVlcOption) {
            optionText += xml.text().toString();
        } else if (xml.isEndElement()) {
            const QStringView name = xml.name();
            if (inVlcOption && name == u"option") {
                inVlcOption = false;
                const QString opt = optionText.trimmed();
                if (opt.startsWith("program=")) {
                    currentProgram = opt.mid(QString("program=").size()).trimmed();
                }
                optionText.clear();
            } else if (inTrack && name == u"track") {
                flushTrack();
                inTrack = false;
                currentTitle.clear();
                currentProgram.clear();
                currentFrequency.clear();
            }
        }
    }

    if (xml.hasError()) {
        appendLog("Failed to parse XSPF playlist: " + xml.errorString());
        if (!loadedJsonHints) {
            xspfNumberByTuneKey_.clear();
            xspfProgramByChannel_.clear();
        }
        return;
    }

    QString saveError;
    if (!saveChannelHintsToJson(xspfNumberByTuneKey_, xspfProgramByChannel_, &saveError)
        && !saveError.trimmed().isEmpty()) {
        appendLog(saveError);
    }

    appendLog(QString("Loaded %1 XSPF program mappings from %2 and cached them to %3")
                  .arg(xspfProgramByChannel_.size())
                  .arg(xspfPath)
                  .arg(resolveChannelHintsJsonPath()));
}

void MainWindow::loadChannelsFileIfPresent()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appendLog("Could not resolve app data directory for channels list.");
        return;
    }

    QDir dir(appDataPath);
    channelsFilePath_ = dir.filePath("channels.conf");
    QFile file(channelsFilePath_);
    if (!file.exists()) {
        appendLog("No saved channels file found. Run a scan once to create one.");
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog("Could not open channels file: " + channelsFilePath_);
        return;
    }

    channelsTable_->setRowCount(0);
    channelLines_.clear();

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty()) {
            parseAndStoreLine(line);
        }
    }

    appendLog(QString("Loaded %1 channel entries from %2")
                  .arg(channelLines_.size())
                  .arg(channelsFilePath_));
}
