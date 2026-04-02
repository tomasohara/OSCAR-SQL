/* Cloud Uploader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Uploads .oscar files to a cloud hosting service (currently 0x0.st)
 * and returns a shareable URL.  Captures the delete token so the upload
 * can be removed later.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CLOUD_UPLOADER_H
#define CLOUD_UPLOADER_H

#include <QObject>
#include <QString>

class QFile;
class QHttpMultiPart;
class QNetworkAccessManager;
class QNetworkReply;

/*!
 * \class CloudUploader
 * \brief Uploads an .oscar file to 0x0.st and returns a share URL.
 *
 * Performs an asynchronous HTTP POST of the file as multipart form data.
 * On success, the returned URL can be shared with another user.
 * The X-Token header is captured so the upload can be deleted later.
 *
 * Usage:
 * \code
 *   CloudUploader uploader;
 *   uploader.setFilePath("/path/to/share.oscar");
 *   connect(&uploader, &CloudUploader::uploadFinished, ...);
 *   connect(&uploader, &CloudUploader::uploadFailed, ...);
 *   uploader.start();
 * \endcode
 */
class CloudUploader : public QObject
{
    Q_OBJECT

public:
    explicit CloudUploader(QObject* parent = nullptr);
    ~CloudUploader() override;

    /// Set the local file path to upload.
    void setFilePath(const QString& path);

    /// Begin the asynchronous upload.
    void start();

    /// Abort an in-progress upload.
    void abort();

    /// The share URL returned by the service (valid after uploadFinished).
    QString shareUrl() const;

    /// The delete token returned by the service (valid after uploadFinished).
    QString deleteToken() const;

    /// Delete a previously uploaded file using a saved URL and token.
    /// Emits deleteFinished or deleteFailed.
    void deleteUpload(const QString& fileUrl, const QString& token);

signals:
    /// Emitted periodically during upload.
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);

    /// Emitted when upload completes successfully.
    void uploadFinished(const QString& shareUrl);

    /// Emitted on upload failure.
    void uploadFailed(const QString& errorMessage);

    /// Emitted when a delete request completes successfully.
    void deleteFinished();

    /// Emitted when a delete request fails.
    void deleteFailed(const QString& errorMessage);

private slots:
    void onUploadReplyFinished();
    void onDeleteReplyFinished();

private:
    void cleanupReply();

    QNetworkAccessManager* m_nam       = nullptr;
    QNetworkReply*         m_reply     = nullptr;
    QHttpMultiPart*        m_multiPart = nullptr;
    QFile*                 m_file      = nullptr;
    QString                m_filePath;
    QString                m_shareUrl;
    QString                m_deleteToken;
    bool                   m_aborted   = false;
};

#endif // CLOUD_UPLOADER_H
