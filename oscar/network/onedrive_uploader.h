/* OneDrive Uploader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Uploads .oscar files to the authenticated user's OneDrive and creates
 * a publicly-readable share link.  Uses OAuth2Handler for authentication
 * (Authorization Code + PKCE flow, as required by the Microsoft Identity
 * Platform for native/desktop applications).
 *
 * Upload sequence:
 *   1. Create an upload session (POST to the Graph API createUploadSession endpoint).
 *   2. Upload the file body (PUT to the session URL returned in step 1).
 *   3. Create an anonymous share link for the uploaded file.
 *   4. Emit uploadFinished with the shareable OneDrive URL.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ONEDRIVE_UPLOADER_H
#define ONEDRIVE_UPLOADER_H

#include <QObject>
#include <QString>
#include <QUrl>

class OAuth2Handler;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

/*!
 * \class OneDriveUploader
 * \brief Uploads an .oscar file to the user's OneDrive and creates a share link.
 *
 * Handles OAuth2 authentication (via OAuth2Handler), upload session creation,
 * file upload to the Microsoft Graph API, anonymous link creation, and returns
 * a shareable URL.  The file is stored in an "OSCAR Shared Profiles" folder
 * in the user's own OneDrive.
 *
 * Usage:
 * \code
 *   OneDriveUploader uploader;
 *   uploader.setFilePath("/path/to/share.oscar");
 *   connect(&uploader, &OneDriveUploader::uploadFinished, ...);
 *   uploader.startUpload();  // Authenticates first if needed.
 * \endcode
 */
class OneDriveUploader : public QObject
{
    Q_OBJECT

public:
    explicit OneDriveUploader(QObject* parent = nullptr);
    ~OneDriveUploader() override;

    /// Set the local file path to upload.
    void setFilePath(const QString& path);

    /// Start the upload.  Authenticates first if no valid token.
    void startUpload();

    /// Start authentication only (without uploading).
    void authenticate();

    /// Whether the user is currently authenticated with OneDrive.
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
    void onUploadSessionReplyFinished();
    void onUploadReplyFinished();
    void onShareLinkReplyFinished();

private:
    void createUploadSession();
    void doUpload(const QUrl& uploadUrl);
    void createShareLink(const QString& fileId);

    static constexpr const char* PROVIDER_KEY = "OneDrive";

    // ---------------------------------------------------------------------------
    // OAuth2 credentials — Public client registered in the Azure portal.
    // Replace this placeholder with the real Application (client) ID before building a release.
    // To obtain credentials:
    //   1. Go to portal.azure.com → Azure Active Directory → App registrations → New registration.
    //   2. Set "Supported account types" to "Accounts in any organizational directory
    //      and personal Microsoft accounts".
    //   3. Under "Redirect URIs", add: http://localhost:17178/callback  (type: Public client/native).
    //   4. Under API Permissions → Add a permission → Microsoft Graph → Delegated →
    //      Files.ReadWrite and offline_access.
    //   5. Copy the Application (client) ID from the Overview page and paste it below.
    // No client secret is needed; PKCE is used instead.
    // ---------------------------------------------------------------------------
    static constexpr const char* OD_CLIENT_ID = "YOUR_AZURE_CLIENT_ID_HERE";

    OAuth2Handler*          m_oauth         = nullptr;
    QNetworkAccessManager*  m_nam           = nullptr;
    QNetworkReply*          m_reply         = nullptr;
    QFile*                  m_file          = nullptr;
    QString                 m_filePath;
    QString                 m_shareUrl;
    bool                    m_pendingUpload = false;
    bool                    m_aborted       = false;
};

#endif // ONEDRIVE_UPLOADER_H
