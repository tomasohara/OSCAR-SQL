# Profile Backup/Restore Design
**Version:** 2.0  
**Date:** 2026 Q1  
**Status:** Design Document - Updated for Schema v8  
**Copyright:** Copyright (c) 2026 The OSCAR Team

---

## Overview

This document describes the design for backing up and restoring complete OSCAR profiles. With Schema v8's database-only architecture, **ALL profile data now resides in the SQLite database**, including waveforms, events, and metadata. This dramatically simplifies backup and restore operations compared to the original design.

---

## Key Changes from v1.0 Design

### What Changed in Schema v8

1. **Database-Only Storage**: All data (metadata, waveforms, events) now stored in SQLite database
2. **No External Files**: Eliminated .001 files - waveform/event data stored in `event_data` table as BLOBs
3. **Simplified Backup**: Single database file contains 100% of profile data
4. **Transactional Integrity**: All operations protected by ACID transactions

### Impact on Backup/Restore

| Aspect | Original Design (v1.0) | Updated Design (v2.0) |
|--------|----------------------|----------------------|
| **Backup Source** | Database + .001 files | Database only |
| **Package Contents** | SQL files + file tree | Database export only |
| **Complexity** | High (sync DB + files) | Low (single source) |
| **Integrity** | File/DB sync issues | Transactional guarantee |
| **Size** | Large (uncompressed files) | Smaller (compressed BLOBs) |

---

## Requirements

### Functional Requirements

1. **Backup Complete Profile**
   - Export all database records for a profile
   - Maintain data integrity and relationships
   - Single-file backup package

2. **Restore Profile**
   - Import into existing database
   - Handle ID conflicts/remapping
   - Handle username conflicts
   - Preserve all data relationships
   - Transaction-based (all-or-nothing)

3. **Portable Format**
   - Self-contained package
   - Cross-platform compatible
   - Human-readable metadata
   - Compressed for efficiency

4. **Safety**
   - Validate data before restore
   - Transaction-based (atomic operations)
   - No data loss on failure
   - Clear error messages

### Non-Functional Requirements

1. **Performance**: Backup/restore should handle years of data efficiently
2. **Reliability**: 100% data fidelity
3. **Usability**: Progress feedback, clear error messages
4. **Compatibility**: Forward/backward compatible within reason

---

## Backup Package Format

### Package Structure (Simplified)

```
profile_backup_<username>_<timestamp>.oscar
├── manifest.json                    # Package metadata
└── database/
    ├── profile.sql                  # Profile record
    ├── user_info.sql               # User information
    ├── doctor_info.sql             # Doctor information
    ├── preferences.sql             # All preferences
    ├── channels.sql                # Channel customizations
    ├── daily_summaries.sql         # Daily summaries
    └── machines/
        ├── machine_<id>.sql        # Machine record
        └── machine_<id>_sessions/
            ├── sessions.sql        # All sessions
            ├── session_settings.sql
            ├── session_channels.sql
            ├── session_channel_values.sql    # v7+ value/time summaries
            ├── respiratory_events.sql
            ├── session_summaries.sql
            ├── session_slices.sql
            ├── event_lists.sql             # v8+ EventList metadata
            └── event_data.sql              # v8+ Waveform/event BLOBs
```

**Note**: No `files/` directory needed - all data is in the database!

### Manifest Format (manifest.json)

```json
{
  "format_version": "2.0",
  "oscar_version": "1.6.0",
  "schema_version": 8,
  "export_date": "2026-01-06T18:00:00Z",
  "exported_by": "OSCAR Profile Backup v2.0",
  
  "profile": {
    "username": "JohnDoe",
    "original_profile_id": 123,
    "data_folder": "PROF/JohnDoe",
    "created_at": "2024-01-01T00:00:00Z",
    "status": "active"
  },
  
  "statistics": {
    "machines_count": 2,
    "sessions_count": 1825,
    "event_lists_count": 14600,
    "date_range": {
      "first_session": "2020-01-01",
      "last_session": "2025-12-28"
    },
    "total_nights": 1825,
    "database_size_bytes": 524288000,
    "compressed_size_bytes": 262144000
  },
  
  "tables_exported": [
    "profiles",
    "user_info",
    "doctor_info",
    "profile_preferences",
    "channels",
    "machines",
    "sessions",
    "session_settings",
    "session_channels",
    "session_channel_values",
    "respiratory_events",
    "session_summaries",
    "session_slices",
    "daily_summaries",
    "event_lists",
    "event_data"
  ],
  
  "export_options": {
    "include_disabled_sessions": true,
    "include_archived_data": true,
    "compress_package": true,
    "database_only": true
  },
  
  "checksums": {
    "manifest": "sha256:...",
    "database_export": "sha256:...",
    "package": "sha256:..."
  }
}
```

---

## SQL Export Format

### Key Principles

1. **Use INSERT statements** with explicit column names
2. **Export in dependency order** (parents before children)
3. **Use placeholders for IDs** to enable remapping
4. **Include BLOB data** encoded as hex strings for portability
5. **Transaction-wrapped** for safety

### Example: Event Data Export (New in v8)

```sql
-- EventList metadata
INSERT INTO event_lists (
    session_id,           -- Will be remapped
    channel_id,
    eventlist_index,
    event_type,
    first_time,
    last_time,
    count,
    rate,
    gain,
    offset,
    min_value,
    max_value,
    dimension,
    has_second_field,
    min2_value,
    max2_value,
    data_size,
    compressed_size,
    created_at
) VALUES (
    @SESSION_ID@,         -- Placeholder
    0x1003,              -- CPAP_Pressure channel
    0,
    1,                   -- Waveform type
    1672531200000,       -- Unix ms
    1672560000000,
    86400,               -- 24 hours * 3600 samples
    1000.0,              -- 1 Hz
    0.01,                -- Gain
    0.0,                 -- Offset
    4.0,                 -- Min pressure
    20.0,                -- Max pressure
    'cmH₂O',
    0,
    NULL,
    NULL,
    172800,              -- 86400 * 2 bytes
    68920,               -- 40% compression
    '2026-01-01T00:00:00Z'
);
-- @EVENT_LIST_ID@ = LAST_INSERT_ID

-- EventData BLOB (compressed)
INSERT INTO event_data (
    eventlist_id,
    data_blob,
    data_compressed,
    data2_blob,
    data2_compressed,
    time_blob,
    time_compressed,
    compression_method,
    checksum,
    created_at
) VALUES (
    @EVENT_LIST_ID@,      -- Remapped
    NULL,                 -- Uncompressed not stored
    X'789C...',          -- Hex-encoded compressed BLOB
    NULL,
    NULL,
    NULL,
    X'789C...',          -- Compressed time deltas
    1,                   -- qCompress
    0xA5F3,              -- CRC16
    '2026-01-01T00:00:00Z'
);
```

---

## Backup Process (Simplified)

### Phase 1: Validation

1. Verify profile exists and is accessible
2. Check database connection
3. Estimate backup size

### Phase 2: Database Export

```
For profile_id:
  1. Export profiles table → profile.sql
  2. Export user_info → user_info.sql
  3. Export doctor_info → doctor_info.sql
  4. Export profile_preferences → preferences.sql
  5. Export channels → channels.sql
  6. Export daily_summaries → daily_summaries.sql
  
  For each machine:
    7. Export machines → machine_<id>.sql
    
    For each session:
      8. Export sessions → sessions.sql
      9. Export session_settings → session_settings.sql
      10. Export session_channels → session_channels.sql
      11. Export session_channel_values → session_channel_values.sql
      12. Export respiratory_events → respiratory_events.sql
      13. Export session_summaries → session_summaries.sql
      14. Export session_slices → session_slices.sql
      15. Export event_lists → event_lists.sql (v8+)
      16. Export event_data → event_data.sql (v8+ with BLOBs)
```

### Phase 3: Package Creation

```
1. Create temporary directory
2. Write manifest.json
3. Write all SQL files
4. Calculate checksums
5. Create ZIP archive: profile_backup_<username>_<timestamp>.oscar
6. Cleanup temporary files
```

**Eliminated Phase**: File copying (no longer needed!)

---

## Restore Process (Simplified)

### Phase 1: Validation

1. **Verify package integrity**
   - Check file exists
   - Verify checksums
   - Validate manifest.json

2. **Check compatibility**
   - Verify format_version supported
   - Verify schema_version compatible (≤8)
   - Check OSCAR version compatibility

3. **Check for conflicts**
   - Username already exists?

### Phase 2: Conflict Resolution

#### Scenario: Username Exists

```
Options:
1. Abort restore
2. Rename restored profile (append _restored, _2, etc.)
3. Replace existing (dangerous, requires confirmation)
```

**Recommended**: Rename restored profile with suffix `_restored_<timestamp>`

### Phase 3: ID Remapping Strategy

```
During restore, all auto-increment IDs must be remapped:

Original IDs → New IDs Mapping Table:
- profile_id: 123 → 456
- machine_id (DB): 42 → 789
  - machine_id (OSCAR): 0 → 0 (preserved)
- session_id (DB): 1000-2825 → 5000-6825
  - session_id (OSCAR): per machine (preserved)
- event_list_id (DB): 10000-24600 → 50000-64600 (NEW)

Process:
1. Parse SQL files
2. Replace @PROFILE_ID@ with new profile_id
3. Execute INSERT, capture LAST_INSERT_ID
4. Build mapping table as we go
5. Use mapping for foreign key references
```

### Phase 4: Database Restore (Transaction-Based)

```sql
BEGIN TRANSACTION;

-- Step 1: Restore profile
INSERT INTO profiles (...) VALUES (...);
SET @NEW_PROFILE_ID = LAST_INSERT_ID();

-- Step 2: Restore user_info (remap profile_id)
INSERT INTO user_info (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);

-- Step 3: Restore doctor_info (remap profile_id)
INSERT INTO doctor_info (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);

-- Step 4: Restore preferences (remap profile_id for each)
INSERT INTO profile_preferences (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);
-- ... repeat for all preferences

-- Step 5: Restore channels (remap profile_id)
INSERT INTO channels (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);
-- ... repeat for all channels

-- Step 6: Restore machines
FOR EACH machine:
  INSERT INTO machines (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);
  SET @NEW_MACHINE_ID = LAST_INSERT_ID();
  
  -- Step 7: Restore sessions for this machine
  FOR EACH session:
    INSERT INTO sessions (machine_id, ...) VALUES (@NEW_MACHINE_ID, ...);
    SET @NEW_SESSION_ID = LAST_INSERT_ID();
    
    -- Restore session metadata
    INSERT INTO session_settings (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    INSERT INTO session_channels (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    SET @NEW_SESSION_CHANNEL_ID = LAST_INSERT_ID();
    
    -- Restore session_channel_values (v7+)
    INSERT INTO session_channel_values (session_channel_id, ...) 
    VALUES (@NEW_SESSION_CHANNEL_ID, ...);
    
    -- Restore events
    INSERT INTO respiratory_events (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    INSERT INTO session_summaries (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    INSERT INTO session_slices (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Restore waveform/event data (v8+)
    FOR EACH event_list:
      INSERT INTO event_lists (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
      SET @NEW_EVENT_LIST_ID = LAST_INSERT_ID();
      
      INSERT INTO event_data (eventlist_id, data_compressed, ...) 
      VALUES (@NEW_EVENT_LIST_ID, X'...', ...);

-- Step 8: Restore daily_summaries (remap profile_id and machine_id)
INSERT INTO daily_summaries (profile_id, machine_id, ...) 
VALUES (@NEW_PROFILE_ID, @NEW_MACHINE_ID, ...);
-- ... repeat for all daily summaries

COMMIT;
-- If any error occurs, ROLLBACK
```

### Phase 5: Post-Restore Validation

```
1. Verify record counts match manifest
2. Verify event_data checksums
3. Check foreign key integrity
4. Recalculate daily_summaries if needed
5. Mark profile as active
```

**Eliminated Phase**: File copying and validation (no longer needed!)

---

## Class Design

### ProfileBackup

```cpp
/**
 * @file profile_backup.h
 * @brief Profile backup functionality for OSCAR database
 * Copyright (c) 2026 The OSCAR Team
 */

class ProfileBackup : public QObject
{
    Q_OBJECT

public:
    explicit ProfileBackup(qint64 profileId, QObject* parent = nullptr);
    ~ProfileBackup();
    
    // Configuration
    void setOutputPath(const QString& path);
    void setIncludeDisabledSessions(bool include);
    void setCompression(bool compress);
    
    // Execution
    bool createBackup();
    
    // Status
    QString getErrorMessage() const;
    QString getBackupPath() const;
    qint64 getBackupSize() const;
    qint64 getUncompressedSize() const;
    
signals:
    void progressChanged(int percent, const QString& message);
    void backupCompleted(const QString& path);
    void backupFailed(const QString& error);
    
private:
    qint64 m_profileId;
    QString m_outputPath;
    QString m_errorMessage;
    QString m_backupPath;
    bool m_includeDisabled;
    bool m_compress;
    
    // Backup methods
    bool validateProfile();
    bool createManifest(QJsonObject& manifest);
    bool exportTable(const QString& table, const QString& where, 
                     const QString& outputFile);
    bool exportBlobTable(const QString& table, const QString& where,
                         const QString& outputFile);
    bool createPackage(const QString& tempDir);
    
    // Helper methods
    QString generateBackupPath();
    QString calculateChecksum(const QString& path);
    QByteArray blobToHex(const QByteArray& blob);
};
```

### ProfileRestore

```cpp
/**
 * @file profile_restore.h
 * @brief Profile restore functionality for OSCAR database
 * Copyright (c) 2026 The OSCAR Team
 */

enum class ConflictResolution {
    Abort,           // Stop restore if conflict
    Rename,          // Rename restored profile
    Replace          // Replace existing (dangerous)
};

enum class ConflictStatus {
    None,            // No conflicts
    UsernameExists   // Username already in database
};

class ProfileRestore : public QObject
{
    Q_OBJECT

public:
    explicit ProfileRestore(const QString& packagePath, QObject* parent = nullptr);
    ~ProfileRestore();
    
    // Configuration
    void setConflictResolution(ConflictResolution strategy);
    void setNewUsername(const QString& username);
    
    // Validation
    bool validatePackage();
    bool checkCompatibility();
    ConflictStatus checkConflicts();
    
    // Execution
    bool restoreProfile();
    
    // Status
    QString getErrorMessage() const;
    qint64 getRestoredProfileId() const;
    QString getRestoredUsername() const;
    
signals:
    void progressChanged(int percent, const QString& message);
    void restoreCompleted(qint64 profileId, const QString& username);
    void restoreFailed(const QString& error);
    
private:
    QString m_packagePath;
    QString m_errorMessage;
    qint64 m_newProfileId;
    QString m_newUsername;
    ConflictResolution m_resolution;
    QString m_tempDir;
    
    // ID remapping
    QMap<qint64, qint64> m_profileIdMap;
    QMap<qint64, qint64> m_machineIdMap;
    QMap<qint64, qint64> m_sessionIdMap;
    QMap<qint64, qint64> m_sessionChannelIdMap;
    QMap<qint64, qint64> m_eventListIdMap;
    
    // Restore methods
    bool extractPackage();
    bool parseManifest(QJsonObject& manifest);
    bool restoreInTransaction();
    bool executeSqlFile(const QString& sqlFile);
    bool validateRestore(const QJsonObject& manifest);
    
    // Helper methods
    qint64 remapProfileId(qint64 oldId);
    qint64 remapMachineId(qint64 oldId);
    qint64 remapSessionId(qint64 oldId);
    qint64 remapSessionChannelId(qint64 oldId);
    qint64 remapEventListId(qint64 oldId);
    QString resolveUsernameConflict(const QString& originalUsername);
    QByteArray hexToBlob(const QString& hex);
    QString parseAndRemapSql(const QString& sql);
};
```

---

## Error Handling

### Backup Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| ProfileNotFound | Invalid profile_id | Abort, notify user |
| DatabaseError | DB connection failed | Abort, check DB |
| DiskSpaceError | Insufficient space | Abort, free space |
| CompressionError | ZIP creation failed | Save uncompressed, warn user |
| ExportError | SQL export failed | Abort, check DB integrity |

### Restore Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| InvalidPackage | Corrupted file | Abort, notify user |
| ChecksumMismatch | File corruption | Abort, notify user |
| IncompatibleSchema | Wrong schema version | Abort or warn user |
| UsernameConflict | Name already exists | Apply resolution strategy |
| DatabaseError | INSERT failed | Rollback transaction |
| BlobDecodeError | Invalid hex encoding | Rollback transaction |

---

## Progress Reporting

### Backup Progress

```
0%    Validating profile...
10%   Creating manifest...
20%   Exporting profile metadata...
30%   Exporting preferences and settings...
40%   Exporting machines...
50%   Exporting sessions (1/N)...
70%   Exporting waveform data...
90%   Creating backup package...
100%  Backup complete!
```

### Restore Progress

```
0%    Validating package...
5%    Checking compatibility...
10%   Resolving conflicts...
15%   Extracting package...
20%   Restoring profile...
30%   Restoring machines...
50%   Restoring sessions (1/N)...
70%   Restoring waveform data...
90%   Validating restore...
100%  Restore complete!
```

---

## Usage Examples

### Backup Example

```cpp
// Create backup
ProfileBackup backup(profileId);

// Configure
backup.setOutputPath("C:/Backups/OSCAR/");
backup.setIncludeDisabledSessions(true);
backup.setCompression(true);

// Connect signals
connect(&backup, &ProfileBackup::progressChanged, 
        [](int percent, QString message) {
    qDebug() << percent << "%" << message;
});

// Execute backup
if (backup.createBackup()) {
    qDebug() << "Backup successful:" << backup.getBackupPath();
    qDebug() << "Size:" << backup.getBackupSize() << "bytes";
} else {
    qCritical() << "Backup failed:" << backup.getErrorMessage();
}
```

### Restore Example

```cpp
// Create restore
ProfileRestore restore("C:/Backups/OSCAR/profile_backup_JohnDoe_20260106.oscar");

// Validate first
if (!restore.validatePackage()) {
    qCritical() << "Invalid package:" << restore.getErrorMessage();
    return;
}

// Check for conflicts
ConflictStatus conflicts = restore.checkConflicts();
if (conflicts == ConflictStatus::UsernameExists) {
    // Handle conflict - rename the restored profile
    restore.setConflictResolution(ConflictResolution::Rename);
    restore.setNewUsername("JohnDoe_restored");
}

// Connect signals
connect(&restore, &ProfileRestore::progressChanged,
        [](int percent, QString message) {
    qDebug() << percent << "%" << message;
});

// Execute restore
if (restore.restoreProfile()) {
    qDebug() << "Restore successful!";
    qDebug() << "New profile ID:" << restore.getRestoredProfileId();
    qDebug() << "Username:" << restore.getRestoredUsername();
} else {
    qCritical() << "Restore failed:" << restore.getErrorMessage();
}
```

---

## UI Integration Points

### Backup UI

**Location**: File menu → Backup Profile  
**Dialog Components**:
- Profile selection dropdown
- Output directory selection
- Options:
  - [x] Include disabled sessions
  - [x] Compress package
- Backup button
- Progress bar
- Status messages
- Estimated size display

### Restore UI

**Location**: File menu → Restore Profile  
**Dialog Components**:
- Package file selection (.oscar files)
- Package validation status
- Package info display (username, date range, size)
- Conflict resolution options (if needed)
- Restore button
- Progress bar
- Status messages

---

## Alternative Approach: SQLite Backup API

### Direct Database Backup (Simpler Option)

Instead of exporting to SQL files, we could use SQLite's built-in backup API:

```cpp
/**
 * @brief Simple database-level backup using SQLite API
 * 
 * This approach copies the entire database file, which is simpler
 * but doesn't support selective profile backup.
 */
bool DatabaseBackup::backupDatabase(const QString& destinationPath)
{
    QSqlDatabase db = QSqlDatabase::database();
    sqlite3* sourceDb = nullptr;
    sqlite3* destDb = nullptr;
    
    // Get native SQLite handle
    QVariant v = db.driver()->handle();
    if (v.isValid() && qstrcmp(v.typeName(), "sqlite3*") == 0) {
        sourceDb = *static_cast<sqlite3**>(v.data());
    }
    
    // Open destination
    int rc = sqlite3_open(destinationPath.toUtf8().constData(), &destDb);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    // Perform backup
    sqlite3_backup* backup = sqlite3_backup_init(destDb, "main", sourceDb, "main");
    if (backup) {
        do {
            rc = sqlite3_backup_step(backup, 100);  // Copy 100 pages at a time
            // Emit progress: sqlite3_backup_remaining() / sqlite3_backup_pagecount()
        } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
        
        sqlite3_backup_finish(backup);
    }
    
    sqlite3_close(destDb);
    return rc == SQLITE_DONE;
}
```

**Pros:**
- Very simple implementation
- Fast (direct page copying)
- Uses SQLite's native backup mechanism
- Guaranteed consistency

**Cons:**
- Cannot backup individual profiles (all-or-nothing)
- Cannot handle ID remapping (conflicts on restore)
- Backup contains all profiles, not just one
- Larger backup files

**Recommendation:** Use SQL export approach for single-profile backup, consider SQLite backup API for full database backup only.

---

## Performance Considerations

### Backup Performance

**Factors Affecting Speed:**
1. **Session Count**: Each session requires multiple table exports
2. **Waveform Data**: BLOB data is largest component (40-60% of total)
3. **Compression**: ZIP compression adds overhead but reduces file size
4. **Disk I/O**: Writing many small SQL files vs few large files

**Optimization Strategies:**
1. **Batch Processing**: Export sessions in batches (e.g., 100 at a time)
2. **Memory Management**: Stream large BLOBs instead of loading into memory
3. **Parallel Processing**: Export different machines/tables concurrently
4. **Progress Updates**: Update UI every N records, not every record

**Expected Performance:**
- 1 year data (~365 sessions): 30-60 seconds
- 5 years data (~1,825 sessions): 2-4 minutes
- 10 years data (~3,650 sessions): 5-8 minutes

### Restore Performance

**Factors Affecting Speed:**
1. **ID Remapping**: Must parse and replace placeholders
2. **Transaction Overhead**: Single transaction guarantees atomicity
3. **Foreign Key Checks**: Validation adds overhead
4. **Index Rebuilding**: Indexes updated after each INSERT

**Optimization Strategies:**
1. **Disable FK Checks Temporarily**: `PRAGMA foreign_keys = OFF` during restore
2. **Disable Indexes Temporarily**: Drop indexes, restore, rebuild
3. **Batch INSERTs**: Use prepared statements with batch execution
4. **Single Transaction**: Wrap entire restore in one transaction

**Expected Performance:**
- 1 year data: 45-90 seconds
- 5 years data: 3-5 minutes
- 10 years data: 6-10 minutes

---

## Testing Strategy

### Unit Tests

1. **Backup Tests**
   - Export single profile
   - Export profile with disabled sessions
   - Export profile with large dataset (>1000 sessions)
   - Verify manifest generation
   - Verify checksum calculation
   - Test BLOB encoding (binary → hex)

2. **Restore Tests**
   - Restore to empty database
   - Restore with username conflict (rename)
   - Restore with username conflict (abort)
   - Restore corrupted package (validation failure)
   - Restore incompatible schema version
   - Test BLOB decoding (hex → binary)

3. **ID Remapping Tests**
   - Verify profile ID remapping
   - Verify machine ID remapping
   - Verify session ID remapping
   - Verify session_channel ID remapping
   - Verify event_list ID remapping
   - Test foreign key integrity after remapping

### Integration Tests

1. **Round-Trip Test**
   - Backup profile → Restore to same DB → Verify data identical
   - Backup profile → Restore to different DB → Verify data identical

2. **Concurrent Users Test**
   - Backup while application running
   - Restore while other profiles in use

3. **Large Dataset Test**
   - 10+ years of data
   - Multiple machines
   - Mixed CPAP/oximetry data

4. **Error Recovery Test**
   - Simulate disk full during backup
   - Simulate database error during restore
   - Verify rollback on failure

### Test Data

```cpp
/**
 * @brief Create test profile with known data
 */
class BackupRestoreTestFixture
{
public:
    void setUp() {
        // Create test profile
        testProfileId = createProfile("TestUser");
        
        // Add user info
        addUserInfo(testProfileId, "John", "Doe", "1980-01-01");
        
        // Add machines
        testMachineId = addMachine(testProfileId, "ResMed", "AirSense 10");
        
        // Add sessions (100 sessions spanning 100 days)
        for (int i = 0; i < 100; i++) {
            qint64 sessionId = addSession(testMachineId, i, 
                                         QDateTime::currentSecsSinceEpoch() - (i * 86400));
            addSessionData(sessionId);  // Settings, channels, events
            addEventLists(sessionId);    // Waveform data
        }
        
        // Calculate daily summaries
        calculateDailySummaries(testProfileId);
    }
    
    void tearDown() {
        // Clean up test data
        deleteProfile(testProfileId);
    }
    
private:
    qint64 testProfileId;
    qint64 testMachineId;
};
```

### Test Cases

| Test ID | Description | Expected Result |
|---------|-------------|-----------------|
| BK-001 | Backup profile with 1 session | Success, package created |
| BK-002 | Backup profile with 1000 sessions | Success, <2 min |
| BK-003 | Backup with disabled sessions excluded | Only enabled in package |
| BK-004 | Backup with compression | Package size <50% uncompressed |
| BK-005 | Backup to read-only directory | Error, no package created |
| RS-001 | Restore to empty database | Success, profile created |
| RS-002 | Restore with username conflict (rename) | Success, username suffixed |
| RS-003 | Restore with username conflict (abort) | Aborted, no changes |
| RS-004 | Restore corrupted package | Error, no changes |
| RS-005 | Restore incompatible schema (v9) | Error or warning |
| RT-001 | Backup + Restore to same DB | Success, data identical |
| RT-002 | Backup + Restore to different DB | Success, data identical |
| RT-003 | Multiple backup/restore cycles | Success, data identical |

---

## Security Considerations

### Data Sensitivity

**Sensitive Information in Backups:**
1. **Personal Identification**: Name, DOB, address, phone, email
2. **Medical Data**: All CPAP session data, AHI, events
3. **Authentication**: Password hashes (SHA1)
4. **Doctor Information**: Provider details, patient ID

### Security Recommendations

1. **Storage Security**
   - Store backups in secure locations
   - Use encrypted file systems
   - Restrict file permissions (owner read/write only)

2. **Transfer Security**
   - Use secure channels for backup transfer (HTTPS, SFTP)
   - Don't email backups (too sensitive)
   - Use encrypted USB drives for physical transfer

3. **User Education**
   - Warn users about sensitive data in backups
   - Recommend secure storage locations
   - Suggest backup encryption (future feature)

4. **Access Control**
   - No password required to create backup (user already authenticated)
   - Consider password protection for restore (future)

### Future Enhancements: Encryption

```cpp
/**
 * @brief Encrypt backup package with password (future)
 */
class EncryptedBackup : public ProfileBackup
{
public:
    void setEncryption(bool enable, const QString& password);
    
private:
    bool encryptPackage(const QString& source, const QString& dest);
    // Use AES-256 encryption with password-derived key (PBKDF2)
};
```

---

## Database Schema Compatibility

### Supported Schema Versions

| Schema Version | Support Level | Notes |
|----------------|---------------|-------|
| 1-5 | Not Supported | Pre-dates event_lists/event_data |
| 6 | Partial | Missing session_channel_values |
| 7 | Full | Has session_channel_values |
| 8 | Full | Current version with event_lists/event_data |
| 9+ | Unknown | Future versions (forward compatible?) |

### Handling Version Mismatches

**Backup Created from v8, Restore to v7:**
- **Problem**: v7 doesn't have event_lists/event_data tables
- **Solution**: Abort with error, or migrate BLOBs to .001 files (complex)
- **Recommendation**: Upgrade target database to v8 first

**Backup Created from v7, Restore to v8:**
- **Problem**: v7 backup has no event_lists/event_data
- **Solution**: Acceptable, just no waveform data (summary-only sessions)
- **Recommendation**: Allow with warning

**Backup Created from v6, Restore to v8:**
- **Problem**: v6 backup missing session_channel_values AND event_lists
- **Solution**: Acceptable, but weighted averages will be incorrect
- **Recommendation**: Allow with warning, suggest re-import

### Schema Detection

```cpp
bool ProfileRestore::checkSchemaCompatibility(int backupSchemaVersion)
{
    int currentSchemaVersion = DatabaseManager::instance()->getSchemaVersion();
    
    if (backupSchemaVersion > currentSchemaVersion) {
        // Backup from newer version
        m_errorMessage = QString("Backup schema v%1 is newer than current v%2. "
                                "Please upgrade OSCAR before restoring.")
                        .arg(backupSchemaVersion)
                        .arg(currentSchemaVersion);
        return false;
    }
    
    if (backupSchemaVersion < 6) {
        // Too old - missing critical tables
        m_errorMessage = QString("Backup schema v%1 is too old (minimum v6 required).")
                        .arg(backupSchemaVersion);
        return false;
    }
    
    if (backupSchemaVersion < currentSchemaVersion) {
        // Backup from older version - warn but allow
        emit progressChanged(5, QString("Warning: Backup from older schema v%1, "
                                       "current is v%2. Some data may be missing.")
                            .arg(backupSchemaVersion)
                            .arg(currentSchemaVersion));
    }
    
    return true;
}
```

---

## Future Enhancements

### Phase 2 Features

1. **Selective Backup**
   - Date range selection (e.g., last 6 months only)
   - Specific machines only
   - Exclude certain data types (e.g., oximetry)
   - Summary-only backup (exclude waveforms)

2. **Incremental Backup**
   - Export only changes since last backup
   - Backup manifest tracks "last backup date"
   - Much faster for regular backups
   - Requires merge logic on restore

3. **Automated Backups**
   - Schedule automatic backups (daily, weekly)
   - Background backup without UI interruption
   - Backup rotation (keep last N backups)
   - Cloud storage integration (Dropbox, Google Drive)

4. **Encryption**
   - AES-256 encryption with password
   - PBKDF2 key derivation
   - Password prompt on restore
   - Optional: public/key encryption for sharing

5. **Compression Optimization**
   - Use better compression for BLOBs (zstd instead of qCompress)
   - Selective compression (only compress if >10% savings)
   - Multiple compression levels (fast vs best)

6. **Backup Verification**
   - Automatic integrity check after backup
   - Periodic verification of stored backups
   - Report corruption before it's too late

7. **Cloud Integration**
   - Direct backup to cloud storage
   - Automatic synchronization
   - Encrypted cloud backups
   - Multi-device profile sync

### Phase 3 Features (Advanced)

1. **Profile Merging**
   - Merge two profiles (combine sessions)
   - Duplicate session detection
   - Conflict resolution UI
   - Useful for machine changes/upgrades

2. **Data Anonymization**
   - Strip PII for sharing with doctors/researchers
   - Remove name, DOB, address, etc.
   - Keep serial numbers anonymized
   - Maintain data integrity

3. **Export to Standard Formats**
   - Export to EDF (European Data Format)
   - Export to CSV for analysis
   - Export to PDF reports
   - Integration with research databases

---

## Implementation Plan

### Phase 1: Core Functionality (MVP)

**Timeline**: 2-3 weeks

**Tasks**:
1. Create `ProfileBackup` class
   - [ ] Profile validation
   - [ ] SQL export (non-BLOB tables)
   - [ ] BLOB export (event_data)
   - [ ] Manifest generation
   - [ ] ZIP packaging
   - [ ] Progress reporting

2. Create `ProfileRestore` class
   - [ ] Package validation
   - [ ] Manifest parsing
   - [ ] Schema compatibility check
   - [ ] Conflict detection
   - [ ] ID remapping logic
   - [ ] Transactional restore
   - [ ] Post-restore validation

3. Basic UI
   - [ ] Backup dialog (simple)
   - [ ] Restore dialog (simple)
   - [ ] Progress display
   - [ ] Error messages

4. Testing
   - [ ] Unit tests
   - [ ] Integration tests
   - [ ] Manual testing

**Deliverable**: Working backup/restore for single profile

### Phase 2: Polish & Optimization (Optional)

**Timeline**: 1-2 weeks

**Tasks**:
1. Performance optimization
   - [ ] Batch processing
   - [ ] Parallel export
   - [ ] Memory optimization
   
2. Enhanced UI
   - [ ] Better progress feedback
   - [ ] Backup package info viewer
   - [ ] Conflict resolution UI
   
3. Additional features
   - [ ] Include/exclude disabled sessions
   - [ ] Backup size estimation
   - [ ] Automatic backup verification

**Deliverable**: Production-ready backup/restore

### Phase 3: Advanced Features (Future)

**Timeline**: TBD

**Tasks**:
1. Encryption support
2. Incremental backups
3. Automated/scheduled backups
4. Cloud integration
5. Profile merging

**Deliverable**: Advanced backup ecosystem

---

## Implementation Notes

### File Organization

```
oscar/database/
├── backup/
│   ├── profile_backup.h
│   ├── profile_backup.cpp
│   ├── profile_restore.h
│   ├── profile_restore.cpp
│   ├── backup_manifest.h
│   ├── backup_manifest.cpp
│   └── sql_exporter.h
│   └── sql_exporter.cpp
```

### Qt Dependencies

**Required Qt Modules:**
- QtCore: Core functionality
- QtSql: Database access
- Qt::Concurrent: Parallel processing (optional)
- QZip: Package compression (or QuaZip library)

**External Dependencies:**
- SQLite3: Already used
- QuaZip: Better ZIP support than Qt's built-in (optional)

### Code Style

- Follow OSCAR coding standards
- Doxygen documentation for all public methods
- Use Qt naming conventions (camelCase for methods)
- Include copyright headers
- Comprehensive error handling
- Logging for debugging

---

## Appendix A: SQL Export Examples

### Profile Export

```sql
-- Profile with all fields
INSERT INTO profiles (id, username, data_folder, status, status_changed_at, created_at, updated_at)
VALUES (123, 'JohnDoe', 'PROF/JohnDoe', 'active', NULL, '2024-01-01T00:00:00Z', '2025-12-28T10:00:00Z');
```

### Machine Export

```sql
-- Machine record
INSERT INTO machines (id, profile_id, machine_id, loader_name, machine_type, brand, model, 
                     series, serial_number, model_number, last_imported, purge_date, 
                     data_version, properties, created_at)
VALUES (42, @PROFILE_ID@, 0, 'ResMed', 0, 'ResMed', 'AirSense 10 AutoSet', 
        'AirSense 10', 'ABC123456', 'AS10', '2025-12-28T08:00:00Z', NULL, 
        1, '{"resmed_model":"37207"}', '2020-01-01T00:00:00Z');
```

### Session Export with EventData

```sql
-- Session
INSERT INTO sessions (id, session_id, machine_id, start_time, end_time, duration, 
                     enabled, summary_only, no_settings, events_loaded, 
                     events_file, summary_file, created_at, updated_at)
VALUES (1000, 0, @MACHINE_ID@, 1672531200, 1672560000, 28800, 
        1, 0, 0, 1, NULL, NULL, '2023-01-01T00:00:00Z', '2023-01-01T08:00:00Z');

-- EventList
INSERT INTO event_lists (id, session_id, channel_id, eventlist_index, event_type,
                        first_time, last_time, count, rate, gain, offset,
                        min_value, max_value, dimension, has_second_field,
                        min2_value, max2_value, data_size, compressed_size, created_at)
VALUES (10000, @SESSION_ID@, 4099, 0, 1, 1672531200000, 1672560000000,
        28800, 1000.0, 0.01, 0.0, 4.0, 20.0, 'cmH₂O', 0,
        NULL, NULL, 57600, 23040, '2023-01-01T00:00:00Z');

-- EventData with compressed BLOB
INSERT INTO event_data (id, eventlist_id, data_blob, data_compressed, 
                       data2_blob, data2_compressed, time_blob, time_compressed,
                       compression_method, checksum, created_at)
VALUES (10000, @EVENT_LIST_ID@, NULL, 
        X'789C...', -- Hex-encoded qCompressed data
        NULL, NULL, NULL,
        X'789C...', -- Hex-encoded time deltas
        1, 0xA5F3, '2023-01-01T00:00:00Z');
```

---

## Appendix B: Manifest Schema

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "OSCAR Backup Manifest",
  "type": "object",
  "required": ["format_version", "oscar_version", "schema_version", "export_date", "profile", "tables_exported"],
  "properties": {
    "format_version": {
      "type": "string",
      "pattern": "^\\d+\\.\\d+$"
    },
    "oscar_version": {
      "type": "string"
    },
    "schema_version": {
      "type": "integer",
      "minimum": 6,
      "maximum": 99
    },
    "export_date": {
      "type": "string",
      "format": "date-time"
    },
    "exported_by": {
      "type": "string"
    },
    "profile": {
      "type": "object",
      "required": ["username", "original_profile_id"],
      "properties": {
        "username": {"type": "string"},
        "original_profile_id": {"type": "integer"},
        "data_folder": {"type": "string"},
        "created_at": {"type": "string", "format": "date-time"},
        "status": {"type": "string", "enum": ["active", "missing", "archived"]}
      }
    },
    "statistics": {
      "type": "object",
      "properties": {
        "machines_count": {"type": "integer"},
        "sessions_count": {"type": "integer"},
        "event_lists_count": {"type": "integer"},
        "date_range": {
          "type": "object",
          "properties": {
            "first_session": {"type": "string", "format": "date"},
            "last_session": {"type": "string", "format": "date"}
          }
        },
        "total_nights": {"type": "integer"},
        "database_size_bytes": {"type": "integer"},
        "compressed_size_bytes": {"type": "integer"}
      }
    },
    "tables_exported": {
      "type": "array",
      "items": {"type": "string"}
    },
    "export_options": {
      "type": "object",
      "properties": {
        "include_disabled_sessions": {"type": "boolean"},
        "include_archived_data": {"type": "boolean"},
        "compress_package": {"type": "boolean"},
        "database_only": {"type": "boolean"}
      }
    },
    "checksums": {
      "type": "object",
      "properties": {
        "manifest": {"type": "string"},
        "database_export": {"type": "string"},
        "package": {"type": "string"}
      }
    }
  }
}
```

---

**Document Status**: Ready for Review  
**Next Steps**: Review and approval, then proceed to implementation  
**Priority**: High (enables critical backup and data portability features)  
**Estimated Effort**: 2-3 weeks for MVP, 1-2 weeks for polish

**Questions for Review:**
1. Should we support backup of individual machines (not just whole profile)?
2. Should we implement encryption in Phase 1 or Phase 2?
3. Should backup be accessible from UI or command-line (or both)?
4. What should default conflict resolution be (rename vs abort)?
5. Should we support .001 file export for backward compatibility?

---

**End of Document**
