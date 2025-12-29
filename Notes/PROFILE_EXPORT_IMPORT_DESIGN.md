# Profile Export/Import Design
**Version:** 1.0  
**Date:** 2025 Q4  
**Status:** Design Document

---

## Overview

This document describes the design for exporting a complete OSCAR profile (including all associated data) and importing it into a different database. This enables backup, data migration, and profile sharing capabilities.

---

## Requirements

### Functional Requirements

1. **Export Complete Profile**
   - All database records for a profile
   - All associated file data (waveform files, event files)
   - Maintain data integrity and relationships

2. **Import Profile**
   - Import into existing database
   - Handle ID conflicts/remapping
   - Handle username conflicts
   - Preserve all data relationships
   - Copy file data to correct locations

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

```
profile_export_<username>_<timestamp>.oscar
├── manifest.json                    # Package metadata
├── database/
│   ├── profile.sql                  # Profile record
│   ├── user_info.sql               # User information
│   ├── doctor_info.sql             # Doctor information
│   ├── preferences.sql             # All preferences
│   ├── channels.sql                # Channel customizations
│   ├── daily_summaries.sql         # Daily summaries
│   └── machines/
│       ├── machine_<id>.sql        # Machine record
│       └── machine_<id>_sessions/
│           ├── sessions.sql        # All sessions for this machine
│           ├── session_settings.sql
│           ├── session_channels.sql
│           ├── respiratory_events.sql
│           ├── session_summaries.sql
│           └── session_slices.sql
└── files/
    └── <data_folder>/              # Complete data_folder structure
        └── <machine_folders>/
            ├── EventData files
            ├── Summary files
            └── Other session files
```

### Manifest Format (manifest.json)

```json
{
  "format_version": "1.0",
  "oscar_version": "1.5.3",
  "schema_version": 6,
  "export_date": "2025-12-29T13:00:00Z",
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
      "last_session": "2025-12-28"
    },
    "total_nights": 1825,
    "file_count": 3650,
    "total_size_bytes": 524288000
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
    "respiratory_events",
    "session_summaries",
    "session_slices",
    "daily_summaries"
  ],
  
  "export_options": {
    "include_disabled_sessions": true,
    "include_archived_data": true,
    "compress_files": true
  },
  
  "checksums": {
    "manifest": "sha256:...",
    "database_files": "sha256:...",
    "data_files": "sha256:..."
  }
}
```

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

---

## Export Process

### Phase 1: Validation

1. Verify profile exists and is accessible
2. Check database connections
3. Verify file system access to data_folder
4. Estimate export size

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
      11. Export respiratory_events → respiratory_events.sql
      12. Export session_summaries → session_summaries.sql
      13. Export session_slices → session_slices.sql
```

### Phase 3: File Export

```
1. Locate data_folder for profile
2. For each machine folder:
   a. Copy all EventData files
   b. Copy all Summary files
   c. Copy all other session-related files
   d. Maintain directory structure
3. Calculate checksums
```

### Phase 4: Package Creation

```
1. Create temporary directory
2. Write manifest.json
3. Write all SQL files
4. Copy all data files
5. Calculate checksums
6. Create ZIP archive: profile_export_<username>_<timestamp>.oscar
7. Cleanup temporary files
```

---

## Import Process

### Phase 1: Validation

1. **Verify package integrity**
   - Check file exists
   - Verify checksums
   - Validate manifest.json

2. **Check compatibility**
   - Verify format_version supported
   - Verify schema_version compatible
   - Check OSCAR version compatibility

3. **Check for conflicts**
   - Username already exists?
   - Data folder path conflicts?

### Phase 2: Conflict Resolution

#### Scenario A: Username Exists

```
Options:
1. Abort import
2. Rename imported profile (append _imported, _2, etc.)
3. Merge with existing (advanced, risky)
4. Replace existing (dangerous, requires confirmation)
```

**Recommended**: Rename imported profile with suffix

#### Scenario B: Data Folder Conflict

```
Options:
1. Create new data folder path
2. Use temporary location
3. Abort import
```

**Recommended**: Create new folder: `PROF/<username>_imported_<timestamp>`

### Phase 3: ID Remapping Strategy

```
During import, all auto-increment IDs must be remapped:

Original IDs → New IDs Mapping Table:
- profile_id: 123 → 456
- machine_id (DB): 42 → 789
  - machine_id (OSCAR): 0 → 0 (preserved)
- session_id (DB): 1000-2825 → 5000-6825
  - session_id (OSCAR): per machine (preserved)

Process:
1. Parse SQL files
2. Replace @PROFILE_ID@ with new profile_id
3. Execute INSERT, capture LAST_INSERT_ID
4. Build mapping table as we go
5. Use mapping for foreign key references
```

### Phase 4: Database Import (Transaction-Based)

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
    
    -- Import session_settings (remap session_id)
    INSERT INTO session_settings (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import session_channels (remap session_id)
    INSERT INTO session_channels (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import respiratory_events (remap session_id)
    INSERT INTO respiratory_events (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import session_summaries (remap session_id)
    INSERT INTO session_summaries (session_id, ...) VALUES (@NEW_SESSION_ID, ...);
    
    -- Import session_slices (remap session_id)
    INSERT INTO session_slices (session_id, ...) VALUES (@NEW_SESSION_ID, ...);

-- Step 8: Import daily_summaries (remap profile_id and machine_id)
INSERT INTO daily_summaries (profile_id, machine_id, ...) 
VALUES (@NEW_PROFILE_ID, @NEW_MACHINE_ID, ...);
-- ... repeat for all daily summaries

COMMIT;
-- If any error occurs, ROLLBACK
```

### Phase 5: File Import

```
1. Create new data_folder path in OSCAR data directory
2. Extract files from package to new location
3. Update file paths in database if needed
4. Verify all files copied successfully
5. Update profile.data_folder in database
```

### Phase 6: Post-Import Validation

```
1. Verify record counts match manifest
2. Verify file counts match manifest
3. Check foreign key integrity
4. Recalculate daily_summaries if needed
5. Mark profile as active
```

---

## Class Design

### ProfileExporter

```cpp
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
    
private:
    qint64 profileId;
    QString outputPath;
    QString errorMessage;
    bool includeDisabled;
    bool compress;
    
    // Export methods
    bool validateProfile();
    bool exportDatabaseRecords();
    bool exportDataFiles();
    bool createManifest();
    bool createPackage();
    
    // Helper methods
    QString generateExportPath();
    bool exportTableToSQL(const QString& table, const QString& outputFile);
    bool copyDataFolder(const QString& source, const QString& dest);
    QString calculateChecksum(const QString& path);
};
```

### ProfileImporter

```cpp
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
    
    QMap<qint64, qint64> profileIdMap;   // old → new
    QMap<qint64, qint64> machineIdMap;   // old → new
    QMap<qint64, qint64> sessionIdMap;   // old → new
    
    // Import methods
    bool extractPackage();
    bool parseManifest();
    bool importDatabaseRecords();
    bool importDataFiles();
    bool validateImport();
    
    // Helper methods
    qint64 remapProfileId(qint64 oldId);
    qint64 remapMachineId(qint64 oldId);
    qint64 remapSessionId(qint64 oldId);
    bool executeSQL(const QString& sql, QMap<QString, QVariant>& bindings);
    QString resolveUsernameConflict(const QString& originalUsername);
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
    None,            // No conflicts
    UsernameExists,  // Username already in database
    DataFolderExists // Data folder path conflicts
};

enum class ExportFormat {
    SQL,             // SQL INSERT statements
    JSON,            // JSON format (alternative)
    CSV              // CSV format (alternative)
};
```

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

### Export Progress

```
0%    Validating profile...
10%   Exporting profile metadata...
20%   Exporting preferences and settings...
30%   Exporting machine data...
50%   Exporting session data...
70%   Copying data files...
90%   Creating package...
100%  Export complete!
```

### Import Progress

```
0%    Validating package...
5%    Checking compatibility...
10%   Resolving conflicts...
15%   Extracting package...
20%   Importing profile...
30%   Importing machines...
50%   Importing sessions...
70%   Copying data files...
90%   Validating import...
100%  Import complete!
```

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

## Performance Targets

| Operation | Target Time | Notes |
|-----------|-------------|-------|
| Export (1 year data) | < 30 seconds | With compression |
| Export (5 years data) | < 2 minutes | With compression |
| Import (1 year data) | < 45 seconds | Including file copy |
| Import (5 years data) | < 3 minutes | Including file copy |
| Validation | < 5 seconds | Package integrity check |

---

**Status**: Design Complete  
**Next Steps**: Implementation  
**Priority**: High (enables backup and data portability)
