#include "OutlookAuthService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QUrlQuery>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>

#include <memory>

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

QString deviceCodeErrorHint(const QString& code)
{
    if (code == QLatin1String("invalid_client"))
        return QStringLiteral("Verify the application (client) ID in the app registration.");
    if (code == QLatin1String("unauthorized_client"))
        return QStringLiteral("Enable public client flows in the app registration's Authentication settings.");
    if (code == QLatin1String("invalid_scope"))
        return QStringLiteral("Check that the app registration has the delegated Microsoft Graph Mail.Read permission.");
    if (code == QLatin1String("unsupported_account_type"))
        return QStringLiteral("Configure the app registration to allow personal Microsoft accounts.");
    return QStringLiteral("Check the tenant, client ID, and supported account types in the app registration.");
}

QString microsoftErrorDetail(const QJsonObject& response)
{
    QString detail = response.value(QStringLiteral("error_description")).toString().trimmed();
    if (detail.isEmpty())
        return {};

    // Keep useful AADSTS diagnostics while omitting request-correlation metadata.
    const qsizetype metadataStart = detail.indexOf(QStringLiteral("Trace ID:"), 0, Qt::CaseInsensitive);
    if (metadataStart >= 0)
        detail.truncate(metadataStart);
    detail = detail.simplified();
    if (detail.size() > 500)
        detail = detail.left(497) + QStringLiteral("...");
    return detail;
}

QString deviceCodeErrorHint(const QString& code, const QString& detail)
{
    if (code == QLatin1String("invalid_client") && detail.contains(QLatin1String("AADSTS7000218")))
        return QStringLiteral("This registration is not accepting public-client sign-in. In Azure, open App registrations → your app → Authentication → Advanced settings, set ‘Allow public client flows’ to Yes, and save.");
    if (code == QLatin1String("invalid_client") && detail.contains(QLatin1String("AADSTS700016")))
        return QStringLiteral("Microsoft cannot find this application in the selected tenant. Verify that you copied the Application (client) ID, and that the registration supports the selected account type.");
    return deviceCodeErrorHint(code);
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

void OutlookAuthService::restoreConnectionStatus(const QString& tenant, const QString& clientId)
{
    const int generation = authorizationGeneration_;
    auto* process = new QProcess(this);
    const auto timedOut = std::make_shared<bool>(false);
    connect(process, &QProcess::finished, this,
            [this, process, timedOut, generation, tenant, clientId](int exitCode, QProcess::ExitStatus exitStatus) {
        if (generation != authorizationGeneration_) {
            process->deleteLater();
            return;
        }
        if (*timedOut) {
            emit statusChanged(QStringLiteral("The system credential store did not respond. Unlock it, then reopen Outlook settings."));
            process->deleteLater();
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            emit statusChanged(QStringLiteral("No saved Outlook sign-in was found. Choose Connect to Outlook to get started."));
            process->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(process->readAllStandardOutput());
        process->deleteLater();
        if (!document.isObject()) {
            emit statusChanged(QStringLiteral("No saved Outlook sign-in was found. Choose Connect to Outlook to get started."));
            return;
        }
        const QJsonObject credentials = document.object();
        if (credentials.value(QStringLiteral("refresh_token")).toString().isEmpty()) {
            emit statusChanged(QStringLiteral("The saved Outlook sign-in is incomplete. Disconnect, then connect again."));
            return;
        }
        const bool sameTenant = credentials.value(QStringLiteral("tenant")).toString().compare(tenant.trimmed(), Qt::CaseInsensitive) == 0;
        const bool sameClient = credentials.value(QStringLiteral("client_id")).toString().compare(clientId.trimmed(), Qt::CaseInsensitive) == 0;
        if (!sameTenant || !sameClient) {
            emit statusChanged(QStringLiteral("The saved Outlook sign-in uses different app settings. Restore the matching settings or disconnect and connect again."));
            return;
        }
        emit statusChanged(QStringLiteral("Outlook sign-in is saved. Choose Check mailbox access to confirm it still works."));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || generation != authorizationGeneration_)
            return;
        emit statusChanged(QStringLiteral("The system credential store helper is unavailable. Install Secret Service to use Outlook."));
        process->deleteLater();
    });
    QTimer::singleShot(5000, process, [process, timedOut] {
        if (process->state() == QProcess::Running) {
            *timedOut = true;
            process->kill();
        }
    });
    process->start(QStringLiteral("secret-tool"), {
        QStringLiteral("lookup"), QStringLiteral("application"), QStringLiteral("PersonalProductivityApp"),
        QStringLiteral("service"), QStringLiteral("outlook-graph")
    });
}

void OutlookAuthService::requestDeviceCode()
{
    QNetworkRequest request(QUrl(QStringLiteral("https://login.microsoftonline.com/%1/oauth2/v2.0/devicecode").arg(tenant_)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId_);
    form.addQueryItem(QStringLiteral("scope"),
                      QStringLiteral("offline_access https://graph.microsoft.com/Mail.ReadWrite https://graph.microsoft.com/Mail.Send"));
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
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        const QJsonObject response = QJsonDocument::fromJson(payload).object();
        if (transportFailed || response.value(QStringLiteral("device_code")).toString().isEmpty()) {
            const QString errorCode = response.value(QStringLiteral("error")).toString();
            const QString errorDetail = microsoftErrorDetail(response);
            const QString message = transportFailed
                ? QStringLiteral("Could not reach Microsoft sign-in. Check your connection and try again.")
                : QStringLiteral("Microsoft rejected the sign-in request (%1). %2%3")
                    .arg(errorCode.isEmpty() ? QStringLiteral("unknown error") : errorCode,
                         deviceCodeErrorHint(errorCode, errorDetail),
                         errorDetail.isEmpty() ? QString() : QStringLiteral("\n\nMicrosoft detail: %1").arg(errorDetail));
            finishFailure(message);
            qCWarning(outlookAuthLog) << "Device authorization request failed; HTTP status:"
                                      << httpStatus << "OAuth error code:" << errorCode
                                      << "Microsoft code:" << (errorDetail.section(QLatin1Char(':'), 0, 0).startsWith(QLatin1String("AADSTS"))
                                          ? errorDetail.section(QLatin1Char(':'), 0, 0) : QStringLiteral("unavailable"))
                                      << "transport error:" << transportFailed;
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
             verificationUrl.host() != QLatin1String("login.microsoft.com") &&
             verificationUrl.host() != QLatin1String("login.microsoftonline.com"))) {
            const QString address = QStringLiteral("%1://%2")
                .arg(verificationUrl.scheme(), verificationUrl.host());
            finishFailure(QStringLiteral("Microsoft returned an unexpected sign-in address (%1).")
                              .arg(address));
            qCWarning(outlookAuthLog) << "Rejected an unexpected device verification address; scheme:"
                                      << verificationUrl.scheme() << "host:" << verificationUrl.host();
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
                {QStringLiteral("tenant"), tenant_},
                {QStringLiteral("client_id"), clientId_},
                {QStringLiteral("delegated_scopes"), QJsonArray{
                    QStringLiteral("Mail.ReadWrite"), QStringLiteral("Mail.Send")
                }},
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

void OutlookAuthService::withValidAccessToken(
    const QString& tenant, const QString& clientId,
    std::function<void(const QString& token, const QString& error)> callback,
    bool forceRefresh)
{
    QJsonObject credentials;
    QString credentialError;
    if (!loadCredentials(credentials, credentialError)) {
        callback({}, credentialError);
        return;
    }

    const bool sameTenant = credentials.value(QStringLiteral("tenant")).toString()
                                .compare(tenant.trimmed(), Qt::CaseInsensitive) == 0;
    const bool sameClient = credentials.value(QStringLiteral("client_id")).toString()
                                .compare(clientId.trimmed(), Qt::CaseInsensitive) == 0;
    if (!sameTenant || !sameClient) {
        callback({}, QStringLiteral("The saved Outlook sign-in belongs to different app settings. Restore those settings or disconnect and connect again."));
        return;
    }

    const QJsonArray grantedScopes = credentials.value(QStringLiteral("delegated_scopes")).toArray();
    if (!grantedScopes.contains(QStringLiteral("Mail.ReadWrite")) ||
        !grantedScopes.contains(QStringLiteral("Mail.Send"))) {
        callback({}, QStringLiteral("Outlook permissions changed. Disconnect and reconnect to grant the new delegated permissions."));
        return;
    }

    const QString accessToken = credentials.value(QStringLiteral("access_token")).toString();
    const QDateTime savedAt = QDateTime::fromString(
        credentials.value(QStringLiteral("saved_at_utc")).toString(), Qt::ISODateWithMs).toUTC();
    const int expiresIn = credentials.value(QStringLiteral("expires_in")).toInt();
    if (!forceRefresh && !accessToken.isEmpty() && savedAt.isValid() &&
        savedAt.addSecs(qMax(0, expiresIn - 120)) > QDateTime::currentDateTimeUtc()) {
        callback(accessToken, {});
        return;
    }

    const QString refreshToken = credentials.value(QStringLiteral("refresh_token")).toString();
    if (refreshToken.isEmpty()) {
        callback({}, QStringLiteral("No refresh token is available. Reconnect the Outlook account."));
        return;
    }

    QNetworkRequest request(tokenEndpoint(tenant));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("scope"),
                      QStringLiteral("offline_access https://graph.microsoft.com/Mail.ReadWrite https://graph.microsoft.com/Mail.Send"));
    QNetworkReply* reply = network_.post(request, encodedForm(form));
    pendingReply_ = reply;
    const int generation = authorizationGeneration_;
    QPointer<OutlookAuthService> self(this);
    connect(reply, &QNetworkReply::finished, this, [self, reply, generation, credentials,
                                                    callback = std::move(callback), tenant, clientId] () mutable {
        if (!self || generation != self->authorizationGeneration_) {
            reply->deleteLater();
            return;
        }
        const QByteArray payload = reply->readAll();
        const bool transportFailed = reply->error() != QNetworkReply::NoError &&
            !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        const QJsonObject response = QJsonDocument::fromJson(payload).object();
        if (transportFailed) {
            callback({}, QStringLiteral("Could not reach Microsoft to refresh sign-in. Check the connection."));
            qCWarning(outlookAuthLog) << "Token refresh transport failure";
            return;
        }

        const QString refreshedAccessToken = response.value(QStringLiteral("access_token")).toString();
        if (refreshedAccessToken.isEmpty()) {
            const QString errorCode = response.value(QStringLiteral("error")).toString();
            const bool credentialsRevoked = errorCode == QLatin1String("invalid_grant") ||
                errorCode == QLatin1String("interaction_required");
            const QString error = credentialsRevoked
                ? QStringLiteral("Outlook sign-in expired or was revoked. Connect to Outlook again.")
                : QStringLiteral("Microsoft could not refresh sign-in (%1). Reconnect the Outlook account.")
                    .arg(errorCode.isEmpty() ? QStringLiteral("unknown error") : errorCode);
            if (credentialsRevoked) {
                self->clearCredentials();
                emit self->statusChanged(error);
            }
            callback({}, error);
            qCWarning(outlookAuthLog) << "Token refresh rejected; HTTP status:" << httpStatus
                                      << "OAuth error code:" << errorCode;
            return;
        }

        QJsonObject updatedCredentials = credentials;
        updatedCredentials.insert(QStringLiteral("access_token"), refreshedAccessToken);
        const QString rotatedRefreshToken = response.value(QStringLiteral("refresh_token")).toString();
        if (!rotatedRefreshToken.isEmpty())
            updatedCredentials.insert(QStringLiteral("refresh_token"), rotatedRefreshToken);
        updatedCredentials.insert(QStringLiteral("expires_in"), response.value(QStringLiteral("expires_in")));
        updatedCredentials.insert(QStringLiteral("saved_at_utc"),
                                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        updatedCredentials.insert(QStringLiteral("tenant"), tenant);
        updatedCredentials.insert(QStringLiteral("client_id"), clientId);
        updatedCredentials.insert(QStringLiteral("delegated_scopes"), QJsonArray{
            QStringLiteral("Mail.ReadWrite"), QStringLiteral("Mail.Send")
        });
        if (!self->storeCredentials(QJsonDocument(updatedCredentials).toJson(QJsonDocument::Compact))) {
            callback({}, QStringLiteral("Could not update tokens in the system credential store."));
            return;
        }
        qCInfo(outlookAuthLog) << "Microsoft access token refreshed";
        callback(refreshedAccessToken, {});
    });
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

bool OutlookAuthService::loadCredentials(QJsonObject& credentials, QString& error) const
{
    QProcess process;
    process.start(QStringLiteral("secret-tool"), {
        QStringLiteral("lookup"), QStringLiteral("application"), QStringLiteral("PersonalProductivityApp"),
        QStringLiteral("service"), QStringLiteral("outlook-graph")
    });
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        error = QStringLiteral("Could not access the system credential store.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QStringLiteral("No Outlook account is connected. Connect the account first.");
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!document.isObject()) {
        error = QStringLiteral("Stored Outlook credentials could not be read. Reconnect the account.");
        return false;
    }
    credentials = document.object();
    if (credentials.value(QStringLiteral("refresh_token")).toString().isEmpty()) {
        error = QStringLiteral("Stored Outlook credentials are incomplete. Reconnect the account.");
        return false;
    }
    return true;
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
