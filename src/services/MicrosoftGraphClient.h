#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>

class QNetworkAccessManager;
class QUrl;

namespace productivity::services {

class ApplicationSettings;
class OutlookAuthService;

class MicrosoftGraphClient final : public QObject
{
    Q_OBJECT
public:
    MicrosoftGraphClient(ApplicationSettings& settings, OutlookAuthService& authService,
                         QObject* parent = nullptr);

    quint64 request(const QByteArray& method, const QString& relativePath,
                    const QByteArray& jsonBody = {});
    quint64 listMessages(int pageSize = 25);
    quint64 listMailboxMessagesPage(const QString& nextLink);
    quint64 listRootMailFolders();
    quint64 listChildMailFolders(const QString& folderId);
    quint64 listMailFoldersPage(const QString& nextLink);
    quint64 listFolderMessageDelta(const QString& folderId, const QString& deltaLink = {});
    quint64 listFolderMessageDeltaPage(const QString& nextLink);
    quint64 translateMessageIds(const QStringList& ids);
    quint64 setMessageRead(const QString& messageId, bool isRead);
    quint64 sendMail(const QString& recipient, const QString& subject, const QString& body);

signals:
    void requestFinished(quint64 requestId, int httpStatus, const QByteArray& responseBody);
    void requestFailed(quint64 requestId, const QString& error);
    void requestRetrying(quint64 requestId, int attempt, int delayMs, const QString& reason);

private:
    void sendRequest(quint64 requestId, const QByteArray& method, const QUrl& url,
                     const QByteArray& jsonBody, int retryAttempt = 0,
                     bool forceRefreshToken = false, bool refreshAlreadyAttempted = false);

    ApplicationSettings& settings_;
    OutlookAuthService& authService_;
    QNetworkAccessManager* network_ = nullptr;
    quint64 nextRequestId_ = 1;
};

} // namespace productivity::services
