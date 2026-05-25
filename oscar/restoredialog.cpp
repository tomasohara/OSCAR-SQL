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

#include <QtConcurrent>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include "database/database_manager.h"
#include "database/app_preferences_repository.h"
#include "database/backup/backup_manifest.h"
#include "database/backup/profile_restore.h"
#include "database/database_schema.h"
#include "database/preferences_repository.h"
#include "database/profile_repository.h"
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

    connect(ui->closeButton, &QPushButton::clicked, this, [this]() {
        if (m_restoreInProgress) {
            m_cancelRequested = true;
            if (m_restore) m_restore->requestCancel();
            ui->closeButton->setEnabled(false);
            ui->closeButton->setText(tr("Cancelling..."));
        } else {
            reject();
        }
    });

    restoreSettings();

    // Any change to the conflict-resolution radio buttons re-evaluates
    // whether the Restore button should be enabled.
    connect(ui->renameRadio, &QRadioButton::toggled, this,
            [this](bool checked) {
                if (checked) {
                    const QString baseName = ui->profileNameEdit->text().trimmed();
                    if (!baseName.isEmpty()) {
                        // setText triggers on_profileNameEdit_textChanged, which
                        // re-checks conflicts and hides the conflict group when clear.
                        ui->profileNameEdit->setText(computeRenameSuggestion(baseName));
                        return;
                    }
                }
                updateRestoreButtonState();
                updateStatusLabel();
            });
    connect(ui->replaceRadio, &QRadioButton::toggled, this,
            [this](bool) { updateRestoreButtonState(); updateStatusLabel(); });
    connect(ui->abortRadio,   &QRadioButton::toggled, this,
            [this](bool) { updateRestoreButtonState(); updateStatusLabel(); });

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
    if (m_validateWatcher && m_validateWatcher->isRunning()) {
        m_validateWatcher->waitForFinished();
    }
    delete m_validateWatcher;
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
    if (!visible) {
        // Reset to default state so it's clean next time it appears.
        ui->replaceRadio->setEnabled(true);
        ui->noReplaceLabel->setVisible(false);
    }
}

void RestoreDialog::setBusy(bool busy)
{
    ui->browseButton->setEnabled(!busy && ui->sourceLocalRadio->isChecked());
    ui->downloadButton->setEnabled(!busy && ui->sourceUrlRadio->isChecked()
                                   && !ui->urlEdit->text().trimmed().isEmpty());
    ui->validateButton->setEnabled(!busy && !ui->packagePathEdit->text().isEmpty());
    ui->closeButton->setEnabled(!busy);
    ui->profileNameEdit->setEnabled(!busy);
    ui->abortRadio->setEnabled(!busy);
    ui->renameRadio->setEnabled(!busy);
    ui->replaceRadio->setEnabled(!busy);
    ui->sourceLocalRadio->setEnabled(!busy);
    ui->sourceUrlRadio->setEnabled(!busy);
    ui->urlEdit->setEnabled(!busy && ui->sourceUrlRadio->isChecked());

    if (busy) {
        ui->restoreButton->setEnabled(false);
    } else {
        updateRestoreButtonState();
    }
}

void RestoreDialog::updateRestoreButtonState()
{
    // Require a validated package, a non-empty name, and (if a conflict exists)
    // an explicit resolution other than Abort.
    bool ready = m_restore != nullptr
              && !ui->profileNameEdit->text().trimmed().isEmpty()
              && (!ui->conflictGroup->isVisible() || !ui->abortRadio->isChecked());
    ui->restoreButton->setEnabled(ready);
}

QString RestoreDialog::computeRenameSuggestion(const QString& baseName) const
{
    // Strip any existing " (copy N)" suffix so we always build from the root name.
    static const QRegularExpression copyRe(QStringLiteral(R"( \(copy \d+\)$)"));
    const QString root = QString(baseName).remove(copyRe);
    for (int n = 2; n <= 999; ++n) {
        const QString candidate = root + QStringLiteral(" (copy %1)").arg(n);
        if (!m_restore ||
            m_restore->checkConflicts(candidate) != ConflictStatus::UsernameExists) {
            return candidate;
        }
    }
    return root + QStringLiteral(" (copy 999)");
}

void RestoreDialog::ensureOnScreen()
{
    QTimer::singleShot(0, this, [this]() {
        QScreen* screen = QGuiApplication::screenAt(frameGeometry().topLeft());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen) return;

        const QRect avail = screen->availableGeometry();
        QRect frame = frameGeometry();
        if (frame.bottom() > avail.bottom()) frame.moveBottom(avail.bottom());
        if (frame.top()    < avail.top())    frame.moveTop(avail.top());
        if (frame.right()  > avail.right())  frame.moveRight(avail.right());
        if (frame.left()   < avail.left())   frame.moveLeft(avail.left());
        // move() positions the client area; subtract the frame inset.
        const QPoint frameInset = frameGeometry().topLeft() - geometry().topLeft();
        move(frame.topLeft() - frameInset);
    });
}

void RestoreDialog::resetValidation()
{
    delete m_restore;
    m_restore       = nullptr;
    m_packageIsShare = false;
    m_backupHasSD   = false;
    m_existingHasSD = false;
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
        ui->statusLabel->setText(tr("Profile name cannot be empty."));
        updateRestoreButtonState();
        return;
    }

    ConflictStatus conflict = m_restore->checkConflicts(name);
    if (conflict == ConflictStatus::UsernameExists) {
        showConflictGroup(true);

        // Replace is forbidden when the package has no SD data: restoring would
        // delete the existing profile's SD card files with nothing to replace them.
        m_backupHasSD = m_restore->manifestJson()
                            [QStringLiteral("export_options")].toObject()
                            [QStringLiteral("includes_sd_data")].toBool(false);
        ui->replaceRadio->setEnabled(m_backupHasSD);
        ui->noReplaceLabel->setVisible(!m_backupHasSD);

        // Check whether the existing profile also has SD card data.
        // SD card backups live in a Backup/ subdirectory under each machine directory
        // (e.g. ProfileName/ResMed_12345678/Backup/), so scan one level down.
        const QString profilesDir = p_pref->Get(QStringLiteral("{home}/Profiles"));
        const QDir existingProfileDir(profilesDir + QLatin1Char('/') + name);
        m_existingHasSD = false;
        if (existingProfileDir.exists()) {
            const QStringList machineDirs =
                existingProfileDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& machineDir : machineDirs) {
                if (QDir(existingProfileDir.filePath(machineDir)
                         + QStringLiteral("/Backup")).exists()) {
                    m_existingHasSD = true;
                    break;
                }
            }
        }

        // Update rename radio label to show the actual proposed copy name.
        ui->renameRadio->setText(
            tr("Rename — import as \"%1\"").arg(computeRenameSuggestion(name)));

        // Default is Abort radio — Restore stays disabled until user picks Rename or Replace.
        ui->abortRadio->setChecked(true);
    } else {
        m_backupHasSD   = false;
        m_existingHasSD = false;
        showConflictGroup(false);
    }
    updateStatusLabel();
    updateRestoreButtonState();
}

void RestoreDialog::updateStatusLabel()
{
    // Build the base message from current state.
    QString msg;
    if (ui->conflictGroup->isVisible()) {
        const QString name = ui->profileNameEdit->text().trimmed();
        msg = tr("A profile named \"%1\" already exists. Select a resolution option.").arg(name);
    } else if (m_restore) {
        msg = tr("Package validated successfully.");
    }

    // Append a red SD-data warning when Replace is chosen and both sides have SD data.
    if (m_backupHasSD && m_existingHasSD
            && ui->conflictGroup->isVisible()
            && ui->replaceRadio->isChecked()) {
        const QString warning = tr("WARNING: SD card images in profile will be deleted and "
                                   "replaced by those in the backup file.");
        msg = msg.toHtmlEscaped()
            + QStringLiteral("<br><span style='color:red; font-weight:bold;'>")
            + warning.toHtmlEscaped()
            + QStringLiteral("</span>");
        ui->statusLabel->setTextFormat(Qt::RichText);
    } else {
        ui->statusLabel->setTextFormat(Qt::AutoText);
    }

    ui->statusLabel->setText(msg);
}

void RestoreDialog::restoreSettings()
{
    AppPreferencesRepository repo;
    const auto rows = repo.loadByCategory("RestoreDialog");
    for (const AppPrefData& row : rows) {
        if (row.key == "lastPackageDir") m_lastPackageDir = row.value;
    }
}

void RestoreDialog::saveSettings()
{
    AppPreferencesRepository repo;
    repo.save("RestoreDialog", "lastPackageDir", m_lastPackageDir);
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
    const QString path = ui->packagePathEdit->text();
    if (path.isEmpty()) return;

    // Reset state from any previous validation.
    delete m_restore;
    m_restore = nullptr;
    showInfoGroup(false);
    showNameGroup(false);
    showConflictGroup(false);
    ui->restoreButton->setEnabled(false);
    ui->statusLabel->setText(tr("Validating..."));

    // Disable controls and show an indeterminate progress bar while the
    // package is being extracted and checksummed in the background.
    setBusy(true);
    ui->progressBar->setRange(0, 0);

    m_restore = new ProfileRestore(path, this);

    // Clean up any previous watcher before creating a new one.
    delete m_validateWatcher;
    m_validateWatcher = new QFutureWatcher<bool>(this);
    connect(m_validateWatcher, &QFutureWatcher<bool>::finished,
            this, &RestoreDialog::onValidationFinished);

    // Run validatePackage() on a thread-pool thread.  validatePackage() does
    // no signal emission and does not interact with the Qt event loop, so it
    // is safe to call from a worker thread while the main thread is idle.
    m_validateWatcher->setFuture(QtConcurrent::run([this]() {
        return m_restore->validatePackage();
    }));
}

void RestoreDialog::onValidationFinished()
{
    // Restore determinate progress bar before any early returns.
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    setBusy(false);

    const bool valid = m_validateWatcher->result();

    if (!valid) {
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

    const QString path = ui->packagePathEdit->text();

    // Detect whether this is a share package.  Prefer the manifest's
    // package_type field (present in packages created after v2.0.0-rc-1);
    // fall back to filename check for older packages.
    const QString packageType =
        m_restore->manifestJson()[QStringLiteral("package_type")].toString();
    if (!packageType.isEmpty()) {
        m_packageIsShare = (packageType == QStringLiteral("share"));
    } else {
        m_packageIsShare = QFileInfo(path).fileName()
                               .startsWith(QStringLiteral("share_"), Qt::CaseInsensitive);
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

    // Pre-fill the profile name.  For share packages append " (Shared)" so the
    // restored profile is visually distinguished from the owner's own profiles.
    QString defaultName = originalUsername;
    if (m_packageIsShare && !defaultName.isEmpty()) {
        defaultName += QStringLiteral(" (Shared)");
    }

    // Show the profile name field pre-filled with the (possibly decorated) username.
    // The user may edit it freely; changing the name re-checks for conflicts.
    ui->profileNameEdit->blockSignals(true);
    ui->profileNameEdit->setText(defaultName);
    ui->profileNameEdit->blockSignals(false);
    showNameGroup(true);

    // Run initial conflict check against the default name.
    updateConflictForName(defaultName);

    // The dialog may have grown to show infoGroup / nameGroup / conflictGroup.
    // Reposition if any part is now hidden behind the taskbar.
    ensureOnScreen();
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

    m_cancelRequested   = false;
    m_restoreInProgress = true;
    setBusy(true);
    ui->closeButton->setText(tr("Cancel"));
    ui->closeButton->setEnabled(true);
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

void RestoreDialog::onRestoreCompleted(qint64 profileId, const QString& username)
{
    m_restoreInProgress = false;
    ui->progressBar->setValue(100);
    ui->statusLabel->setText(tr("Restore complete."));

    // Record how this profile originated so it can be identified later.
    if (profileId > 0) {
        const QString source = m_packageIsShare
            ? QStringLiteral("Share")
            : QStringLiteral("Backup");
        PreferencesRepository prefRepo;
        prefRepo.savePreference(profileId, QStringLiteral("profile"),
                                QStringLiteral("Source"), source);
    }

    QMessageBox::information(this, tr("Restore Complete"),
        tr("Profile \"%1\" restored successfully.").arg(username));

    accept();  // Close dialog with Accepted result so caller can refresh profile list.
}

void RestoreDialog::onRestoreFailed(const QString& error)
{
    m_restoreInProgress = false;
    ui->progressBar->setValue(0);
    setBusy(false);
    ui->closeButton->setText(tr("Cancel"));
    ui->closeButton->setEnabled(true);

    if (m_cancelRequested) {
        m_cancelRequested = false;
        ui->statusLabel->setText(tr("Restore cancelled."));
    } else {
        ui->statusLabel->setText(tr("Restore failed."));
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("The restore could not be completed. The database was not modified.\n\n%1").arg(error));
    }
}
