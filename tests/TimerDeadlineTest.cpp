#include "TimerService.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QtTest>

using namespace productivity::services;

class TimerDeadlineTest final : public QObject
{
    Q_OBJECT

private slots:
    void stableTodoDeadlineIsScheduledAndUpdated()
    {
        const QString connectionName = QStringLiteral("timer-deadline-test");
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(database.open());

            TimerService timers(database);
            QVERIFY(timers.initialize());

            const QDateTime firstDeadline = QDateTime::currentDateTimeUtc().addSecs(3600);
            const QString eventId = timers.schedule(QStringLiteral("Prepare report"), firstDeadline,
                                                    NotificationPolicy::Deadline,
                                                    QStringLiteral("todo:example-id"));
            QCOMPARE(eventId, QStringLiteral("todo:example-id"));

            auto events = timers.activeEvents();
            QCOMPARE(events.size(), 1);
            QCOMPARE(events.first().deadline.toMSecsSinceEpoch(), firstDeadline.toMSecsSinceEpoch());
            QCOMPARE(events.first().notificationPolicy, NotificationPolicy::Deadline);

            const QDateTime revisedDeadline = firstDeadline.addSecs(1800);
            QCOMPARE(timers.schedule(QStringLiteral("Prepare report"), revisedDeadline,
                                     NotificationPolicy::Deadline,
                                     QStringLiteral("todo:example-id")), eventId);

            events = timers.activeEvents();
            QCOMPARE(events.size(), 1);
            QCOMPARE(events.first().deadline.toMSecsSinceEpoch(), revisedDeadline.toMSecsSinceEpoch());
        }
        QSqlDatabase::removeDatabase(connectionName);
    }
};

QTEST_GUILESS_MAIN(TimerDeadlineTest)
#include "TimerDeadlineTest.moc"
