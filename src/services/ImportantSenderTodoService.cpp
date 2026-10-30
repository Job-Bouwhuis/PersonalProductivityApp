#include "ImportantSenderTodoService.h"

#include "ApplicationSettings.h"
#include "TodoService.h"
#include "WorkDescriptorService.h"

#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QTime>
#include <QTimeZone>

namespace productivity::services {
namespace {

bool isImportantSender(const QString& address, const QStringList& rules)
{
    if (address.trimmed().isEmpty())
        return false;
    for (const QString& rule : rules) {
        if (address.compare(rule.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QDateTime deadlineForReceivedTime(const QDateTime& receivedAt)
{
    const QDateTime localReceived = receivedAt.toLocalTime();
    QDate deadlineDate = localReceived.date();
    QTime deadlineTime(17, 0);
    if (localReceived.time() >= QTime(21, 0)) {
        deadlineDate = deadlineDate.addDays(1);
    } else if (localReceived.time() >= QTime(16, 0)) {
        deadlineTime = QTime(22, 0);
    }
    const QDateTime localDeadline(deadlineDate, deadlineTime, QTimeZone::systemTimeZone());
    return localDeadline.toUTC();
}

} // namespace

ImportantSenderTodoService::ImportantSenderTodoService(ApplicationSettings& settings,
                                                       TodoService& todoService,
                                                       WorkDescriptorService& workService)
    : settings_(settings)
    , todoService_(todoService)
    , workService_(workService)
{
}

void ImportantSenderTodoService::createForNewMessages(const QJsonArray& messages)
{
    const QStringList importantSenders = settings_.importantSenderRules();
    if (importantSenders.isEmpty() || messages.isEmpty())
        return;

    QJsonArray eligibleMessages;
    for (const QJsonValue& value : messages) {
        const QJsonObject message = value.toObject();
        const QString address = message.value(QStringLiteral("from")).toObject()
            .value(QStringLiteral("emailAddress")).toObject()
            .value(QStringLiteral("address")).toString().trimmed();
        if (isImportantSender(address, importantSenders))
            eligibleMessages.append(message);
    }
    if (eligibleMessages.isEmpty())
        return;

    const QString parentTaskId = workService_.startTask(QStringLiteral("Create TODOs from important senders"));
    QSet<QString> processedMessageIds;
    int createdCount = 0;
    for (int index = 0; index < eligibleMessages.size(); ++index) {
        const QJsonObject message = eligibleMessages.at(index).toObject();
        const QString messageId = message.value(QStringLiteral("id")).toString();
        const QString childTaskId = workService_.startTask(QStringLiteral("Create email follow-up TODO"), parentTaskId);
        if (messageId.isEmpty() || processedMessageIds.contains(messageId)) {
            workService_.failTask(childTaskId, QStringLiteral("Message has no unique stable ID"));
            continue;
        }
        processedMessageIds.insert(messageId);

        const QString receivedText = message.value(QStringLiteral("receivedDateTime")).toString();
        QDateTime receivedAt = QDateTime::fromString(receivedText, Qt::ISODateWithMs);
        if (!receivedAt.isValid())
            receivedAt = QDateTime::fromString(receivedText, Qt::ISODate);
        if (!receivedAt.isValid()) {
            workService_.failTask(childTaskId, QStringLiteral("Message received time is unavailable"));
            continue;
        }
        const QDateTime deadline = deadlineForReceivedTime(receivedAt);
        if (!deadline.isValid()) {
            workService_.failTask(childTaskId, QStringLiteral("Could not calculate the follow-up deadline"));
            continue;
        }

        const QJsonObject emailAddress = message.value(QStringLiteral("from")).toObject()
            .value(QStringLiteral("emailAddress")).toObject();
        const QString senderName = emailAddress.value(QStringLiteral("name")).toString().trimmed();
        const QString senderAddress = emailAddress.value(QStringLiteral("address")).toString().trimmed();
        const QString subject = message.value(QStringLiteral("subject")).toString().trimmed();
        TodoRecord todo;
        todo.title = subject.isEmpty()
            ? QStringLiteral("Follow up with %1").arg(senderName.isEmpty() ? senderAddress : senderName)
            : QStringLiteral("Follow up: %1").arg(subject);
        todo.description = QStringLiteral("Email from %1").arg(senderAddress);
        todo.deadline = deadline;
        todo.linkType = QStringLiteral("email");
        todo.linkTarget = messageId;
        todo.linkLabel = senderName.isEmpty() ? senderAddress : senderName;
        if (!todoService_.saveTodo(todo).isEmpty()) {
            ++createdCount;
            workService_.completeTask(childTaskId, QStringLiteral("Email follow-up TODO created"));
        } else {
            workService_.failTask(childTaskId, QStringLiteral("Could not save email follow-up TODO"));
        }
        workService_.updateTask(parentTaskId,
            QStringLiteral("Created %1 of %2 follow-up TODO(s)").arg(createdCount).arg(index + 1),
            ((index + 1) * 100) / eligibleMessages.size());
    }
    workService_.completeTask(parentTaskId,
        QStringLiteral("Created %1 email follow-up TODO(s)").arg(createdCount));
}

} // namespace productivity::services
