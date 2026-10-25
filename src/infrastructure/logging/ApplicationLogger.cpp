#include "ApplicationLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStringView>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>
#include <memory>

Q_LOGGING_CATEGORY(loggerLog, "application.startup")

namespace {

QMutex logMutex;
std::unique_ptr<QFile> logFile;

QString severityName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

void writeLogMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QJsonObject record{
        {QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("severity"), severityName(type)},
        {QStringLiteral("subsystem"), QString::fromUtf8(context.category ? context.category : "application")},
        {QStringLiteral("message"), message},
    };
    const QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';

    {
        QMutexLocker lock(&logMutex);
        if (logFile && logFile->isOpen()) {
            logFile->write(line);
            logFile->flush();
        } else {
            std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
            std::fflush(stderr);
        }
    }

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

namespace productivity::infrastructure {

void initializeApplicationLogging()
{
    const QString appDataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString logDirectory = appDataDirectory + QStringLiteral("/logs");
    if (!QDir().mkpath(logDirectory)) {
        std::fprintf(stderr, "Could not create application log directory: %s\n", qPrintable(logDirectory));
        return;
    }

    logFile = std::make_unique<QFile>(logDirectory + QStringLiteral("/application.jsonl"));
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "Could not open application log file: %s\n", qPrintable(logFile->fileName()));
        logFile.reset();
        return;
    }

    qInstallMessageHandler(writeLogMessage);
    qCInfo(loggerLog) << "Structured application logging initialized";
}

} // namespace productivity::infrastructure
