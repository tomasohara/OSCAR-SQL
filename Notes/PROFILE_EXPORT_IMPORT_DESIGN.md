# Profile Export/Import Design
**Version:** 1.0  
**Date:** 2026 Q1  
**Status:** Initial Design for Schema v9 (Database-Only Mode)  
**Last Updated:** 2026-01-15

> **NOTE**: This is the initial design for profile export/import. Database schema v8 already stores all data (including waveforms/events) in the database, eliminating the need for separate .000/.001 files.

---

## Overview

This document describes the design for exporting a complete OSCAR profile (including all associated data) and importing it into a different database. This enables backup, data migration, and profile sharing capabilities.

---

## Requirements

### Functional Requirements

1. **Export Complete Profile**
   - All database records for a profile (including BLOB data)
   - Maintain data integrity and relationships

2. **Import Profile**
   - Import into existing database
   - Handle ID conflicts/remapping
   - Handle username conflicts
   - Preserve all data relationships

3. **Portable Format**
   - Self-contained package
   - Cross-platform compatible
   - Human-readable metadata
   - Compressed for efficiency

4. **Safety**
   - Validate data before import
   - Transaction-based (all-or-nothing)
   - No data loss on failure
   - Clear error messages

### Non-Functional Requirements

1. **Performance**: Export/import should handle years of data efficiently
2. **Reliability**: 100% data fidelity
3. **Usability**: Progress feedback, clear error messages
4. **Compatibility**: Forward/backward compatible within reason

---

## Export Package Format

### Package Structure

**DATABASE-ONLY ARCHITECTURE** (Schema v8+)  
_All waveform/event data stored in database BLOBs - no separate files needed!_

```
profile_export_<username>_<timestamp>.oscar
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
            ├── sessions.sql                # All sessions for this machine
            ├── session_settings.sql        # Settings (includes json_value v9)
            ├── session_channels.sql        # Channel statistics
            ├── session_channel_values.sql  # Value/time data (v7)
            ├── respiratory_events.sql      # Respiratory events
            ├── session_summaries.sql       # Cached summaries
            ├── session_slices.sql          # Mask on/off periods
            ├── event_lists.sql             # EventList metadata (v8)
            └── event_data.sql              # Waveform BLOBs (v8)
```

**KEY FEATURES:**
- Database-only architecture (no separate files)
- 16 tables exported including BLOB data
- session_channel_values for accurate weighted averages (v7)
- event_lists and event_data for waveform storage (v8)
- json_value in session_settings for complex data types (v9)
- Single database file contains 100% of profile data
- Simpler, faster, smaller packages

### Manifest Format (manifest.json)

```json
{
  "format_version": "1.0",
  "oscar_version": "1.6.0",
  "schema_version": 9,
  "export_date": "2026-01-15T13:00:00Z",
  "exported_by": "OSCAR Profile Exporter v1.0",
  
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
    "date_range": {
      "first_session": "2020-01-01",
      "last_session": "2026-01-14"
    },
    "total_nights": 1825,
    "database_size_bytes": 314572800,
    "blob_data_bytes": 280000000,
    "compression_ratio": 0.55,
    "eventlist_count": 54750
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
    "compress_package": true
  },
  
  "checksums": {
    "manifest": "sha256:...",
    "database_files": "sha256:...",
    "blob_data": "sha256:..."
  }
}
```

**KEY MANIFEST FEATURES:**
- `format_version`: "1.0" (database-only format)
- `schema_version`: 9 (current OSCAR schema)
- `statistics.database_size_bytes`: Total database export size
- `statistics.blob_data_bytes`: Size of BLOB data (waveforms/events)
- `statistics.compression_ratio`: BLOB compression efficiency
- `statistics.eventlist_count`: Number of EventLists exported
- Includes all 16 tables required for complete profile data
- Checksums for manifest, database files, and BLOB data

---

## SQL Export Format

### Key Principles

1. **Use INSERT statements** with explicit column names
2. **Export in dependency order** (parents before children)
3. **Use placeholders for IDs** to enable remapping
4. **Include all data** including NULLs
5. **Transaction-wrapped** for safety

### Example: Machine Export

```sql
-- Machine ID: 42 (original)
-- Machine will be assigned new ID on import

INSERT INTO machines (
    profile_id,  -- Will be remapped on import
    machine_id,
    loader_name,
    machine_type,
    brand,
    model,
    series,
    serial_number,
    model_number,
    last_imported,
    purge_date,
    data_version,
    properties,
    created_at
) VALUES (
    @PROFILE_ID@,  -- Placeholder for remapped profile_id
    0,             -- Original machine_id (OSCAR internal)
    'ResMed',
    0,
    'ResMed',
    'AirSense 10 AutoSet',
    'AirSense 10',
    'ABC123456',
    'AS10',
    '2025-12-28T08:00:00Z',
    NULL,
    1,
    '{"resmed_model": "37207"}',
    '2020-01-01T00:00:00Z'
);
-- @MACHINE_DB_ID@ = LAST_INSERT_ID  -- Store for remapping
```

### Example: BLOB Data Export (v8+)

```sql
-- EventList metadata
INSERT INTO event_lists (
    session_id,
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
    data_size,
    compressed_size,
    created_at
) VALUES (
    @SESSION_ID@,  -- Placeholder for remapped session_id
    0x00010102,    -- CPAP_Pressure channel ID
    0,             -- First EventList for this channel
    1,             -- Waveform type
    1641024000000, -- First time (Unix ms)
    1641052800000, -- Last time (Unix ms)
    28800,         -- Number of data points
    1000,          -- Sample rate (1 sample/second)
    1.0,           -- Gain
    0.0,           -- Offset
    4.5,           -- Min value
    18.2,          -- Max value
    'cmH2O',       -- Dimension
    0,             -- No second field
    57600,         -- Uncompressed size (28800 × 2 bytes)
    22140,         -- Compressed size
    '2026-01-15T12:00:00Z'
);
-- @EVENTLIST_DB_ID@ = LAST_INSERT_ID  -- Store for remapping

-- EventList binary data (hex-encoded BLOB)
INSERT INTO event_data (
    eventlist_id,
    data_compressed,  -- Compressed primary data
    compression_method,
    checksum,
    created_at
) VALUES (
    @EVENTLIST_DB_ID@,  -- Placeholder for remapped eventlist_id
    X'1F8B080000000000000363606060E06260606060E46260606060626066606060E1626060606062...',  -- Hex BLOB
    1,                  -- qCompress
    45821,              -- CRC16 checksum
    '2026-01-15T12:00:00Z'
);
```

**BLOB Encoding Notes:**
- Use `X'...'` hex notation for SQLite BLOB literals
- Only store compressed OR uncompressed data (not both)
- Compress if >10% space savings, otherwise store raw
- Include CRC16 checksum for integrity verification
- Typical compression ratio: 40-60% for CPAP waveforms

---

## Export Process

### Phase 1: Validation

1. Verify profile exists and is accessible
2. Check database connections
3. ~~Verify file system access to data_folder~~ (NO LONGER NEEDED - database only)
4. Estimate export size (query database for record counts and BLOB sizes)

### Phase 2: Database Export (INCLUDING BLOBs)

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
      9. Export session_settings → session_settings.sql (includes json_value v9)
      10. Export session_channels → session_channels.sql
      11. Export session_channel_values → session_channel_values.sql (v7)
      12. Export respiratory_events → respiratory_events.sql
      13. Export session_summaries → session_summaries.sql
      14. Export session_slices → session_slices.sql
      15. Export event_lists → event_lists.sql (v8)
      16. Export event_data → event_data.sql (v8 - hex-encoded BLOBs)
```

**NEW TABLES IN v7-v9:**
- `session_channel_values`: Value/time summary data for accurate weighted averages
- `event_lists`: EventList metadata (replaces .001 file headers)
- `event_data`: Waveform/event binary data as compressed BLOBs (replaces .001 file data)

**BLOB HANDLING:**
- BLOBs encoded as hex literals: `X'1F8B08...'`
- Only compressed OR uncompressed stored (whichever is smaller)
- CRC16 checksums included for integrity verification

### ~~Phase 3: File Export~~ (REMOVED IN v8+)

**NO LONGER NEEDED** - All data now in database!

❌ No .000/.001 files to copy  
❌ No data_folder to traverse  
❌ No file checksums to calculate  
✅ Dramatically simpler export process  
✅ Faster export (no file I/O overhead)

### Phase 3: Package Creation (SIMPLIFIED)

```
1. Create temporary directory
2. Write manifest.json (with BLOB statistics)
3. Write all SQL files (including hex-encoded BLOBs)
4. Calculate checksums (database files and BLOB data)
5. Create ZIP archive: profile_export_<username>_<timestamp>.oscar
6. Cleanup temporary files
```

**CHANGES FROM v6:**
- ❌ No file copying step
- ✅ BLOB data included in SQL files
- ✅ Manifest includes BLOB statistics (size, compression ratio, count)
- ✅ Simpler, faster process

---

## Import Process

### Phase 1: Validation

1. **Verify package integrity**
   - Check file exists
   - Verify checksums (manifest, database files, BLOB data)
   - Validate manifest.json

2. **Check compatibility**
   - Verify format_version supported (1.0 or 2.0)
   - Verify schema_version compatible (v7-v9 supported)
   - Check OSCAR version compatibility
   - Detect database-only vs file-based format

3. **Check for conflicts**
   - Username already exists?
   - ~~Data folder path conflicts?~~ (NO LONGER RELEVANT in v8+)

### Phase 2: Conflict Resolution (SIMPLIFIED)

#### Scenario A: Username Exists

```
Options:
1. Abort import
2. Rename imported profile (append _imported, _2, etc.)
3. Merge with existing (advanced, risky)
4. Replace existing (dangerous, requires confirmation)
```

**Recommended**: Rename imported profile with suffix

#### ~~Scenario B: Data Folder Conflict~~ (REMOVED IN v8+)

**NO LONGER NEEDED** - Database-only mode eliminates data folder conflicts!

### Phase 3: ID Remapping Strategy (EXPANDED)

```
During import, all auto-increment IDs must be remapped:

Original IDs → New IDs Mapping Table:
- profile_id: 123 → 456
- machine_id (DB): 42 → 789
  - machine_id (OSCAR): 0 → 0 (preserved)
- session_id (DB): 1000-2825 → 5000-6825
  - session_id (OSCAR): per machine (preserved)
- session_channel_id (DB): 5000-10000 → 12000-17000 (v7+)
- eventlist_id (DB): 1000-50000 → 75000-124000 (v8+)

Process:
1. Parse SQL files
2. Replace @PROFILE_ID@ placeholders with new profile_id
3. Execute INSERT, capture LAST_INSERT_ID
4. Build mapping table as we go
5. Use mapping for foreign key references
6. Handle BLOB data with remapped eventlist_ids (v8+)
```

### Phase 4: Database Import (Transaction-Based, INCLUDING BLOBs)

```sql
BEGIN TRANSACTION;

-- Step 1: Import profile
INSERT INTO profiles (...) VALUES (...);
SET @NEW_PROFILE_ID = LAST_INSERT_ID();

-- Step 2: Import user_info (remap profile_id)
INSERT INTO user_info (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);

-- Step 3: Import doctor_info (remap profile_id)
INSERT INTO doctor_info (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);

-- Step 4: Import preferences (remap profile_id for each)
INSERT INTO profile_preferences (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);
-- ... repeat for all preferences

-- Step 5: Import channels (remap profile_id)
INSERT INTO channels (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);
-- ... repeat for all channels

-- Step 6: Import machines
FOR EACH machine:
  INSERT INTO machines (profile_id, ...) VALUES (@NEW_PROFILE_ID, ...);
  SET @NEW_MACHINE_ID = LAST_INSERT_ID();
  
  -- Step 7: Import sessions for this machine
  FOR EACH session:
    INSERT INTO sessions (machine_id, ...) VALUES (@NEW_MACHINE_ID, ...);
    SET @NEW_SESSION_ID = LAST_INSERT_ID();
    
    -- Import session_settings (remap session_id, includes json_value v9)
    INSERT INTO session_settings (session_id, channel_id, value, json_value, ...) 
    VALUES (@NEW_SESSION_ID, ...);
    
    -- Import session_channels (remap session_id)
    INSERT INTO session_channels (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    SET @NEW_SESSION_CHANNEL_ID = LAST_INSERT_ID();
    
    -- Import session_channel_values (remap session_channel_id) ✅ NEW IN v7
    INSERT INTO session_channel_values (session_channel_id, value, count, time_ms)
    VALUES (@NEW_SESSION_CHANNEL_ID, ...);
    -- ... repeat for all value/time pairs
    
    -- Import respiratory_events (remap session_id)
    INSERT INTO respiratory_events (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import session_summaries (remap session_id)
    INSERT INTO session_summaries (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import session_slices (remap session_id)
    INSERT INTO session_slices (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import event_lists (remap session_id) ✅ NEW IN v8
    FOR EACH event_list:
      INSERT INTO event_lists (session_id, channel_id, ...) 
      VALUES (@NEW_SESSION_ID, ...);
      SET @NEW_EVENTLIST_ID = LAST_INSERT_ID();
      
      -- Import event_data (remap eventlist_id, includes BLOBs) ✅ NEW IN v8
      INSERT INTO event_data (eventlist_id, data_compressed, checksum, ...)
      VALUES (@NEW_EVENTLIST_ID, X'hex_blob_data...', ...);

-- Step 8: Import daily_summaries (remap profile_id and machine_id)
INSERT INTO daily_summaries (profile_id, machine_id, ...) 
VALUES (@NEW_PROFILE_ID, @NEW_MACHINE_ID, ...);
-- ... repeat for all daily summaries

COMMIT;
-- If any error occurs, ROLLBACK
```

**NEW IN v7-v9:**
- session_channel_values import with session_channel_id remapping
- event_lists import with session_id remapping
- event_data import with eventlist_id remapping and BLOB handling
- json_value handling in session_settings

**BLOB IMPORT NOTES:**
- Hex-encoded BLOBs parsed from SQL: `X'1F8B08...'`
- Verify CRC16 checksums after import
- Memory-efficient streaming for large BLOBs

### ~~Phase 5: File Import~~ (REMOVED IN v8+)

**NO LONGER NEEDED** - All data now in database!

❌ No files to extract  
❌ No data_folder to create  
❌ No file verification needed  
✅ Dramatically simpler import process  
✅ Faster import (no file I/O overhead)

### Phase 5: Post-Import Validation (UPDATED)

```
1. Verify record counts match manifest
2. ~~Verify file counts~~ (removed - no files in v8+)
3. Check foreign key integrity
4. Verify BLOB checksums (v8+)
5. Verify session_channel_values loaded (v7+)
6. Recalculate daily_summaries if needed
7. Mark profile as active
```

**CHANGES FROM v6:**
- ❌ No file count verification
- ✅ BLOB checksum verification added
- ✅ session_channel_values verification added
- ✅ Simpler validation process

---

## Class Design

### ProfileExporter (UPDATED FOR v8+)

```cpp
/**
 * @brief Exports a complete OSCAR profile to a portable package
 * @details Database-only export (v8+) - no file copying required
 * 
 * Copyright (c) 2026 The OSCAR Team
 */
class ProfileExporter
{
public:
    ProfileExporter(qint64 profileId);
    
    // Configuration
    void setOutputPath(const QString& path);
    void setIncludeDisabledSessions(bool include);
    void setCompression(bool compress);
    
    // Execution
    bool exportProfile();
    
    // Progress tracking
    void setProgressCallback(std::function<void(int, QString)> callback);
    
    // Status
    QString getErrorMessage() const;
    QString getExportPath() const;
    qint64 getExportSize() const;
    qint64 getBlobDataSize() const;              // ✅ NEW IN v8
    
private:
    qint64 profileId;
    QString outputPath;
    QString errorMessage;
    bool includeDisabled;
    bool compress;
    
    // Export methods
    bool validateProfile();
    bool exportDatabaseRecords();                // ✅ UPDATED: includes BLOBs
    // ❌ REMOVED: bool exportDataFiles();       // No longer needed in v8+
    bool createManifest();
    bool createPackage();
    
    // Helper methods
    QString generateExportPath();
    bool exportTableToSQL(const QString& table, const QString& outputFile);
    bool exportBlobToSQL(const QString& table, const QString& outputFile); // ✅ NEW IN v8
    QString encodeBlobAsHex(const QByteArray& data);                       // ✅ NEW IN v8
    // ❌ REMOVED: bool copyDataFolder(...);     // No longer needed in v8+
    QString calculateChecksum(const QString& path);
    QString calculateBlobChecksum(qint64 profileId);                       // ✅ NEW IN v8
};
```

### ProfileImporter (UPDATED FOR v7+)

```cpp
/**
 * @brief Imports an OSCAR profile package into the database
 * @details Supports both legacy (v6 with files) and modern (v8+ database-only) formats
 * 
 * Copyright (c) 2026 The OSCAR Team
 */
class ProfileImporter
{
public:
    ProfileImporter(const QString& packagePath);
    
    // Configuration
    void setConflictResolution(ConflictResolution strategy);
    void setNewUsername(const QString& username);  // For rename strategy
    
    // Validation
    bool validatePackage();
    bool checkCompatibility();
    ConflictStatus checkConflicts();
    
    // Execution
    bool importProfile();
    
    // Progress tracking
    void setProgressCallback(std::function<void(int, QString)> callback);
    
    // Status
    QString getErrorMessage() const;
    qint64 getImportedProfileId() const;
    QString getImportedUsername() const;
    
private:
    QString packagePath;
    QString errorMessage;
    qint64 newProfileId;
    QString newUsername;
    ConflictResolution resolution;
    int schemaVersion;                                  // ✅ NEW: Track package schema version
    bool isDatabaseOnly;                                // ✅ NEW IN v8: Format detection
    
    QMap<qint64, qint64> profileIdMap;                  // old → new
    QMap<qint64, qint64> machineIdMap;                  // old → new
    QMap<qint64, qint64> sessionIdMap;                  // old → new
    QMap<qint64, qint64> sessionChannelIdMap;           // old → new (v7+)
    QMap<qint64, qint64> eventListIdMap;                // old → new (v8+)
    
    // Import methods
    bool extractPackage();
    bool parseManifest();
    bool importDatabaseRecords();                       // ✅ UPDATED: includes BLOBs
    // ❌ CONDITIONAL: bool importDataFiles();          // Only for legacy v6 packages
    bool validateImport();
    
    // Helper methods
    qint64 remapProfileId(qint64 oldId);
    qint64 remapMachineId(qint64 oldId);
    qint64 remapSessionId(qint64 oldId);
    qint64 remapSessionChannelId(qint64 oldId);         // ✅ NEW IN v7
    qint64 remapEventListId(qint64 oldId);              // ✅ NEW IN v8
    bool executeSQL(const QString& sql, QMap<QString, QVariant>& bindings);
    bool importBlobData(const QString& sqlFile);        // ✅ NEW IN v8
    QByteArray decodeBlobFromHex(const QString& hex);   // ✅ NEW IN v8
    bool verifyBlobChecksum(qint64 eventDataId);        // ✅ NEW IN v8
    QString resolveUsernameConflict(const QString& originalUsername);
    bool detectPackageFormat();                         // ✅ NEW IN v8
};
```

### Supporting Enums

```cpp
enum class ConflictResolution {
    Abort,           // Stop import if conflict
    Rename,          // Rename imported profile
    Replace,         // Replace existing (dangerous)
    Merge            // Merge data (complex)
};

enum class ConflictStatus {
    None,                // No conflicts
    UsernameExists,      // Username already in database
    DataFolderExists     // Data folder path conflicts (legacy v6 only)
};

enum class ExportFormat {
    SQL,             // SQL INSERT statements (current)
    JSON,            // JSON format (alternative)
    CSV              // CSV format (alternative)
};

enum class PackageFormat {   // ✅ NEW IN v8
    Legacy,          // v6 format with files/ directory
    DatabaseOnly     // v8+ format, database only
};
```

**KEY CHANGES FROM v6:**
- ✅ **ADDED**: BLOB export/import methods
- ✅ **ADDED**: Hex encoding/decoding for BLOB data
- ✅ **ADDED**: BLOB checksum verification
- ✅ **ADDED**: ID remapping for session_channel_id and eventlist_id
- ✅ **ADDED**: Package format detection
- ❌ **REMOVED**: File copying methods (exportDataFiles, copyDataFolder, importDataFiles)
- ✅ **BENEFIT**: Simpler class design, focused on database operations

---

## Error Handling

### Export Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| ProfileNotFound | Invalid profile_id | Abort, notify user |
| DatabaseError | DB connection failed | Abort, check DB |
| FileSystemError | Can't read data folder | Abort, check permissions |
| DiskSpaceError | Insufficient space | Abort, free space |
| CompressionError | ZIP creation failed | Save uncompressed, warn user |

### Import Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| InvalidPackage | Corrupted file | Abort, notify user |
| ChecksumMismatch | File corruption | Abort, notify user |
| IncompatibleVersion | Wrong OSCAR version | Abort or warn user |
| UsernameConflict | Name already exists | Apply resolution strategy |
| DatabaseError | INSERT failed | Rollback transaction |
| FileSystemError | Can't write files | Rollback, cleanup |

---

## Progress Reporting

### Export Progress (UPDATED FOR v8+)

```
0%    Validating profile...
10%   Exporting profile metadata...
20%   Exporting preferences and settings...
30%   Exporting machine data...
40%   Exporting session metadata...
50%   Exporting session data and BLOBs...
75%   Exporting event lists and waveform data...
90%   Creating package...
95%   Compressing archive...
100%  Export complete!
```

**CHANGES FROM v6:**
- ~~"Copying data files"~~ → Removed (no files in v8+)
- "Exporting event lists and waveform data" → Added for BLOB export

### Import Progress (UPDATED FOR v8+)

```
0%    Validating package...
5%    Checking compatibility...
8%    Detecting package format...
10%   Resolving conflicts...
15%   Extracting package...
20%   Importing profile...
25%   Importing preferences...
30%   Importing machines...
40%   Importing sessions...
55%   Importing session data...
70%   Importing event lists and waveforms...
85%   Validating import...
95%   Verifying BLOB checksums...
100%  Import complete!
```

**CHANGES FROM v6:**
- ~~"Copying data files"~~ → Removed (no files in v8+)
- "Detecting package format" → Added for legacy compatibility
- "Importing event lists and waveforms" → Added for BLOB import
- "Verifying BLOB checksums" → Added for data integrity

---

## Usage Examples

### Export Example

```cpp
// Create exporter
ProfileExporter exporter(profileId);

// Configure
exporter.setOutputPath("C:/Backups/OSCAR/");
exporter.setIncludeDisabledSessions(true);
exporter.setCompression(true);

// Set progress callback
exporter.setProgressCallback([](int percent, QString message) {
    qDebug() << percent << "%" << message;
});

// Execute export
if (exporter.exportProfile()) {
    qDebug() << "Export successful:" << exporter.getExportPath();
    qDebug() << "Size:" << exporter.getExportSize() << "bytes";
} else {
    qCritical() << "Export failed:" << exporter.getErrorMessage();
}
```

### Import Example

```cpp
// Create importer
ProfileImporter importer("C:/Backups/OSCAR/profile_export_JohnDoe_20251229.oscar");

// Validate first
if (!importer.validatePackage()) {
    qCritical() << "Invalid package:" << importer.getErrorMessage();
    return;
}

// Check for conflicts
ConflictStatus conflicts = importer.checkConflicts();
if (conflicts == ConflictStatus::UsernameExists) {
    // Handle conflict - rename the imported profile
    importer.setConflictResolution(ConflictResolution::Rename);
    importer.setNewUsername("JohnDoe_imported");
}

// Set progress callback
importer.setProgressCallback([](int percent, QString message) {
    qDebug() << percent << "%" << message;
});

// Execute import
if (importer.importProfile()) {
    qDebug() << "Import successful!";
    qDebug() << "New profile ID:" << importer.getImportedProfileId();
    qDebug() << "Username:" << importer.getImportedUsername();
} else {
    qCritical() << "Import failed:" << importer.getErrorMessage();
}
```

---

## UI Integration Points

### Export UI

**Location**: File menu → Export Profile  
**Dialog Components**:
- Profile selection dropdown
- Output directory selection
- Options:
  - [x] Include disabled sessions
  - [x] Compress package
- Export button
- Progress bar
- Status messages

### Import UI

**Location**: File menu → Import Profile  
**Dialog Components**:
- Package file selection (.oscar files)
- Package validation status
- Conflict resolution options (if needed)
- Import button
- Progress bar
- Status messages

---

## Future Enhancements

### Phase 2 Features

1. **Selective Export**
   - Date range selection
   - Specific machines only
   - Exclude certain data types

2. **Incremental Backup**
   - Export only changes since last export
   - Append to existing package

3. **Cloud Integration**
   - Export to cloud storage
   - Import from cloud storage

4. **Encryption**
   - Password-protected exports
   - Encrypted sensitive data (passwords, patient info)

5. **Merge Strategy**
   - Smart merging of profiles
   - Duplicate detection
   - Conflict resolution UI

---

## Testing Strategy

### Unit Tests

1. Export individual tables
2. Import individual tables
3. ID remapping logic
4. Checksum calculation
5. Conflict detection

### Integration Tests

1. Full export → import cycle
2. Import with username conflict
3. Import with missing files
4. Import with corrupted package
5. Large dataset (5+ years)

### Test Cases

| Test | Description | Expected Result |
|------|-------------|-----------------|
| T1 | Export profile, import to same DB | Success, renamed profile |
| T2 | Export profile, import to different DB | Success, same name |
| T3 | Export with disabled sessions | All sessions exported |
| T4 | Import corrupted package | Error, no changes |
| T5 | Import incompatible version | Warning or error |
| T6 | Export large profile (10GB) | Success, proper compression |

---

## Security Considerations

1. **Sensitive Data**
   - User passwords (hashed but still sensitive)
   - Personal information (DOB, address)
   - Medical data (all session data)

2. **Recommendations**
   - Warn users about sensitive data in exports
   - Suggest encryption for sharing
   - Don't export to unsecured locations
   - Option to exclude personal info from export

3. **Future**: Add encryption support with password protection

---

## Performance Targets (UPDATED FOR v8+)

| Operation | Target Time (v8+) | Previous (v6) | Notes |
|-----------|-------------------|---------------|-------|
| Export (1 year data) | < 20 seconds | < 30 seconds | ✅ 40% faster - no file I/O |
| Export (5 years data) | < 75 seconds | < 2 minutes | ✅ 37% faster - database only |
| Import (1 year data) | < 30 seconds | < 45 seconds | ✅ 33% faster - no file copy |
| Import (5 years data) | < 120 seconds | < 3 minutes | ✅ 33% faster - direct DB import |
| Validation | < 5 seconds | < 5 seconds | Same (checksum validation) |
| BLOB checksum verify | < 3 seconds | N/A | ✅ New in v8 |

**PERFORMANCE IMPROVEMENTS:**
- ✅ **Export**: 33-40% faster (eliminated file copying phase)
- ✅ **Import**: 33% faster (eliminated file extraction and copying)
- ✅ **Package Size**: ~42% smaller (better compression in unified database)
- ✅ **Memory**: Similar or lower (streaming BLOB operations)
- ✅ **Reliability**: Higher (ACID transactions for all data)

**ASSUMPTIONS:**
- Standard HDD: ~100 MB/s read, ~80 MB/s write
- SSD: 2-3x faster performance
- Typical profile: 60-70 MB database, 250-300 MB with BLOBs
- Network storage: May be slower, but still faster than v6 file-based approach

---

**Status**: Design Complete - Ready for Implementation  
**Next Steps**: Implementation  
**Priority**: High (enables backup and data portability)
