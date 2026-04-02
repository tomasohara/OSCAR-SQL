/* Restore Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "restoredialog.h"
#include "ui_restoredialog.h"
#include "translation.h"

#include <QDir>
#include <QFileDialog>
#include <QSettings>
#include <QFileInfo>
#include <QMessageBox>
#include <QRadioButton>
#include <QStandardPaths>

#include "database/backup/backup_manifest.h"
#include "database/backup/profile_restore.h"
#include "database/database_schema.h"
#include "network/cloud_downloader.h"
#include "mainwindow.h"
#include "SleepLib/preferences.h"

extern MainWindow *mainwin;
extern Preferences *p_pref;


RestoreDialog::RestoreDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::RestoreDialog)
{
    qDebug() << "RestoreDialog::RestoreDialog() entered";
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Ensure group boxes start hidden.
    showInfoGroup(false);
    showConflictGroup(false);

    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);

    restoreSettings();

    // Radio button state → Restore button enabled/disabled.
    connect(ui->renameRadio,  &QRadioButton::toggled, this,
            [this](bool checked) { if (checked) ui->restoreButton->setEnabled(true); });
    connect(ui->replaceRadio, &QRadioButton::toggled, this,
            [this](bool checked) { if (checked) ui->restoreButton->setEnabled(true); });
    connect(ui->abortRadio,   &QRadioButton::toggled, this,
            [this](bool checked) { if (checked) ui->restoreButton->setEnabled(false); });

    // Source radio buttons toggle between local file and URL modes.
    connect(ui->sourceLocalRadio, &QRadioButton::toggled,
            this, &RestoreDialog::on_sourceLocalRadio_toggled);

    // URL text changes update the provider hint and Download button state.
    connect(ui->urlEdit, &QLineEdit::textChanged,
            this, &RestoreDialog::onUrlTextChanged);

    // Start in local-file mode (sourceLocalRadio is checked in .ui).
    on_sourceLocalRadio_toggled(true);
}

RestoreDialog::~RestoreDialog()
{
    delete m_downloader;
    delete m_restore;
    delete ui;
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void RestoreDialog::displayPackageInfo(const BackupManifest& manifest)
{
    qDebug() << "RestoreDialog::displayPackageInfo() entered";
    ui->infoUsernameLabel->setText(manifest.username());

    if (manifest.isPartial()) {
        ui->infoTypeLabel->setText(tr("Partial (date range)"));
        QDate s = manifest.startDate();
        QDate e = manifest.endDate();
        if (s.isValid() && e.isValid()) {
            ui->infoDateRangeLabel->setText(
                QString("%1 \u2013 %2")
                    .arg(s.toString(Qt::ISODate), e.toString(Qt::ISODate)));
        } else {
            ui->infoDateRangeLabel->setText(tr("(not recorded)"));
        }
    } else {
        ui->infoTypeLabel->setText(tr("Full export"));
        ui->infoDateRangeLabel->setText(tr("All dates"));
    }

    ui->infoPrivacyLabel->setText(manifest.privacyApplied() ? tr("Yes") : tr("No"));
    ui->infoSessionsLabel->setText(QString::number(manifest.sessionsCount()));

    // Show the compressed file size.
    QFileInfo fi(ui->packagePathEdit->text());
    qint64 bytes = fi.size();
    QString sizeStr;
    if (bytes >= 1024 * 1024) {
        sizeStr = QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        sizeStr = QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    ui->infoSizeLabel->setText(sizeStr);
}

void RestoreDialog::showInfoGroup(bool visible)
{
    ui->infoGroup->setVisible(visible);
}

void RestoreDialog::showNameGroup(bool visible)
{
    ui->nameGroup->setVisible(visible);
}

void RestoreDialog::showConflictGroup(bool visible)
{
    ui->conflictGroup->setVisible(visible);
}

void RestoreDialog::setBusy(bool busy)
{
    ui->browseButton->setEnabled(!busy && ui->sourceLocalRadio->isChecked());
    ui->downloadButton->setEnabled(!busy && ui->sourceUrlRadio->isChecked()
                                   && !ui->urlEdit->text().trimmed().isEmpty());
    ui->validateButton->setEnabled(!busy && !ui->packagePathEdit->text().isEmpty());
    ui->restoreButton->setEnabled(!busy);
    ui->closeButton->setEnabled(!busy);
    ui->profileNameEdit->setEnabled(!busy);
    ui->abortRadio->setEnabled(!busy);
    ui->renameRadio->setEnabled(!busy);
    ui->replaceRadio->setEnabled(!busy);
    ui->sourceLocalRadio->setEnabled(!busy);
    ui->sourceUrlRadio->setEnabled(!busy);
    ui->urlEdit->setEnabled(!busy && ui->sourceUrlRadio->isChecked());
}

void RestoreDialog::resetValidation()
{
    delete m_restore;
    m_restore = nullptr;
    showInfoGroup(false);
    showNameGroup(false);
    showConflictGroup(false);
    ui->restoreButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->statusLabel->clear();
}

void RestoreDialog::updateConflictForName(const QString& name)
{
    if (!m_restore) return;

    if (name.trimmed().isEmpty()) {
        showConflictGroup(false);
        ui->restoreButton->setEnabled(false);
        ui->statusLabel->setText(tr("Profile name cannot be empty."));
        return;
    }

    ConflictStatus conflict = m_restore->checkConflicts(name);
    if (conflict == ConflictStatus::UsernameExists) {
        showConflictGroup(true);
        // Default is Abort radio — Restore stays disabled until user picks Rename or Replace.
        ui->abortRadio->setChecked(true);
        ui->restoreButton->setEnabled(false);
        ui->statusLabel->setText(tr("A profile named \"%1\" already exists. Select a resolution option.").arg(name));
    } else {
        showConflictGroup(false);
        ui->restoreButton->setEnabled(true);
        ui->statusLabel->setText(tr("Package validated successfully."));
    }
}

void RestoreDialog::restoreSettings()
{
    QSettings s;
    s.beginGroup("RestoreDialog");
    m_lastPackageDir = s.value("lastPackageDir").toString();
    s.endGroup();
}

void RestoreDialog::saveSettings()
{
    QSettings s;
    s.beginGroup("RestoreDialog");
    s.setValue("lastPackageDir", m_lastPackageDir);
    s.endGroup();
}

// ---------------------------------------------------------------------------
//  Source mode toggling
// ---------------------------------------------------------------------------

void RestoreDialog::on_sourceLocalRadio_toggled(bool checked)
{
    // Local-file controls.
    ui->packagePathEdit->setEnabled(checked);
    ui->browseButton->setEnabled(checked);

    // URL controls — enabled only in URL mode.
    ui->urlEdit->setEnabled(!checked);
    ui->downloadButton->setEnabled(!checked && !ui->urlEdit->text().trimmed().isEmpty());
    ui->providerHintLabel->setVisible(!checked);

    // Validate button tracks whichever mode has content.
    if (checked) {
        ui->validateButton->setEnabled(!ui->packagePathEdit->text().isEmpty());
    } else {
        ui->validateButton->setEnabled(false);
    }

    // Reset any prior validation when switching modes.
    resetValidation();
}

// ---------------------------------------------------------------------------
//  URL handling
// ---------------------------------------------------------------------------

void RestoreDialog::onUrlTextChanged(const QString& text)
{
    QString trimmed = text.trimmed();
    bool hasText = !trimmed.isEmpty();
    ui->downloadButton->setEnabled(hasText && ui->sourceUrlRadio->isChecked());

    if (hasText) {
        QUrl url(trimmed);
        CloudProvider provider = CloudDownloader::identifyProvider(url);
        if (CloudDownloader::isProviderSupported(provider)) {
            ui->providerHintLabel->setText(
                tr("Detected: %1").arg(CloudDownloader::providerName(provider)));
        } else if (provider == CloudProvider::ProtonDrive) {
            ui->providerHintLabel->setText(
                tr("Proton Drive links require manual download"));
        } else if (provider == CloudProvider::Unknown) {
            ui->providerHintLabel->setText(
                tr("Unrecognized service — you may need to download the file manually"));
        }
    } else {
        ui->providerHintLabel->clear();
    }
}

void RestoreDialog::on_downloadButton_clicked()
{
    QString urlText = ui->urlEdit->text().trimmed();
    if (urlText.isEmpty()) return;

    QUrl url(urlText);
    if (!url.isValid()) {
        QMessageBox::warning(this, tr("Restore Profile"),
            tr("The URL you entered is not valid."));
        return;
    }

    // Clean up any previous downloader.
    delete m_downloader;
    m_downloader = nullptr;

    // Reset any prior validation state.
    resetValidation();
    ui->packagePathEdit->clear();

    m_downloader = new CloudDownloader(this);
    m_downloader->setUrl(url);

    connect(m_downloader, &CloudDownloader::downloadProgress,
            this,         &RestoreDialog::onDownloadProgress);
    connect(m_downloader, &CloudDownloader::downloadFinished,
            this,         &RestoreDialog::onDownloadFinished);
    connect(m_downloader, &CloudDownloader::downloadFailed,
            this,         &RestoreDialog::onDownloadFailed);

    // Disable controls during download.
    setBusy(true);

    CloudProvider provider = CloudDownloader::identifyProvider(url);
    ui->statusLabel->setText(tr("Downloading from %1...")
                                 .arg(CloudDownloader::providerName(provider)));
    ui->progressBar->setValue(0);

    m_downloader->start();
}

void RestoreDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        int percent = static_cast<int>(bytesReceived * 100 / bytesTotal);
        ui->progressBar->setValue(percent);

        // Show human-readable sizes.
        QString received, total;
        if (bytesTotal >= 1024 * 1024) {
            received = QString::number(bytesReceived / (1024.0 * 1024.0), 'f', 1) + " MB";
            total    = QString::number(bytesTotal / (1024.0 * 1024.0), 'f', 1) + " MB";
        } else {
            received = QString::number(bytesReceived / 1024.0, 'f', 0) + " KB";
            total    = QString::number(bytesTotal / 1024.0, 'f', 0) + " KB";
        }
        ui->statusLabel->setText(tr("Downloading... (%1 / %2)").arg(received, total));
    } else {
        // Total unknown — show indeterminate progress.
        ui->progressBar->setRange(0, 0);
        QString received;
        if (bytesReceived >= 1024 * 1024) {
            received = QString::number(bytesReceived / (1024.0 * 1024.0), 'f', 1) + " MB";
        } else {
            received = QString::number(bytesReceived / 1024.0, 'f', 0) + " KB";
        }
        ui->statusLabel->setText(tr("Downloading... (%1 received)").arg(received));
    }
}

void RestoreDialog::onDownloadFinished(const QString& localPath)
{
    // Restore progress bar to determinate mode.
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(100);
    ui->statusLabel->setText(tr("Download complete. Validating..."));

    // Set the package path to the downloaded temp file and auto-validate.
    ui->packagePathEdit->setText(localPath);
    ui->validateButton->setEnabled(true);

    // Re-enable non-busy controls.
    setBusy(false);

    // Auto-trigger validation so the user doesn't have to click Validate.
    on_validateButton_clicked();
}

void RestoreDialog::onDownloadFailed(const QString& error)
{
    // Restore progress bar to determinate mode.
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Download failed."));

    QMessageBox::warning(this, tr("Download Failed"), error);

    setBusy(false);
}

// ---------------------------------------------------------------------------
//  Local file browsing
// ---------------------------------------------------------------------------

void RestoreDialog::on_browseButton_clicked()
{
    const QString startDir = m_lastPackageDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : m_lastPackageDir;

    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Backup Package"),
        startDir,
        tr("OSCAR Backup Files (*.oscar);;All Files (*)"), nullptr, nativeDialogOption());

    if (!path.isEmpty()) {
        m_lastPackageDir = QFileInfo(path).absolutePath();
        saveSettings();
        resetValidation();
        ui->packagePathEdit->setText(path);
        ui->validateButton->setEnabled(true);
    }
}

// ---------------------------------------------------------------------------
//  Validation
// ---------------------------------------------------------------------------

void RestoreDialog::on_profileNameEdit_textChanged(const QString& text)
{
    if (!m_restore) return;
    updateConflictForName(text);
}

void RestoreDialog::on_validateButton_clicked()
{
    qDebug() << "RestoreDialog::validateButton_clicked()";
    QString path = ui->packagePathEdit->text();
    if (path.isEmpty()) return;

    // Reset state from any previous validation.
    delete m_restore;
    m_restore = nullptr;
    showInfoGroup(false);
    showNameGroup(false);
    showConflictGroup(false);
    ui->restoreButton->setEnabled(false);
    ui->statusLabel->setText(tr("Validating..."));

    m_restore = new ProfileRestore(path, this);

    if (!m_restore->validatePackage()) {
        ui->statusLabel->setText(tr("Validation failed: %1").arg(m_restore->getErrorMessage()));
        QMessageBox::warning(this, tr("Restore Profile"),
            tr("The selected file is not a valid backup package.\n\n%1")
                .arg(m_restore->getErrorMessage()));
        delete m_restore;
        m_restore = nullptr;
        return;
    }

    if (!m_restore->checkCompatibility()) {
        ui->statusLabel->setText(tr("Incompatible: %1").arg(m_restore->getErrorMessage()));
        QMessageBox::warning(this, tr("Restore Profile"),
            tr("This backup cannot be restored.\n\n%1")
                .arg(m_restore->getErrorMessage()));
        delete m_restore;
        m_restore = nullptr;
        return;
    }

    // Warn the user when the backup's schema is older than the current one.
    // The restore will still proceed; executeSqlFile() skips removed tables and
    // new columns receive their DEFAULT values.
    {
        const int backupSchema =
            m_restore->manifestJson()[QStringLiteral("schema_version")].toInt(0);
        if (backupSchema < DatabaseSchema::CURRENT_SCHEMA_VERSION) {
            QMessageBox::warning(this, tr("Restore Profile"),
                tr("This backup was created with an older database schema (v%1; "
                   "current is v%2).\n\n"
                   "Your sleep session data will be fully restored. Some settings "
                   "or report configurations may not be restored and will be "
                   "regenerated by OSCAR on first use.\n\n"
                   "Click Validate to continue.")
                    .arg(backupSchema)
                    .arg(DatabaseSchema::CURRENT_SCHEMA_VERSION));
        }
    }

    // Show manifest info.  Reuse the already-parsed manifest from ProfileRestore.
    BackupManifest displayManifest;
    displayManifest.fromJson(m_restore->manifestJson());
    QString originalUsername;
    if (displayManifest.isValid()) {
        displayPackageInfo(displayManifest);
        showInfoGroup(true);
        originalUsername = displayManifest.username();
    }

    // Show the profile name field pre-filled with the original username.
    // The user may edit it freely; changing the name re-checks for conflicts.
    ui->profileNameEdit->blockSignals(true);
    ui->profileNameEdit->setText(originalUsername);
    ui->profileNameEdit->blockSignals(false);
    showNameGroup(true);

    // Run initial conflict check against the original name.
    updateConflictForName(originalUsername);
}

// ---------------------------------------------------------------------------
//  Restore
// ---------------------------------------------------------------------------

void RestoreDialog::on_restoreButton_clicked()
{
    if (!m_restore) return;

    // Apply the profile name the user has entered.
    const QString targetName = ui->profileNameEdit->text().trimmed();
    if (targetName.isEmpty()) {
        ui->statusLabel->setText(tr("Profile name cannot be empty."));
        return;
    }
    m_restore->setNewUsername(targetName);

    // Apply conflict resolution if the conflict group is visible.
    if (ui->conflictGroup->isVisible()) {
        if (ui->abortRadio->isChecked()) {
            return;  // Should never reach here (button is disabled for Abort).
        } else if (ui->renameRadio->isChecked()) {
            m_restore->setConflictResolution(ConflictResolution::Rename);
        } else if (ui->replaceRadio->isChecked()) {
            // Determine whether the incoming package will wipe the profile
            // directory (only happens when it includes SD card data) and
            // whether the existing profile has a Backup subdirectory that
            // would be destroyed in that wipe.
            const bool incomingHasSD =
                m_restore->manifestJson()[QStringLiteral("export_options")]
                          .toObject()[QStringLiteral("includes_sd_data")].toBool(false);
            const QString profilesDir = p_pref->Get(QStringLiteral("{home}/Profiles"));
            const bool existingHasBackup =
                QDir(profilesDir + QLatin1Char('/') + targetName
                     + QStringLiteral("/Backup")).exists();

            QString warningTitle;
            QString warningText;
            if (incomingHasSD && existingHasBackup) {
                warningTitle = tr("Confirm Replace — Backup Data Will Be Deleted");
                warningText  = tr(
                    "WARNING: The existing profile \"%1\" contains a Backup directory "
                    "that holds CPAP backup data.\n\n"
                    "Because this restore package includes SD card data, the entire "
                    "profile directory — including all CPAP backup data — will be "
                    "permanently deleted and replaced.\n\n"
                    "This cannot be undone. Are you sure you want to continue?")
                    .arg(targetName);
            } else {
                warningTitle = tr("Confirm Replace");
                warningText  = tr(
                    "This will permanently delete the existing profile and all its data.\n\n"
                    "Are you sure you want to replace it?");
            }
            int ret = QMessageBox::warning(
                this, warningTitle, warningText,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes) return;
            m_restore->setConflictResolution(ConflictResolution::Replace);
        }
    } else {
        m_restore->setConflictResolution(ConflictResolution::Abort);
    }

    // Connect signals.  UniqueConnection prevents duplicate handlers if the
    // user retries after a failed restore (same m_restore object, same slot).
    connect(m_restore, &ProfileRestore::progressChanged,
            this,      &RestoreDialog::onProgressChanged,
            Qt::UniqueConnection);
    connect(m_restore, &ProfileRestore::restoreCompleted,
            this,      &RestoreDialog::onRestoreCompleted,
            Qt::UniqueConnection);
    connect(m_restore, &ProfileRestore::restoreFailed,
            this,      &RestoreDialog::onRestoreFailed,
            Qt::UniqueConnection);

    setBusy(true);
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Starting restore..."));

    m_restore->restoreProfile();
}

void RestoreDialog::onProgressChanged(int percent, const QString& message)
{
    ui->progressBar->setValue(percent);
    ui->statusLabel->setText(message);
    qDebug() << "RestoreDialog::onProgressChanged:" << percent << message;
}

void RestoreDialog::onRestoreCompleted(qint64 /*profileId*/, const QString& username)
{
    ui->progressBar->setValue(100);
    ui->statusLabel->setText(tr("Restore complete."));

    QMessageBox::information(this, tr("Restore Complete"),
        tr("Profile \"%1\" restored successfully.").arg(username));

    accept();  // Close dialog with Accepted result so caller can refresh profile list.
}

void RestoreDialog::onRestoreFailed(const QString& error)
{
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Restore failed."));

    QMessageBox::critical(this, tr("Restore Failed"),
        tr("The restore could not be completed. The database was not modified.\n\n%1").arg(error));

    setBusy(false);
}
