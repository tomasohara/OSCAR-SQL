# Profile Backup/Restore Implementation Plan
**Version:** 2.0
**Date:** 2026-02-18 (updated)
**Status:** Ready for Implementation
**Copyright:** Copyright (c) 2026 The OSCAR Team

---

## Overview

This document provides a detailed, step-by-step implementation plan for the Profile Backup/Restore feature as specified in `PROFILE_BACKUP_RESTORE_DESIGN.md`. This plan is designed to be executed incrementally with testable milestones.

---

## Prerequisites

### Required Knowledge
- Qt6 framework (QtCore, QtSql)
- SQLite database operations
- C++ modern practices
- OSCAR database schema (v13)
- Existing database repository pattern

### Development Environment
- Qt6 development environment
- SQLite3 tools for testing
- Access to OSCAR test data
- GitLab access for version control

---

## Phase 1: Core Infrastructure (Week 1)

### Milestone 1.1: Create Base Classes and File Structure

**Estimated Time:** 1-2 days

**Tasks:**

1. **Create directory structure**
   ```
   oscar/database/backup/
   ```

2. **Create header files**
   - `oscar/database/backup/profile_backup.h`
   - `oscar/database/backup/profile_restore.h`
   - `oscar/database/backup/backup_manifest.h`
   - `oscar/database/backup/sql_exporter.h`

3. **Create implementation files**
   - `oscar/database/backup/profile_backup.cpp`
   - `oscar/database/backup/profile_restore.cpp`
   - `oscar/database/backup/backup_manifest.cpp`
   - `oscar/database/backup/sql_exporter.cpp`

4. **Update oscar.pro**
   ```qmake
   # Add to oscar.pro
   HEADERS += \
       database/backup/profile_backup.h \
       database/backup/profile_restore.h \
       database/backup/backup_manifest.h \
       database/backup/sql_exporter.h
   
   SOURCES += \
       database/backup/profile_backup.cpp \
       database/backup/profile_restore.cpp \
       database/backup/backup_manifest.cpp \
       database/backup/sql_exporter.cpp
   ```

**Deliverable:** File structure in place, compiles without errors

---

### Milestone 1.2: Implement BackupManifest Class

**Estimated Time:** 1 day

**Purpose:** Handle manifest.json creation and parsing

**Implementation:**

```cpp
// backup_manifest.h
/**
 * @file backup_manifest.h
 * @brief Backup package manifest handler
 * Copyright (c) 2026 The OSCAR Team
 */

#ifndef BACKUP_MANIFEST_H
#define BACKUP_MANIFEST_H

#include <QObject>
#include <QJsonObject>
#include <QDateTime>

class BackupManifest
{
public:
    BackupManifest();
    
    // Setters
    void setFormatVersion(const QString& version);
    void setOscarVersion(const QString& version);
    void setSchemaVersion(int version);
    void setProfileInfo(const QString& username, qint64 profileId, 
                        const QString& dataFolder, const QString& status);
    void setStatistics(int machinesCount, int sessionsCount, 
                       int eventListsCount, const QString& firstSession,
                       const QString& lastSession, qint64 dbSize);
    void addExportedTable(const QString& tableName);
    void setExportOptions(bool includeDisabled, bool compress,
                          bool isPartial = false,
                          const QDate& startDate = QDate(),
                          const QDate& endDate = QDate(),
                          bool privacyApplied = false);
    void setChecksums(const QString& manifestChecksum, 
                      const QString& dbChecksum, 
                      const QString& packageChecksum);
    
    // Getters
    QString formatVersion() const;
    QString oscarVersion() const;
    int schemaVersion() const;
    QString username() const;
    qint64 originalProfileId() const;
    
    // Serialization
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);
    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    
    // Validation
    bool isValid() const;
    QString errorMessage() const;
    
private:
    QJsonObject m_data;
    QString m_errorMessage;
};

#endif // BACKUP_MANIFEST_H
```

**Tests:**
- Create manifest with test data
- Serialize to JSON
- Save to file
- Load from file
- Validate round-trip

**Deliverable:** Working BackupManifest class with unit tests

---

### Milestone 1.3: Implement SqlExporter Class

**Estimated Time:** 2 days

**Purpose:** Export database tables to SQL INSERT statements

**Implementation:**

```cpp
// sql_exporter.h
/**
 * @file sql_exporter.h
 * @brief SQL table export utility
 * Copyright (c) 2026 The OSCAR Team
 */

#ifndef SQL_EXPORTER_H
#define SQL_EXPORTER_H

#include <QObject>
#include <QString>
#include <QSqlQuery>
#include <QTextStream>

class SqlExporter : public QObject
{
    Q_OBJECT

public:
    explicit SqlExporter(QObject* parent = nullptr);
    
    // Export single table
    bool exportTable(const QString& tableName, 
                     const QString& whereClause,
                     const QString& outputFile);
    
    // Export table with BLOB columns (hex encoding)
    bool exportBlobTable(const QString& tableName,
                         const QString& whereClause,
                         const QString& outputFile,
                         const QStringList& blobColumns);
    
    // Helper: Convert BLOB to hex string
    static QString blobToHex(const QByteArray& blob);
    
    // Helper: Convert hex string to BLOB
    static QByteArray hexToBlob(const QString& hex);
    
    QString errorMessage() const;
    
signals:
    void progressChanged(int current, int total);
    
private:
    QString m_errorMessage;
    
    QString generateInsertStatement(const QSqlQuery& query,
                                    const QStringList& blobColumns);
    QString escapeValue(const QVariant& value, bool isBlobColumn);
};

#endif // SQL_EXPORTER_H
```

**Key Features:**
- Generate INSERT statements with all columns
- Handle NULL values correctly
- Encode BLOBs as hex strings (X'...')
- Support placeholders for ID remapping (@PROFILE_ID@, etc.)
- Progress reporting for large exports

**Tests:**
- Export simple table (profiles)
- Export table with NULLs (user_info)
- Export table with BLOBs (event_data)
- Verify SQL syntax
- Test hex encoding/decoding

**Deliverable:** Working SqlExporter with comprehensive tests

---

## Phase 2: Backup Implementation (Week 2)

### Milestone 2.1: Implement ProfileBackup Class (Basic)

**Estimated Time:** 3 days

**Implementation Steps:**

1. **Profile validation**
   ```cpp
   bool ProfileBackup::validateProfile()
   {
       // Check profile exists
       // Check database connection
       // Estimate backup size
       return true;
   }
   ```

2. **Export profile metadata**
   ```cpp
   bool ProfileBackup::exportProfileMetadata(const QString& outputDir)
   {
       // Export profiles table (always full)
       // Export user_info table:
       //   - If m_privacyMode is true, blank all data fields except id and profile_id
       //     (firstname, lastname, dob, email, phone, address, city, state, postcode, country, etc.)
       //   - If m_privacyMode is false, export normally
       // Export doctor_info table (same privacy mode logic as user_info)
       // Export profile_preferences table (always full)
       // Export channels table (always full; includes 'type' field added in v12)
       return true;
   }
   ```

3. **Export machines and sessions**
   ```cpp
   bool ProfileBackup::exportMachinesAndSessions(const QString& outputDir)
   {
       // Build date filter for sessions if m_startDate / m_endDate are set:
       //   WHERE start_time >= startEpoch AND start_time <= endEpoch
       // (No WHERE clause when doing a full export.)
       //
       // For each machine:
       //   - Export machine record (always full)
       //   - Build session_id IN (...) subquery for all matching sessions
       //   - For sessions within the date range:
       //     * Export sessions  (no events_file/summary_file columns; removed in v12)
       //     * Export session_settings  (includes profile_id; use @PROFILE_ID@ placeholder)
       //     * Export session_channels  (includes profile_id; use @PROFILE_ID@ placeholder)
       //     * Export session_channel_values
       //     * Export respiratory_events  (includes profile_id and channel_id; use @PROFILE_ID@ placeholder)
       //     * Export session_summaries  (includes profile_id; use @PROFILE_ID@ placeholder)
       //     * Export session_slices
       return true;
   }
   ```

4. **Export waveform data**
   ```cpp
   bool ProfileBackup::exportWaveformData(const QString& outputDir)
   {
       // For sessions within the date range (or all sessions for full export):
       //   - Export event_lists (metadata; includes profile_id; use @PROFILE_ID@ placeholder)
       //   - Export event_data (BLOBs as hex) for those event_lists
       // Note: report_tree is NOT exported - not profile-specific
       return true;
   }
   ```

5. **Export daily summaries**
   ```cpp
   bool ProfileBackup::exportDailySummaries(const QString& outputDir)
   {
       // Export daily_summaries table
       // If date range set: WHERE date >= startDate AND date <= endDate
       // Otherwise: export all rows for this profile
       return true;
   }
   ```

**Tests:**
- Backup profile with 1 session
- Backup profile with 10 sessions
- Backup profile with disabled sessions
- Verify all files created
- Verify SQL syntax in all files

**Deliverable:** Basic backup functionality working

---

### Milestone 2.2: Add Manifest and Packaging

**Estimated Time:** 1-2 days

**Implementation:**

1. **Create manifest**
   ```cpp
   bool ProfileBackup::createManifest(const QString& outputDir)
   {
       BackupManifest manifest;

       manifest.setFormatVersion("2.0");
       manifest.setOscarVersion(getOscarVersion());
       manifest.setSchemaVersion(getDatabaseSchemaVersion());

       // Add profile info, statistics, tables exported
       // Set export options — record date range and privacy mode:
       manifest.setExportOptions(
           m_includeDisabled,
           m_compress,
           /*isPartial=*/ m_startDate.isValid() || m_endDate.isValid(),
           m_startDate,
           m_endDate,
           m_privacyMode
       );

       manifest.saveToFile(outputDir + "/manifest.json");
       return true;
   }
   ```

2. **Create ZIP package**
   ```cpp
   bool ProfileBackup::createPackage(const QString& tempDir)
   {
       // Generate output filename:
       //   Full export:    profile_backup_<username>_<timestamp>.oscar
       //   Partial export: profile_backup_<username>_<startdate>_<enddate>_<timestamp>.oscar
       // Use QuaZip or Qt's built-in compression
       // Create .oscar file (ZIP format)
       // Add all SQL files and manifest
       // Calculate final checksum
       return true;
   }
   ```

3. **Progress reporting**
   ```cpp
   void ProfileBackup::reportProgress(int percent, const QString& message)
   {
       emit progressChanged(percent, message);
   }
   ```

**Tests:**
- Create complete backup package
- Verify ZIP file structure
- Verify manifest contents
- Verify checksums

**Deliverable:** Complete backup with packaging

---

## Phase 3: Restore Implementation (Week 2-3)

### Milestone 3.1: Implement ProfileRestore Class (Basic)

**Estimated Time:** 3 days

**Implementation Steps:**

1. **Package validation**
   ```cpp
   bool ProfileRestore::validatePackage()
   {
       // Check file exists
       // Extract to temp directory
       // Load manifest
       // Verify checksums
       // Check manifest validity
       return true;
   }
   ```

2. **Schema compatibility check**
   ```cpp
   bool ProfileRestore::checkCompatibility()
   {
       // Compare schema versions
       // Check format version
       // Schema v12+ policy: no incremental migration supported.
       // Only exact schema version match (v13 == v13) is acceptable.
       // Error if backup schema != current schema (no partial restore)
       return true;
   }
   ```

3. **Conflict detection**
   ```cpp
   ConflictStatus ProfileRestore::checkConflicts()
   {
       // Check if username exists
       // Return appropriate status
       return ConflictStatus::None;
   }
   ```

4. **Conflict resolution**
   ```cpp
   QString ProfileRestore::resolveUsernameConflict(const QString& original)
   {
       // Apply configured resolution strategy
       // - Abort: return empty string
       // - Rename: return username_restored_timestamp
       // - Replace: return original (dangerous!)
       return resolvedUsername;
   }
   ```

**Tests:**
- Validate good package
- Detect corrupted package
- Check schema compatibility
- Detect username conflicts
- Test resolution strategies

**Deliverable:** Validation and conflict resolution working

---

### Milestone 3.2: Implement ID Remapping

**Estimated Time:** 2 days

**Purpose:** Remap auto-increment IDs during restore

**Implementation:**

```cpp
class ProfileRestore
{
private:
    // ID mapping tables
    QMap<qint64, qint64> m_profileIdMap;
    QMap<qint64, qint64> m_machineIdMap;
    QMap<qint64, qint64> m_sessionIdMap;
    QMap<qint64, qint64> m_sessionChannelIdMap;
    QMap<qint64, qint64> m_eventListIdMap;
    
    // Remapping methods
    qint64 remapProfileId(qint64 oldId);
    qint64 remapMachineId(qint64 oldId);
    qint64 remapSessionId(qint64 oldId);
    qint64 remapSessionChannelId(qint64 oldId);
    qint64 remapEventListId(qint64 oldId);
    
    // SQL parsing and remapping
    QString parseAndRemapSql(const QString& sql);
};
```

**Key Features:**
- Parse SQL INSERT statements
- Replace placeholders (@PROFILE_ID@, @MACHINE_ID@, etc.)
- Track LAST_INSERT_ID after each INSERT
- Build ID mapping tables incrementally

**Tests:**
- Parse simple INSERT
- Replace single placeholder
- Replace multiple placeholders
- Handle nested replacements
- Verify foreign key integrity

**Deliverable:** Working ID remapping system

---

### Milestone 3.3: Implement Transactional Restore

**Estimated Time:** 2-3 days

**Implementation:**

```cpp
bool ProfileRestore::restoreInTransaction()
{
    QSqlDatabase db = QSqlDatabase::database();
    
    // Start transaction
    if (!db.transaction()) {
        m_errorMessage = "Failed to start transaction";
        return false;
    }
    
    try {
        emit progressChanged(20, "Restoring profile...");
        restoreProfile();
        
        emit progressChanged(30, "Restoring machines...");
        restoreMachines();
        
        emit progressChanged(50, "Restoring sessions...");
        restoreSessions();
        
        emit progressChanged(70, "Restoring waveform data...");
        restoreWaveformData();
        
        emit progressChanged(90, "Restoring daily summaries...");
        restoreDailySummaries();
        
        // Commit transaction
        if (!db.commit()) {
            throw std::runtime_error("Failed to commit transaction");
        }
        
        emit progressChanged(100, "Restore complete!");
        return true;
        
    } catch (const std::exception& e) {
        // Rollback on any error
        db.rollback();
        m_errorMessage = e.what();
        return false;
    }
}
```

**Key Features:**
- Single transaction for entire restore
- Automatic rollback on error
- Progress reporting at each stage
- Foreign key integrity maintained

**Tests:**
- Successful restore (commit)
- Failed restore (rollback)
- Verify no partial data on failure
- Verify foreign keys valid

**Deliverable:** Atomic restore with rollback

---

### Milestone 3.4: Post-Restore Validation

**Estimated Time:** 1 day

**Implementation:**

```cpp
bool ProfileRestore::validateRestore(const QJsonObject& manifest)
{
    // Verify record counts
    int expectedSessions = manifest["statistics"]["sessions_count"].toInt();
    int actualSessions = countSessions(m_newProfileId);
    
    if (expectedSessions != actualSessions) {
        m_errorMessage = QString("Session count mismatch: expected %1, got %2")
                        .arg(expectedSessions).arg(actualSessions);
        return false;
    }
    
    // Verify foreign key integrity
    if (!checkForeignKeyIntegrity(m_newProfileId)) {
        m_errorMessage = "Foreign key integrity check failed";
        return false;
    }
    
    // Verify event_data checksums (sample)
    if (!verifyEventDataChecksums(m_newProfileId)) {
        m_errorMessage = "Event data checksum verification failed";
        return false;
    }
    
    return true;
}
```

**Tests:**
- Detect count mismatches
- Detect FK violations
- Detect checksum errors
- Verify clean success

**Deliverable:** Comprehensive validation

---

## Phase 4: UI Integration (Week 3)

### Milestone 4.1: Create Backup Dialog

**Estimated Time:** 3 days

**Files:**
- `oscar/backupdialog.h`
- `oscar/backupdialog.cpp`
- `oscar/backupdialog.ui`

**UI Components:**
- Profile selection `QComboBox` (pre-selected with current profile)
- Date range section:
  - Range `QComboBox` (Everything / Most Recent Day / Last Week / Last Fortnight / Last Month / Last 6 Months / Last Year / Custom) — mirrors ExportCSV pattern
  - From `QDateEdit` + To `QDateEdit` with locale-aware 4-digit year format; disabled unless "Custom"; calendar popups colour days with data
- Privacy section:
  - `QCheckBox` "Replace personal information with blanks"
- Output section:
  - `QLineEdit` directory path + `QPushButton` Browse (opens `QFileDialog::getExistingDirectory`)
  - `QLabel` showing auto-generated filename and estimated size
- Status section:
  - `QProgressBar` (hidden until backup starts)
  - `QLabel` status message
- Buttons: Backup, Cancel

**Security warning dialog** — shown when Backup is clicked (before starting):
- Lists sensitive data categories
- Notes that privacy mode omits personal fields if checked
- Lists storage recommendations (encrypted drives, secure locations)
- `QCheckBox` "I understand and will store my backup securely"
- Continue button disabled until checkbox ticked

**Implementation:**

```cpp
class BackupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BackupDialog(QWidget* parent = nullptr);
    ~BackupDialog();

private slots:
    void onProfileChanged(int index);
    void onRangeChanged(const QString& range);
    void onStartDateChanged(const QDate& date);
    void onEndDateChanged(const QDate& date);
    void onBrowseClicked();
    void onBackupClicked();
    void onProgressChanged(int percent, const QString& message);
    void onBackupCompleted(const QString& path);
    void onBackupFailed(const QString& error);

private:
    Ui::BackupDialog* ui;
    ProfileBackup* m_backup;

    void updateFilenamePreview();
    void updateCalendarDay(QDateEdit* dateEdit, QDate date);
    bool showSecurityWarning();   // Returns true if user confirmed
};
```

**Behaviour notes:**
- When range combo changes (non-Custom), auto-set From/To and disable them
- When range combo = "Custom", enable From/To; calendar popup colours days with data
- When profile or date range changes, update filename preview and estimated size
- `showSecurityWarning()` opens a modal dialog with confirmation checkbox; returns true only if confirmed

**Deliverable:** Working backup dialog with date range, privacy mode, and security warning

---

### Milestone 4.2: Create Restore Dialog

**Estimated Time:** 2 days

**Files:**
- `oscar/restoredialog.h`
- `oscar/restoredialog.cpp`
- `oscar/restoredialog.ui`

**UI Components:**
- Package file browser (.oscar files)
- Package info display (after validation):
  - Username
  - Full or partial export (date range if partial)
  - Privacy applied: yes/no
  - Session count
  - Size
- Conflict resolution options (if conflict detected):
  - ( ) Abort
  - (•) Rename profile
  - ( ) Replace existing [dangerous]
- Restore button
- Progress bar
- Status label
- Cancel button

**Implementation:**

```cpp
class RestoreDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RestoreDialog(QWidget* parent = nullptr);
    ~RestoreDialog();

private slots:
    void onBrowseClicked();
    void onPackageSelected(const QString& path);
    void onRestoreClicked();
    void onProgressChanged(int percent, const QString& message);
    void onRestoreCompleted(qint64 profileId, const QString& username);
    void onRestoreFailed(const QString& error);

private:
    Ui::RestoreDialog* ui;
    ProfileRestore* m_restore;
    
    void displayPackageInfo(const BackupManifest& manifest);
    // Displays: username, full/partial status, date range (if partial),
    // privacy applied, session count, compressed size
    void handleConflict(ConflictStatus status);
};
```

**Deliverable:** Working restore dialog

---

### Milestone 4.3: Integrate with Main Menu

**Estimated Time:** 0.5 days

**Files to Modify:**
- `oscar/mainwindow.ui` (add menu items)
- `oscar/mainwindow.h` (add slots)
- `oscar/mainwindow.cpp` (implement slots)

**Menu Structure:**
```
File
  ├── New Profile
  ├── Open Profile
  ├── ─────────────
  ├── Backup Profile...    [NEW]
  ├── Restore Profile...   [NEW]
  ├── ─────────────
  ├── Import Data
  ├── Preferences
  ├── ─────────────
  ├── Exit
```

**Implementation:**

```cpp
// mainwindow.h
private slots:
    void on_actionBackupProfile_triggered();
    void on_actionRestoreProfile_triggered();

// mainwindow.cpp
void MainWindow::on_actionBackupProfile_triggered()
{
    BackupDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionRestoreProfile_triggered()
{
    RestoreDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // Refresh profile list
        refreshProfileList();
    }
}
```

**Deliverable:** Backup/Restore accessible from File menu

---

## Phase 5: Testing & Documentation (Week 3-4)

### Milestone 5.1: Unit Tests

**Estimated Time:** 2 days

**Test Coverage:**

1. **BackupManifest Tests**
   - Serialization/deserialization
   - Validation
   - File I/O

2. **SqlExporter Tests**
   - Simple table export
   - BLOB export
   - Hex encoding/decoding
   - SQL syntax validation

3. **ProfileBackup Tests**
   - Profile validation
   - Single session backup
   - Multiple sessions backup
   - Disabled sessions handling
   - Manifest generation
   - Package creation

4. **ProfileRestore Tests**
   - Package validation
   - Schema compatibility
   - Conflict detection
   - ID remapping
   - Transactional restore
   - Rollback on error

**Framework:** Qt Test or Google Test

**Deliverable:** Comprehensive unit test suite

---

### Milestone 5.2: Integration Tests

**Estimated Time:** 2 days

**Test Scenarios:**

1. **Round-Trip Tests**
   - Backup → Restore to same database
   - Backup → Restore to different database
   - Multiple backup/restore cycles

2. **Large Dataset Tests**
   - 1 year of data (~365 sessions)
   - 5 years of data (~1,825 sessions)
   - Multiple machines

3. **Error Handling Tests**
   - Corrupted package
   - Disk full during backup
   - Database error during restore
   - Interrupted operations

4. **Edge Cases**
   - Empty profile (no sessions)
   - Profile with only summary data
   - Profile with missing data

**Deliverable:** Passing integration tests

---

### Milestone 5.3: Documentation

**Estimated Time:** 1 day

**Documents to Create/Update:**

1. **User Documentation**
   - How to backup a profile
   - How to restore a profile
   - Handling conflicts
   - Best practices
   - Troubleshooting

2. **Developer Documentation**
   - Code documentation (Doxygen)
   - Architecture overview
   - API reference
   - Extension points

3. **Release Notes**
   - New feature announcement
   - Usage instructions
   - Known limitations

**Deliverable:** Complete documentation

---

## Implementation Checklist

### Phase 1: Core Infrastructure
- [ ] Create file structure
- [ ] Implement BackupManifest class
- [ ] Implement SqlExporter class
- [ ] Unit tests for infrastructure

### Phase 2: Backup Implementation
- [ ] Implement ProfileBackup::validateProfile()
- [ ] Implement ProfileBackup::exportProfileMetadata() — with privacy mode (blank user_info/doctor_info)
- [ ] Implement ProfileBackup::setDateRange() and buildSessionIdSubquery()
- [ ] Implement ProfileBackup::setPrivacyMode()
- [ ] Implement ProfileBackup::exportMachinesAndSessions() — with date WHERE clauses
- [ ] Implement ProfileBackup::exportWaveformData() — with date filtering
- [ ] Implement ProfileBackup::exportDailySummaries() — with date WHERE clause
- [ ] Implement ProfileBackup::createManifest() — record is_partial, date_range, privacy_applied
- [ ] Implement ProfileBackup::createPackage() — date-range filename for partial exports
- [ ] Unit tests for backup (including BK-006 through BK-010)

### Phase 3: Restore Implementation
- [ ] Implement ProfileRestore::validatePackage()
- [ ] Implement ProfileRestore::checkCompatibility()
- [ ] Implement ProfileRestore::checkConflicts()
- [ ] Implement ID remapping system
- [ ] Implement ProfileRestore::restoreInTransaction()
- [ ] Implement ProfileRestore::validateRestore()
- [ ] Unit tests for restore

### Phase 4: UI Integration
- [ ] Create BackupDialog (UI + logic):
  - [ ] Profile selection combo
  - [ ] Date range combo + From/To QDateEdit + calendar colouring
  - [ ] Privacy mode checkbox
  - [ ] Output directory + filename preview + estimated size
  - [ ] Security warning dialog (confirmation checkbox gating Continue)
  - [ ] Progress bar + status label + error dialogs
- [ ] Create RestoreDialog (UI + logic):
  - [ ] Package file selection
  - [ ] Package info display (partial/full, date range, privacy applied, session count, size)
  - [ ] Conflict resolution options
  - [ ] Progress bar + status label
- [ ] Integrate with main menu
- [ ] Manual UI testing

### Phase 5: Testing & Documentation
- [ ] Complete unit test suite
- [ ] Complete integration tests
- [ ] Write user documentation
- [ ] Write developer documentation
- [ ] Update release notes

---

## Risk Mitigation

### Technical Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Large BLOB export crashes | High | Medium | Stream BLOBs, don't load all in memory |
| Transaction timeout | High | Low | Break into smaller transactions if needed |
| ID remapping errors | High | Medium | Comprehensive testing, validation |
| ZIP library issues | Medium | Low | Use well-tested library (QuaZip) |
| Cross-platform issues | Medium | Medium | Test on all platforms early |

### Schedule Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Underestimated complexity | Delays | Add 20% buffer time |
| Integration issues | Delays | Early integration with existing code |
| Testing takes longer | Delays | Start testing early, test incrementally |

---

## Success Criteria

### Functional Requirements
- ✓ Can backup complete profile to .oscar file
- ✓ Can backup a date-range subset (partial export)
- ✓ Privacy mode blanks personal data fields before export
- ✓ Can restore profile from .oscar file
- ✓ Handles username conflicts correctly
- ✓ All data preserved (100% fidelity)
- ✓ Transactional (atomic) restore
- ✓ Partial package restore merges sessions correctly

### Non-Functional Requirements
- ✓ Backup 1 year data in <60 seconds
- ✓ Restore 1 year data in <90 seconds
- ✓ Clear progress feedback
- ✓ Helpful error messages
- ✓ Cross-platform compatible

### Quality Requirements
- ✓ All unit tests passing
- ✓ All integration tests passing
- ✓ Code documented (Doxygen)
- ✓ User documentation complete
- ✓ No critical bugs

---

## Timeline Summary

| Phase | Duration | Key Deliverables |
|-------|----------|------------------|
| Phase 1: Infrastructure | 4-5 days | Base classes, SqlExporter |
| Phase 2: Backup | 4-5 days | Working backup with packaging |
| Phase 3: Restore | 6-8 days | Working restore with validation |
| Phase 4: UI | 4-5 days | Backup/Restore dialogs |
| Phase 5: Testing/Docs | 5 days | Tests, documentation |
| **Total** | **23-28 days** | **Complete feature** |

**Note:** Timeline assumes single developer, full-time work. Adjust for part-time or multiple developers.

---

## Post-Implementation

### Phase 1 Enhancements (Future)

After initial release and user feedback:

1. **Performance Optimization**
   - Profile and optimize slow operations
   - Implement parallel export/restore
   - Add caching where appropriate

2. **User Experience**
   - Add backup verification tool
   - Show backup history
   - Add backup reminder feature

3. **Advanced Features**
   - Incremental backup
   - Encrypted backup
   - Cloud storage integration
   - Selective backup by machine or data type

### Maintenance

- Monitor user feedback
- Fix bugs promptly
- Update documentation as needed
- Consider schema version migration

---

---

**Document Status:** Ready for Implementation
**Next Action:** Begin Phase 1 implementation
**Contact:** Development team via GitLab

---

**End of Implementation Plan**
