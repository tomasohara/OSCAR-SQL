/* Restore Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "restoredialog.h"
#include "ui_restoredialog.h"

#include <QFileDialog>
#include <QSettings>
#include <QFileInfo>
#include <QMessageBox>
#include <QRadioButton>
#include <QStandardPaths>

#include "database/backup/backup_manifest.h"
#include "database/backup/profile_restore.h"
#include "database/database_schema.h"
#include "mainwindow.h"

extern MainWindow *mainwin;


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
}

RestoreDialog::~RestoreDialog()
{
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
    ui->browseButton->setEnabled(!busy);
    ui->validateButton->setEnabled(!busy && !ui->packagePathEdit->text().isEmpty());
    ui->restoreButton->setEnabled(!busy);
    ui->closeButton->setEnabled(!busy);
    ui->profileNameEdit->setEnabled(!busy);
    ui->abortRadio->setEnabled(!busy);
    ui->renameRadio->setEnabled(!busy);
    ui->replaceRadio->setEnabled(!busy);
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
//  Slots
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
        tr("OSCAR Backup Files (*.oscar);;All Files (*)"));

    if (!path.isEmpty()) {
        m_lastPackageDir = QFileInfo(path).absolutePath();
        saveSettings();
        ui->packagePathEdit->setText(path);
        ui->validateButton->setEnabled(true);
        ui->restoreButton->setEnabled(false);
        showInfoGroup(false);
        showNameGroup(false);
        showConflictGroup(false);
        ui->progressBar->setValue(0);
        ui->statusLabel->clear();
    }
}

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
            // Extra confirmation for the destructive Replace option.
            int ret = QMessageBox::warning(
                this, tr("Confirm Replace"),
                tr("This will permanently delete the existing profile and all its data.\n\n"
                   "Are you sure you want to replace it?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes) return;
            m_restore->setConflictResolution(ConflictResolution::Replace);
        }
    } else {
        m_restore->setConflictResolution(ConflictResolution::Abort);
    }

    // Connect signals.
    connect(m_restore, &ProfileRestore::progressChanged,
            this,      &RestoreDialog::onProgressChanged);
    connect(m_restore, &ProfileRestore::restoreCompleted,
            this,      &RestoreDialog::onRestoreCompleted);
    connect(m_restore, &ProfileRestore::restoreFailed,
            this,      &RestoreDialog::onRestoreFailed);

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
