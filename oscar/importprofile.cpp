/* Import Profile Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ImportProfile dialog implementation for importing
 * profiles from file-based OSCAR (OSCAR_Data) to SQL-based OSCAR 2.0.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "importprofile.h"
#include "ui_importprofile.h"
#include "common_gui.h"
#include "SleepLib/preferences.h"
#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QMessageBox>
#include <QFileInfo>

ImportProfile::ImportProfile(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImportProfile)
{
    ui->setupUi(this);
    ui->importButton->setEnabled(false);
    loadSettings();
}

ImportProfile::~ImportProfile()
{
    saveSettings();
    delete ui;
}

void ImportProfile::loadSettings()
{
    QSettings settings("OSCAR", "OSCAR");
    m_lastImportPath = settings.value("ImportProfile/LastPath", "").toString();
}

void ImportProfile::saveSettings()
{
    if (!m_selectedPath.isEmpty()) {
        QSettings settings("OSCAR", "OSCAR");
        // Save the parent directory of the selected profile folder
        QFileInfo fi(m_selectedPath);
        settings.setValue("ImportProfile/LastPath", fi.absolutePath());
    }
}

QString ImportProfile::getDefaultImportPath()
{
    // If we have a remembered path, use it
    if (!m_lastImportPath.isEmpty() && QDir(m_lastImportPath).exists()) {
        return m_lastImportPath;
    }
    
    // Try ../OSCAR_Data/Profiles relative to current data directory
    QString appData = GetAppData();  // Current OSCAR20_Data location
    QDir currentDir(appData);
    
    // Go up one level and look for OSCAR_Data
    if (currentDir.cdUp()) {
        QString oscarDataPath = currentDir.absolutePath() + "/OSCAR_Data/Profiles";
        if (QDir(oscarDataPath).exists()) {
            return oscarDataPath;
        }
    }
    
    // Fall back to home directory
    return QDir::homePath();
}

void ImportProfile::on_sourcePathButton_clicked()
{
    QString startPath = getDefaultImportPath();
    
    QString path = QFileDialog::getExistingDirectory(
        this,
        tr("Select Profile Folder from File-Based OSCAR"),
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (path.isEmpty()) return;
    
    // Validate that this looks like a profile folder
    QDir dir(path);
    if (!dir.exists("machines.xml")) {
        QMessageBox::warning(this, tr("Invalid Profile"),
            tr("The selected folder does not appear to be a valid OSCAR profile.\n"
               "Please select a folder that contains machines.xml"));
        return;
    }
    
    // Check profile size and warn if > 4GB
    qint64 sizeBytes = calculateProfileSize(path);
    if (sizeBytes > 4294967296LL) {  // 4GB
        double sizeGB = sizeBytes / 1073741824.0;
        int ret = QMessageBox::warning(this, tr("Large Profile"),
            tr("This profile is %.1f GB in size.\n"
               "Import may take a significant amount of time.\n\n"
               "Do you want to continue?").arg(sizeGB),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }
    
    m_selectedPath = path;
    
    // Extract profile name from folder name
    QString folderName = QFileInfo(path).fileName();
    ui->profileName->setText(folderName);
    
    ui->sourcePathLabel->setText(tr("Source: %1").arg(path));
    updateStatus(tr("Ready to import. Enter a name for the new profile."));
}

qint64 ImportProfile::calculateProfileSize(const QString &path)
{
    qint64 totalSize = 0;
    QDir dir(path);
    
    QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, 
        QDir::DirsFirst
    );
    
    for (const QFileInfo &info : entries) {
        if (info.isFile()) {
            totalSize += info.size();
        } else if (info.isDir()) {
            totalSize += calculateProfileSize(info.absoluteFilePath());
        }
    }
    
    return totalSize;
}

void ImportProfile::on_profileName_textChanged(const QString &text)
{
    Q_UNUSED(text);
    m_newProfileName = ui->profileName->text().trimmed();
    ui->importButton->setEnabled(validateSelection());
}

bool ImportProfile::validateSelection()
{
    if (m_selectedPath.isEmpty()) {
        updateStatus(tr("Please select a source profile folder."));
        return false;
    }
    
    if (m_newProfileName.isEmpty()) {
        updateStatus(tr("Please enter a profile name."));
        return false;
    }
    
    // Check if profile name already exists
    QString profilesPath = GetAppData() + "/Profiles/" + m_newProfileName;
    if (QDir(profilesPath).exists()) {
        // Auto-append (copy) or number
        int num = 2;
        QString baseName = m_newProfileName;
        while (QDir(GetAppData() + "/Profiles/" + m_newProfileName).exists()) {
            m_newProfileName = baseName + QString(" (copy %1)").arg(num);
            num++;
        }
        ui->profileName->setText(m_newProfileName);
        updateStatus(tr("Profile name exists. Using: %1").arg(m_newProfileName));
        return true;
    }
    
    updateStatus(tr("Ready to import."));
    return true;
}

void ImportProfile::updateStatus(const QString &message)
{
    ui->statusLabel->setText(message);
}

void ImportProfile::on_importButton_clicked()
{
    accept();
}

void ImportProfile::on_cancelButton_clicked()
{
    reject();
}
