#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLoggingCategory>
#include <QPalette>

#include <fcntl.h>
#include <unistd.h>

#include "DisplayTheme.h"
#include "MainWindow.h"

namespace {
bool verboseQtLoggingEnabled()
{
    const QByteArray value = qgetenv("TV_TUNER_GUI_VERBOSE_QT_LOGS").trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
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

bool appendDirectLineToLog(const QString &logPath, const QString &line)
{
    const QByteArray encodedPath = QFile::encodeName(logPath);
    const int fd = ::open(encodedPath.constData(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0664);
    if (fd < 0) {
        return false;
    }

    QByteArray payload = line.toUtf8();
    payload.append('\n');

    qsizetype totalWritten = 0;
    while (totalWritten < payload.size()) {
        const ssize_t written =
            ::write(fd, payload.constData() + totalWritten, static_cast<size_t>(payload.size() - totalWritten));
        if (written <= 0) {
            ::close(fd);
            return false;
        }
        totalWritten += written;
    }

    ::close(fd);
    return true;
}

void appendQtMessageToLog(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const QString logPath = resolveProjectLogPath();
    static thread_local bool inHandler = false;
    static bool reportedLogOpenFailure = false;
    if (inHandler) {
        return;
    }
    inHandler = true;

    const char *level = "DEBUG";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "CRIT";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString category = context.category != nullptr ? QString::fromUtf8(context.category) : QString("qt");
    const QString line = QString("[%1] [QT:%2] [%3] %4")
                             .arg(timestamp, level, category, message);

    if (!appendDirectLineToLog(logPath, line) && !reportedLogOpenFailure) {
        reportedLogOpenFailure = true;
        fprintf(stderr, "tv_tuner_gui: failed to open log file for append: %s\n",
                logPath.toLocal8Bit().constData());
    }
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());

    inHandler = false;
    if (type == QtFatalMsg) {
        abort();
    }
}
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
    if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) {
        qputenv("QT_XCB_GL_INTEGRATION", "none");
    }
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }
    if (qEnvironmentVariableIsEmpty("QT_FFMPEG_DECODING_HW_DEVICE_TYPES")) {
        // Force software video decode path to avoid black video on problematic GPU/video-sink stacks.
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "none");
    }

    const QString startupLogPath = resolveProjectLogPath();
    const bool verboseQtLogs = verboseQtLoggingEnabled();
    const QString startupLine = QString("[%1] [APP:INFO] [default] logger initialized path=%2 cwd=%3 verboseQtLogs=%4")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                         startupLogPath,
                                         QDir::currentPath(),
                                         verboseQtLogs ? "true" : "false");
    if (!appendDirectLineToLog(startupLogPath, startupLine)) {
        fprintf(stderr, "tv_tuner_gui: failed to initialize log file: %s\n",
                startupLogPath.toLocal8Bit().constData());
    }
    fprintf(stderr, "%s\n", startupLine.toLocal8Bit().constData());

    qInstallMessageHandler(appendQtMessageToLog);
    if (verboseQtLogs) {
        QLoggingCategory::setFilterRules(QStringLiteral(
            "qt.multimedia.*=true\n"
            "qt.ffmpeg.*=true\n"
            "qt.qpa.*=true\n"));
    } else {
        QLoggingCategory::setFilterRules(QStringLiteral(
            "qt.qpa.*=false\n"
            "qt.ffmpeg.*=false\n"
            "qt.multimedia.*=false\n"
            "qt.multimedia.*.warning=true\n"
            "qt.multimedia.*.critical=true\n"));
    }

    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QCoreApplication::setApplicationName(QStringLiteral("tv_tuner_gui"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Voncloft TV Tuner"));
    QGuiApplication::setDesktopFileName(QStringLiteral("tv_tuner_gui"));
    QApplication app(argc, argv);
    const QIcon appIcon(":/assets/tv-icon.svg");
    app.setWindowIcon(appIcon);
    QString displayThemeError;
    DisplayThemeStore displayThemeStore;
    if (!loadDisplayThemeStore(&displayThemeStore, &displayThemeError)) {
        displayThemeStore = defaultDisplayThemeStore();
    }
    const DisplayTheme currentDisplayTheme = normalizedDisplayTheme(displayThemeStore.currentTheme);
    app.setFont(
        qFontFromDisplayFontStyle(displayThemeFontStyle(currentDisplayTheme, DisplayThemeKeys::AppFont), app.font()));
    app.setPalette(buildApplicationPalette(currentDisplayTheme, app.palette()));
    app.setStyleSheet(buildScrollBarStyleSheet(currentDisplayTheme) + buildSliderStyleSheet(currentDisplayTheme));
    if (!displayThemeError.trimmed().isEmpty()) {
        qWarning().noquote() << "display-theme:" << displayThemeError;
    }
    qInfo() << "Startup env:"
            << "QT_QPA_PLATFORM=" << qEnvironmentVariable("QT_QPA_PLATFORM")
            << "QT_XCB_GL_INTEGRATION=" << qEnvironmentVariable("QT_XCB_GL_INTEGRATION")
            << "QT_MEDIA_BACKEND=" << qEnvironmentVariable("QT_MEDIA_BACKEND")
            << "QT_FFMPEG_DECODING_HW_DEVICE_TYPES=" << qEnvironmentVariable("QT_FFMPEG_DECODING_HW_DEVICE_TYPES");
    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();
    return app.exec();
}
