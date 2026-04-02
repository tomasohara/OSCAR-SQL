/* Cloud Downloader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Downloads .oscar backup files from cloud service share URLs.
 * Recognizes share-link patterns for supported cloud services,
 * transforms them into direct-download URLs, and streams the file
 * to a temporary location on disk.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CLOUD_DOWNLOADER_H
#define CLOUD_DOWNLOADER_H

#include <QObject>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

/// Identifies which cloud service (if any) a URL belongs to.
enum class CloudProvider {
    Unknown,        ///< Unrecognized domain.
    Dropbox,        ///< www.dropbox.com or dropbox.com
    GoogleDrive,    ///< drive.google.com
    OneDrive,       ///< 1drv.ms or *.sharepoint.com
    ProtonDrive,    ///< drive.proton.me (not supported for download)
    ZeroX0,         ///< 0x0.st
    Box,            ///< app.box.com
    DirectLink      ///< Any URL ending in .oscar (assumed direct download)
};

/*!
 * \class CloudDownloader
 * \brief Downloads an .oscar file from a cloud share URL.
 *
 * Recognizes share-link patterns for Dropbox, Google Drive, OneDrive,
 * Box, and 0x0.st, transforms them into direct-download URLs, and
 * streams the response to a temporary file on disk.  Emits progress
 * signals suitable for driving a QProgressBar.
 *
 * Usage:
 * \code
 *   CloudDownloader dl;
 *   dl.setUrl(url);
 *   connect(&dl, &CloudDownloader::downloadProgress, ...);
 *   connect(&dl, &CloudDownloader::downloadFinished, ...);
 *   connect(&dl, &CloudDownloader::downloadFailed, ...);
 *   dl.start();
 *   // On success: dl.localFilePath() returns path to the temp .oscar file.
 * \endcode
 */
class CloudDownloader : public QObject
{
    Q_OBJECT

public:
    explicit CloudDownloader(QObject* parent = nullptr);
    ~CloudDownloader() override;

    /// Set the share URL to download from.
    void setUrl(const QUrl& url);

    /// Identify the cloud provider from a URL (static, usable for UI feedback).
    static CloudProvider identifyProvider(const QUrl& url);

    /// Human-readable name for a provider (e.g., "Dropbox", "Google Drive").
    static QString providerName(CloudProvider provider);

    /// Returns true if this provider's URLs can be direct-downloaded by OSCAR.
    static bool isProviderSupported(CloudProvider provider);

    /// Begin the asynchronous download.  Emits downloadFinished or downloadFailed.
    void start();

    /// Abort an in-progress download.
    void abort();

    /// Path to the downloaded .oscar file (valid after downloadFinished).
    QString localFilePath() const;

signals:
    /// Emitted periodically during download.
    /// \a bytesTotal may be -1 if the server did not send Content-Length.
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

    /// Emitted when the download completes successfully.
    void downloadFinished(const QString& localPath);

    /// Emitted on any failure (network error, unsupported provider, etc.).
    void downloadFailed(const QString& errorMessage);

private slots:
    void onReadyRead();
    void onReplyFinished();

private:
    /// Transform a share URL into a direct-download URL for the given provider.
    QUrl transformUrl(const QUrl& shareUrl, CloudProvider provider);

    /// Start the actual HTTP GET request to \a url.
    void startRequest(const QUrl& url);

    /// Clean up the current reply (if any) without deleting the temp file.
    void cleanupReply();

    QNetworkAccessManager* m_nam       = nullptr;
    QNetworkReply*         m_reply     = nullptr;
    QFile*                 m_tempFile  = nullptr;
    QUrl                   m_originalUrl;
    CloudProvider          m_provider  = CloudProvider::Unknown;
    QString                m_localPath;
    bool                   m_aborted   = false;
};

#endif // CLOUD_DOWNLOADER_H
