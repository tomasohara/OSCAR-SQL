/* OneDrive Uploader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "onedrive_uploader.h"
#include "oauth2_handler.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>

// Microsoft Identity Platform OAuth2 endpoints (common = personal + work/school accounts).
static const QUrl AUTH_URL(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"));
static const QUrl TOKEN_URL(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token"));

// Microsoft Graph API — share link creation; %1 is the file ID.
static const QString SHARE_LINK_URL_TEMPLATE(QStringLiteral(
    "https://graph.microsoft.com/v1.0/me/drive/items/%1/createLink"));

// Destination folder name in the user's OneDrive.
static const QString SHARE_FOLDER_NAME(QStringLiteral("OSCAR Shared Profiles"));

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

OneDriveUploader::OneDriveUploader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    OAuth2Handler::Config config;
    config.authUrl      = AUTH_URL;
    config.tokenUrl     = TOKEN_URL;
    config.clientId     = QLatin1String(OD_CLIENT_ID);
    // Files.ReadWrite: upload and manage files this app creates.
    // offline_access: receive a refresh token so the user doesn't re-auth each session.
    config.scope        = QStringLiteral("Files.ReadWrite offline_access");

    m_oauth = new OAuth2Handler(config, this);
    m_oauth->loadTokens(QLatin1String(PROVIDER_KEY));

    connect(m_oauth, &OAuth2Handler::authenticated,
            this,    &OneDriveUploader::onAuthenticated);
    connect(m_oauth, &OAuth2Handler::authFailed,
            this,    &OneDriveUploader::onAuthFailed);
}

OneDriveUploader::~OneDriveUploader()
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

void OneDriveUploader::setFilePath(const QString& path)
{
    m_filePath = path;
}

bool OneDriveUploader::isAuthenticated() const
{
    return m_oauth->hasValidToken() || m_oauth->hasRefreshToken();
}

void OneDriveUploader::authenticate()
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

void OneDriveUploader::signOut()
{
    m_oauth->clearTokens(QLatin1String(PROVIDER_KEY));
}

QString OneDriveUploader::shareUrl() const
{
    return m_shareUrl;
}

void OneDriveUploader::startUpload()
{
    m_aborted = false;
    m_shareUrl.clear();

    if (m_filePath.isEmpty()) {
        emit uploadFailed(tr("No file specified for upload."));
        return;
    }

    if (m_oauth->hasValidToken()) {
        createUploadSession();
    } else if (m_oauth->hasRefreshToken()) {
        m_pendingUpload = true;
        m_oauth->refreshToken();
    } else {
        m_pendingUpload = true;
        m_oauth->startAuth();
    }
}

void OneDriveUploader::abort()
{
    m_aborted = true;
    if (m_reply) {
        m_reply->abort();
    }
}

// ---------------------------------------------------------------------------
//  Auth callbacks
// ---------------------------------------------------------------------------

void OneDriveUploader::onAuthenticated(const QString& accessToken)
{
    Q_UNUSED(accessToken);
    m_oauth->saveTokens(QLatin1String(PROVIDER_KEY));
    emit authComplete(true);

    if (m_pendingUpload) {
        m_pendingUpload = false;
        createUploadSession();
    }
}

void OneDriveUploader::onAuthFailed(const QString& error)
{
    m_pendingUpload = false;
    emit authComplete(false);
    emit uploadFailed(tr("OneDrive authentication failed: %1").arg(error));
}

// ---------------------------------------------------------------------------
//  Upload — step 1: create an upload session
// ---------------------------------------------------------------------------

void OneDriveUploader::cleanupReply()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    delete m_file;
    m_file = nullptr;
}

void OneDriveUploader::createUploadSession()
{
    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        emit uploadFailed(tr("File does not exist: %1").arg(m_filePath));
        return;
    }

    // Microsoft Graph upload sessions accept a maximum of 60 MiB per request chunk.
    static constexpr qint64 MAX_UPLOAD_SIZE = 60LL * 1024 * 1024;
    if (fi.size() > MAX_UPLOAD_SIZE) {
        emit uploadFailed(tr("File is too large for OneDrive upload (%1 MB). "
                             "Maximum is 60 MB.")
                              .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
        return;
    }

    // Build the Graph API path-based URL.  Spaces in the folder name are
    // auto-encoded to %20 by QUrl.  The colons (:) are kept literal —
    // they are Graph API path addressing syntax, not percent-encoded.
    QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/root:/")
             + SHARE_FOLDER_NAME + QStringLiteral("/") + fi.fileName()
             + QStringLiteral(":/createUploadSession"));

    // Request conflict behavior: rename if a file with the same name already exists.
    QJsonObject item;
    item[QStringLiteral("@microsoft.graph.conflictBehavior")] = QStringLiteral("rename");
    QJsonObject body;
    body[QStringLiteral("item")] = item;
    QByteArray jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization",
        QStringLiteral("Bearer %1").arg(m_oauth->accessToken()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json"));

    qDebug() << "OneDriveUploader: creating upload session for" << fi.fileName()
             << "(" << fi.size() << "bytes)";

    m_reply = m_nam->post(request, jsonBody);
    connect(m_reply, &QNetworkReply::finished,
            this,    &OneDriveUploader::onUploadSessionReplyFinished);
}

void OneDriveUploader::onUploadSessionReplyFinished()
{
    if (!m_reply) return;

    QByteArray body = m_reply->readAll();
    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    cleanupReply();

    if (m_aborted) {
        emit uploadFailed(tr("Upload was cancelled."));
        return;
    }

    if (httpStatus != 200) {
        QString errorMsg;
        if (httpStatus == 401) {
            errorMsg = tr("OneDrive authentication expired. Please sign in again.");
            m_oauth->clearTokens(QLatin1String(PROVIDER_KEY));
        } else {
            errorMsg = tr("OneDrive upload session creation failed (HTTP %1).").arg(httpStatus);
        }
        qDebug() << "OneDriveUploader: upload session failed:" << httpStatus << body;
        emit uploadFailed(errorMsg);
        return;
    }

    QUrl uploadUrl = QUrl(QJsonDocument::fromJson(body)
                              .object()
                              .value(QStringLiteral("uploadUrl"))
                              .toString());

    if (!uploadUrl.isValid() || uploadUrl.isEmpty()) {
        emit uploadFailed(tr("OneDrive did not return an upload URL."));
        return;
    }

    qDebug() << "OneDriveUploader: upload session created, uploading file...";
    doUpload(uploadUrl);
}

// ---------------------------------------------------------------------------
//  Upload — step 2: PUT the file to the session URL
// ---------------------------------------------------------------------------

void OneDriveUploader::doUpload(const QUrl& uploadUrl)
{
    m_file = new QFile(m_filePath);
    if (!m_file->open(QIODevice::ReadOnly)) {
        emit uploadFailed(tr("Could not open file for reading:\n%1")
                              .arg(m_file->errorString()));
        delete m_file;
        m_file = nullptr;
        return;
    }

    qint64 fileSize = m_file->size();

    // The upload session URL is pre-authorized — no Authorization header needed.
    QNetworkRequest request(uploadUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/octet-stream"));
    request.setHeader(QNetworkRequest::ContentLengthHeader, fileSize);
    // Content-Range is required even for single-chunk uploads.
    request.setRawHeader("Content-Range",
        QStringLiteral("bytes 0-%1/%2").arg(fileSize - 1).arg(fileSize).toUtf8());

    m_reply = m_nam->put(request, m_file);

    connect(m_reply, &QNetworkReply::uploadProgress,
            this,    &OneDriveUploader::uploadProgress);
    connect(m_reply, &QNetworkReply::finished,
            this,    &OneDriveUploader::onUploadReplyFinished);
}

void OneDriveUploader::onUploadReplyFinished()
{
    if (m_aborted) {
        cleanupReply();
        emit uploadFailed(tr("Upload was cancelled."));
        return;
    }

    if (!m_reply) return;

    QByteArray body = m_reply->readAll();
    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_reply->error() != QNetworkReply::NoError) {
        qDebug() << "OneDriveUploader: upload failed:" << httpStatus << body;
        QString errorStr = m_reply->errorString();
        cleanupReply();
        emit uploadFailed(tr("OneDrive upload failed (HTTP %1): %2")
                              .arg(httpStatus).arg(errorStr));
        return;
    }

    cleanupReply();

    // 200 = file replaced (shouldn't happen with rename behavior), 201 = created.
    if (httpStatus != 200 && httpStatus != 201) {
        emit uploadFailed(tr("OneDrive upload returned unexpected status (HTTP %1).")
                              .arg(httpStatus));
        return;
    }

    QString fileId = QJsonDocument::fromJson(body)
                         .object()
                         .value(QStringLiteral("id"))
                         .toString();

    qDebug() << "OneDriveUploader: file uploaded, id =" << fileId;

    if (fileId.isEmpty()) {
        emit uploadFailed(tr("Upload succeeded but OneDrive did not return a file ID."));
        return;
    }

    createShareLink(fileId);
}

// ---------------------------------------------------------------------------
//  Upload — step 3: create an anonymous "view" share link
// ---------------------------------------------------------------------------

void OneDriveUploader::createShareLink(const QString& fileId)
{
    QJsonObject body;
    body[QStringLiteral("type")]  = QStringLiteral("view");
    body[QStringLiteral("scope")] = QStringLiteral("anonymous");
    QByteArray jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QUrl url(SHARE_LINK_URL_TEMPLATE.arg(fileId));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization",
        QStringLiteral("Bearer %1").arg(m_oauth->accessToken()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json"));

    m_reply = m_nam->post(request, jsonBody);
    connect(m_reply, &QNetworkReply::finished,
            this,    &OneDriveUploader::onShareLinkReplyFinished);
}

void OneDriveUploader::onShareLinkReplyFinished()
{
    if (!m_reply) return;

    QByteArray body = m_reply->readAll();
    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    cleanupReply();

    if (httpStatus != 200 && httpStatus != 201) {
        qDebug() << "OneDriveUploader: share link creation failed:" << httpStatus << body;
        emit uploadFailed(
            tr("File uploaded to OneDrive but could not create a share link (HTTP %1).\n"
               "Note: anonymous sharing may be disabled in your Microsoft account settings.")
                .arg(httpStatus));
        return;
    }

    m_shareUrl = QJsonDocument::fromJson(body)
                     .object()
                     .value(QStringLiteral("link"))
                     .toObject()
                     .value(QStringLiteral("webUrl"))
                     .toString();

    if (m_shareUrl.isEmpty()) {
        emit uploadFailed(tr("File uploaded to OneDrive but the share link response was empty."));
        return;
    }

    qDebug() << "OneDriveUploader: share link:" << m_shareUrl;
    emit uploadFinished(m_shareUrl);
}
