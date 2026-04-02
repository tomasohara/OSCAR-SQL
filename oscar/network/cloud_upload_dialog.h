/* Cloud Upload Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog that uploads an .oscar file to a cloud hosting service
 * and displays the resulting share URL.  Supports 0x0.st (anonymous)
 * and Dropbox (OAuth2).  Shows a service-appropriate warning before
 * uploading, progress during upload, and a Copy Link button on
 * completion.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CLOUD_UPLOAD_DIALOG_H
#define CLOUD_UPLOAD_DIALOG_H

#include <QDialog>

class CloudUploader;
class DropboxUploader;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;

/*!
 * \class CloudUploadDialog
 * \brief Modal dialog for uploading an .oscar file to a cloud service.
 *
 * Offers a choice between 0x0.st (anonymous temporary hosting) and
 * Dropbox (authenticated, persistent).  Performs the upload with
 * progress feedback, then displays the resulting share URL with a
 * Copy Link button.
 */
class CloudUploadDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CloudUploadDialog(const QString& filePath, QWidget* parent = nullptr);
    ~CloudUploadDialog() override;

private slots:
    void onUploadClicked();
    void onUploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void onUploadFinished(const QString& shareUrl);
    void onUploadFailed(const QString& error);
    void onCopyLinkClicked();
    void onDeleteClicked();
    void onDeleteFinished();
    void onDeleteFailed(const QString& error);
    void onProviderChanged();
    void onDropboxSignInClicked();
    void onDropboxAuthComplete(bool success);

private:
    void buildUi();
    void setUiUploading();
    void setUiComplete(const QString& url);
    void setUiError(const QString& error);
    void updateProviderInfo();

    QString           m_filePath;
    CloudUploader*    m_zerox0Uploader  = nullptr;
    DropboxUploader*  m_dropboxUploader = nullptr;
    QString           m_shareUrl;
    QString           m_deleteToken;
    bool              m_uploadDone      = false;

    // UI widgets.
    QRadioButton*     m_zerox0Radio     = nullptr;
    QRadioButton*     m_dropboxRadio    = nullptr;
    QPushButton*      m_dropboxSignIn   = nullptr;
    QLabel*           m_providerInfo    = nullptr;
    QLabel*           m_warningLabel    = nullptr;
    QProgressBar*     m_progressBar     = nullptr;
    QLabel*           m_statusLabel     = nullptr;
    QLineEdit*        m_urlEdit         = nullptr;
    QPushButton*      m_uploadButton    = nullptr;
    QPushButton*      m_copyButton      = nullptr;
    QPushButton*      m_deleteButton    = nullptr;
    QPushButton*      m_closeButton     = nullptr;
};

#endif // CLOUD_UPLOAD_DIALOG_H
