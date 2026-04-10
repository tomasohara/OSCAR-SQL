/* Cloud Downloader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "cloud_downloader.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

#include "version.h"

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

CloudDownloader::CloudDownloader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

CloudDownloader::~CloudDownloader()
{
    cleanupReply();

    // Clean up the temp file if it was never consumed.
    if (m_tempFile) {
        m_tempFile->remove();
        delete m_tempFile;
        m_tempFile = nullptr;
    }
}

// ---------------------------------------------------------------------------
//  Public interface
// ---------------------------------------------------------------------------

void CloudDownloader::setUrl(const QUrl& url)
{
    m_originalUrl = url;
    m_provider = identifyProvider(url);
}

void CloudDownloader::start()
{
    m_aborted = false;

    if (!m_originalUrl.isValid()) {
        emit downloadFailed(tr("Invalid URL."));
        return;
    }

    // Require HTTPS (with exception for localhost, for testing).
    if (m_originalUrl.scheme() != QStringLiteral("https")
        && m_originalUrl.host() != QStringLiteral("localhost")
        && m_originalUrl.host() != QStringLiteral("127.0.0.1")) {
        emit downloadFailed(tr("Only HTTPS URLs are supported for security reasons."));
        return;
    }

    if (!isProviderSupported(m_provider)) {
        QString msg;
        if (m_provider == CloudProvider::ProtonDrive) {
            msg = tr("Proton Drive share links cannot be downloaded directly because "
                     "files are end-to-end encrypted and require browser-based decryption.\n\n"
                     "Please download the file in your browser and use the Local File option.");
        } else {
            msg = tr("OSCAR does not recognize this URL as a supported cloud service.\n\n"
                     "Please download the file in your browser and use the Local File option.\n\n"
                     "Supported services: Dropbox, Google Drive, OneDrive, Box, 0x0.st, "
                     "or any direct link to a .oscar file.");
        }
        emit downloadFailed(msg);
        return;
    }

    QUrl downloadUrl = transformUrl(m_originalUrl, m_provider);
    if (!downloadUrl.isValid()) {
        emit downloadFailed(tr("Could not determine a download URL from the share link."));
        return;
    }

    qDebug() << "CloudDownloader: provider =" << providerName(m_provider)
             << "download URL =" << downloadUrl.toString();

    // Create a uniquely-named temp file in the system temp directory.
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempFile = new QTemporaryFile(
        tempDir + QStringLiteral("/oscar_download_XXXXXX.oscar"), this);
    m_tempFile->setAutoRemove(false);  // File must outlive this object after success.
    if (!m_tempFile->open()) {
        emit downloadFailed(tr("Could not create temporary file:\n%1")
                                .arg(m_tempFile->errorString()));
        delete m_tempFile;
        m_tempFile = nullptr;
        return;
    }
    m_localPath = m_tempFile->fileName();

    startRequest(downloadUrl);
}

void CloudDownloader::abort()
{
    m_aborted = true;
    if (m_reply) {
        m_reply->abort();
    }
}

QString CloudDownloader::localFilePath() const
{
    return m_localPath;
}

// ---------------------------------------------------------------------------
//  Provider identification
// ---------------------------------------------------------------------------

CloudProvider CloudDownloader::identifyProvider(const QUrl& url)
{
    if (!url.isValid()) return CloudProvider::Unknown;

    const QString host = url.host().toLower();

    if (host == QStringLiteral("www.dropbox.com")
        || host == QStringLiteral("dropbox.com")
        || host == QStringLiteral("dl.dropboxusercontent.com")) {
        return CloudProvider::Dropbox;
    }

    if (host == QStringLiteral("drive.google.com")
        || host == QStringLiteral("docs.google.com")) {
        return CloudProvider::GoogleDrive;
    }

    if (host == QStringLiteral("1drv.ms")
        || host.endsWith(QStringLiteral(".sharepoint.com"))
        || host.endsWith(QStringLiteral(".live.com"))) {
        return CloudProvider::OneDrive;
    }

    if (host == QStringLiteral("drive.proton.me")) {
        return CloudProvider::ProtonDrive;
    }

    if (host == QStringLiteral("0x0.st")) {
        return CloudProvider::ZeroX0;
    }

    if (host == QStringLiteral("app.box.com")
        || host == QStringLiteral("www.box.com")) {
        return CloudProvider::Box;
    }

    // Check for a direct link to a .oscar file.
    if (url.path().endsWith(QStringLiteral(".oscar"), Qt::CaseInsensitive)) {
        return CloudProvider::DirectLink;
    }

    return CloudProvider::Unknown;
}

QString CloudDownloader::providerName(CloudProvider provider)
{
    switch (provider) {
    case CloudProvider::Dropbox:      return QStringLiteral("Dropbox");
    case CloudProvider::GoogleDrive:  return QStringLiteral("Google Drive");
    case CloudProvider::OneDrive:     return QStringLiteral("OneDrive");
    case CloudProvider::ProtonDrive:  return QStringLiteral("Proton Drive");
    case CloudProvider::ZeroX0:       return QStringLiteral("0x0.st");
    case CloudProvider::Box:          return QStringLiteral("Box");
    case CloudProvider::DirectLink:   return QStringLiteral("Direct Link");
    case CloudProvider::Unknown:      return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

bool CloudDownloader::isProviderSupported(CloudProvider provider)
{
    switch (provider) {
    case CloudProvider::Dropbox:
    case CloudProvider::GoogleDrive:
    case CloudProvider::OneDrive:
    case CloudProvider::ZeroX0:
    case CloudProvider::Box:
    case CloudProvider::DirectLink:
        return true;
    case CloudProvider::ProtonDrive:
    case CloudProvider::Unknown:
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  URL transformation
// ---------------------------------------------------------------------------

QUrl CloudDownloader::transformUrl(const QUrl& shareUrl, CloudProvider provider)
{
    switch (provider) {

    case CloudProvider::Dropbox: {
        // Dropbox share links: https://www.dropbox.com/s{l}/HASH/filename?dl=0
        // Change dl=0 to dl=1 for direct download.  If no dl param, append it.
        QUrl url(shareUrl);
        QUrlQuery query(url);
        query.removeQueryItem(QStringLiteral("dl"));
        query.addQueryItem(QStringLiteral("dl"), QStringLiteral("1"));
        url.setQuery(query);
        return url;
    }

    case CloudProvider::GoogleDrive: {
        // Google Drive share links:
        //   https://drive.google.com/file/d/FILE_ID/view?usp=sharing
        //   https://drive.google.com/open?id=FILE_ID
        // Direct download:
        //   https://drive.google.com/uc?export=download&id=FILE_ID
        QString path = shareUrl.path();

        // Extract FILE_ID from /file/d/FILE_ID/... pattern.
        static QRegularExpression fileIdRe(QStringLiteral("/file/d/([^/]+)"));
        QRegularExpressionMatch match = fileIdRe.match(path);

        QString fileId;
        if (match.hasMatch()) {
            fileId = match.captured(1);
        } else {
            // Try ?id=FILE_ID query parameter.
            QUrlQuery query(shareUrl);
            fileId = query.queryItemValue(QStringLiteral("id"));
        }

        if (fileId.isEmpty()) {
            return QUrl();  // Could not extract file ID.
        }

        QUrl url(QStringLiteral("https://drive.google.com/uc"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("export"), QStringLiteral("download"));
        query.addQueryItem(QStringLiteral("id"), fileId);
        url.setQuery(query);
        return url;
    }

    case CloudProvider::OneDrive: {
        // OneDrive share links: https://1drv.ms/u/s!ENCODED_TOKEN
        // Microsoft's algorithm: base64-encode the share URL, prepend "u!",
        // replace / with _ and + with -, trim trailing =.
        // Then: https://api.onedrive.com/v1.0/shares/u!{encoded}/root/content
        QString shareUrlStr = shareUrl.toString();
        QByteArray encoded = shareUrlStr.toUtf8().toBase64();
        QString token = QString::fromLatin1(encoded);

        // Make URL-safe: replace / with _, + with -, remove trailing =.
        token.replace(QLatin1Char('/'), QLatin1Char('_'));
        token.replace(QLatin1Char('+'), QLatin1Char('-'));
        while (token.endsWith(QLatin1Char('='))) {
            token.chop(1);
        }

        // Prepend "u!" marker.
        token = QStringLiteral("u!") + token;

        QUrl url(QStringLiteral("https://api.onedrive.com/v1.0/shares/%1/root/content")
                     .arg(token));
        return url;
    }

    case CloudProvider::Box: {
        // Box share links: https://app.box.com/s/HASH
        // Shared download link: append /download to the path.
        // The server will redirect to the actual download CDN URL.
        QUrl url(shareUrl);
        QString path = url.path();
        if (!path.endsWith(QStringLiteral("/download"))) {
            if (!path.endsWith(QLatin1Char('/')))
                path.append(QLatin1Char('/'));
            path.append(QStringLiteral("download"));
        }
        url.setPath(path);
        return url;
    }

    case CloudProvider::ZeroX0:
    case CloudProvider::DirectLink:
        // Already a direct download link.
        return shareUrl;

    case CloudProvider::ProtonDrive:
    case CloudProvider::Unknown:
        return QUrl();
    }

    return QUrl();
}

// ---------------------------------------------------------------------------
//  Network request
// ---------------------------------------------------------------------------

void CloudDownloader::startRequest(const QUrl& url)
{
    QNetworkRequest request(url);

    // Set a User-Agent so cloud services don't reject the request.
    QString userAgent = QStringLiteral("OSCAR/%1").arg(getVersion().toString());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);

    // Follow redirects automatically (cloud services redirect frequently).
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::readyRead,
            this,    &CloudDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished,
            this,    &CloudDownloader::onReplyFinished);
    connect(m_reply, &QNetworkReply::downloadProgress,
            this,    &CloudDownloader::downloadProgress);
}

void CloudDownloader::cleanupReply()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------

void CloudDownloader::onReadyRead()
{
    // Stream data to the temp file as it arrives, keeping memory usage low.
    if (m_tempFile && m_reply) {
        m_tempFile->write(m_reply->readAll());
    }
}

void CloudDownloader::onReplyFinished()
{
    if (m_aborted) {
        cleanupReply();
        if (m_tempFile) {
            m_tempFile->close();
            m_tempFile->remove();
            delete m_tempFile;
            m_tempFile = nullptr;
        }
        emit downloadFailed(tr("Download was cancelled."));
        return;
    }

    if (!m_reply) return;

    // Check for HTTP errors.
    if (m_reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("Download failed: %1").arg(m_reply->errorString());

        int httpStatus = m_reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus > 0) {
            errorMsg = tr("Download failed (HTTP %1): %2")
                           .arg(httpStatus)
                           .arg(m_reply->errorString());
        }

        cleanupReply();
        if (m_tempFile) {
            m_tempFile->close();
            m_tempFile->remove();
            delete m_tempFile;
            m_tempFile = nullptr;
        }
        emit downloadFailed(errorMsg);
        return;
    }

    // Flush any remaining data.
    if (m_tempFile && m_reply->bytesAvailable() > 0) {
        m_tempFile->write(m_reply->readAll());
    }

    // Close the temp file.
    if (m_tempFile) {
        m_tempFile->flush();
        m_tempFile->close();
    }

    // Verify we got something.
    QFileInfo fi(m_localPath);
    if (!fi.exists() || fi.size() == 0) {
        if (m_tempFile) {
            m_tempFile->remove();
            delete m_tempFile;
            m_tempFile = nullptr;
        }
        cleanupReply();
        emit downloadFailed(tr("Downloaded file is empty. The share link may have "
                               "expired or the file may not be publicly accessible."));
        return;
    }

    qDebug() << "CloudDownloader: download complete," << fi.size() << "bytes saved to" << m_localPath;

    cleanupReply();

    // Release the QTemporaryFile object without removing the file on disk.
    // (autoRemove is false, so delete only frees the object.)
    // The destructor's remove() guard only fires if m_tempFile is non-null,
    // so we must null it here to prevent deleting a file the caller now owns.
    delete m_tempFile;
    m_tempFile = nullptr;

    emit downloadFinished(m_localPath);
}
