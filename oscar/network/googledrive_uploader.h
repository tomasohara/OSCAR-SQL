/* Google Drive Uploader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Uploads .oscar files to the authenticated user's Google Drive and creates
 * a publicly-readable share link.  Uses OAuth2Handler for authentication
 * (Authorization Code + PKCE flow with client_secret, as required by Google
 * for native/desktop applications).
 *
 * Upload sequence:
 *   1. Initiate a resumable upload session (POST to /upload/drive/v3/files).
 *   2. Upload the file body (PUT to the session URI returned in step 1).
 *   3. Create an "anyone / reader" permission on the uploaded file.
 *   4. Emit uploadFinished with the shareable Drive URL.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GOOGLEDRIVE_UPLOADER_H
#define GOOGLEDRIVE_UPLOADER_H

#include <QObject>
#include <QString>
#include <QUrl>

class OAuth2Handler;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

/*!
 * \class GoogleDriveUploader
 * \brief Uploads an .oscar file to the user's Google Drive and creates a share link.
 *
 * Handles OAuth2 authentication (via OAuth2Handler), resumable file upload to
 * the Google Drive API, public permission creation, and returns a shareable URL.
 * The file is stored in the user's own Drive and counts against their quota.
 *
 * Usage:
 * \code
 *   GoogleDriveUploader uploader;
 *   uploader.setFilePath("/path/to/share.oscar");
 *   connect(&uploader, &GoogleDriveUploader::uploadFinished, ...);
 *   uploader.startUpload();  // Authenticates first if needed.
 * \endcode
 */
class GoogleDriveUploader : public QObject
{
    Q_OBJECT

public:
    explicit GoogleDriveUploader(QObject* parent = nullptr);
    ~GoogleDriveUploader() override;

    /// Set the local file path to upload.
    void setFilePath(const QString& path);

    /// Start the upload.  Authenticates first if no valid token.
    void startUpload();

    /// Start authentication only (without uploading).
    void authenticate();

    /// Whether the user is currently authenticated with Google.
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

    /// Emitted when upload and permission creation complete.
    void uploadFinished(const QString& shareUrl);

    /// Emitted on any failure.
    void uploadFailed(const QString& errorMessage);

private slots:
    void onAuthenticated(const QString& accessToken);
    void onAuthFailed(const QString& error);
    void onInitiateReplyFinished();
    void onUploadReplyFinished();
    void onPermissionReplyFinished();

private:
    void doUpload();
    void uploadToSession(const QUrl& sessionUri);
    void createPermission(const QString& fileId);

    static constexpr const char* PROVIDER_KEY = "GoogleDrive";

    // ---------------------------------------------------------------------------
    // OAuth2 credentials — Desktop app client created in Google Cloud Console.
    // Replace these placeholders with the real values before building a release.
    // To obtain credentials:
    //   1. Go to console.cloud.google.com and create or select a project.
    //   2. Enable the Google Drive API.
    //   3. Create an OAuth 2.0 Client ID with application type "Desktop app".
    //   4. Copy the Client ID and Client Secret shown in the console.
    // ---------------------------------------------------------------------------
    static constexpr const char* GD_CLIENT_ID     = "387488760965-24e178j8nt9viep2emrc1j6qndge5svn.apps.googleusercontent.com";
    static constexpr const char* GD_CLIENT_SECRET = "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";

    OAuth2Handler*          m_oauth         = nullptr;
    QNetworkAccessManager*  m_nam           = nullptr;
    QNetworkReply*          m_reply         = nullptr;
    QFile*                  m_file          = nullptr;
    QString                 m_filePath;
    QString                 m_shareUrl;
    bool                    m_pendingUpload = false;
    bool                    m_aborted       = false;
};

#endif // GOOGLEDRIVE_UPLOADER_H
