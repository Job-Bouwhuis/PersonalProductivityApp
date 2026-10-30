#include "MicrosoftGraphClient.h"

#include "ApplicationSettings.h"
#include "OutlookAuthService.h"
#include "WorkDescriptorService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(graphLog, "application.outlook.graph")

namespace productivity::services {

namespace {

QString operationTitle(const QByteArray& method, const QString& path)
{
    if (path.contains(QStringLiteral("translateExchangeIds")))
        return QStringLiteral("Translate saved mail identifiers");
    if (path.contains(QStringLiteral("mailFolders")))
        return QStringLiteral("Load mail folders");
    if (path.endsWith(QStringLiteral("/sendMail")))
        return QStringLiteral("Send email");
    if (method == QByteArrayLiteral("PATCH"))
        return QStringLiteral("Update mail");
    if (path.contains(QStringLiteral("/messages")))
        return QStringLiteral("Retrieve mail messages");
    return QStringLiteral("Contact Microsoft Graph");
}

} // namespace

MicrosoftGraphClient::MicrosoftGraphClient(ApplicationSettings& settings,
                                           OutlookAuthService& authService,
                                           WorkDescriptorService& workService,
                                           QObject* parent)
    : QObject(parent)
    , settings_(settings)
    , authService_(authService)
    , network_(new QNetworkAccessManager(this))
    , workService_(workService)
{
    connect(this, &MicrosoftGraphClient::requestFinished, this,
            [this](quint64 requestId, int httpStatus, const QByteArray&) {
        const QString taskId = workTaskIds_.take(requestId);
        if (taskId.isEmpty())
            return;
        if (httpStatus >= 200 && httpStatus < 300)
            workService_.completeTask(taskId, QStringLiteral("Microsoft Graph request complete"));
        else
            workService_.failTask(taskId, QStringLiteral("Microsoft Graph returned HTTP %1").arg(httpStatus));
    });
    connect(this, &MicrosoftGraphClient::requestFailed, this,
            [this](quint64 requestId, const QString& error) {
        const QString taskId = workTaskIds_.take(requestId);
        if (!taskId.isEmpty())
            workService_.failTask(taskId, error);
    });
    connect(this, &MicrosoftGraphClient::requestRetrying, this,
            [this](quint64 requestId, int attempt, int, const QString& reason) {
        const QString taskId = workTaskIds_.value(requestId);
        if (!taskId.isEmpty())
            workService_.updateTask(taskId, QStringLiteral("%1 (retry %2)").arg(reason).arg(attempt));
    });
}

quint64 MicrosoftGraphClient::request(const QByteArray& method, const QString& relativePath,
                                      const QByteArray& jsonBody, const QString& parentWorkId)
{
    const quint64 requestId = nextRequestId_++;
    trackRequest(requestId, operationTitle(method, relativePath), parentWorkId);
    static const QList<QByteArray> allowedMethods{
        QByteArrayLiteral("GET"), QByteArrayLiteral("POST"),
        QByteArrayLiteral("PATCH"), QByteArrayLiteral("DELETE")
    };
    if (!allowedMethods.contains(method) || !relativePath.startsWith(QLatin1Char('/')) ||
        relativePath.startsWith(QStringLiteral("//"))) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit requestFailed(requestId, QStringLiteral("Invalid Microsoft Graph request."));
        }, Qt::QueuedConnection);
        return requestId;
    }

    const QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0") + relativePath);
    if (!url.isValid() || url.scheme() != QLatin1String("https") ||
        url.host() != QLatin1String("graph.microsoft.com")) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit requestFailed(requestId, QStringLiteral("Invalid Microsoft Graph address."));
        }, Qt::QueuedConnection);
        return requestId;
    }

    sendRequest(requestId, method, url, jsonBody);
    return requestId;
}

void MicrosoftGraphClient::trackRequest(quint64 requestId, const QString& title,
                                       const QString& parentWorkId)
{
    const QString taskId = workService_.startTask(title, parentWorkId);
    workService_.updateTask(taskId, QStringLiteral("Waiting for Microsoft Graph…"));
    workTaskIds_.insert(requestId, taskId);
}

void MicrosoftGraphClient::sendRequest(quint64 requestId, const QByteArray& method, const QUrl& url,
                                       const QByteArray& jsonBody, int retryAttempt,
                                       bool forceRefreshToken, bool refreshAlreadyAttempted)
{
    QPointer<MicrosoftGraphClient> self(this);
    authService_.withValidAccessToken(settings_.outlookTenant(), settings_.outlookClientId(),
        [self, requestId, method, url, jsonBody, retryAttempt, forceRefreshToken, refreshAlreadyAttempted]
        (const QString& token, const QString& error) {
            if (!self)
                return;
            if (token.isEmpty()) {
                const QString failure = error.isEmpty()
                    ? QStringLiteral("No valid Outlook access token is available.") : error;
                QMetaObject::invokeMethod(self, [self, requestId, failure] {
                    if (self)
                        emit self->requestFailed(requestId, failure);
                }, Qt::QueuedConnection);
                return;
            }
            QNetworkRequest networkRequest(url);
            networkRequest.setRawHeader(QByteArrayLiteral("Authorization"),
                                        QByteArrayLiteral("Bearer ") + token.toUtf8());
            networkRequest.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
            if (url.path().contains(QStringLiteral("/messages")))
                networkRequest.setRawHeader(QByteArrayLiteral("Prefer"), QByteArrayLiteral("IdType=\"ImmutableId\""));
            networkRequest.setTransferTimeout(30000);
            if (!jsonBody.isEmpty())
                networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            QNetworkReply* reply = self->network_->sendCustomRequest(networkRequest, method, jsonBody);
            connect(reply, &QNetworkReply::finished, self,
                    [self, reply, requestId, method, url, jsonBody, retryAttempt, forceRefreshToken,
                     refreshAlreadyAttempted] {
                if (!self) {
                    reply->deleteLater();
                    return;
                }
                const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool transportFailure = reply->error() != QNetworkReply::NoError && httpStatus == 0;
                const QByteArray responseBody = reply->readAll();
                const QByteArray retryAfter = reply->rawHeader(QByteArrayLiteral("Retry-After"));
                reply->deleteLater();
                if (transportFailure) {
                    if (retryAttempt < 4) {
                        const int delayMs = qMin(30000, 1000 * (1 << retryAttempt));
                        qCWarning(graphLog) << "Microsoft Graph transport failure; retrying request" << requestId
                                            << "after milliseconds:" << delayMs;
                        emit self->requestRetrying(requestId, retryAttempt + 1, delayMs,
                                                   QStringLiteral("Connection interrupted"));
                        QTimer::singleShot(delayMs, self, [self, requestId, method, url, jsonBody, retryAttempt,
                                                          refreshAlreadyAttempted, forceRefreshToken] {
                            if (self)
                                self->sendRequest(requestId, method, url, jsonBody, retryAttempt + 1,
                                                  false, refreshAlreadyAttempted || forceRefreshToken);
                        });
                    } else {
                        qCWarning(graphLog) << "Microsoft Graph request transport failure after retries; request ID:" << requestId;
                        emit self->requestFailed(requestId, QStringLiteral("Could not reach Microsoft Graph after several attempts. Check your connection and retry."));
                    }
                    return;
                }
                if (httpStatus == 401 && !refreshAlreadyAttempted) {
                    qCInfo(graphLog) << "Microsoft Graph rejected the access token; refreshing sign-in for request" << requestId;
                    self->sendRequest(requestId, method, url, jsonBody, retryAttempt, true, true);
                    return;
                }
                if ((httpStatus == 429 || httpStatus == 502 || httpStatus == 503 || httpStatus == 504) &&
                    retryAttempt < 4) {
                    bool retryAfterIsSeconds = false;
                    const int retryAfterSeconds = retryAfter.toInt(&retryAfterIsSeconds);
                    int delayMs = retryAfterIsSeconds
                        ? qBound(1, retryAfterSeconds, 120) * 1000
                        : qMin(30000, 1000 * (1 << retryAttempt));
                    if (!retryAfterIsSeconds) {
                        const QDateTime retryAt = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
                        if (retryAt.isValid()) {
                            const qint64 waitMs = QDateTime::currentDateTimeUtc().msecsTo(retryAt.toUTC());
                            delayMs = static_cast<int>(qBound<qint64>(qint64(1000), waitMs, qint64(120000)));
                        }
                    }
                    const QString reason = httpStatus == 429 ? QStringLiteral("Microsoft Graph is throttling requests")
                                                              : QStringLiteral("Microsoft Graph is temporarily unavailable");
                    qCWarning(graphLog) << reason << "; retrying request" << requestId << "after milliseconds:" << delayMs;
                    emit self->requestRetrying(requestId, retryAttempt + 1, delayMs, reason);
                    QTimer::singleShot(delayMs, self, [self, requestId, method, url, jsonBody, retryAttempt,
                                                      refreshAlreadyAttempted, forceRefreshToken] {
                        if (self)
                            self->sendRequest(requestId, method, url, jsonBody, retryAttempt + 1,
                                              false, refreshAlreadyAttempted || forceRefreshToken);
                    });
                    return;
                }
                qCInfo(graphLog) << "Microsoft Graph request completed; request ID:" << requestId
                                 << "HTTP status:" << httpStatus;
                if (httpStatus == 401) {
                    emit self->requestFailed(requestId, QStringLiteral("Outlook sign-in expired or was revoked. Reconnect Outlook, then retry."));
                    return;
                }
                emit self->requestFinished(requestId, httpStatus, responseBody);
            });
        }, forceRefreshToken);
}

quint64 MicrosoftGraphClient::listMessages(int pageSize, const QString& parentWorkId)
{
    const int boundedPageSize = qBound(1, pageSize, 100);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QString::number(boundedPageSize));
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,subject,from,receivedDateTime,isRead,parentFolderId"));
    return request(QByteArrayLiteral("GET"),
                   QStringLiteral("/me/messages?") + query.toString(QUrl::FullyEncoded), {}, parentWorkId);
}

quint64 MicrosoftGraphClient::getMessage(const QString& messageId, const QString& parentWorkId)
{
    if (messageId.trimmed().isEmpty()) {
        const quint64 requestId = nextRequestId_++;
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit requestFailed(requestId, QStringLiteral("A message ID is required."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(messageId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$select"),
        QStringLiteral("id,subject,from,toRecipients,ccRecipients,receivedDateTime,isRead,importance,parentFolderId,body,bodyPreview"));
    query.addQueryItem(QStringLiteral("$expand"), QStringLiteral("attachments"));
    return request(QByteArrayLiteral("GET"),
                   QStringLiteral("/me/messages/%1?%2").arg(encodedId, query.toString(QUrl::FullyEncoded)),
                   {}, parentWorkId);
}

quint64 MicrosoftGraphClient::listMailboxMessagesPage(const QString& nextLink, const QString& parentWorkId)
{
    const quint64 requestId = nextRequestId_++;
    trackRequest(requestId, QStringLiteral("Retrieve mail messages"), parentWorkId);
    const QUrl nextUrl(nextLink, QUrl::StrictMode);
    const QString path = nextUrl.path();
    const bool mailboxMessagesPath = path.startsWith(QStringLiteral("/v1.0/me/messages"));
    if (!nextUrl.isValid() || nextUrl.scheme() != QLatin1String("https") ||
        nextUrl.host() != QLatin1String("graph.microsoft.com") || !nextUrl.userInfo().isEmpty() ||
        (nextUrl.port(-1) != -1 && nextUrl.port(-1) != 443) || nextUrl.hasFragment() ||
        !mailboxMessagesPath) {
        QMetaObject::invokeMethod(this, [this, requestId, host = nextUrl.host(), path] {
            emit requestFailed(requestId, QStringLiteral("Microsoft Graph returned an invalid mailbox page address (host %1, path %2).")
                                   .arg(host, path));
        }, Qt::QueuedConnection);
        return requestId;
    }
    sendRequest(requestId, QByteArrayLiteral("GET"), nextUrl, {});
    return requestId;
}

quint64 MicrosoftGraphClient::listRootMailFolders(const QString& parentWorkId)
{
    return request(QByteArrayLiteral("GET"), QStringLiteral(
        "/me/mailFolders?includeHiddenFolders=true&$top=100&$select=id,displayName,childFolderCount"), {}, parentWorkId);
}

quint64 MicrosoftGraphClient::listChildMailFolders(const QString& folderId, const QString& parentWorkId)
{
    if (folderId.isEmpty()) {
        const quint64 requestId = nextRequestId_++;
        emit requestFailed(requestId, QStringLiteral("A mail folder ID is required."));
        return requestId;
    }
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(folderId));
    return request(QByteArrayLiteral("GET"), QStringLiteral(
        "/me/mailFolders/%1/childFolders?includeHiddenFolders=true&$top=100&$select=id,displayName,childFolderCount").arg(encodedId), {}, parentWorkId);
}

quint64 MicrosoftGraphClient::listMailFoldersPage(const QString& nextLink, const QString& parentWorkId)
{
    const quint64 requestId = nextRequestId_++;
    trackRequest(requestId, QStringLiteral("Load mail folders"), parentWorkId);
    const QUrl nextUrl(nextLink, QUrl::StrictMode);
    const QString path = nextUrl.path();
    if (!nextUrl.isValid() || nextUrl.scheme() != QLatin1String("https") ||
        nextUrl.host() != QLatin1String("graph.microsoft.com") || !nextUrl.userInfo().isEmpty() ||
        (nextUrl.port(-1) != -1 && nextUrl.port(-1) != 443) || nextUrl.hasFragment() ||
        !path.startsWith(QStringLiteral("/v1.0/me/mailFolders"))) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit requestFailed(requestId, QStringLiteral("Microsoft Graph returned an invalid mail-folder page address."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    sendRequest(requestId, QByteArrayLiteral("GET"), nextUrl, {});
    return requestId;
}

quint64 MicrosoftGraphClient::listFolderMessageDelta(const QString& folderId, const QString& deltaLink,
                                                       const QString& parentWorkId)
{
    if (!deltaLink.isEmpty())
        return listFolderMessageDeltaPage(deltaLink, parentWorkId);
    if (folderId.isEmpty()) {
        const quint64 requestId = nextRequestId_++;
        emit requestFailed(requestId, QStringLiteral("A mail folder ID is required."));
        return requestId;
    }
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(folderId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,subject,from,receivedDateTime,isRead,parentFolderId"));
    return request(QByteArrayLiteral("GET"), QStringLiteral("/me/mailFolders/%1/messages/delta?%2")
                       .arg(encodedId, query.toString(QUrl::FullyEncoded)), {}, parentWorkId);
}

quint64 MicrosoftGraphClient::listFolderMessageDeltaPage(const QString& nextLink, const QString& parentWorkId)
{
    const quint64 requestId = nextRequestId_++;
    trackRequest(requestId, QStringLiteral("Read mail changes"), parentWorkId);
    const QUrl nextUrl(nextLink, QUrl::StrictMode);
    const QString path = nextUrl.path();
    const bool folderPath = path.startsWith(QStringLiteral("/v1.0/me/mailFolders/")) ||
                            path.startsWith(QStringLiteral("/v1.0/me/mailFolders('"));
    if (!nextUrl.isValid() || nextUrl.scheme() != QLatin1String("https") ||
        nextUrl.host() != QLatin1String("graph.microsoft.com") || !nextUrl.userInfo().isEmpty() ||
        (nextUrl.port(-1) != -1 && nextUrl.port(-1) != 443) || nextUrl.hasFragment() ||
        !folderPath ||
        !path.contains(QStringLiteral("/messages/delta"))) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit requestFailed(requestId, QStringLiteral("Microsoft Graph returned an invalid mail delta page address."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    sendRequest(requestId, QByteArrayLiteral("GET"), nextUrl, {});
    return requestId;
}

quint64 MicrosoftGraphClient::translateMessageIds(const QStringList& ids, const QString& parentWorkId)
{
    if (ids.isEmpty() || ids.size() > 1000) {
        const quint64 requestId = nextRequestId_++;
        emit requestFailed(requestId, QStringLiteral("Message ID translation requires between 1 and 1,000 IDs."));
        return requestId;
    }
    QJsonArray inputIds;
    for (const QString& id : ids)
        inputIds.append(id);
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("inputIds"), inputIds},
        {QStringLiteral("sourceIdType"), QStringLiteral("restId")},
        {QStringLiteral("targetIdType"), QStringLiteral("restImmutableEntryId")}
    }).toJson(QJsonDocument::Compact);
    return request(QByteArrayLiteral("POST"), QStringLiteral("/me/translateExchangeIds"), body, parentWorkId);
}

quint64 MicrosoftGraphClient::setMessageRead(const QString& messageId, bool isRead, const QString& parentWorkId)
{
    if (messageId.isEmpty()) {
        const quint64 requestId = nextRequestId_++;
        emit requestFailed(requestId, QStringLiteral("A message ID is required."));
        return requestId;
    }
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("isRead"), isRead}})
        .toJson(QJsonDocument::Compact);
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(messageId));
    return request(QByteArrayLiteral("PATCH"), QStringLiteral("/me/messages/%1").arg(encodedId), body, parentWorkId);
}

quint64 MicrosoftGraphClient::sendMail(const QString& recipient, const QString& subject,
                                       const QString& body)
{
    if (recipient.trimmed().isEmpty() || subject.trimmed().isEmpty()) {
        const quint64 requestId = nextRequestId_++;
        emit requestFailed(requestId, QStringLiteral("A recipient and subject are required."));
        return requestId;
    }
    const QJsonObject emailAddress{{QStringLiteral("address"), recipient.trimmed()}};
    const QJsonObject toRecipient{{QStringLiteral("emailAddress"), emailAddress}};
    const QJsonArray toRecipients{toRecipient};
    const QJsonObject message{
        {QStringLiteral("subject"), subject.trimmed()},
        {QStringLiteral("body"), QJsonObject{
            {QStringLiteral("contentType"), QStringLiteral("Text")},
            {QStringLiteral("content"), body}
        }},
        {QStringLiteral("toRecipients"), toRecipients}
    };
    const QByteArray requestBody = QJsonDocument(QJsonObject{
        {QStringLiteral("message"), message},
        {QStringLiteral("saveToSentItems"), true}
    }).toJson(QJsonDocument::Compact);
    return request(QByteArrayLiteral("POST"), QStringLiteral("/me/sendMail"), requestBody);
}

} // namespace productivity::services
