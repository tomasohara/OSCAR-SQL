/* Dropbox Uploader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dropbox_uploader.h"
#include "oauth2_handler.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>

#include "version.h"

// Dropbox API endpoints.
static const QUrl AUTH_URL(QStringLiteral("https://www.dropbox.com/oauth2/authorize"));
static const QUrl TOKEN_URL(QStringLiteral("https://api.dropboxapi.com/oauth2/token"));
static const QUrl UPLOAD_URL(QStringLiteral("https://content.dropboxapi.com/2/files/upload"));
static const QUrl SHARE_URL(QStringLiteral("https://api.dropboxapi.com/2/sharing/create_shared_link_with_settings"));

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

DropboxUploader::DropboxUploader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    OAuth2Handler::Config config;
    config.authUrl  = AUTH_URL;
    config.tokenUrl = TOKEN_URL;
    config.clientId = QLatin1String(DROPBOX_APP_KEY);
    // Dropbox scopes for app folder access + sharing.
    config.scope = QStringLiteral("files.content.write sharing.write");
    // Dropbox-specific: request offline access (returns a refresh token).
    config.extraAuthParams[QStringLiteral("token_access_type")] = QStringLiteral("offline");

    m_oauth = new OAuth2Handler(config, this);
    m_oauth->loadTokens(QLatin1String(PROVIDER_KEY));

    connect(m_oauth, &OAuth2Handler::authenticated,
            this,    &DropboxUploader::onAuthenticated);
    connect(m_oauth, &OAuth2Handler::authFailed,
            this,    &DropboxUploader::onAuthFailed);
}

DropboxUploader::~DropboxUploader()
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

void DropboxUploader::setFilePath(const QString& path)
{
    m_filePath = path;
}

bool DropboxUploader::isAuthenticated() const
{
    return m_oauth->hasValidToken() || m_oauth->hasRefreshToken();
}

void DropboxUploader::authenticate()
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

void DropboxUploader::signOut()
{
    m_oauth->clearTokens(QLatin1String(PROVIDER_KEY));
}

QString DropboxUploader::shareUrl() const
{
    return m_shareUrl;
}

void DropboxUploader::startUpload()
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

void DropboxUploader::abort()
{
    m_aborted = true;
    if (m_reply) {
        m_reply->abort();
    }
}

// ---------------------------------------------------------------------------
//  Auth callbacks
// ---------------------------------------------------------------------------

void DropboxUploader::onAuthenticated(const QString& accessToken)
{
    Q_UNUSED(accessToken);
    m_oauth->saveTokens(QLatin1String(PROVIDER_KEY));
    emit authComplete(true);

    if (m_pendingUpload) {
        m_pendingUpload = false;
        doUpload();
    }
}

void DropboxUploader::onAuthFailed(const QString& error)
{
    m_pendingUpload = false;
    emit authComplete(false);
    emit uploadFailed(tr("Dropbox authentication failed: %1").arg(error));
}

// ---------------------------------------------------------------------------
//  Upload
// ---------------------------------------------------------------------------

void DropboxUploader::doUpload()
{
    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        emit uploadFailed(tr("File does not exist: %1").arg(m_filePath));
        return;
    }

    // Dropbox has a 150 MB limit for simple upload; larger files need
    // upload sessions.  Most .oscar files will be well under this.
    static constexpr qint64 MAX_SIMPLE_UPLOAD = 150LL * 1024 * 1024;
    if (fi.size() > MAX_SIMPLE_UPLOAD) {
        emit uploadFailed(tr("File is too large for Dropbox simple upload (%1 MB). "
                             "Maximum is 150 MB.")
                              .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
        return;
    }

    m_file = new QFile(m_filePath);
    if (!m_file->open(QIODevice::ReadOnly)) {
        emit uploadFailed(tr("Could not open file for reading:\n%1")
                              .arg(m_file->errorString()));
        delete m_file;
        m_file = nullptr;
        return;
    }

    // Dropbox upload API: file content in request body,
    // API parameters in the Dropbox-API-Arg header as JSON.
    QString dropboxPath = QStringLiteral("/%1").arg(fi.fileName());

    QJsonObject apiArg;
    apiArg[QStringLiteral("path")] = dropboxPath;
    apiArg[QStringLiteral("mode")] = QStringLiteral("add");        // Don't overwrite.
    apiArg[QStringLiteral("autorename")] = true;                   // Rename if exists.
    QByteArray apiArgJson = QJsonDocument(apiArg).toJson(QJsonDocument::Compact);

    QNetworkRequest request(UPLOAD_URL);
    request.setRawHeader("Authorization",
        QStringLiteral("Bearer %1").arg(m_oauth->accessToken()).toUtf8());
    request.setRawHeader("Dropbox-API-Arg", apiArgJson);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/octet-stream"));
    request.setHeader(QNetworkRequest::ContentLengthHeader, fi.size());

    qDebug() << "DropboxUploader: uploading" << m_filePath << "to" << dropboxPath;

    m_reply = m_nam->post(request, m_file);

    connect(m_reply, &QNetworkReply::uploadProgress,
            this,    &DropboxUploader::uploadProgress);
    connect(m_reply, &QNetworkReply::finished,
            this,    &DropboxUploader::onUploadReplyFinished);
}

void DropboxUploader::cleanupReply()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    delete m_file;
    m_file = nullptr;
}

void DropboxUploader::onUploadReplyFinished()
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
        QString errorMsg;
        if (httpStatus == 401) {
            errorMsg = tr("Dropbox authentication expired. Please sign in again.");
            m_oauth->clearTokens(QLatin1String(PROVIDER_KEY));
        } else {
            errorMsg = tr("Dropbox upload failed (HTTP %1): %2")
                           .arg(httpStatus).arg(m_reply->errorString());
        }
        qDebug() << "DropboxUploader: upload failed:" << httpStatus << body;
        cleanupReply();
        emit uploadFailed(errorMsg);
        return;
    }

    cleanupReply();

    // Parse the response to get the actual path (may be auto-renamed).
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString dropboxPath = doc.object().value(QStringLiteral("path_display")).toString();

    qDebug() << "DropboxUploader: file uploaded to" << dropboxPath;

    if (dropboxPath.isEmpty()) {
        emit uploadFailed(tr("Upload succeeded but Dropbox did not return a file path."));
        return;
    }

    // Now create a shared link.
    createShareLink(dropboxPath);
}

// ---------------------------------------------------------------------------
//  Share link creation
// ---------------------------------------------------------------------------

void DropboxUploader::createShareLink(const QString& dropboxPath)
{
    QJsonObject body;
    body[QStringLiteral("path")] = dropboxPath;

    QJsonObject settings;
    settings[QStringLiteral("requested_visibility")] = QStringLiteral("public");
    body[QStringLiteral("settings")] = settings;

    QNetworkRequest request(SHARE_URL);
    request.setRawHeader("Authorization",
        QStringLiteral("Bearer %1").arg(m_oauth->accessToken()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json"));

    QByteArray jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);

    m_reply = m_nam->post(request, jsonBody);
    connect(m_reply, &QNetworkReply::finished,
            this,    &DropboxUploader::onShareLinkReplyFinished);
}

void DropboxUploader::onShareLinkReplyFinished()
{
    if (!m_reply) return;

    QByteArray body = m_reply->readAll();
    int httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Dropbox returns 409 Conflict if a shared link already exists.
    // The error body contains the existing link in shared_link_already_exists.
    if (httpStatus == 409) {
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonObject err = doc.object().value(QStringLiteral("error")).toObject();
        QJsonObject existing = err.value(QStringLiteral("shared_link_already_exists")).toObject();
        QJsonObject metadata = existing.value(QStringLiteral("metadata")).toObject();
        m_shareUrl = metadata.value(QStringLiteral("url")).toString();

        if (!m_shareUrl.isEmpty()) {
            qDebug() << "DropboxUploader: reusing existing shared link:" << m_shareUrl;
            cleanupReply();
            emit uploadFinished(m_shareUrl);
            return;
        }
    }

    if (m_reply->error() != QNetworkReply::NoError) {
        qDebug() << "DropboxUploader: share link creation failed:" << httpStatus << body;
        cleanupReply();
        emit uploadFailed(tr("File uploaded to Dropbox but could not create a shared link (HTTP %1).")
                              .arg(httpStatus));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(body);
    m_shareUrl = doc.object().value(QStringLiteral("url")).toString();

    cleanupReply();

    if (m_shareUrl.isEmpty()) {
        emit uploadFailed(tr("File uploaded but Dropbox did not return a share link."));
        return;
    }

    qDebug() << "DropboxUploader: shared link created:" << m_shareUrl;
    emit uploadFinished(m_shareUrl);
}
