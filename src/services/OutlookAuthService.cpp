#include "OutlookAuthService.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QUrlQuery>
#include <QNetworkReply>
#include <QNetworkRequest>

Q_LOGGING_CATEGORY(outlookAuthLog, "application.outlook.auth")

namespace productivity::services {
namespace {

const QUrl tokenEndpoint(const QString& tenant)
{
    return QUrl(QStringLiteral("https://login.microsoftonline.com/%1/oauth2/v2.0/token").arg(tenant));
}

QByteArray encodedForm(const QUrlQuery& form)
{
    return form.query(QUrl::FullyEncoded).toUtf8();
}

} // namespace

OutlookAuthService::OutlookAuthService(QObject* parent)
    : QObject(parent)
{
    pollTimer_.setSingleShot(true);
    connect(&pollTimer_, &QTimer::timeout, this, [this] { pollForToken(); });
}

void OutlookAuthService::connectAccount(const QString& tenant, const QString& clientId)
{
    if (tenant.isEmpty() || clientId.isEmpty()) {
        emit statusChanged(QStringLiteral("Save a tenant and client ID before connecting."));
        return;
    }
    ++authorizationGeneration_;
    if (pendingReply_)
        pendingReply_->abort();
    pollTimer_.stop();
    tenant_ = tenant.trimmed();
    clientId_ = clientId.trimmed();
    deviceCode_.clear();
    pollIntervalSeconds_ = 5;
    emit statusChanged(QStringLiteral("Contacting Microsoft sign-in…"));
    requestDeviceCode();
}

void OutlookAuthService::requestDeviceCode()
{
    QNetworkRequest request(QUrl(QStringLiteral("https://login.microsoftonline.com/%1/oauth2/v2.0/devicecode").arg(tenant_)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId_);
    form.addQueryItem(QStringLiteral("scope"), QStringLiteral("offline_access https://graph.microsoft.com/Mail.Read"));
    QNetworkReply* reply = network_.post(request, encodedForm(form));
    pendingReply_ = reply;
    const int generation = authorizationGeneration_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (generation != authorizationGeneration_) {
            reply->deleteLater();
            return;
        }
        const QByteArray payload = reply->readAll();
        const bool transportFailed = reply->error() != QNetworkReply::NoError &&
            !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        reply->deleteLater();
        const QJsonObject response = QJsonDocument::fromJson(payload).object();
        if (transportFailed || response.value(QStringLiteral("device_code")).toString().isEmpty()) {
            finishFailure(QStringLiteral("Could not start Microsoft sign-in. Check the tenant and client ID, then try again."));
            qCWarning(outlookAuthLog) << "Device authorization request failed; transport error:" << transportFailed;
            return;
        }
        deviceCode_ = response.value(QStringLiteral("device_code")).toString();
        const int expiresIn = response.value(QStringLiteral("expires_in")).toInt();
        pollIntervalSeconds_ = qMax(1, response.value(QStringLiteral("interval")).toInt(5));
        authorizationExpiresAt_ = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
        const QUrl verificationUrl(response.value(QStringLiteral("verification_uri")).toString());
        if (verificationUrl.scheme() != QLatin1String("https") ||
            (verificationUrl.host() != QLatin1String("microsoft.com") &&
             verificationUrl.host() != QLatin1String("www.microsoft.com") &&
             verificationUrl.host() != QLatin1String("login.microsoftonline.com"))) {
            finishFailure(QStringLiteral("Microsoft returned an unexpected sign-in address."));
            qCWarning(outlookAuthLog) << "Rejected an unexpected device verification host" << verificationUrl.host();
            return;
        }
        emit deviceAuthorizationRequired(response.value(QStringLiteral("user_code")).toString(),
                                         verificationUrl,
                                         response.value(QStringLiteral("message")).toString());
        emit statusChanged(QStringLiteral("Waiting for Microsoft sign-in to finish…"));
        pollTimer_.start(pollIntervalSeconds_ * 1000);
    });
}

void OutlookAuthService::pollForToken()
{
    if (QDateTime::currentDateTimeUtc() >= authorizationExpiresAt_) {
        finishFailure(QStringLiteral("Microsoft sign-in code expired. Start connecting again."));
        return;
    }

    QNetworkRequest request(tokenEndpoint(tenant_));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    form.addQueryItem(QStringLiteral("client_id"), clientId_);
    form.addQueryItem(QStringLiteral("device_code"), deviceCode_);
    QNetworkReply* reply = network_.post(request, encodedForm(form));
    pendingReply_ = reply;
    const int generation = authorizationGeneration_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (generation != authorizationGeneration_) {
            reply->deleteLater();
            return;
        }
        const QByteArray payload = reply->readAll();
        const bool transportFailed = reply->error() != QNetworkReply::NoError &&
            !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        reply->deleteLater();
        const QJsonObject response = QJsonDocument::fromJson(payload).object();
        if (transportFailed) {
            emit statusChanged(QStringLiteral("Connection interrupted; retrying Microsoft sign-in…"));
            pollTimer_.start(qMin(pollIntervalSeconds_, 30) * 1000);
            qCWarning(outlookAuthLog) << "Token polling encountered a transport error; retrying";
            return;
        }
        const QString error = response.value(QStringLiteral("error")).toString();
        if (error == QLatin1String("authorization_pending")) {
            pollTimer_.start(pollIntervalSeconds_ * 1000);
            return;
        }
        if (error == QLatin1String("slow_down")) {
            pollIntervalSeconds_ += 5;
            pollTimer_.start(pollIntervalSeconds_ * 1000);
            return;
        }
        const QString accessToken = response.value(QStringLiteral("access_token")).toString();
        const QString refreshToken = response.value(QStringLiteral("refresh_token")).toString();
        if (error.isEmpty() && !accessToken.isEmpty() && !refreshToken.isEmpty()) {
            const QJsonObject credentials{
                {QStringLiteral("access_token"), accessToken},
                {QStringLiteral("refresh_token"), refreshToken},
                {QStringLiteral("expires_in"), response.value(QStringLiteral("expires_in"))},
                {QStringLiteral("saved_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            };
            if (!storeCredentials(QJsonDocument(credentials).toJson(QJsonDocument::Compact))) {
                finishFailure(QStringLiteral("Signed in, but the system credential store is unavailable. Install or unlock Secret Service and try again."));
                return;
            }
            deviceCode_.clear();
            emit statusChanged(QStringLiteral("Outlook account connected. Authentication tokens are stored in the system credential store."));
            qCInfo(outlookAuthLog) << "Microsoft sign-in completed and credentials were stored securely";
            return;
        }
        finishFailure(QStringLiteral("Microsoft sign-in was declined or failed. Check the app registration and try again."));
        qCWarning(outlookAuthLog) << "Token request failed with Microsoft error code:" << error;
    });
}

void OutlookAuthService::disconnectAccount()
{
    ++authorizationGeneration_;
    if (pendingReply_)
        pendingReply_->abort();
    pollTimer_.stop();
    deviceCode_.clear();
    if (clearCredentials()) {
        emit statusChanged(QStringLiteral("Outlook credentials removed from the system credential store."));
        qCInfo(outlookAuthLog) << "Stored Outlook credentials removed";
    } else {
        emit statusChanged(QStringLiteral("Could not access the system credential store."));
    }
}

void OutlookAuthService::finishFailure(const QString& message)
{
    pollTimer_.stop();
    deviceCode_.clear();
    emit statusChanged(message);
}

bool OutlookAuthService::storeCredentials(const QByteArray& json)
{
    QProcess process;
    process.start(QStringLiteral("secret-tool"), {
        QStringLiteral("store"), QStringLiteral("--label=Personal Productivity App Outlook"),
        QStringLiteral("application"), QStringLiteral("PersonalProductivityApp"),
        QStringLiteral("service"), QStringLiteral("outlook-graph")
    });
    if (!process.waitForStarted(3000))
        return false;
    process.write(json);
    process.write("\n");
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool OutlookAuthService::clearCredentials()
{
    QProcess process;
    process.start(QStringLiteral("secret-tool"), {
        QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("PersonalProductivityApp"),
        QStringLiteral("service"), QStringLiteral("outlook-graph")
    });
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace productivity::services
