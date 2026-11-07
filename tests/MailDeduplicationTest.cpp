#include "MailIndexService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QtTest>

using namespace productivity::services;

class MailDeduplicationTest final : public QObject
{
    Q_OBJECT

private slots:
    void repeatedMessageUpdatesOneIndexedEntry()
    {
        const QString connectionName = QStringLiteral("mail-deduplication-test");
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(database.open());

            MailIndexService index(database);
            QVERIFY(index.initialize());

            const QJsonObject firstMessage{
                {QStringLiteral("id"), QStringLiteral("immutable-message-id")},
                {QStringLiteral("subject"), QStringLiteral("Original subject")},
                {QStringLiteral("isRead"), false}
            };
            QString error;
            QJsonArray newlyInserted;
            QCOMPARE(index.indexMessages(QJsonArray{firstMessage}, error, &newlyInserted), 1);
            QVERIFY(error.isEmpty());
            QCOMPARE(newlyInserted.size(), 1);

            const QJsonObject updatedMessage{
                {QStringLiteral("id"), QStringLiteral("immutable-message-id")},
                {QStringLiteral("subject"), QStringLiteral("Updated subject")},
                {QStringLiteral("isRead"), true}
            };
            QCOMPARE(index.indexMessages(QJsonArray{updatedMessage}, error, &newlyInserted), 1);
            QVERIFY(error.isEmpty());
            QVERIFY(newlyInserted.isEmpty());
            QCOMPARE(index.indexedMessageCount(), 1);

            const auto messages = index.allMessages();
            QCOMPARE(messages.size(), 1);
            QCOMPARE(messages.first().subject, QStringLiteral("Updated subject"));
        }
        QSqlDatabase::removeDatabase(connectionName);
    }
};

QTEST_GUILESS_MAIN(MailDeduplicationTest)
#include "MailDeduplicationTest.moc"
