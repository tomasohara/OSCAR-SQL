/* Dropbox Uploader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Uploads .oscar files to Dropbox via the Dropbox API and creates
 * a shared link.  Uses OAuth2Handler for authentication.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DROPBOX_UPLOADER_H
#define DROPBOX_UPLOADER_H

#include <QObject>
#include <QString>

class OAuth2Handler;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

/*!
 * \class DropboxUploader
 * \brief Uploads an .oscar file to Dropbox and creates a shared link.
 *
 * Handles OAuth2 authentication (via OAuth2Handler), file upload to
 * the Dropbox content API, and shared link creation.  The resulting
 * URL can be shared with another user who can download the file.
 *
 * Usage:
 * \code
 *   DropboxUploader uploader;
 *   uploader.setFilePath("/path/to/share.oscar");
 *   connect(&uploader, &DropboxUploader::uploadFinished, ...);
 *   uploader.startUpload();  // Will authenticate first if needed.
 * \endcode
 */
class DropboxUploader : public QObject
{
    Q_OBJECT

public:
    explicit DropboxUploader(QObject* parent = nullptr);
    ~DropboxUploader() override;

    /// Set the local file path to upload.
    void setFilePath(const QString& path);

    /// Start the upload.  Authenticates first if no valid token.
    void startUpload();

    /// Start authentication only (without uploading).
    void authenticate();

    /// Whether the user is currently authenticated with Dropbox.
    bool isAuthenticated() const;

    /// Sign out: clear stored tokens.
    void signOut();

    /// Abort an in-progress upload.
    void abort();

    /// The share URL (valid after uploadFinished).
    QString shareUrl() const;

signals:
    /// Emitted when authentication completes (initial or refresh).
    void authComplete(bool success);

    /// Emitted periodically during upload.
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);

    /// Emitted when upload and link creation complete.
    void uploadFinished(const QString& shareUrl);

    /// Emitted on any failure.
    void uploadFailed(const QString& errorMessage);

private slots:
    void onAuthenticated(const QString& accessToken);
    void onAuthFailed(const QString& error);
    void onUploadReplyFinished();
    void onShareLinkReplyFinished();

private:
    void doUpload();
    void createShareLink(const QString& dropboxPath);

    static constexpr const char* PROVIDER_KEY   = "Dropbox";
    static constexpr const char* DROPBOX_APP_KEY = "zriwl29dto0v1e6";

    OAuth2Handler*          m_oauth     = nullptr;
    QNetworkAccessManager*  m_nam       = nullptr;
    QNetworkReply*          m_reply     = nullptr;
    QFile*                  m_file      = nullptr;
    QString                 m_filePath;
    QString                 m_shareUrl;
    bool                    m_pendingUpload = false;
    bool                    m_aborted       = false;
};

#endif // DROPBOX_UPLOADER_H
