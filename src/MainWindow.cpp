#include "MainWindow.h"
#include "TvGuideDialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAudioOutput>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
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
#include <QXmlStreamReader>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>
#include <QScreen>
#include <QWindow>
#include <QCursor>
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

namespace {
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

QString guideEntryToolTipText(const TvGuideEntry &entry)
{
    return QString("%1\n%2 - %3")
        .arg(entry.title,
             entry.startUtc.toLocalTime().toString("ddd h:mm AP"),
             entry.endUtc.toLocalTime().toString("ddd h:mm AP"));
}

struct GuideChannelInfo {
    QString name;
    qint64 frequencyHz{0};
    int serviceId{-1};
};

struct RawGuideEvent {
    int serviceId{-1};
    QDateTime startUtc;
    QDateTime endUtc;
    QString title;
};

struct SectionAssembler {
    QByteArray bytes;
    int expectedLength{-1};
};

struct ParsedGuideData {
    QList<RawGuideEvent> events;
    QHash<int, int> atscSourceToProgram;
    QSet<int> atscPsipTableIds;
};

struct DemuxSectionReader {
    int pid{-1};
    int fd{-1};
};

QSet<int> mappedProgramIdsFromParsedGuideData(const ParsedGuideData &parsed);
bool hasCoverageForAllExpectedServices(const ParsedGuideData &parsed, const QSet<int> &expectedServiceIds);

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
                events.append(event);
            }
        }

        offset = descriptorsEnd;
    }
}

ParsedGuideData parseGuideEventsFromTransport(const QByteArray &transport)
{
    ParsedGuideData parsed;
    QSet<QString> dedupe;
    QHash<int, SectionAssembler> sectionsByPid;
    QSet<int> atscPsipPids;
    atscPsipPids.insert(kAtscPsipPid);

    auto processSection = [&](int pid, const QByteArray &section) {
        if (section.size() < 3) {
            return;
        }

        if (atscPsipPids.contains(pid)) {
            parsed.atscPsipTableIds.insert(byteAt(section, 0));
            appendAtscPsipPidsFromMgtSection(section, atscPsipPids);
            appendAtscSourceMapFromVctSection(section, parsed.atscSourceToProgram);
            appendAtscEventsFromSection(section, parsed.events, dedupe);
        } else if (pid == kDvbEitPid) {
            appendDvbEventsFromSection(section, parsed.events, dedupe);
        }
    };

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
            processSection(pid, section);
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

    return parsed;
}

void processGuideSectionForPid(int pid,
                               const QByteArray &section,
                               ParsedGuideData &parsed,
                               QSet<QString> &dedupe,
                               QSet<int> &atscPsipPids)
{
    if (section.size() < 3) {
        return;
    }

    if (atscPsipPids.contains(pid)) {
        parsed.atscPsipTableIds.insert(byteAt(section, 0));
        appendAtscPsipPidsFromMgtSection(section, atscPsipPids);
        appendAtscSourceMapFromVctSection(section, parsed.atscSourceToProgram);
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
                                 int maxCaptureMs = kGuideCaptureMaxMs)
{
    events.clear();
    atscSourceToProgram.clear();
    atscPsipTableIds.clear();
    errorText.clear();

    const int effectiveCaptureMaxMs = std::max(250, maxCaptureMs);
    const int captureMinMs = std::min(kGuideCaptureMinMs, effectiveCaptureMaxMs);

    const QString demuxPath = QString("/dev/dvb/adapter%1/demux0").arg(adapter);
    QList<DemuxSectionReader> readers;
    QSet<int> activePids;
    ParsedGuideData parsed;
    QSet<QString> dedupe;
    QSet<int> discoveredAtscPids{ kAtscPsipPid };
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
                                              discoveredAtscPids);
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

            if (hasCoverageForAllExpectedServices(parsed, expectedServiceIds)) {
                break;
            }

            if (!parsed.events.isEmpty()
                && !parsed.atscSourceToProgram.isEmpty()
                && elapsedMs >= 2200
                && elapsedMs - lastProgressMs >= 900) {
                break;
            }
        }
    }

    closeReaders();

    events = parsed.events;
    atscSourceToProgram = parsed.atscSourceToProgram;
    atscPsipTableIds = parsed.atscPsipTableIds;

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

bool hasCoverageForAllExpectedServices(const ParsedGuideData &parsed, const QSet<int> &expectedServiceIds)
{
    if (expectedServiceIds.isEmpty()) {
        return false;
    }
    const QSet<int> mappedPrograms = mappedProgramIdsFromParsedGuideData(parsed);
    for (int expectedServiceId : expectedServiceIds) {
        if (!mappedPrograms.contains(expectedServiceId)) {
            return false;
        }
    }
    return true;
}

QVector<GuideChannelInfo> parseGuideChannels(const QStringList &channelLines)
{
    QVector<GuideChannelInfo> channels;
    QSet<QString> dedupe;

    for (const QString &line : channelLines) {
        const QStringList parts = line.split(':');
        if (parts.size() < 6) {
            continue;
        }

        const QString name = parts[0].trimmed();
        bool freqOk = false;
        bool serviceOk = false;
        const qint64 frequencyHz = parts[1].trimmed().toLongLong(&freqOk);
        const int serviceId = parts[5].trimmed().toInt(&serviceOk, 0);
        if (name.isEmpty() || !freqOk || frequencyHz <= 0 || !serviceOk || serviceId <= 0) {
            continue;
        }

        const QString key = QString("%1|%2|%3").arg(name).arg(frequencyHz).arg(serviceId);
        if (dedupe.contains(key)) {
            continue;
        }
        dedupe.insert(key);

        GuideChannelInfo info;
        info.name = name;
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
                               QString &errorText)
{
    events.clear();
    atscSourceToProgram.clear();
    atscPsipTableIds.clear();
    errorText.clear();

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

            if (hasCoverageForAllExpectedServices(parsed, expectedServiceIds)) {
                break;
            }

            if (!parsed.events.isEmpty()
                && !parsed.atscSourceToProgram.isEmpty()
                && elapsedMs >= 2200
                && elapsedMs - lastProgressMs >= 900) {
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
    events = parsed.events;
    atscSourceToProgram = parsed.atscSourceToProgram;
    atscPsipTableIds = parsed.atscPsipTableIds;
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
                                  int totalTimeoutMs = kGuideLookupTotalMaxMs)
{
    events.clear();
    atscSourceToProgram.clear();
    atscPsipTableIds.clear();
    errorText.clear();

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
                                                      remainingCaptureMs);
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
    mediaPlayer_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    reconnectTimer_ = new QTimer(this);
    currentShowTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    currentShowTimer_->setSingleShot(true);

    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setVideoOutput(videoWidget_);
    QSettings settings("tv_tuner_gui", "watcher");
    const int savedVolume = std::clamp(settings.value("volume_percent", 85).toInt(), 0, 100);
    audioOutput_->setVolume(static_cast<float>(savedVolume) / 100.0f);
    volumeSlider_->setValue(savedVolume);

    connect(scanProcess_, &QProcess::readyReadStandardOutput, this, &MainWindow::handleStdOut);
    connect(scanProcess_, &QProcess::readyReadStandardError, this, &MainWindow::handleStdErr);
    connect(scanProcess_, &QProcess::finished, this, &MainWindow::processFinished);
    connect(zapProcess_, &QProcess::readyReadStandardError, this, &MainWindow::handleZapStdErr);
    connect(zapProcess_, &QProcess::finished, this, &MainWindow::handleZapFinished);
    connect(zapProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        appendLog(QString("zap: errorOccurred=%1 (%2)")
                      .arg(processErrorToString(error), zapProcess_->errorString()));
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
    connect(volumeSlider_, &QSlider::valueChanged, this, &MainWindow::handleVolumeChanged);
    connect(muteButton_, &QPushButton::toggled, this, &MainWindow::handleMuteToggled);

    logFilePath_ = resolveProjectLogPath();
    loadFavorites();
    loadXspfChannelHints();
    loadChannelsFileIfPresent();
    refreshQuickButtons();
    playbackStatusLabel_->setText(playbackStatusText());
    setCurrentShowStatus("NO EIT DATA");
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
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl());
    }

    suppressBridgeExitReconnect_ = true;
    stopProcess(streamBridgeProcess_, 1200);
    suppressBridgeExitReconnect_ = false;

    suppressZapExitReconnect_ = true;
    stopProcess(zapProcess_, 1200);
    suppressZapExitReconnect_ = false;

    stopProcess(scanProcess_, 1200);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    appendLog("Application closing: releasing tuner resources.");
    exitFullscreen();
    stopWatching();
    stopProcess(scanProcess_, 1200);
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == videoWidget_ && event->type() == QEvent::MouseButtonDblClick) {
        toggleFullscreen();
        return true;
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

void MainWindow::buildUi()
{
    setWindowTitle("Voncloft TV Tuner");
    resize(1320, 840);

    QMenu *fileMenu = menuBar()->addMenu("File");
    QAction *aboutAction = fileMenu->addAction("About");
    aboutAction->setShortcut(QKeySequence(Qt::Key_F1));
    aboutAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About",
            QString("Created by Voncloft\nVersion %1").arg(QStringLiteral(TV_TUNER_GUI_VERSION)));
    });

    QMenu *viewMenu = menuBar()->addMenu("View");
    QAction *tvGuideAction = viewMenu->addAction("TV Guide");
    tvGuideAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    tvGuideAction->setShortcutContext(Qt::WindowShortcut);
    connect(tvGuideAction, &QAction::triggered, this, &MainWindow::openTvGuide);

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

    auto *tuningPage = new QWidget(tabs_);
    auto *tuningLayout = new QVBoxLayout(tuningPage);

    auto *logsPage = new QWidget(tabs_);
    auto *logsLayout = new QVBoxLayout(logsPage);

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
    currentShowLabel_ = new QLabel("NO EIT DATA", watchPage_);
    addFavoriteButton_ = new QPushButton("Add Favorite", watchPage_);
    removeFavoriteButton_ = new QPushButton("Remove Favorite", watchPage_);

    stopButton_->setEnabled(false);
    stopWatchButton_->setEnabled(false);
    muteButton_->setCheckable(true);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(85);
    volumeSlider_->setMinimumWidth(120);
    volumeSlider_->setMaximumWidth(220);
    playbackStatusLabel_->setMinimumWidth(100);
    playbackStatusLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    currentShowLabel_->setMinimumWidth(160);
    currentShowLabel_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    currentShowLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

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
    videoWidget_->setMinimumHeight(360);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget_->setStyleSheet("background: #000;");

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
    favoritesControlsRow->addSpacing(8);
    favoritesControlsRow->addWidget(new QLabel("Favorites:", watchPage_));
    for (int i = 0; i < 8; ++i) {
        quickFavoriteButtons_[i] = new QPushButton(QString::number(i + 1), watchPage_);
        quickFavoriteButtons_[i]->setEnabled(false);
        quickFavoriteButtons_[i]->setMinimumWidth(56);
        quickFavoriteButtons_[i]->setMaximumWidth(110);
        quickFavoriteButtons_[i]->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        favoritesControlsRow->addWidget(quickFavoriteButtons_[i]);
        connect(quickFavoriteButtons_[i], &QPushButton::clicked, this, &MainWindow::triggerQuickFavorite);
    }
    favoritesControlsRow->addStretch(1);

    auto *statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(8);
    statusRow->addWidget(playbackStatusLabel_);
    statusRow->addStretch(1);
    statusRow->addWidget(currentShowLabel_, 1);

    favoritesLayout->addLayout(favoritesControlsRow);
    favoritesLayout->addLayout(statusRow);

    watchLayout->addWidget(watchControlsContainer_);
    watchLayout->addWidget(contentSplitter_, 1);
    watchLayout->addWidget(favoritesContainer_);

    logOutput_ = new QPlainTextEdit(logsPage);
    logOutput_->setReadOnly(true);
    logOutput_->setMaximumBlockCount(4000);
    logOutput_->setPlaceholderText("w_scan2 and tuning output will appear here...");
    logsLayout->addWidget(logOutput_);

    tabs_->addTab(watchPage_, "Video");
    tabs_->addTab(tuningPage, "Tuning");
    tabs_->addTab(logsPage, "Logs");
    mainLayout->addWidget(tabs_, 1);
    setCentralWidget(root);

    videoWidget_->installEventFilter(this);

    statusBar()->showMessage("Ready");

    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopScan);
    connect(watchButton_, &QPushButton::clicked, this, &MainWindow::watchSelectedChannel);
    connect(stopWatchButton_, &QPushButton::clicked, this, &MainWindow::stopWatching);
    connect(openFileButton_, &QPushButton::clicked, this, &MainWindow::openMediaFile);
    connect(addFavoriteButton_, &QPushButton::clicked, this, &MainWindow::addSelectedFavorite);
    connect(removeFavoriteButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedFavorite);
    connect(channelsTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        watchSelectedChannel();
    });
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
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

    const QString program = "w_scan2";
    const QStringList args = makeArguments();

    appendLog("[" + QDateTime::currentDateTime().toString(Qt::ISODate) + "] Starting: " + program + " " + args.join(' '));
    scanProcess_->start(program, args);

    if (!scanProcess_->waitForStarted(2000)) {
        QMessageBox::critical(this, "Failed to start", "Could not launch w_scan2. Check that it is in your PATH.");
        appendLog("Failed to start w_scan2.");
        return;
    }

    setScanningState(true);
    statusBar()->showMessage("Scanning...");
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

    for (int i = 0; i < lines.size() - 1; ++i) {
        const QString line = lines[i].trimmed();
        if (!line.isEmpty()) {
            appendLog("stderr: " + line);
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
    const int rowCount = channelsTable_->rowCount();
    const QString endMsg = QString("Scan finished (exit=%1). Channels parsed: %2").arg(exitCode).arg(rowCount);
    appendLog(endMsg);
    statusBar()->showMessage(endMsg);
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
}

void MainWindow::appendLog(const QString &line)
{
    logOutput_->appendPlainText(line);
    QTextCursor cursor = logOutput_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logOutput_->setTextCursor(cursor);

    if (!logFilePath_.isEmpty()) {
        QFile logFile(logFilePath_);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            const QString entry = QString("[%1] %2\n")
                                      .arg(QDateTime::currentDateTime().toString(Qt::ISODate), line);
            logFile.write(entry.toUtf8());
        }
    }
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

    const QString channelNumber = xspfNumberByTuneKey_.value(tuneKeyForParts(parts), parts[5].trimmed());
    const bool sortingEnabled = channelsTable_->isSortingEnabled();
    const int sortColumn = channelsTable_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = channelsTable_->horizontalHeader()->sortIndicatorOrder();

    if (sortingEnabled) {
        channelsTable_->setSortingEnabled(false);
    }

    const int row = channelsTable_->rowCount();
    channelsTable_->insertRow(row);
    channelsTable_->setItem(row, 0, new QTableWidgetItem(channelNumber));
    channelsTable_->setItem(row, 1, new QTableWidgetItem(channelName));
    channelsTable_->setItem(row, 2, new QTableWidgetItem(normalizedLine));

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

QString MainWindow::programIdForChannel(const QString &channelName) const
{
    if (channelName.isEmpty()) {
        return {};
    }

    if (xspfProgramByChannel_.contains(channelName)) {
        return xspfProgramByChannel_.value(channelName);
    }

    auto parseProgramFromLine = [&channelName](const QString &line) -> QString {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) {
            return {};
        }
        const QStringList parts = trimmedLine.split(':');
        if (parts.size() < 6) {
            return {};
        }
        if (parts[0].trimmed() != channelName) {
            return {};
        }
        return parts[5].trimmed();
    };

    for (const QString &line : channelLines_) {
        const QString programId = parseProgramFromLine(line);
        if (!programId.isEmpty()) {
            return programId;
        }
    }

    QFile file(channelsFilePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        const QString programId = parseProgramFromLine(line);
        if (!programId.isEmpty()) {
            return programId;
        }
    }
    return {};
}

void MainWindow::watchSelectedChannel()
{
    if (scanProcess_->state() != QProcess::NotRunning) {
        QMessageBox::warning(this, "Scan in progress", "Stop scanning before starting live viewing.");
        return;
    }

    const QString channelName = selectedChannelNameFromTable();
    if (channelName.isEmpty()) {
        QMessageBox::information(this, "Select a channel", "Select a channel row first.");
        return;
    }

    startWatchingChannel(channelName, false);
}

void MainWindow::openMediaFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Open Media File",
        QDir::homePath(),
        "Video Files (*.mp4 *.mkv *.webm *.avi *.mov *.ts *.m2ts);;All Files (*)");
    if (filePath.isEmpty()) {
        return;
    }

    stopWatching();
    userStoppedWatching_ = true;
    currentChannelName_ = "File: " + QFileInfo(filePath).fileName();
    setCurrentShowStatus(QFileInfo(filePath).fileName());

    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setSource(QUrl::fromLocalFile(filePath));
    mediaPlayer_->play();

    appendLog("player: Opened local media file: " + filePath);
    playbackStatusLabel_->setText(playbackStatusText());
    statusBar()->showMessage("Opening local file...");
}

void MainWindow::openTvGuide()
{
    if (tvGuideDialog_ == nullptr) {
        tvGuideDialog_ = new TvGuideDialog(this);
        connect(tvGuideDialog_, &TvGuideDialog::refreshRequested, this, &MainWindow::refreshTvGuide);
    }

    tvGuideDialog_->show();
    tvGuideDialog_->raise();
    tvGuideDialog_->activateWindow();
    refreshTvGuide();
}

void MainWindow::refreshTvGuide()
{
    if (scanProcess_ != nullptr && scanProcess_->state() != QProcess::NotRunning) {
        QMessageBox::warning(this, "Scan in progress", "Stop scanning before opening the TV guide.");
        return;
    }

    if (tvGuideDialog_ == nullptr) {
        tvGuideDialog_ = new TvGuideDialog(this);
        connect(tvGuideDialog_, &TvGuideDialog::refreshRequested, this, &MainWindow::refreshTvGuide);
    }

    if (channelLines_.isEmpty()) {
        loadChannelsFileIfPresent();
    }
    if (channelLines_.isEmpty()) {
        QMessageBox::information(this, "No channels", "No channels are loaded yet. Run a scan first.");
        return;
    }

    if (!persistChannelsFile() && channelsFilePath_.isEmpty()) {
        QMessageBox::warning(this, "Missing channels file", "Could not prepare channels.conf for guide collection.");
        return;
    }

    const QVector<GuideChannelInfo> channels = parseGuideChannels(channelLines_);
    if (channels.isEmpty()) {
        QMessageBox::warning(this,
                             "Unsupported channel format",
                             "Could not parse channels.conf entries for guide mapping.");
        return;
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
        QMessageBox::warning(this, "Guide unavailable", "No tuner is available for EIT/guide collection.");
        return;
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
            QMessageBox::warning(this,
                                 "Guide unavailable",
                                 "Could not determine the currently tuned multiplex while live playback is active.");
            return;
        }
    }

    const bool liveMuxOnlyRefresh = livePlaybackActive && guideAdapter == playbackAdapter && liveFrequencyHz > 0;
    const bool hadGuideCache = !guideEntriesCache_.isEmpty();

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
    tvGuideDialog_->setLoadingState(loadingMessage);
    statusBar()->showMessage(statusMessage);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QList<qint64> frequencies = channelsByFrequency.keys();
    if (liveMuxOnlyRefresh) {
        frequencies = { liveFrequencyHz };
    }

    QProgressDialog progress(liveMuxOnlyRefresh
                                 ? "Collecting OTA EIT schedule data from the current multiplex..."
                                 : "Collecting OTA EIT schedule data...",
                             "Cancel",
                             0,
                             frequencies.size(),
                             this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QHash<QString, QList<TvGuideEntry>> entriesByChannel = liveMuxOnlyRefresh ? guideEntriesCache_
                                                                              : QHash<QString, QList<TvGuideEntry>>{};
    QStringList errors;
    int frequenciesWithData = 0;
    int decodedEvents = 0;
    QSet<int> observedPsipTableIds;
    QStringList muxSummaryLines;
    QStringList missingChannelNames;

    QApplication::setOverrideCursor(Qt::BusyCursor);
    for (int i = 0; i < frequencies.size(); ++i) {
        const qint64 frequencyHz = frequencies.at(i);
        const QVector<GuideChannelInfo> frequencyChannels = channelsByFrequency.value(frequencyHz);
        if (frequencyChannels.isEmpty()) {
            continue;
        }
        const bool reuseCurrentTunedMux = liveMuxOnlyRefresh && frequencyHz == liveFrequencyHz;

        progress.setValue(i);
        progress.setLabelText(QString("%1 %2 MHz (%3/%4) via %5")
                                  .arg(reuseCurrentTunedMux ? "Reading" : "Tuning")
                                  .arg(QString::number(static_cast<double>(frequencyHz) / 1000000.0, 'f', 1))
                                  .arg(i + 1)
                                  .arg(frequencies.size())
                                  .arg(frequencyChannels.first().name));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        if (progress.wasCanceled()) {
            break;
        }

        QList<RawGuideEvent> frequencyEvents;
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
                                        errorText);
        } else {
            captureGuideEventsForChannel(channelsFilePath_,
                                         guideAdapter,
                                         guideFrontend,
                                         frequencyChannels.first().name,
                                         expectedServiceIds,
                                         frequencyEvents,
                                         atscSourceToProgram,
                                         psipTableIds,
                                         errorText);
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

        QHash<int, QStringList> namesByServiceId;
        for (const GuideChannelInfo &channel : frequencyChannels) {
            namesByServiceId[channel.serviceId].append(channel.name);
        }

        int mappedForFrequency = 0;
        QSet<QString> mappedChannelNamesForMux;
        QHash<QString, QList<TvGuideEntry>> mappedEntriesForMux;
        for (const RawGuideEvent &event : frequencyEvents) {
            int mappedServiceId = event.serviceId;
            if (atscSourceToProgram.contains(mappedServiceId)) {
                mappedServiceId = atscSourceToProgram.value(mappedServiceId);
            }
            const QStringList mappedChannels = namesByServiceId.value(mappedServiceId);
            if (mappedChannels.isEmpty()) {
                continue;
            }

            TvGuideEntry guideEntry;
            guideEntry.startUtc = event.startUtc;
            guideEntry.endUtc = event.endUtc;
            guideEntry.title = event.title;
            for (const QString &channelName : mappedChannels) {
                mappedEntriesForMux[channelName].append(guideEntry);
                mappedChannelNamesForMux.insert(channelName);
                ++mappedForFrequency;
            }
        }
        if (!mappedEntriesForMux.isEmpty()) {
            for (const GuideChannelInfo &channel : frequencyChannels) {
                entriesByChannel.remove(channel.name);
            }
            for (auto it = mappedEntriesForMux.cbegin(); it != mappedEntriesForMux.cend(); ++it) {
                entriesByChannel.insert(it.key(), it.value());
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
    }
    progress.setValue(frequencies.size());
    QApplication::restoreOverrideCursor();

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    int mappedEntries = 0;
    QDateTime latestEndUtc = nowUtc.addSecs(6 * 3600);

    for (const QString &channelName : channelOrder) {
        QList<TvGuideEntry> sorted = entriesByChannel.value(channelName);
        std::sort(sorted.begin(), sorted.end(), [](const TvGuideEntry &a, const TvGuideEntry &b) {
            return a.startUtc < b.startUtc;
        });

        QSet<QString> dedupe;
        QList<TvGuideEntry> cleaned;
        for (const TvGuideEntry &entry : sorted) {
            if (!entry.startUtc.isValid() || !entry.endUtc.isValid() || entry.endUtc <= entry.startUtc) {
                continue;
            }
            if (entry.endUtc < nowUtc.addSecs(-7200) || entry.startUtc > nowUtc.addDays(2)) {
                continue;
            }

            const QString dedupeKey = QString("%1|%2|%3")
                                          .arg(entry.startUtc.toSecsSinceEpoch())
                                          .arg(entry.endUtc.toSecsSinceEpoch())
                                          .arg(entry.title);
            if (dedupe.contains(dedupeKey)) {
                continue;
            }
            dedupe.insert(dedupeKey);
            cleaned.append(entry);
            if (entry.endUtc > latestEndUtc) {
                latestEndUtc = entry.endUtc;
            }
        }

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

    guideEntriesCache_ = entriesByChannel;
    applyCurrentShowStatusFromGuideCache();
    tvGuideDialog_->setGuideData(channelOrder, entriesByChannel, windowStartUtc, slotMinutes, slotCount, statusText);
    statusBar()->showMessage(statusText.section('\n', 0, 0));
}

void MainWindow::setCurrentShowStatus(const QString &text, const QString &toolTip)
{
    if (currentShowLabel_ == nullptr) {
        return;
    }

    currentShowLabel_->setText(text);
    currentShowLabel_->setToolTip(toolTip);
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

    TvGuideEntry entry;
    bool isCurrent = false;
    if (!findCurrentOrNextGuideEntry(guideEntriesCache_.value(currentChannelName_),
                                     QDateTime::currentDateTimeUtc(),
                                     entry,
                                     isCurrent)) {
        if (currentShowTimer_ != nullptr) {
            currentShowTimer_->stop();
        }
        return false;
    }

    const QString label = isCurrent ? entry.title : QString("Next: %1").arg(entry.title);
    setCurrentShowStatus(label, guideEntryToolTipText(entry));
    scheduleCurrentShowRefresh((isCurrent ? entry.endUtc : entry.startUtc).addSecs(1));
    return true;
}

void MainWindow::probeCurrentShowBeforePlayback(const QString &channelName, int programId)
{
    currentShowTimer_->stop();

    if (applyCurrentShowStatusFromGuideCache()) {
        appendLog(QString("current-show: using cached EIT for %1 before playback").arg(channelName));
        return;
    }
    if (noAutoCurrentShowLookupChannels_.contains(channelName)) {
        setCurrentShowStatus("NO EIT DATA", "Auto EIT lookup disabled after a prior no-data result for this channel.");
        appendLog(QString("current-show: skipping pre-playback EIT probe for %1 (previous no-data result)").arg(channelName));
        return;
    }
    if (programId <= 0) {
        setCurrentShowStatus("NO EIT DATA");
        noAutoCurrentShowLookupChannels_.insert(channelName);
        appendLog(QString("current-show: no valid program id for %1; skipping EIT probe").arg(channelName));
        return;
    }

    int lookupFrontend = -1;
    const int playbackAdapter = adapterSpin_->value();
    const int lookupAdapter = findPreferredSeparateGuideAdapter(playbackAdapter,
                                                                frontendSpin_->value(),
                                                                lookupFrontend);
    if (lookupAdapter < 0 || lookupFrontend < 0) {
        currentShowTimer_->stop();
        setCurrentShowStatus("NO EIT DATA", "No separate EIT tuner is available before playback.");
        appendLog(QString("current-show: no separate guide tuner available for %1 before playback").arg(channelName));
        return;
    }

    setCurrentShowStatus("Detecting...", QString("Checking EIT on adapter%1/frontend%2 before playback").arg(lookupAdapter).arg(lookupFrontend));
    statusBar()->showMessage(QString("Checking EIT on adapter%1 before playback...").arg(lookupAdapter));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QList<RawGuideEvent> events;
    QHash<int, int> atscSourceToProgram;
    QSet<int> psipTableIds;
    QString errorText;
    QSet<int> expectedServiceIds;
    expectedServiceIds.insert(programId);

    QApplication::setOverrideCursor(Qt::BusyCursor);
    const bool captured = captureGuideEventsForChannel(channelsFilePath_,
                                                       lookupAdapter,
                                                       lookupFrontend,
                                                       channelName,
                                                       expectedServiceIds,
                                                       events,
                                                       atscSourceToProgram,
                                                       psipTableIds,
                                                       errorText);
    QApplication::restoreOverrideCursor();

    QList<TvGuideEntry> entries;
    for (const RawGuideEvent &event : events) {
        int mappedServiceId = event.serviceId;
        if (atscSourceToProgram.contains(mappedServiceId)) {
            mappedServiceId = atscSourceToProgram.value(mappedServiceId);
        }
        if (mappedServiceId != programId) {
            continue;
        }

        TvGuideEntry entry;
        entry.startUtc = event.startUtc;
        entry.endUtc = event.endUtc;
        entry.title = event.title;
        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const TvGuideEntry &a, const TvGuideEntry &b) {
        return a.startUtc < b.startUtc;
    });

    appendLog(QString("current-show: pre-playback probe %1 on adapter%2/frontend%3 captured=%4 events=%5 mapped=%6 tables=%7")
                  .arg(channelName)
                  .arg(lookupAdapter)
                  .arg(lookupFrontend)
                  .arg(captured ? "yes" : "no")
                  .arg(events.size())
                  .arg(entries.size())
                  .arg(formatTableIdSet(psipTableIds).isEmpty() ? "none" : formatTableIdSet(psipTableIds)));

    if (!entries.isEmpty()) {
        noAutoCurrentShowLookupChannels_.remove(channelName);
        guideEntriesCache_.insert(channelName, entries);
        if (!applyCurrentShowStatusFromGuideCache()) {
            const TvGuideEntry &entry = entries.first();
            setCurrentShowStatus(entry.title, guideEntryToolTipText(entry));
        }
        return;
    }

    noAutoCurrentShowLookupChannels_.insert(channelName);
    currentShowTimer_->stop();
    setCurrentShowStatus("NO EIT DATA", errorText.isEmpty() ? "No EIT data was found before playback started." : errorText);
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

    if (applyCurrentShowStatusFromGuideCache()) {
        return;
    }
    currentShowTimer_->stop();
    if (noAutoCurrentShowLookupChannels_.contains(currentChannelName_)) {
        setCurrentShowStatus("NO EIT DATA", "EIT was checked before playback and no data was found for this channel.");
        return;
    }
    setCurrentShowStatus("NO EIT DATA", "No cached EIT data remains for this channel.");
}

bool MainWindow::startWatchingChannel(const QString &channelName, bool reconnectAttempt)
{
    if (channelName.isEmpty()) {
        return false;
    }

    if (!persistChannelsFile()) {
        QMessageBox::warning(this, "No channel list", "No saved channels are available yet. Run a scan first.");
        return false;
    }

    const QString zapExe = QStandardPaths::findExecutable("dvbv5-zap");
    if (zapExe.isEmpty()) {
        QMessageBox::critical(this, "Missing dependency", "dvbv5-zap was not found in PATH.");
        return false;
    }

    userStoppedWatching_ = false;
    reconnectTimer_->stop();
    if (!reconnectAttempt) {
        reconnectAttemptCount_ = 0;
        useResilientBridgeMode_ = false;
        resilientBridgeTried_ = false;
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

    currentChannelName_ = channelName;
    currentProgramId_ = programIdForChannel(channelName);
    currentShowTimer_->stop();
    ++currentShowLookupSerial_;
    probeCurrentShowBeforePlayback(channelName, currentProgramId_.toInt());

    QStringList args;
    args << "-I" << "ZAP"
         << "-c" << channelsFilePath_
         << "-a" << QString::number(adapterSpin_->value())
         << "-f" << QString::number(frontendSpin_->value())
         << "-o" << "-"
         << "-p";

    args << channelName;
    appendLog(QString("Tuning channel: %1 (program=%2)")
                  .arg(channelName, currentProgramId_.isEmpty() ? "unknown" : currentProgramId_));
    if (!startPlaybackPipeline(zapExe, args)) {
        return false;
    }

    stopWatchButton_->setEnabled(true);
    playbackStatusLabel_->setText(playbackStatusText());
    statusBar()->showMessage("Starting live playback...");
    return true;
}

void MainWindow::stopWatching()
{
    exitFullscreen();
    userStoppedWatching_ = true;
    reconnectTimer_->stop();
    currentShowTimer_->stop();
    ++currentShowLookupSerial_;
    reconnectAttemptCount_ = 0;
    useResilientBridgeMode_ = false;
    resilientBridgeTried_ = false;
    currentChannelName_.clear();
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
    setCurrentShowStatus("NO EIT DATA");
    if (scanProcess_->state() == QProcess::NotRunning) {
        statusBar()->showMessage("Ready");
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
        }
    }
}

bool MainWindow::startPlaybackPipeline(const QString &zapExe, const QStringList &zapArgs)
{
    const QString ffmpegExe = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpegExe.isEmpty()) {
        appendLog("player: ffmpeg not found in PATH for live DVB bridge.");
        scheduleReconnect("Missing ffmpeg for live stream");
        return false;
    }

    if (streamBridgeProcess_ != nullptr && streamBridgeProcess_->state() != QProcess::NotRunning) {
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1000);
        suppressBridgeExitReconnect_ = false;
    }

    QStringList ffmpegArgs;
    if (useResilientBridgeMode_) {
        ffmpegArgs << "-hide_banner"
                   << "-nostdin"
                   << "-loglevel" << "warning"
                   << "-fflags" << "+genpts+discardcorrupt"
                   << "-err_detect" << "ignore_err"
                   << "-analyzeduration" << "4M"
                   << "-probesize" << "4M"
                   << "-f" << "mpegts"
                   << "-i" << "pipe:0"
                   << "-map" << "0:v:0?"
                   << "-map" << "0:a:0?"
                   << "-sn"
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
                   << "-i" << "pipe:0"
                   << "-map" << "0:v:0?"
                   << "-map" << "0:a:0?"
                   << "-sn"
                   << "-dn"
                   << "-c" << "copy"
                   << "-mpegts_flags" << "+resend_headers+pat_pmt_at_frames"
                   << "-flush_packets" << "1"
                   << "-f" << "mpegts"
                   << QString("udp://127.0.0.1:%1?pkt_size=1316").arg(23000 + adapterSpin_->value());
    }

    appendLog("ffmpeg bridge: launch " + formatCommandLine(ffmpegExe, ffmpegArgs));
    zapProcess_->setStandardOutputProcess(streamBridgeProcess_);
    streamBridgeProcess_->setProgram(ffmpegExe);
    streamBridgeProcess_->setArguments(ffmpegArgs);
    streamBridgeProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    streamBridgeProcess_->start();
    if (!streamBridgeProcess_->waitForStarted(2000)) {
        appendLog(QString("player: Failed to start ffmpeg bridge (%1)")
                      .arg(streamBridgeProcess_->errorString()));
        scheduleReconnect("Could not start ffmpeg bridge");
        return false;
    }

    appendLog("zap: launch " + formatCommandLine(zapExe, zapArgs));
    zapProcess_->start(zapExe, zapArgs);
    if (!zapProcess_->waitForStarted(2000)) {
        appendLog(QString("Failed to start dvbv5-zap for %1 (%2)")
                      .arg(currentChannelName_, zapProcess_->errorString()));
        suppressBridgeExitReconnect_ = true;
        stopProcess(streamBridgeProcess_, 1000);
        suppressBridgeExitReconnect_ = false;
        scheduleReconnect("Failed to start tuner process");
        return false;
    }

    const int udpPort = 23000 + adapterSpin_->value();
    const QUrl liveUrl(QString("udp://127.0.0.1:%1").arg(udpPort));
    appendLog(QString("player: Starting playback from zap stdout via %1 (mode=%2, program=%3)")
                  .arg(liveUrl.toString(),
                       useResilientBridgeMode_ ? "resilient" : "normal",
                       currentProgramId_.isEmpty() ? "unknown" : currentProgramId_));
    mediaPlayer_->setAudioOutput(audioOutput_);
    currentShowTimer_->stop();
    if (applyCurrentShowStatusFromGuideCache()) {
        // Cached guide data already resolved the current show.
    } else if (noAutoCurrentShowLookupChannels_.contains(currentChannelName_)) {
        setCurrentShowStatus("NO EIT DATA", "EIT was checked before playback and no data was found for this channel.");
    }
    QTimer::singleShot(3000, this, [this, liveUrl]() {
        if (streamBridgeProcess_ == nullptr || streamBridgeProcess_->state() != QProcess::Running) {
            appendLog("player: ffmpeg bridge exited before media attach.");
            return;
        }
        appendLog("player: Attaching UDP live stream to media player.");
        mediaPlayer_->setSource(liveUrl);
        mediaPlayer_->play();
    });
    return true;
}

void MainWindow::addSelectedFavorite()
{
    const QString channelName = selectedChannelNameFromTable();
    if (channelName.isEmpty()) {
        QMessageBox::information(this, "Select channel", "Select a channel to add to favorites.");
        return;
    }

    if (favorites_.contains(channelName)) {
        return;
    }

    favorites_.append(channelName);
    saveFavorites();
    refreshQuickButtons();
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
    if (exitStatus != QProcess::NormalExit) {
        appendLog(QString("zap: tuner process crashed (code=%1, error=%2)")
                      .arg(exitCode)
                      .arg(zapProcess_->errorString()));
    }
    if (!suppressZapExitReconnect_ && exitCode != 0 && !userStoppedWatching_ && !currentChannelName_.isEmpty()) {
        scheduleReconnect(QString("Tuner process exited (%1)").arg(exitCode));
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

void MainWindow::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    appendLog(QString("player: mediaStatusChanged=%1").arg(static_cast<int>(status)));
    playbackStatusLabel_->setText(playbackStatusText());
    if (!currentChannelName_.isEmpty()) {
        const bool isLocalFile = currentChannelName_.startsWith("File: ");
        if (status == QMediaPlayer::InvalidMedia) {
            statusBar()->showMessage(isLocalFile ? "Local file error" : "Playback error");
        } else if (mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) {
            statusBar()->showMessage(isLocalFile ? "Playing local file" : "Watching");
        } else {
            statusBar()->showMessage(isLocalFile ? "Opening local file..." : "Starting live playback...");
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
        if (tryDynamicBridgeFallback("Player error")) {
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
    if (resilientBridgeTried_ || useResilientBridgeMode_) {
        return false;
    }

    resilientBridgeTried_ = true;
    useResilientBridgeMode_ = true;
    reconnectTimer_->stop();
    reconnectAttemptCount_ = 0;
    appendLog(QString("player: %1; retrying with resilient bridge mode").arg(reason));
    startWatchingChannel(currentChannelName_, true);
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
        statusBar()->showMessage("Reconnect failed");
        return;
    }

    ++reconnectAttemptCount_;
    const int delayMs = 800 + (reconnectAttemptCount_ * 900);
    appendLog(QString("Reconnect attempt %1/%2 in %3 ms (%4)")
                  .arg(reconnectAttemptCount_)
                  .arg(maxReconnectAttempts_)
                  .arg(delayMs)
                  .arg(reason));
    statusBar()->showMessage("Reconnecting...");
    reconnectTimer_->start(delayMs);
}

void MainWindow::triggerReconnect()
{
    if (currentChannelName_.isEmpty() || userStoppedWatching_) {
        return;
    }
    startWatchingChannel(currentChannelName_, true);
}

void MainWindow::handleMuteToggled(bool checked)
{
    audioOutput_->setMuted(checked);
    muteButton_->setText(checked ? "Unmute" : "Mute");
}

void MainWindow::handleVolumeChanged(int value)
{
    audioOutput_->setVolume(static_cast<float>(value) / 100.0f);
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue("volume_percent", value);
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

void MainWindow::enterFullscreen()
{
    if (fullscreenActive_ || tabs_ == nullptr || contentSplitter_ == nullptr) {
        return;
    }

    QScreen *targetScreen = screenForWidget(this);
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen == nullptr) {
        return;
    }

    wasMaximizedBeforeFullscreen_ = isMaximized();
    menuBarWasVisibleBeforeFullscreen_ = menuBar()->isVisible();
    statusBarWasVisibleBeforeFullscreen_ = statusBar()->isVisible();
    tabBarWasVisibleBeforeFullscreen_ = tabs_->tabBar()->isVisible();
    previousTabIndex_ = tabs_->currentIndex();
    splitterHandleWidthBeforeFullscreen_ = contentSplitter_->handleWidth();
    splitterSizesBeforeFullscreen_ = contentSplitter_->sizes();
    windowGeometryBeforeFullscreen_ = saveGeometry();

    tabs_->setCurrentWidget(watchPage_);
    watchControlsContainer_->hide();
    favoritesContainer_->hide();
    channelsTable_->hide();
    tabs_->tabBar()->hide();
    menuBar()->hide();
    statusBar()->hide();
    contentSplitter_->setHandleWidth(0);
    contentSplitter_->setSizes({1, 0});
    videoWidget_->setAspectRatioMode(Qt::IgnoreAspectRatio);

    if (windowHandle() != nullptr) {
        windowHandle()->setScreen(targetScreen);
    }
    showFullScreen();
    raise();
    activateWindow();
    videoWidget_->setFocus(Qt::ActiveWindowFocusReason);
    fullscreenActive_ = true;
    handleFullscreenChanged(true);
}

void MainWindow::exitFullscreen()
{
    if (!fullscreenActive_) {
        return;
    }

    fullscreenActive_ = false;

    showNormal();
    if (!windowGeometryBeforeFullscreen_.isEmpty()) {
        restoreGeometry(windowGeometryBeforeFullscreen_);
    }
    if (wasMaximizedBeforeFullscreen_) {
        showMaximized();
    }

    if (watchControlsContainer_ != nullptr) {
        watchControlsContainer_->show();
    }
    if (favoritesContainer_ != nullptr) {
        favoritesContainer_->show();
    }
    if (channelsTable_ != nullptr) {
        channelsTable_->show();
    }
    if (tabs_ != nullptr) {
        if (tabBarWasVisibleBeforeFullscreen_) {
            tabs_->tabBar()->show();
        }
        tabs_->setCurrentIndex(previousTabIndex_);
    }
    if (menuBarWasVisibleBeforeFullscreen_) {
        menuBar()->show();
    }
    if (statusBarWasVisibleBeforeFullscreen_) {
        statusBar()->show();
    }
    if (!splitterSizesBeforeFullscreen_.isEmpty()) {
        contentSplitter_->setSizes(splitterSizesBeforeFullscreen_);
    }
    if (splitterHandleWidthBeforeFullscreen_ >= 0) {
        contentSplitter_->setHandleWidth(splitterHandleWidthBeforeFullscreen_);
    }

    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    videoWidget_->updateGeometry();
    videoWidget_->update();
    handleFullscreenChanged(false);
}

void MainWindow::refreshQuickButtons()
{
    for (int i = 0; i < 8; ++i) {
        auto *button = quickFavoriteButtons_[i];
        if (button == nullptr) {
            continue;
        }
        if (i < favorites_.size()) {
            const QString channel = favorites_.at(i);
            QString label = channel;
            if (label.size() > 18) {
                label = label.left(15) + "...";
            }
            button->setText(QString("%1 %2").arg(i + 1).arg(label));
            button->setToolTip(channel);
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
    QSettings settings("tv_tuner_gui", "watcher");
    settings.setValue("favorites", favorites_);
}

void MainWindow::loadFavorites()
{
    QSettings settings("tv_tuner_gui", "watcher");
    favorites_ = settings.value("favorites").toStringList();
    favorites_.removeDuplicates();
}

void MainWindow::loadXspfChannelHints()
{
    xspfNumberByTuneKey_.clear();
    xspfProgramByChannel_.clear();

    const QString xspfPath = QDir::home().filePath("Desktop/tv.xspf");
    QFile file(xspfPath);
    if (!file.exists()) {
        appendLog("No XSPF playlist found on Desktop; using channels.conf metadata.");
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
            if (!channelNumber.isEmpty() && !currentFrequency.isEmpty()) {
                xspfNumberByTuneKey_.insert(tuneKey(currentFrequency, currentProgram), channelNumber);
            }
            xspfProgramByChannel_.insert(channelName, currentProgram);
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
        xspfNumberByTuneKey_.clear();
        xspfProgramByChannel_.clear();
        return;
    }

    appendLog(QString("Loaded %1 XSPF program mappings from %2")
                  .arg(xspfProgramByChannel_.size())
                  .arg(xspfPath));
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
