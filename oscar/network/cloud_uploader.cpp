/* Cloud Uploader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "cloud_uploader.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>

#include "version.h"

static const QUrl UPLOAD_ENDPOINT(QStringLiteral("https://0x0.st"));

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

CloudUploader::CloudUploader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

CloudUploader::~CloudUploader()
{
    cleanupReply();
    delete m_file;
}

// ---------------------------------------------------------------------------
//  Public interface
// ---------------------------------------------------------------------------

void CloudUploader::setFilePath(const QString& path)
{
    m_filePath = path;
}

QString CloudUploader::shareUrl() const
{
    return m_shareUrl;
}

QString CloudUploader::deleteToken() const
{
    return m_deleteToken;
}

void CloudUploader::start()
{
    m_aborted = false;
    m_shareUrl.clear();
    m_deleteToken.clear();

    if (m_filePath.isEmpty()) {
        emit uploadFailed(tr("No file specified for upload."));
        return;
    }

    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        emit uploadFailed(tr("File does not exist: %1").arg(m_filePath));
        return;
    }

    // 0x0.st limit is 512 MiB.
    static constexpr qint64 MAX_SIZE = 512LL * 1024 * 1024;
    if (fi.size() > MAX_SIZE) {
        emit uploadFailed(tr("File is too large for upload (%1 MB). "
                             "The maximum is 512 MB.")
                              .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
        return;
    }

    // Open the file for reading — QHttpMultiPart takes ownership of the
    // QIODevice but we also keep a pointer so we can close it on cleanup.
    m_file = new QFile(m_filePath);
    if (!m_file->open(QIODevice::ReadOnly)) {
        emit uploadFailed(tr("Could not open file for reading:\n%1")
                              .arg(m_file->errorString()));
        delete m_file;
        m_file = nullptr;
        return;
    }

    // Build the multipart form data.
    m_multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                     .arg(fi.fileName())));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
        QVariant(QStringLiteral("application/octet-stream")));
    filePart.setBodyDevice(m_file);
    // m_file is now owned by m_multiPart for reading purposes,
    // but we still track the pointer for cleanup.

    m_multiPart->append(filePart);

    // Set retention to 30 days.
    QHttpPart expiresPart;
    expiresPart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"expires\"")));
    expiresPart.setBody(QByteArrayLiteral("720"));  // 30 days in hours
    m_multiPart->append(expiresPart);

    // Build the request.
    QNetworkRequest request(UPLOAD_ENDPOINT);
    QString userAgent = QStringLiteral("OSCAR/%1").arg(getVersion().toString());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);

    qDebug() << "CloudUploader: uploading" << m_filePath
             << "(" << fi.size() << "bytes) to" << UPLOAD_ENDPOINT.toString();

    m_reply = m_nam->post(request, m_multiPart);
    // m_multiPart is deleted when the reply finishes.
    m_multiPart->setParent(m_reply);
    m_multiPart = nullptr;

    connect(m_reply, &QNetworkReply::uploadProgress,
            this,    &CloudUploader::uploadProgress);
    connect(m_reply, &QNetworkReply::finished,
            this,    &CloudUploader::onUploadReplyFinished);
}

void CloudUploader::abort()
{
    m_aborted = true;
    if (m_reply) {
        m_reply->abort();
    }
}

// ---------------------------------------------------------------------------
//  Delete a previously uploaded file
// ---------------------------------------------------------------------------

void CloudUploader::deleteUpload(const QString& fileUrl, const QString& token)
{
    if (fileUrl.isEmpty() || token.isEmpty()) {
        emit deleteFailed(tr("Missing URL or delete token."));
        return;
    }

    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart tokenPart;
    tokenPart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"token\"")));
    tokenPart.setBody(token.toUtf8());
    multiPart->append(tokenPart);

    QHttpPart deletePart;
    deletePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"delete\"")));
    deletePart.setBody(QByteArray());
    multiPart->append(deletePart);

    QUrl deleteUrl(fileUrl);
    QNetworkRequest request(deleteUrl);
    QString userAgent = QStringLiteral("OSCAR/%1").arg(getVersion().toString());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);

    QNetworkReply* reply = m_nam->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished,
            this,  &CloudUploader::onDeleteReplyFinished);
}

// ---------------------------------------------------------------------------
//  Private slots
// ---------------------------------------------------------------------------

void CloudUploader::onUploadReplyFinished()
{
    if (m_aborted) {
        cleanupReply();
        emit uploadFailed(tr("Upload was cancelled."));
        return;
    }

    if (!m_reply) return;

    if (m_reply->error() != QNetworkReply::NoError) {
        int httpStatus = m_reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg;
        if (httpStatus == 429) {
            errorMsg = tr("Upload rejected: too many requests. Please wait a moment and try again.");
        } else if (httpStatus == 503) {
            errorMsg = tr("The upload service (0x0.st) is temporarily unavailable. Please try again later.");
        } else if (httpStatus > 0) {
            errorMsg = tr("Upload failed (HTTP %1): %2")
                           .arg(httpStatus)
                           .arg(m_reply->errorString());
        } else {
            errorMsg = tr("Upload failed: %1").arg(m_reply->errorString());
        }
        cleanupReply();
        emit uploadFailed(errorMsg);
        return;
    }

    // Read the share URL from the response body.
    m_shareUrl = QString::fromUtf8(m_reply->readAll()).trimmed();

    // Read the delete token from the X-Token header.
    if (m_reply->hasRawHeader("X-Token")) {
        m_deleteToken = QString::fromUtf8(m_reply->rawHeader("X-Token")).trimmed();
    }

    qDebug() << "CloudUploader: upload complete, URL =" << m_shareUrl
             << "token =" << (m_deleteToken.isEmpty() ? "(none)" : "(received)");

    cleanupReply();

    if (m_shareUrl.isEmpty()) {
        emit uploadFailed(tr("Upload succeeded but no URL was returned."));
        return;
    }

    emit uploadFinished(m_shareUrl);
}

void CloudUploader::onDeleteReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg = tr("Delete failed (HTTP %1): %2")
                               .arg(httpStatus)
                               .arg(reply->errorString());
        reply->deleteLater();
        emit deleteFailed(errorMsg);
        return;
    }

    reply->deleteLater();
    emit deleteFinished();
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void CloudUploader::cleanupReply()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    // Close the file if still open.
    if (m_file) {
        if (m_file->isOpen()) m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}
