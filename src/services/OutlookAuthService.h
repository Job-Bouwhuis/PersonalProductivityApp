#pragma once

#include <QDateTime>
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

namespace productivity::services {

class OutlookAuthService final : public QObject
{
    Q_OBJECT
public:
    explicit OutlookAuthService(QObject* parent = nullptr);

    void connectAccount(const QString& tenant, const QString& clientId);
    void disconnectAccount();

signals:
    void statusChanged(const QString& status);
    void deviceAuthorizationRequired(const QString& userCode, const QUrl& verificationUrl,
                                     const QString& instructions);

private:
    void requestDeviceCode();
    void pollForToken();
    void finishFailure(const QString& message);
    bool storeCredentials(const QByteArray& json);
    bool clearCredentials();

    QNetworkAccessManager network_;
    QTimer pollTimer_;
    QString tenant_;
    QString clientId_;
    QString deviceCode_;
    QDateTime authorizationExpiresAt_;
    int pollIntervalSeconds_ = 5;
    int authorizationGeneration_ = 0;
    QPointer<QNetworkReply> pendingReply_;
};

} // namespace productivity::services
