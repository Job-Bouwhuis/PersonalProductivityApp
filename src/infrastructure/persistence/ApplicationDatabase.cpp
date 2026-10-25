#include "ApplicationDatabase.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

Q_LOGGING_CATEGORY(databaseLog, "application.database")

namespace productivity::infrastructure {

ApplicationDatabase::~ApplicationDatabase()
{
    if (!connectionName_.isEmpty()) {
        close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

void ApplicationDatabase::close()
{
    if (database_.isOpen())
        database_.close();
}

bool ApplicationDatabase::open()
{
    const QString appDataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (appDataDirectory.isEmpty() || !QDir().mkpath(appDataDirectory)) {
        qCCritical(databaseLog) << "Could not create application data directory";
        return false;
    }

    connectionName_ = QStringLiteral("personal-productivity-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(appDataDirectory + QStringLiteral("/productivity.sqlite3"));
    if (!database_.open()) {
        qCCritical(databaseLog) << "Could not open local SQLite database:" << database_.lastError().text();
        return false;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON")) ||
        !query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))) {
        qCCritical(databaseLog) << "Could not initialize SQLite schema:" << query.lastError().text();
        return false;
    }

    qCInfo(databaseLog) << "Local SQLite database ready at" << database_.databaseName();
    return true;
}

QSqlDatabase ApplicationDatabase::connection() const
{
    return database_;
}

} // namespace productivity::infrastructure
