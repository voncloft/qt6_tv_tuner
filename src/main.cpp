#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLoggingCategory>
#include <QTextStream>

#include "MainWindow.h"

namespace {
QString resolveProjectLogPath()
{
    QDir dir(QDir::currentPath());
    if (dir.dirName() == "build") {
        dir.cdUp();
    }
    return dir.filePath("tv_tuner_gui.log");
}

void appendQtMessageToLog(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const QString logPath = resolveProjectLogPath();
    static thread_local bool inHandler = false;
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

    QFile file(logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QTextStream ts(&file);
        ts << line << '\n';
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

    qInstallMessageHandler(appendQtMessageToLog);
    QLoggingCategory::setFilterRules(QStringLiteral(
        "qt.multimedia.*=true\n"
        "qt.ffmpeg.*=true\n"
        "qt.qpa.*=true\n"));

    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    const QIcon appIcon(":/assets/tv-icon.svg");
    app.setWindowIcon(appIcon);
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
