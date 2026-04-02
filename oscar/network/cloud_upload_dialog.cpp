/* Cloud Upload Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "cloud_upload_dialog.h"
#include "cloud_uploader.h"
#include "dropbox_uploader.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

CloudUploadDialog::CloudUploadDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setWindowTitle(tr("Upload to Cloud"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(520);

    // Create the Dropbox uploader early so we can check auth status.
    m_dropboxUploader = new DropboxUploader(this);

    buildUi();
    updateProviderInfo();
}

CloudUploadDialog::~CloudUploadDialog()
{
    delete m_zerox0Uploader;
}

// ---------------------------------------------------------------------------
//  UI construction
// ---------------------------------------------------------------------------

void CloudUploadDialog::buildUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Provider selection group.
    QGroupBox* providerGroup = new QGroupBox(tr("Upload Service"), this);
    QVBoxLayout* providerLayout = new QVBoxLayout(providerGroup);

    // Dropbox option.
    QHBoxLayout* dropboxRow = new QHBoxLayout();
    m_dropboxRadio = new QRadioButton(tr("Dropbox"), this);
    dropboxRow->addWidget(m_dropboxRadio);
    m_dropboxSignIn = new QPushButton(this);
    m_dropboxSignIn->setFixedWidth(100);
    dropboxRow->addWidget(m_dropboxSignIn);
    dropboxRow->addStretch();
    providerLayout->addLayout(dropboxRow);

    // 0x0.st option.
    m_zerox0Radio = new QRadioButton(tr("0x0.st (anonymous temporary hosting)"), this);
    providerLayout->addWidget(m_zerox0Radio);

    layout->addWidget(providerGroup);

    // Provider info / warning.
    m_providerInfo = new QLabel(this);
    m_providerInfo->setWordWrap(true);
    layout->addWidget(m_providerInfo);

    // File info.
    QFileInfo fi(m_filePath);
    QString sizeStr;
    qint64 bytes = fi.size();
    if (bytes >= 1024 * 1024) {
        sizeStr = QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        sizeStr = QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    QLabel* fileLabel = new QLabel(
        tr("File: %1  (%2)").arg(fi.fileName(), sizeStr), this);
    layout->addWidget(fileLabel);

    layout->addSpacing(4);

    // Progress bar (hidden until upload starts).
    m_progressBar = new QProgressBar(this);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar);

    // Status label.
    m_statusLabel = new QLabel(this);
    layout->addWidget(m_statusLabel);

    // URL display (hidden until upload completes).
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setReadOnly(true);
    m_urlEdit->setVisible(false);
    layout->addWidget(m_urlEdit);

    layout->addSpacing(4);

    // Button row.
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_deleteButton = new QPushButton(tr("Delete Uploaded Copy"), this);
    m_deleteButton->setVisible(false);
    buttonLayout->addWidget(m_deleteButton);

    buttonLayout->addStretch();

    m_copyButton = new QPushButton(tr("Copy Link"), this);
    m_copyButton->setVisible(false);
    buttonLayout->addWidget(m_copyButton);

    m_uploadButton = new QPushButton(tr("Upload"), this);
    buttonLayout->addWidget(m_uploadButton);

    m_closeButton = new QPushButton(tr("Close"), this);
    buttonLayout->addWidget(m_closeButton);

    layout->addLayout(buttonLayout);

    // Default to Dropbox if authenticated, otherwise 0x0.st.
    if (m_dropboxUploader->isAuthenticated()) {
        m_dropboxRadio->setChecked(true);
    } else {
        m_zerox0Radio->setChecked(true);
    }

    // Connections.
    connect(m_dropboxRadio,   &QRadioButton::toggled,
            this,             &CloudUploadDialog::onProviderChanged);
    connect(m_zerox0Radio,    &QRadioButton::toggled,
            this,             &CloudUploadDialog::onProviderChanged);
    connect(m_dropboxSignIn,  &QPushButton::clicked,
            this,             &CloudUploadDialog::onDropboxSignInClicked);
    connect(m_uploadButton,   &QPushButton::clicked,
            this,             &CloudUploadDialog::onUploadClicked);
    connect(m_copyButton,     &QPushButton::clicked,
            this,             &CloudUploadDialog::onCopyLinkClicked);
    connect(m_deleteButton,   &QPushButton::clicked,
            this,             &CloudUploadDialog::onDeleteClicked);
    connect(m_closeButton,    &QPushButton::clicked,
            this,             &QDialog::accept);
    connect(m_dropboxUploader, &DropboxUploader::authComplete,
            this,              &CloudUploadDialog::onDropboxAuthComplete);
}

// ---------------------------------------------------------------------------
//  Provider info updates
// ---------------------------------------------------------------------------

void CloudUploadDialog::updateProviderInfo()
{
    bool dropboxAuthed = m_dropboxUploader->isAuthenticated();

    if (m_dropboxRadio->isChecked()) {
        if (dropboxAuthed) {
            m_dropboxSignIn->setText(tr("Sign Out"));
            m_providerInfo->setText(
                tr("Your file will be uploaded to your Dropbox account and a "
                   "shared link will be created."));
            m_uploadButton->setEnabled(true);
        } else {
            m_dropboxSignIn->setText(tr("Sign In..."));
            m_providerInfo->setText(
                tr("Sign in to Dropbox to upload files to your account."));
            m_uploadButton->setEnabled(false);
        }
    } else {
        // 0x0.st selected.
        m_dropboxSignIn->setText(dropboxAuthed ? tr("Sign Out") : tr("Sign In..."));
        m_providerInfo->setText(
            tr("Your file will be uploaded to <b>0x0.st</b>, a third-party "
               "file hosting service not operated by the OSCAR team. "
               "Anyone with the link can download the file. "
               "The file will be automatically deleted after 30 days."));
        m_uploadButton->setEnabled(true);
    }
}

void CloudUploadDialog::onProviderChanged()
{
    updateProviderInfo();
}

void CloudUploadDialog::onDropboxSignInClicked()
{
    if (m_dropboxUploader->isAuthenticated()) {
        // Sign out.
        m_dropboxUploader->signOut();
        updateProviderInfo();
    } else {
        // Sign in.
        m_statusLabel->setText(tr("Opening browser for Dropbox authorization..."));
        m_dropboxUploader->authenticate();
    }
}

void CloudUploadDialog::onDropboxAuthComplete(bool success)
{
    if (success) {
        m_statusLabel->setText(tr("Dropbox authorization successful."));
    } else {
        m_statusLabel->setText(tr("Dropbox authorization failed."));
    }
    updateProviderInfo();
}

// ---------------------------------------------------------------------------
//  UI state helpers
// ---------------------------------------------------------------------------

void CloudUploadDialog::setUiUploading()
{
    m_uploadButton->setEnabled(false);
    m_closeButton->setEnabled(false);
    m_dropboxRadio->setEnabled(false);
    m_zerox0Radio->setEnabled(false);
    m_dropboxSignIn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Uploading..."));
}

void CloudUploadDialog::setUiComplete(const QString& url)
{
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    m_statusLabel->setText(
        tr("Upload complete. Share this link with the person who will "
           "review your OSCAR data:"));
    m_urlEdit->setText(url);
    m_urlEdit->setVisible(true);
    m_urlEdit->selectAll();
    m_copyButton->setVisible(true);
    // Only show delete for 0x0.st (has delete token).
    if (!m_deleteToken.isEmpty()) {
        m_deleteButton->setVisible(true);
    }
    m_uploadButton->setVisible(false);
    m_closeButton->setEnabled(true);
}

void CloudUploadDialog::setUiError(const QString& error)
{
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Upload failed."));
    m_uploadButton->setEnabled(true);
    m_closeButton->setEnabled(true);
    m_dropboxRadio->setEnabled(true);
    m_zerox0Radio->setEnabled(true);
    m_dropboxSignIn->setEnabled(true);
    Q_UNUSED(error);
}

// ---------------------------------------------------------------------------
//  Upload slots
// ---------------------------------------------------------------------------

void CloudUploadDialog::onUploadClicked()
{
    m_uploadDone = false;
    m_deleteToken.clear();
    m_shareUrl.clear();

    if (m_dropboxRadio->isChecked()) {
        // Dropbox upload.
        m_dropboxUploader->setFilePath(m_filePath);

        connect(m_dropboxUploader, &DropboxUploader::uploadProgress,
                this,              &CloudUploadDialog::onUploadProgress,
                Qt::UniqueConnection);
        connect(m_dropboxUploader, &DropboxUploader::uploadFinished,
                this,              &CloudUploadDialog::onUploadFinished,
                Qt::UniqueConnection);
        connect(m_dropboxUploader, &DropboxUploader::uploadFailed,
                this,              &CloudUploadDialog::onUploadFailed,
                Qt::UniqueConnection);

        setUiUploading();
        m_dropboxUploader->startUpload();
    } else {
        // 0x0.st upload.
        delete m_zerox0Uploader;
        m_zerox0Uploader = new CloudUploader(this);
        m_zerox0Uploader->setFilePath(m_filePath);

        connect(m_zerox0Uploader, &CloudUploader::uploadProgress,
                this,             &CloudUploadDialog::onUploadProgress);
        connect(m_zerox0Uploader, &CloudUploader::uploadFinished,
                this,             &CloudUploadDialog::onUploadFinished);
        connect(m_zerox0Uploader, &CloudUploader::uploadFailed,
                this,             &CloudUploadDialog::onUploadFailed);

        setUiUploading();
        m_zerox0Uploader->start();
    }
}

void CloudUploadDialog::onUploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    if (m_uploadDone) return;

    if (bytesTotal > 0) {
        int percent = static_cast<int>(bytesSent * 100 / bytesTotal);
        m_progressBar->setValue(percent);

        QString sent, total;
        if (bytesTotal >= 1024 * 1024) {
            sent  = QString::number(bytesSent / (1024.0 * 1024.0), 'f', 1) + " MB";
            total = QString::number(bytesTotal / (1024.0 * 1024.0), 'f', 1) + " MB";
        } else {
            sent  = QString::number(bytesSent / 1024.0, 'f', 0) + " KB";
            total = QString::number(bytesTotal / 1024.0, 'f', 0) + " KB";
        }
        m_statusLabel->setText(tr("Uploading... (%1 / %2)").arg(sent, total));
    } else {
        m_progressBar->setRange(0, 0);  // Indeterminate.
    }
}

void CloudUploadDialog::onUploadFinished(const QString& shareUrl)
{
    m_uploadDone = true;
    m_shareUrl = shareUrl;

    // Capture delete token for 0x0.st.
    if (m_zerox0Uploader) {
        m_deleteToken = m_zerox0Uploader->deleteToken();
    }

    setUiComplete(shareUrl);
}

void CloudUploadDialog::onUploadFailed(const QString& error)
{
    m_uploadDone = true;
    setUiError(error);
    QMessageBox::warning(this, tr("Upload Failed"), error);
}

void CloudUploadDialog::onCopyLinkClicked()
{
    QApplication::clipboard()->setText(m_shareUrl);
    m_statusLabel->setText(tr("Link copied to clipboard."));
}

// ---------------------------------------------------------------------------
//  Delete (0x0.st only)
// ---------------------------------------------------------------------------

void CloudUploadDialog::onDeleteClicked()
{
    if (m_shareUrl.isEmpty() || m_deleteToken.isEmpty() || !m_zerox0Uploader) return;

    int ret = QMessageBox::question(this, tr("Delete Uploaded Copy"),
        tr("Are you sure you want to delete the uploaded file from 0x0.st?\n\n"
           "The share link will stop working."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    m_deleteButton->setEnabled(false);
    m_statusLabel->setText(tr("Deleting..."));

    connect(m_zerox0Uploader, &CloudUploader::deleteFinished,
            this,             &CloudUploadDialog::onDeleteFinished,
            Qt::UniqueConnection);
    connect(m_zerox0Uploader, &CloudUploader::deleteFailed,
            this,             &CloudUploadDialog::onDeleteFailed,
            Qt::UniqueConnection);

    m_zerox0Uploader->deleteUpload(m_shareUrl, m_deleteToken);
}

void CloudUploadDialog::onDeleteFinished()
{
    m_statusLabel->setText(tr("Uploaded file has been deleted."));
    m_deleteButton->setVisible(false);
    m_copyButton->setEnabled(false);
    m_urlEdit->setText(tr("(deleted)"));
}

void CloudUploadDialog::onDeleteFailed(const QString& error)
{
    m_deleteButton->setEnabled(true);
    m_statusLabel->setText(tr("Delete failed."));
    QMessageBox::warning(this, tr("Delete Failed"), error);
}
