/* Backup Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "backupdialog.h"
#include "ui_backupdialog.h"

#include <QCalendarWidget>
#include <QSettings>
#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include "SleepLib/profiles.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"
#include "database/backup/profile_backup.h"
#include "mainwindow.h"

extern MainWindow *mainwin;


BackupDialog::BackupDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::BackupDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupCalendarFormatting();
    populateProfiles();

    // Initialize date edits to "today" so they have a sensible default.
    QDate today = QDate::currentDate();
    ui->fromDate->setDate(today);
    ui->toDate->setDate(today);

    // "Everything" is the default range — dates stay disabled.
    applyDateRange(ui->rangeCombo->currentText());

    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);

    restoreSettings();
}

BackupDialog::~BackupDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void BackupDialog::populateProfiles()
{
    ui->profileCombo->clear();
    m_profileIds.clear();

    ProfileRepository repo;
    QList<ProfileData> profiles = repo.findActive();

    // Determine current profile username for pre-selection.
    QString currentUsername;
    if (p_profile && p_profile->user) {
        currentUsername = p_profile->user->userName();
    }

    int preSelectIndex = 0;
    for (int i = 0; i < profiles.size(); ++i) {
        ui->profileCombo->addItem(profiles[i].username);
        m_profileIds.append(profiles[i].id);
        if (profiles[i].username == currentUsername) {
            preSelectIndex = i;
        }
    }

    if (!m_profileIds.isEmpty()) {
        ui->profileCombo->setCurrentIndex(preSelectIndex);
    }
}

void BackupDialog::applyDateRange(const QString& rangeText)
{
    bool isCustom = (rangeText == tr("Custom"));

    ui->fromDate->setEnabled(isCustom);
    ui->toDate->setEnabled(isCustom);

    if (rangeText == tr("Everything")) {
        // No date filter — backup will be full.
    } else if (rangeText == tr("Custom")) {
        // Leave dates as-is; the user controls them directly.
    } else {
        // All other presets are anchored to the last date with data for the
        // selected profile, so the range is always meaningful regardless of
        // when the dialog is opened.
        const QDate last = getLastDataDate();

        if (rangeText == tr("Most Recent Day")) {
            ui->fromDate->setDate(last);
            ui->toDate->setDate(last);
        } else if (rangeText == tr("Last Week")) {
            ui->fromDate->setDate(last.addDays(-6));
            ui->toDate->setDate(last);
        } else if (rangeText == tr("Last Fortnight")) {
            ui->fromDate->setDate(last.addDays(-13));
            ui->toDate->setDate(last);
        } else if (rangeText == tr("Last Month")) {
            ui->fromDate->setDate(last.addMonths(-1).addDays(1));
            ui->toDate->setDate(last);
        } else if (rangeText == tr("Last 6 Months")) {
            ui->fromDate->setDate(last.addMonths(-6).addDays(1));
            ui->toDate->setDate(last);
        } else if (rangeText == tr("Last Year")) {
            ui->fromDate->setDate(last.addYears(-1).addDays(1));
            ui->toDate->setDate(last);
        }
    }

    updateFilenamePreview();
}

void BackupDialog::updateFilenamePreview()
{
    if (ui->outputDirEdit->text().isEmpty()) {
        ui->filenameLabel->setText(tr("(select a directory first)"));
        ui->backupButton->setEnabled(false);
        return;
    }

    int idx = ui->profileCombo->currentIndex();
    QString username = (idx >= 0) ? ui->profileCombo->itemText(idx) : QString();
    if (username.isEmpty()) {
        ui->filenameLabel->setText(tr("(no profile selected)"));
        ui->backupButton->setEnabled(false);
        return;
    }

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString rangeText = ui->rangeCombo->currentText();
    QString filename;

    bool isPartial = (rangeText != tr("Everything"));
    if (isPartial) {
        QString start = ui->fromDate->date().toString("yyyyMMdd");
        QString end   = ui->toDate->date().toString("yyyyMMdd");
        filename = QString("profile_backup_%1_%2_%3_%4.oscar")
                       .arg(username, start, end, ts);
    } else {
        filename = QString("profile_backup_%1_%2.oscar").arg(username, ts);
    }

    ui->filenameLabel->setText(filename);
    ui->backupButton->setEnabled(true);
}

void BackupDialog::setupCalendarFormatting()
{
    // Apply locale-aware 4-digit year format and remove weekend red-highlight.
    QLocale locale = QLocale::system();
    QString fmt = locale.dateFormat(QLocale::ShortFormat);
    if (!fmt.toLower().contains("yyyy")) {
        fmt.replace("yy", "yyyy");
    }
    ui->fromDate->setDisplayFormat(fmt);
    ui->toDate->setDisplayFormat(fmt);

    for (QDateEdit* de : { ui->fromDate, ui->toDate }) {
        QCalendarWidget* cal = de->calendarWidget();
        QTextCharFormat format = cal->weekdayTextFormat(Qt::Saturday);
        format.setForeground(QBrush(Qt::black, Qt::SolidPattern));
        cal->setWeekdayTextFormat(Qt::Saturday, format);
        cal->setWeekdayTextFormat(Qt::Sunday, format);
    }
}

bool BackupDialog::showSecurityWarning()
{
    QDialog warn(this);
    warn.setWindowTitle(tr("Security Warning"));
    warn.setWindowFlags(warn.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* layout = new QVBoxLayout(&warn);

    QLabel* title = new QLabel(tr("<b>Important: Backup Security</b>"), &warn);
    layout->addWidget(title);

    QLabel* body = new QLabel(
        tr("Your .oscar backup contains sensitive medical data:\n"
           "\u2022 Sleep therapy session data and event waveforms\n"
           "\u2022 Personal information (name, date of birth, etc.)\n"
           "  (unless privacy mode is enabled)\n\n"
           "Storage recommendations:\n"
           "\u2022 Store backups on encrypted storage\n"
           "\u2022 Keep backups in a secure, access-controlled location\n"
           "\u2022 Do not share backup files with unauthorised parties"),
        &warn);
    body->setWordWrap(true);
    layout->addWidget(body);

    QCheckBox* check = new QCheckBox(
        tr("I understand and will store my backup securely"), &warn);
    layout->addWidget(check);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &warn);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Continue"));
    layout->addWidget(buttons);

    connect(check, &QCheckBox::toggled,
            buttons->button(QDialogButtonBox::Ok), &QPushButton::setEnabled);
    connect(buttons, &QDialogButtonBox::accepted, &warn, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &warn, &QDialog::reject);

    return warn.exec() == QDialog::Accepted;
}

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------

void BackupDialog::on_profileCombo_currentIndexChanged(int /*index*/)
{
    // Re-apply the current range preset so dates re-anchor to the new profile's data.
    applyDateRange(ui->rangeCombo->currentText());
}

void BackupDialog::on_rangeCombo_currentTextChanged(const QString& text)
{
    applyDateRange(text);
}

void BackupDialog::on_browseButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Output Directory"),
        ui->outputDirEdit->text().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : ui->outputDirEdit->text());

    if (!dir.isEmpty()) {
        ui->outputDirEdit->setText(dir);
        saveSettings();
        updateFilenamePreview();
    }
}

void BackupDialog::on_backupButton_clicked()
{
    int idx = ui->profileCombo->currentIndex();
    if (idx < 0 || idx >= m_profileIds.size()) {
        QMessageBox::warning(this, tr("Backup Profile"), tr("No profile selected."));
        return;
    }
    if (ui->outputDirEdit->text().isEmpty()) {
        QMessageBox::warning(this, tr("Backup Profile"), tr("Please select an output directory."));
        return;
    }
    if (!showSecurityWarning()) {
        return;
    }

    // Disable controls while running.
    ui->backupButton->setEnabled(false);
    ui->closeButton->setEnabled(false);
    ui->profileCombo->setEnabled(false);
    ui->rangeCombo->setEnabled(false);
    ui->browseButton->setEnabled(false);

    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Starting backup..."));

    qint64 profileId = m_profileIds[idx];
    ProfileBackup* backup = new ProfileBackup(profileId, this);
    backup->setOutputPath(ui->outputDirEdit->text());
    backup->setPrivacyMode(ui->privacyCheck->isChecked());

    QString rangeText = ui->rangeCombo->currentText();
    if (rangeText == tr("Everything")) {
        backup->setIncludeSDData(true);
    } else {
        backup->setDateRange(ui->fromDate->date(), ui->toDate->date());
    }

    connect(backup, &ProfileBackup::progressChanged,
            this,   &BackupDialog::onProgressChanged);
    connect(backup, &ProfileBackup::backupCompleted,
            this,   &BackupDialog::onBackupCompleted);
    connect(backup, &ProfileBackup::backupFailed,
            this,   &BackupDialog::onBackupFailed);

    backup->createBackup();
}

void BackupDialog::onProgressChanged(int percent, const QString& message)
{
    ui->progressBar->setValue(percent);
    ui->statusLabel->setText(message);
}

void BackupDialog::onBackupCompleted(const QString& path)
{
    ui->progressBar->setValue(100);
    ui->statusLabel->setText(tr("Backup complete."));

    QFileInfo fi(path);
    QString sizeStr;
    qint64 bytes = fi.size();
    if (bytes >= 1024 * 1024) {
        sizeStr = QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        sizeStr = QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }

    QMessageBox::information(this, tr("Backup Complete"),
        tr("Backup created successfully.\n\nFile: %1\nSize: %2").arg(path, sizeStr));

    accept();
}

void BackupDialog::restoreSettings()
{
    QSettings s;
    s.beginGroup("BackupDialog");
    const QString lastDir = s.value("lastOutputDir").toString();
    s.endGroup();

    if (!lastDir.isEmpty() && QDir(lastDir).exists()) {
        ui->outputDirEdit->setText(lastDir);
        updateFilenamePreview();
    }
}

void BackupDialog::saveSettings()
{
    QSettings s;
    s.beginGroup("BackupDialog");
    s.setValue("lastOutputDir", ui->outputDirEdit->text());
    s.endGroup();
}

QDate BackupDialog::getLastDataDate() const
{
    QDate last = QDate::currentDate();  // fallback if no data or query fails

    int idx = ui->profileCombo->currentIndex();
    if (idx >= 0 && idx < m_profileIds.size()) {
        qint64 pid = m_profileIds[idx];
        QSqlQuery q(DatabaseManager::instance().database());
        // sessions has no direct profile_id; reach it via machines.
        q.prepare(QStringLiteral(
            "SELECT MAX(DATE(start_time/1000, 'unixepoch')) "
            "FROM sessions "
            "WHERE machine_id IN (SELECT id FROM machines WHERE profile_id = :pid)"));
        q.bindValue(QStringLiteral(":pid"), pid);
        if (q.exec() && q.next() && !q.value(0).isNull()) {
            QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
            if (d.isValid()) last = d;
        }
    }
    return last;
}

void BackupDialog::onBackupFailed(const QString& error)
{
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Backup failed."));

    QMessageBox::critical(this, tr("Backup Failed"),
        tr("The backup could not be completed.\n\n%1").arg(error));

    // Re-enable controls.
    ui->backupButton->setEnabled(true);
    ui->closeButton->setEnabled(true);
    ui->profileCombo->setEnabled(true);
    ui->rangeCombo->setEnabled(true);
    ui->browseButton->setEnabled(true);
}
