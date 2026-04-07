/* Google Drive Uploader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "googledrive_uploader.h"
#include "oauth2_handler.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>

// Google OAuth2 and Drive API endpoints.
static const QUrl AUTH_URL(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
static const QUrl TOKEN_URL(QStringLiteral("https://oauth2.googleapis.com/token"));
// Resumable upload: metadata + content type declared upfront, file sent in follow-up PUT.
static const QUrl UPLOAD_INITIATE_URL(QStringLiteral(
    "https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable"));
// Permission creation: %1 is the fileId.
static const QString PERMISSIONS_URL_TEMPLATE(QStringLiteral(
    "https://www.googleapis.com/drive/v3/files/%1/permissions"));
// Shareable link template: %1 is the fileId.
static const QString SHARE_URL_TEMPLATE(QStringLiteral(
    "https://drive.google.com/file/d/%1/view?usp=sharing"));

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

GoogleDriveUploader::GoogleDriveUploader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    OAuth2Handler::Config config;
    config.authUrl      = AUTH_URL;
    config.tokenUrl     = TOKEN_URL;
    config.clientId     = QLatin1String(GD_CLIENT_ID);
    config.clientSecret = QLatin1String(GD_CLIENT_SECRET);
    // drive.file: access only to files this app creates — narrowest useful scope.
    config.scope = QStringLiteral("https://www.googleapis.com/auth/drive.file");
    // Google requires access_type=offline to receive a refresh token.
    // prompt=consent forces the consent screen every time, ensuring a refresh
    // token is returned even if the user previously granted access.
    config.extraAuthParams[QStringLiteral("access_type")] = QStringLiteral("offline");
    config.extraAuthParams[QStringLiteral("prompt")]      = QStringLiteral("consent");

    m_oauth = new OAuth2Handler(config, this);
    m_oauth->loadTokens(QLatin1String(PROVIDER_KEY));

    connect(m_oauth, &OAuth2Handler::authenticated,
            this,    &GoogleDriveUploader::onAuthenticated);
    connect(m_oauth, &OAuth2Handler::authFailed,
            this,    &GoogleDriveUploader::onAuthFailed);
}

GoogleDriveUploader::~GoogleDriveUploader()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
    }
    delete m_file;
}

// ---------------------------------------------------------------------------
//  Public interface
// ---------------------------------------------------------------------------

void GoogleDriveUploader::setFilePath(const QString& path)
{
    m_filePath = path;
}

bool GoogleDriveUploader::isAuthenticated() const
{
    return m_oauth->hasValidToken() || m_oauth->hasRefreshToken();
}

void GoogleDriveUploader::authenticate()
{
    m_pendingUpload = false;
    if (m_oauth->hasValidToken()) {
        emit authComplete(true);
    } else if (m_oauth->hasRefreshToken()) {
        m_oauth->refreshToken();
    } else {
        m_oauth->startAuth();
    }
}

void GoogleDriveUploader::signOut()
{
    m_oauth->clearTokens(QLatin1String(PROVIDER_KEY));
}

QString GoogleDriveUploader::shareUrl() const
{
    return m_shareUrl;
}

void GoogleDriveUploader::startUpload()
{
    m_aborted = false;
    m_shareUrl.clear();

    if (m_filePath.isEmpty()) {
        emit uploadFailed(tr("No file specified for upload."));
        return;
    }

    if (m_oauth->hasValidToken()) {
        doUpload();
    } else if (m_oauth->hasRefreshToken()) {
        m_pendingUpload = true;
        m_oauth->refreshToken();
    } else {
        m_pendingUpload = true;
        m_oauth->startAuth();
    }
}

void GoogleDriveUploader::abort()
{
    m_aborted = true;
    if (m_reply) {
        m_reply->abort();
    }
}

// ---------------------------------------------------------------------------
//  Auth callbacks
// ---------------------------------------------------------------------------

void GoogleDriveUploader::onAuthenticated(const QString& accessToken)
{
    Q_UNUSED(accessToken);
    m_oauth->saveTokens(QLatin1String(PROVIDER_KEY));
    emit authComplete(true);

    if (m_pendingUpload) {
        m_pendingUpload = false;
        doUpload();
    }
}

void GoogleDriveUploader::onAuthFailed(const QString& error)
{
    m_pendingUpload = false;
    emit authComplete(false);
    emit uploadFailed(tr("Google authentication failed: %1").arg(error));
}

// ---------------------------------------------------------------------------
//  Upload — step 1: initiate resumable upload session
// ---------------------------------------------------------------------------

void GoogleDriveUploader::doUpload()
{
    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        emit uploadFailed(tr("File does not exist: %1").arg(m_filePath));
        return;
    }

    // Build the file metadata JSON — Drive will use this as the file name.
    QJsonObject metadata;
    metadata[QStringLiteral("name")] = fi.fileName();
    QByteArray metadataJson = QJsonDocument(metadata).toJson(QJsonDocument::Compact);

    QNetworkRequest request(UPLOAD_INITIATE_URL);
    request.setRawHeader("Authorization",
        QStringLiteral("Bearer %1").arg(m_oauth->accessToken()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json; charset=UTF-8"));
    request.setRawHeader("X-Upload-Content-Type", "application/octet-stream");
    request.setRawHeader("X-Upload-Content-Length",
        QString::number(fi.size()).toUtf8());

    qDebug() << "GoogleDriveUploader: initiating resumable upload for" << fi.fileName()
             << "(" << fi.size() << "bytes)";

    m_reply = m_nam->post(request, metadataJson);
    connect(m_reply, &QNetworkReply::finished,
            this,    &GoogleDriveUploader::onInitiateReplyFinished);
}

void GoogleDriveUploader::onInitiateReplyFinished()
{
    if (m_aborted) {
        if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
        emit uploadFailed(tr("Upload was cancelled."));
        return;
    }

    if (!m_reply) return;

    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_reply->error() != QNetworkReply::NoError || httpStatus != 200) {
        QByteArray body = m_reply->readAll();
        QString errorMsg;
        if (httpStatus == 401) {
            errorMsg = tr("Google authentication expired. Please sign in again.");
            m_oauth->clearTokens(QLatin1String(PROVIDER_KEY));
        } else {
            errorMsg = tr("Google Drive upload failed to start (HTTP %1): %2")
                           .arg(httpStatus).arg(m_reply->errorString());
        }
        qDebug() << "GoogleDriveUploader: initiate failed:" << httpStatus << body;
        m_reply->deleteLater(); m_reply = nullptr;
        emit uploadFailed(errorMsg);
        return;
    }

    // The session URI is returned in the Location header.
    QUrl sessionUri = m_reply->header(QNetworkRequest::LocationHeader).toUrl();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (!sessionUri.isValid() || sessionUri.isEmpty()) {
        emit uploadFailed(tr("Google Drive did not return an upload session URI."));
        return;
    }

    qDebug() << "GoogleDriveUploader: upload session URI obtained, uploading file...";
    uploadToSession(sessionUri);
}

// ---------------------------------------------------------------------------
//  Upload — step 2: send the file body to the session URI
// ---------------------------------------------------------------------------

void GoogleDriveUploader::uploadToSession(const QUrl& sessionUri)
{
    m_file = new QFile(m_filePath);
    if (!m_file->open(QIODevice::ReadOnly)) {
        emit uploadFailed(tr("Could not open file for reading:\n%1")
                              .arg(m_file->errorString()));
        delete m_file;
        m_file = nullptr;
        return;
    }

    QFileInfo fi(m_filePath);

    QNetworkRequest request(sessionUri);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/octet-stream"));
    request.setHeader(QNetworkRequest::ContentLengthHeader, fi.size());

    m_reply = m_nam->put(request, m_file);

    connect(m_reply, &QNetworkReply::uploadProgress,
            this,    &GoogleDriveUploader::uploadProgress);
    connect(m_reply, &QNetworkReply::finished,
            this,    &GoogleDriveUploader::onUploadReplyFinished);
}

void GoogleDriveUploader::onUploadReplyFinished()
{
    if (m_aborted) {
        if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
        delete m_file; m_file = nullptr;
        emit uploadFailed(tr("Upload was cancelled."));
        return;
    }

    if (!m_reply) return;

    QByteArray body = m_reply->readAll();
    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_reply->error() != QNetworkReply::NoError) {
        qDebug() << "GoogleDriveUploader: upload failed:" << httpStatus << body;
        QString errorStr = m_reply->errorString();
        m_reply->deleteLater(); m_reply = nullptr;
        delete m_file; m_file = nullptr;
        emit uploadFailed(tr("Google Drive upload failed (HTTP %1): %2")
                              .arg(httpStatus).arg(errorStr));
        return;
    }

    m_reply->deleteLater(); m_reply = nullptr;
    delete m_file; m_file = nullptr;

    // Parse the fileId from the response JSON.
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString fileId = doc.object().value(QStringLiteral("id")).toString();

    qDebug() << "GoogleDriveUploader: file uploaded, id =" << fileId;

    if (fileId.isEmpty()) {
        emit uploadFailed(tr("Upload succeeded but Google Drive did not return a file ID."));
        return;
    }

    createPermission(fileId);
}

// ---------------------------------------------------------------------------
//  Upload — step 3: create public "anyone / reader" permission
// ---------------------------------------------------------------------------

void GoogleDriveUploader::createPermission(const QString& fileId)
{
    QJsonObject body;
    body[QStringLiteral("type")] = QStringLiteral("anyone");
    body[QStringLiteral("role")] = QStringLiteral("reader");
    QByteArray jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QUrl url(PERMISSIONS_URL_TEMPLATE.arg(fileId));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization",
        QStringLiteral("Bearer %1").arg(m_oauth->accessToken()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json"));

    m_reply = m_nam->post(request, jsonBody);
    // Store fileId in the share URL now so the reply slot can use it.
    m_shareUrl = SHARE_URL_TEMPLATE.arg(fileId);

    connect(m_reply, &QNetworkReply::finished,
            this,    &GoogleDriveUploader::onPermissionReplyFinished);
}

void GoogleDriveUploader::onPermissionReplyFinished()
{
    if (!m_reply) return;

    QByteArray body = m_reply->readAll();
    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    m_reply->deleteLater();
    m_reply = nullptr;

    if (httpStatus != 200 && httpStatus != 201) {
        qDebug() << "GoogleDriveUploader: permission creation failed:" << httpStatus << body;
        emit uploadFailed(
            tr("File uploaded to Google Drive but could not create a share link (HTTP %1).")
                .arg(httpStatus));
        return;
    }

    qDebug() << "GoogleDriveUploader: share link:" << m_shareUrl;
    emit uploadFinished(m_shareUrl);
}
