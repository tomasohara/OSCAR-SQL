/* Share Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sharedialog.h"
#include "ui_sharedialog.h"
#include "translation.h"
#include "network/cloud_upload_dialog.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QUrl>
#include <QVBoxLayout>

#include "SleepLib/profiles.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"
#include "database/backup/profile_backup.h"
#include "mainwindow.h"

extern MainWindow *mainwin;


ShareDialog::ShareDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ShareDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupCalendarFormatting();
    populateProfiles();

    // Initialize date edits to "today" so they have a sensible default.
    QDate today = QDate::currentDate();
    ui->fromDate->setDate(today);
    ui->toDate->setDate(today);

    // "Last Week" is the default range for sharing.
    applyDateRange(ui->rangeCombo->currentText());

    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);

    connect(ui->filenameEdit, &QLineEdit::textChanged,
            this, [this](const QString& t) {
                ui->shareButton->setEnabled(
                    !t.trimmed().isEmpty() && ui->filenameEdit->isEnabled());
            });

    restoreSettings();
}

ShareDialog::~ShareDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void ShareDialog::populateProfiles()
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

void ShareDialog::applyDateRange(const QString& rangeText)
{
    bool isCustom = (rangeText == tr("Custom"));

    ui->fromDate->setEnabled(isCustom);
    ui->toDate->setEnabled(isCustom);

    if (rangeText == tr("Custom")) {
        // Leave dates as-is; the user controls them directly.
    } else {
        // All presets are anchored to the last date with data for the
        // selected profile.
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

void ShareDialog::updateFilenamePreview()
{
    ui->shareButton->setEnabled(false);

    if (ui->outputDirEdit->text().isEmpty()) {
        ui->filenameEdit->clear();
        ui->filenameEdit->setPlaceholderText(tr("(select a directory first)"));
        ui->filenameEdit->setEnabled(false);
        return;
    }

    const int idx = ui->profileCombo->currentIndex();
    if (idx < 0 || idx >= m_profileIds.size()) {
        ui->filenameEdit->clear();
        ui->filenameEdit->setPlaceholderText(tr("(no profile selected)"));
        ui->filenameEdit->setEnabled(false);
        return;
    }

    const qint64 profileId = m_profileIds[idx];
    Q_UNUSED(profileId);

    // Privacy is always on for sharing — use "p<id>" as the name portion.
    const QString namePart = QString("p%1").arg(m_profileIds[idx]);

    // Simplify is always on — use start date + day count format.
    const QDate start = ui->fromDate->date();
    const QDate end   = ui->toDate->date();
    const int count   = start.daysTo(end) + 1;

    QString filename = QString("share_%1_%2_%3.oscar")
                           .arg(namePart,
                                start.toString(QStringLiteral("yyyyMMdd")),
                                QString::number(count));

    ui->filenameEdit->setText(filename);
    ui->filenameEdit->setEnabled(true);
    ui->shareButton->setEnabled(true);
}

void ShareDialog::setupCalendarFormatting()
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

bool ShareDialog::showSharingWarning()
{
    QDialog warn(this);
    warn.setWindowTitle(tr("Sharing Medical Data"));
    warn.setWindowFlags(warn.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* layout = new QVBoxLayout(&warn);

    QLabel* title = new QLabel(tr("<b>Important: You are sharing medical data</b>"), &warn);
    layout->addWidget(title);

    QLabel* body = new QLabel(
        tr("You are about to create a file containing your sleep therapy data "
           "that will be shared with another person.\n\n"
           "\u2022 The file will contain session data, events, and machine settings\n"
           "  for the selected date range\n"
           "\u2022 Personal information (name, DOB, contact details) will be removed\n\n"
           "Make sure you trust the recipient before sharing this data."),
        &warn);
    body->setWordWrap(true);
    layout->addWidget(body);

    QCheckBox* check = new QCheckBox(
        tr("I understand this data will be shared with another person"), &warn);
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

void ShareDialog::on_profileCombo_currentIndexChanged(int /*index*/)
{
    // Re-apply the current range preset so dates re-anchor to the new profile's data.
    applyDateRange(ui->rangeCombo->currentText());
}

void ShareDialog::on_rangeCombo_currentTextChanged(const QString& text)
{
    applyDateRange(text);
}

void ShareDialog::on_browseButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Output Directory"),
        ui->outputDirEdit->text().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : ui->outputDirEdit->text(),
        QFileDialog::ShowDirsOnly | nativeDialogOption());

    if (!dir.isEmpty()) {
        ui->outputDirEdit->setText(dir);
        saveSettings();
        updateFilenamePreview();
    }
}

void ShareDialog::on_shareButton_clicked()
{
    int idx = ui->profileCombo->currentIndex();
    if (idx < 0 || idx >= m_profileIds.size()) {
        QMessageBox::warning(this, tr("Share Profile"), tr("No profile selected."));
        return;
    }
    if (ui->outputDirEdit->text().isEmpty()) {
        QMessageBox::warning(this, tr("Share Profile"), tr("Please select an output directory."));
        return;
    }
    if (!showSharingWarning()) {
        return;
    }

    // Disable controls while running.
    ui->shareButton->setEnabled(false);
    ui->closeButton->setEnabled(false);
    ui->profileCombo->setEnabled(false);
    ui->rangeCombo->setEnabled(false);
    ui->browseButton->setEnabled(false);
    ui->filenameEdit->setEnabled(false);

    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Creating sharing file..."));

    qint64 profileId = m_profileIds[idx];
    ProfileBackup* backup = new ProfileBackup(profileId, this);
    backup->setOutputPath(ui->outputDirEdit->text());
    backup->setPrivacyMode(true);     // Always on for sharing.
    backup->setIncludeSDData(false);  // Never include SD data for sharing.

    // Use the user-specified (possibly edited) filename.
    const QString filename = ui->filenameEdit->text().trimmed();
    if (!filename.isEmpty()) {
        backup->setFilename(filename);
    }

    // Always set a date range (no "Everything" option for sharing).
    backup->setDateRange(ui->fromDate->date(), ui->toDate->date());

    connect(backup, &ProfileBackup::progressChanged,
            this,   &ShareDialog::onProgressChanged);
    connect(backup, &ProfileBackup::backupCompleted,
            this,   &ShareDialog::onBackupCompleted);
    connect(backup, &ProfileBackup::backupFailed,
            this,   &ShareDialog::onBackupFailed);

    backup->createBackup();
}

void ShareDialog::onProgressChanged(int percent, const QString& message)
{
    ui->progressBar->setValue(percent);
    ui->statusLabel->setText(message);
}

void ShareDialog::onBackupCompleted(const QString& path)
{
    ui->progressBar->setValue(100);
    ui->statusLabel->setText(tr("File created."));

    QFileInfo fi(path);
    QString sizeStr;
    qint64 bytes = fi.size();
    if (bytes >= 1024 * 1024) {
        sizeStr = QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        sizeStr = QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("File Created Successfully"));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(
        tr("Your sharing file has been created.\n\n"
           "File: %1\n"
           "Size: %2\n\n"
           "To share this file:\n"
           "\u2022 Upload it to Dropbox, Google Drive, OneDrive, or another\n"
           "  cloud service and share the download link\n"
           "\u2022 Or use \"Upload to Cloud\" to upload to a temporary\n"
           "  hosting service and get a link immediately\n\n"
           "The recipient can import it in OSCAR using\n"
           "  File \u2192 Profiles \u2192 Restore Profile")
            .arg(fi.fileName(), sizeStr));

    QPushButton* uploadBtn = msgBox.addButton(tr("Upload to Cloud..."), QMessageBox::ActionRole);
    QPushButton* openFolderBtn = msgBox.addButton(tr("Open Containing Folder"), QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Close);

    msgBox.exec();

    if (msgBox.clickedButton() == uploadBtn) {
        CloudUploadDialog uploadDialog(path, this);
        uploadDialog.exec();
    } else if (msgBox.clickedButton() == openFolderBtn) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }

    accept();
}

void ShareDialog::onBackupFailed(const QString& error)
{
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Failed to create sharing file."));

    QMessageBox::critical(this, tr("Share Failed"),
        tr("The sharing file could not be created.\n\n%1").arg(error));

    // Re-enable controls.
    ui->shareButton->setEnabled(true);
    ui->closeButton->setEnabled(true);
    ui->profileCombo->setEnabled(true);
    ui->rangeCombo->setEnabled(true);
    ui->browseButton->setEnabled(true);
    ui->filenameEdit->setEnabled(true);
}

void ShareDialog::restoreSettings()
{
    QSettings s;
    s.beginGroup("ShareDialog");
    const QString lastDir = s.value("lastOutputDir").toString();
    s.endGroup();

    if (!lastDir.isEmpty() && QDir(lastDir).exists()) {
        ui->outputDirEdit->setText(lastDir);
        updateFilenamePreview();
    }
}

void ShareDialog::saveSettings()
{
    QSettings s;
    s.beginGroup("ShareDialog");
    s.setValue("lastOutputDir", ui->outputDirEdit->text());
    s.endGroup();
}

QDate ShareDialog::getFirstDataDate() const
{
    QDate first = QDate::currentDate();

    int idx = ui->profileCombo->currentIndex();
    if (idx >= 0 && idx < m_profileIds.size()) {
        qint64 pid = m_profileIds[idx];
        QSqlQuery q(DatabaseManager::instance().database());
        q.prepare(QStringLiteral(
            "SELECT MIN(DATE(start_time/1000 - 43200, 'unixepoch', 'localtime')) "
            "FROM sessions "
            "WHERE machine_id IN (SELECT id FROM machines WHERE profile_id = :pid)"));
        q.bindValue(QStringLiteral(":pid"), pid);
        if (q.exec() && q.next() && !q.value(0).isNull()) {
            QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
            if (d.isValid()) first = d;
        }
    }
    return first;
}

QDate ShareDialog::getLastDataDate() const
{
    QDate last = QDate::currentDate();

    int idx = ui->profileCombo->currentIndex();
    if (idx >= 0 && idx < m_profileIds.size()) {
        qint64 pid = m_profileIds[idx];
        QSqlQuery q(DatabaseManager::instance().database());
        q.prepare(QStringLiteral(
            "SELECT MAX(DATE(start_time/1000 - 43200, 'unixepoch', 'localtime')) "
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
