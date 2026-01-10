# OSCAR Profile Import Feature - Implementation Plan
**Version:** 2.0 (Updated with file-based migration strategy)  
**Date:** 2026-01-10  
**Status:** Ready for Implementation

---

## Executive Summary

This document outlines the implementation plan for importing profiles from file-based OSCAR (OSCAR_Data) to the new SQL-based OSCAR 2.0 (OSCAR20_Data). The feature allows users to run both versions side-by-side for comparison and validation.

**Key Decision**: Migration will read .000 (summary) and .001 (events/waveform) files directly rather than relying on backups, as backups may be incomplete, disabled, or user-modified data may exist that's not in backups.

---

## Current State Analysis

### What Exists ✅
1. **Session File Loading** - Code exists to load .000 and .001 files (currently commented out in session.cpp)
2. **MigrationManager** - Handles profile/machine metadata migration from machines.xml  
3. **Journal Migration** - Can migrate journal .000 files to database
4. **Channel Migration** - Can migrate channels.dat to database
5. **Database Schema** - Complete schema supporting all data structures
6. **Machine Loaders** - Framework for loading various device types

### What's Missing ❌
1. **ImportProfile Dialog** - UI for selecting and importing old profiles
2. **Menu Integration** - File menu item to launch import
3. **File-based Session Loading** - Uncomment and adapt existing code
4. **Profile Folder Copy** - Selective folder structure copy
5. **Import Workflow** - Orchestrate the complete import process
6. **Progress Tracking** - User feedback during long imports

---

## Architecture Overview

```
User Selection
    ↓
[ImportProfile Dialog]
    ↓
Copy Profile Structure (Journal folders, empty)
    ↓
Load .000 Files (Session summaries)
    ↓
Load .001 Files (Events/waveforms)  
    ↓
Save to Database
    ↓
Migrate machines.xml to DB
    ↓
Calculate Daily Summaries
```

---

## Phase 1: Create Import Dialog UI

### Files to Create

#### `oscar/importprofile.h`
```cpp
#ifndef IMPORTPROFILE_H
#define IMPORTPROFILE_H

#include <QDialog>
#include <QFileSystemModel>

namespace Ui {
class ImportProfile;
}

class ImportProfile : public QDialog
{
    Q_OBJECT

public:
    explicit ImportProfile(QWidget *parent = nullptr);
    ~ImportProfile();
    
    QString selectedProfilePath() const { return m_selectedPath; }
    QString newProfileName() const { return m_newProfileName; }
    
private slots:
    void on_sourcePathButton_clicked();
    void on_importButton_clicked();
    void on_cancelButton_clicked();
    void on_profileName_textChanged(const QString &text);
    
private:
    Ui::ImportProfile *ui;
    QString m_selectedPath;
    QString m_newProfileName;
    QString m_lastImportPath;  // Remember last folder
    
    void loadSettings();
    void saveSettings();
    bool validateSelection();
    void updateStatus(const QString &message);
    qint64 calculateProfileSize(const QString &path);
};

#endif // IMPORTPROFILE_H
```

#### `oscar/importprofile.cpp`
```cpp
#include "importprofile.h"
#include "ui_importprofile.h"
#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QMessageBox>

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
    m_lastImportPath = settings.value("ImportProfile/LastPath", 
                                      QDir::homePath()).toString();
}

void ImportProfile::saveSettings()
{
    if (!m_selectedPath.isEmpty()) {
        QSettings settings("OSCAR", "OSCAR");
        QFileInfo fi(m_selectedPath);
        settings.setValue("ImportProfile/LastPath", fi.absolutePath());
    }
}

void ImportProfile::on_sourcePathButton_clicked()
{
    // Start from last used path, or OSCAR_Data/Profiles if it exists
    QString startPath = m_lastImportPath;
    QDir oscarData(QDir::homePath() + "/OSCAR_Data/Profiles");
    if (oscarData.exists()) {
        startPath = oscarData.absolutePath();
    }
    
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
    m_newProfileName = text.trimmed();
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
```

#### `oscar/importprofile.ui`
Qt Designer XML file with:
- Title label
- Description label
- Source path display + Browse button
- Profile name input field
- Status label
- Import/Cancel buttons
- Progress bar (hidden initially)

---

## Phase 2: Implement File-Based Session Loading

### Strategy

The existing code in `session.cpp` has **commented-out** file loading code that we can uncomment and adapt. Key sections:

1. **LoadSummary()** - Lines ~420-650 (commented)
2. **LoadEvents()** - Lines ~1050-1350 (commented)
3. **StoreSummary()** - Still active, can use as reference
4. **StoreEvents()** - Still active, can use as reference

### Approach

Create **import-specific** loading functions that bypass the database-first logic:

```cpp
// In session.h - add new methods
bool LoadSummaryFromFile(const QString& filename);
bool LoadEventsFromFile(const QString& filename);
```

```cpp
// In session.cpp - implement import-specific loaders
bool Session::LoadSummaryFromFile(const QString& filename)
{
    // Copy the commented-out LoadSummary() file code
    // Remove database checks
    // Load directly from .000 file
    // Populate all member variables (settings, m_cnt, m_sum, etc.)
    
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open summary file" << filename;
        return false;
    }
    
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_4_6);
    in.setByteOrder(QDataStream::LittleEndian);
    
    // Parse magic number, version, file type
    quint32 magicnum;
    quint16 version, filetype;
    in >> magicnum >> version >> filetype;
    
    if (magicnum != magic || filetype != filetype_summary) {
        return false;
    }
    
    // Load all the summary data...
    // (Full implementation based on commented code)
    
    return true;
}

bool Session::LoadEventsFromFile(const QString& filename)
{
    // Copy the commented-out LoadEvents() file code
    // Remove database checks  
    // Load directly from .001 file
    // Create EventLists and populate data
    
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;  // No events file is OK
    }
    
    // Parse header and data
    // (Full implementation based on commented code)
    
    return true;
}
```

---

## Phase 3: Implement Profile Import Orchestrator

### Create `ProfileImporter` Class

#### `oscar/profileimporter.h`
```cpp
#ifndef PROFILEIMPORTER_H
#define PROFILEIMPORTER_H

#include <QString>
#include <QObject>

class Profile;
class ProgressDialog;

class ProfileImporter : public QObject
{
    Q_OBJECT
    
public:
    ProfileImporter(QObject *parent = nullptr);
    ~ProfileImporter();
    
    bool importProfile(const QString& sourcePath,
                      const QString& newProfileName,
                      ProgressDialog* progress);
    
    QString lastError() const { return m_lastError; }
    
signals:
    void progressChanged(int current, int total, const QString& message);
    
private:
    QString m_lastError;
    
    // Phase 1: Copy folder structure
    bool copyProfileStructure(const QString& oldPath, const QString& newPath);
    bool copyJournalFolders(const QString& oldPath, const QString& newPath);
    
    // Phase 2: Load sessions from files
    bool loadSessionsFromFiles(Profile* profile, const QString& oldPath);
    bool loadMachineSessions(Machine* machine, const QString& oldMachinePath);
    
    // Phase 3: Migrate metadata
    bool migrateMetadata(Profile* profile, const QString& oldPath);
    
    // Phase 4: Calculate summaries
    bool calculateSummaries(Profile* profile);
    
    // Validation
    bool validateSourceProfile(const QString& path);
    
    // Rollback on failure
    bool rollbackImport(const QString& profilePath);
};

#endif // PROFILEIMPORTER_H
```

#### `oscar/profileimporter.cpp`
```cpp
#include "profileimporter.h"
#include "SleepLib/profiles.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"
#include "SleepLib/journal.h"
#include "database/migration_manager.h"
#include "progressdialog.h"
#include <QDir>
#include <QFile>

bool ProfileImporter::importProfile(const QString& sourcePath,
                                    const QString& newProfileName,
                                    ProgressDialog* progress)
{
    emit progressChanged(0, 100, tr("Validating source profile..."));
    
    if (!validateSourceProfile(sourcePath)) {
        m_lastError = tr("Invalid source profile");
        return false;
    }
    
    // Create new profile folder
    QString newPath = GetAppData() + "/Profiles/" + newProfileName;
    QDir().mkpath(newPath);
    
    emit progressChanged(10, 100, tr("Copying profile structure..."));
    
    if (!copyProfileStructure(sourcePath, newPath)) {
        rollbackImport(newPath);
        return false;
    }
    
    emit progressChanged(20, 100, tr("Creating profile in database..."));
    
    // Create profile object
    Profile* profile = new Profile(newPath, false);
    
    emit progressChanged(30, 100, tr("Migrating profile metadata..."));
    
    if (!migrateMetadata(profile, sourcePath)) {
        rollbackImport(newPath);
        delete profile;
        return false;
    }
    
    emit progressChanged(40, 100, tr("Loading session data from files..."));
    
    if (!loadSessionsFromFiles(profile, sourcePath)) {
        rollbackImport(newPath);
        delete profile;
        return false;
    }
    
    emit progressChanged(90, 100, tr("Calculating daily summaries..."));
    
    if (!calculateSummaries(profile)) {
        // Don't fail for this - summaries can be regenerated
        qWarning() << "Failed to calculate summaries, but import succeeded";
    }
    
    emit progressChanged(100, 100, tr("Import complete!"));
    
    delete profile;
    return true;
}

bool ProfileImporter::copyProfileStructure(const QString& oldPath, 
                                          const QString& newPath)
{
    QDir oldDir(oldPath);
    QDir newDir(newPath);
    
    // 1. Copy journal folders (structure only, empty)
    if (!copyJournalFolders(oldPath, newPath)) {
        return false;
    }
    
    // 2. Copy machine folders with their structure
    // We'll scan for machine folders and copy them
    QStringList entries = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& entry : entries) {
        // Skip journal folders (already handled)
        if (entry.startsWith("Journal_")) continue;
        
        // Skip generated files
        if (entry == "Summaries" || entry == "Events") continue;
        
        // This must be a machine folder - copy it
        QString oldMachinePath = oldPath + "/" + entry;
        QString newMachinePath = newPath + "/" + entry;
        
        QDir().mkpath(newMachinePath);
        
        // Don't copy .000/.001 files yet - we'll load and save to DB
        // Just create the folder structure
    }
    
    return true;
}

bool ProfileImporter::copyJournalFolders(const QString& oldPath, 
                                        const QString& newPath)
{
    QDir oldDir(oldPath);
    QStringList entries = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& entry : entries) {
        if (entry.startsWith("Journal_")) {
            QDir().mkpath(newPath + "/" + entry);
        }
    }
    
    return true;
}

bool ProfileImporter::loadSessionsFromFiles(Profile* profile, 
                                           const QString& oldPath)
{
    // Scan for machine folders in old profile
    QDir oldDir(oldPath);
    QStringList entries = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& entry : entries) {
        // Skip non-machine folders
        if (entry.startsWith("Journal_") || 
            entry == "Summaries" || 
            entry == "Events") {
            continue;
        }
        
        QString oldMachinePath = oldPath + "/" + entry;
        
        // Find corresponding machine in profile
        // This requires machines.xml to already be migrated
        Machine* machine = findMachineByFolderName(profile, entry);
        if (!machine) {
            qWarning() << "Could not find machine for folder" << entry;
            continue;
        }
        
        if (!loadMachineSessions(machine, oldMachinePath)) {
            return false;
        }
    }
    
    return true;
}

bool ProfileImporter::loadMachineSessions(Machine* machine, 
                                         const QString& oldMachinePath)
{
    // Scan for .000 files in Summaries subdirectory
    QString summariesPath = oldMachinePath + "/Summaries";
    QDir summariesDir(summariesPath);
    
    if (!summariesDir.exists()) {
        qDebug() << "No Summaries folder for" << oldMachinePath;
        return true;  // Not an error
    }
    
    QStringList filters;
    filters << "*.000";
    QFileInfoList files = summariesDir.entryInfoList(filters, QDir::Files);
    
    emit progressChanged(0, files.size(), 
        tr("Loading %1 sessions from %2...").arg(files.size())
                                            .arg(machine->loaderName()));
    
    int loaded = 0;
    
    for (const QFileInfo& fileInfo : files) {
        // Parse session ID from filename (hex)
        QString baseName = fileInfo.baseName();
        bool ok;
        SessionID sessionId = baseName.toLongLong(&ok, 16);
        
        if (!ok) {
            qWarning() << "Invalid session filename:" << fileInfo.fileName();
            continue;
        }
        
        // Create session
        Session* session = new Session(machine, sessionId);
        
        // Load summary from .000 file
        if (!session->LoadSummaryFromFile(fileInfo.absoluteFilePath())) {
            qWarning() << "Failed to load summary:" << fileInfo.fileName();
            delete session;
            continue;
        }
        
        // Load events from .001 file (if exists)
        QString eventsPath = oldMachinePath + "/Events/" + baseName + ".001";
        if (QFile::exists(eventsPath)) {
            session->LoadEventsFromFile(eventsPath);
        }
        
        // Save to database
        session->StoreToDatabase();
        
        // Add to machine
        machine->addSession(session);
        
        loaded++;
        emit progressChanged(loaded, files.size(), 
            tr("Loaded %1 of %2 sessions...").arg(loaded).arg(files.size()));
    }
    
    return true;
}

bool ProfileImporter::migrateMetadata(Profile* profile, 
                                     const QString& oldPath)
{
    // Use existing MigrationManager to handle machines.xml
    MigrationManager migrator;
    
    if (!migrator.migrateProfile(oldPath)) {
        m_lastError = migrator.lastError();
        return false;
    }
    
    // Migrate journal if needed
    if (Journal::NeedsMigration(profile)) {
        if (!Journal::MigrateToDatabase(profile)) {
            qWarning() << "Journal migration failed";
            // Don't fail entire import for this
        }
    }
    
    return true;
}

bool ProfileImporter::calculateSummaries(Profile* profile)
{
    profile->calculateDailySummaries();
    return true;
}

bool ProfileImporter::validateSourceProfile(const QString& path)
{
    QDir dir(path);
    
    if (!dir.exists()) {
        m_lastError = tr("Source path does not exist");
        return false;
    }
    
    if (!QFile::exists(path + "/machines.xml")) {
        m_lastError = tr("Source is not a valid OSCAR profile (missing machines.xml)");
        return false;
    }
    
    return true;
}

bool ProfileImporter::rollbackImport(const QString& profilePath)
{
    // Delete the partially created profile
    QDir dir(profilePath);
    if (dir.exists()) {
        return dir.removeRecursively();
    }
    return true;
}
```

---

## Phase 4: Menu Integration

### Modify `oscar/mainwindow.ui`
Add menu item in Qt Designer:
```
File
  ├── Import Data...
  ├── Import from OSCAR 1.0...  [NEW]
  ├── Export Data...
```

### Modify `oscar/mainwindow.h`
```cpp
private slots:
    void on_action_Import_From_OSCAR_triggered();
```

### Modify `oscar/mainwindow.cpp`
```cpp
void MainWindow::on_action_Import_From_OSCAR_triggered()
{
    // Show import dialog
    ImportProfile dialog(this);
    
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    
    QString sourcePath = dialog.selectedProfilePath();
    QString newName = dialog.newProfileName();
    
    // Create progress dialog
    ProgressDialog progress(this);
    progress.setWindowTitle(tr("Importing Profile"));
    progress.show();
    
    // Perform import
    ProfileImporter importer;
    connect(&importer, &ProfileImporter::progressChanged,
            &progress, &ProgressDialog::setProgress);
    
    bool success = importer.importProfile(sourcePath, newName, &progress);
    
    progress.close();
    
    if (success) {
        QMessageBox::information(this, tr("Import Complete"),
            tr("Profile '%1' has been successfully imported.\n\n"
               "You can now select it from the profile selector.").arg(newName));
    } else {
        QMessageBox::critical(this, tr("Import Failed"),
            tr("Failed to import profile:\n%1").arg(importer.lastError()));
    }
}
```

### Modify `oscar/oscar.pro`
Add new files to build:
```qmake
HEADERS += \
    importprofile.h \
    profileimporter.h \
    # ...

SOURCES += \
    importprofile.cpp \
    profileimporter.cpp \
    # ...

FORMS += \
    importprofile.ui \
    # ...
```

---

## Phase 5: Testing Strategy

### Test Cases

1. **Basic Import**
   - Import profile with single CPAP machine
   - Verify all sessions load correctly
   - Verify statistics match original

2. **Multiple Machines**
   - Import profile with CPAP + oximetry
   - Verify both machine types load
   - Verify data correlates correctly

3. **Large Profile**
   - Import profile > 4GB
   - Verify warning appears
   - Verify import completes successfully

4. **Edge Cases**
   - Profile with journal data
   - Profile with missing .001 files (summary-only)
   - Profile with corrupt data files
   - Profile with disabled sessions

5. **Error Handling**
   - Invalid source path
   - Insufficient disk space
   - Name conflict resolution
   - Import cancellation
   - Rollback after failure

6. **Data Integrity**
   - Compare AHI values
   - Compare event counts
   - Compare pressure statistics
   - Compare graph rendering

---

## Implementation Checklist

### Phase 1: Dialog UI
- [ ] Create `importprofile.h`
- [ ] Create `importprofile.cpp`
- [ ] Create `importprofile.ui` in Qt Designer
- [ ] Implement folder selection with validation
- [ ] Implement name conflict resolution
- [ ] Implement size calculation and warning
- [ ] Implement settings persistence (remember last path)

### Phase 2: File Loading
- [ ] Add `LoadSummaryFromFile()` to Session class
- [ ] Add `LoadEventsFromFile()` to Session class
- [ ] Uncomment and adapt file reading code from session.cpp
- [ ] Test loading various .000 file versions
- [ ] Test loading various .001 file versions
- [ ] Handle missing event files gracefully

### Phase 3: Import Orchestrator
- [ ] Create `profileimporter.h`
- [ ] Create `profileimporter.cpp`
- [ ] Implement folder structure copy
- [ ] Implement session loading from files
- [ ] Implement metadata migration
- [ ] Implement progress reporting
- [ ] Implement rollback on failure

### Phase 4: Integration
- [ ] Add menu item to mainwindow.ui
- [ ] Add slot to mainwindow.h
- [ ] Implement slot in mainwindow.cpp
- [ ] Add files to oscar.pro
- [ ] Test compilation

### Phase 5: Testing
- [ ] Test with small profile (< 100 sessions)
- [ ] Test with large profile (> 1000 sessions)
- [ ] Test with multiple machine types
- [ ] Test journal data import
- [ ] Test error cases
- [ ] Test rollback
- [ ] Compare data integrity with original

### Phase 6: Documentation
- [ ] Update user manual
- [ ] Add FAQ entries
- [ ] Document limitations
- [ ] Create migration guide

---

## Design Decisions Summary

| Decision | Rationale |
|----------|-----------|
| **Separate data directories** | Allows both versions to run side-by-side |
| **Read .000/.001 files directly** | Backups incomplete; user may have modified data |
| **Auto-append for name conflicts** | Better UX than forcing user to retry |
| **Rollback on failure** | Prevents partial/corrupt profiles |
| **Warn if > 4GB** | Sets user expectations for long operations |
| **Remember last import path** | Better UX for importing multiple profiles |
| **No backup of original** | Not modifying source; import is read-only |

---

## File Format Reference

### .000 Summary File Format
```
Header:
  - magic (quint32)
  - version (quint16)
  - filetype (quint16)
  - machine_id (quint32)
  - session_id (quint32)
  - start_time (qint64)
  - end_time (qint64)

Data:
  - settings (QHash<ChannelID, QVariant>)
  - channel counts (QHash<ChannelID, int>)
  - channel statistics (sum, avg, wavg, min, max, etc.)
  - value/time summaries
  - slices
```

### .001 Events File Format
```
Header:
  - magic (quint32)
  - version (quint16)
  - filetype (quint16)
  - machine_id (quint32)
  - session_id (quint32)
  - start_time (qint64)
  - end_time (qint64)
  - compression_method (quint16)
  - machine_type (quint16)
  - data_size (qint32)
  - checksum (quint16)

EventList Metadata (per channel):
  - channel_id
  - eventlist_count
  - For each EventList:
    - first_time, last_time
    - count, type, rate
    - gain, offset, min, max
    - dimension
    - has_second_field

EventList Data (binary):
  - Primary data array (qint16[])
  - Secondary data array (qint16[]) [if present]
  - Time delta array (quint32[]) [if not waveform]
```

---

## Risk Assessment

### High Risk
- **Data corruption** during migration
  - Mitigation: Extensive validation, rollback on failure
  
- **Performance** on large profiles
  - Mitigation: Progress reporting, background thread, size warning

### Medium Risk
- **Incompatible file versions**
  - Mitigation: Version checking, graceful degradation
  
- **Disk space** exhaustion
  - Mitigation: Pre-calculate size, check available space

### Low Risk
- **Name conflicts**
  - Mitigation: Auto-rename with (copy N)
  
- **UI responsiveness**
  - Mitigation: Progress dialog, async operations

---

## Future Enhancements

1. **Batch Import** - Import multiple profiles at once
2. **Selective Import** - Choose which machines to import
3. **Date Range Filter** - Import only recent data
4. **Dry Run Mode** - Preview import without committing
5. **Import Validation Report** - Detailed comparison with source

---

## References

- Database Schema: `Notes/DATABASE_SCHEMA_REFERENCE.md`
- Session class: `oscar/SleepLib/session.cpp`
- Migration Manager: `oscar/database/migration_manager.cpp`
- Journal Migration: `oscar/SleepLib/journal.cpp`

---

**Document Version:** 2.0  
**Last Updated:** 2026-01-10  
**Status:** Ready for implementation
