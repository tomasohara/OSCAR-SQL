# Profile Export and Archive Design

## Overview

This document outlines the design for two related features:
1. **Profile Export** - Package a profile for tech support analysis
2. **Profile Archive** - Store profile data in self-contained format for later restoration

## Use Cases

### Use Case 1: Tech Support Export
**Goal:** User needs to send profile data to OSCAR support for troubleshooting

**Requirements:**
- Extract all profile data (database + files)
- Create portable, compressed package
- Exclude sensitive personal information (optional)
- Include metadata about OSCAR version, OS, etc.
- Easy for support to import and analyze

### Use Case 2: Profile Archive
**Goal:** User wants to archive old profiles while keeping restore capability

**Requirements:**
- Move profile data out of active database
- Store in self-contained format within profile folder
- Preserve all data relationships
- Restore capability to bring profile back
- Mark profile as "archived" in database

---

## Technical Approach

### Database Export Strategy

#### Option 1: SQLite Backup API (RECOMMENDED)
Use SQLite's backup API to create profile-specific database.

**Advantages:**
- Native SQLite functionality
- Efficient and reliable
- Maintains data integrity
- Can be selective (backup only specific profile)

**Implementation:**
```cpp
// Pseudo-code
QSqlDatabase exportDb = createNewDatabase(exportPath);
exportDb.transaction();

// Export profile
exportProfile(sourceDb, exportDb, profileId);

// Export related data using CASCADE relationships
// - machines
// - sessions  
// - session_summaries
// - etc.

exportDb.commit();
```

#### Option 2: SQL Dump
Generate SQL INSERT statements for all profile-related records.

**Advantages:**
- Human-readable
- Easy to debug
- Platform-independent

**Disadvantages:**
- Larger file size
- Slower for large datasets

### File System Strategy

**Profile Files to Include:**
```
ProfileFolder/
├── Machines/
│   ├── {MachineSerial}/
│   │   ├── Summaries/      # .000 files
│   │   ├── Events/         # .001 files
│   │   └── Backups/
├── Screenshots/
├── Journal/
└── [Other profile-specific folders]
```

**Approach:**
1. Identify all files belonging to profile
2. Maintain directory structure
3. Copy files to export/archive location
4. Create manifest file listing all included files

---

## Data Structures

### Export Manifest File
```json
{
  "export_version": "1.0",
  "export_type": "support" | "archive",
  "export_date": "2025-12-26T20:00:00Z",
  "profile": {
    "id": 123,
    "username": "JohnDoe",
    "created_at": "2024-01-01T00:00:00Z"
  },
  "oscar_version": "1.5.0",
  "os_info": {
    "platform": "Windows",
    "version": "10.0.19042"
  },
  "database": {
    "schema_version": 4,
    "file": "profile_export.db",
    "tables": ["profiles", "machines", "sessions", ...]
  },
  "files": {
    "total_count": 1234,
    "total_size_bytes": 52428800,
    "manifest": "files_manifest.txt"
  },
  "privacy": {
    "personal_info_removed": true,
    "fields_anonymized": ["user_name", "email", "phone"]
  }
}
```

### Files Manifest
```
# Files included in this export
# Format: RelativePath|SizeBytes|MD5Hash

Machines/ABC123/Summaries/5f2a1b3c.000|4096|a3b2c1d4e5f6...
Machines/ABC123/Events/5f2a1b3c.001|102400|f6e5d4c3b2a1...
Screenshots/2024-01-15_overview.png|245678|...
```

---

## Implementation Design

### 1. Profile Export Class

```cpp
class ProfileExporter
{
public:
    enum ExportType {
        TechSupport,    // For sending to support
        Archive         // For long-term storage
    };
    
    enum PrivacyLevel {
        Full,           // Include all personal data
        Anonymized,     // Remove/hash personal info
        Minimal         // Only technical data
    };
    
    struct ExportOptions {
        ExportType type;
        PrivacyLevel privacy;
        QString outputPath;
        bool compressData;
        bool includeScreenshots;
        bool includeJournal;
    };
    
    ProfileExporter(qint64 profileId);
    
    // Main export function
    bool exportProfile(const ExportOptions& options);
    
    // Progress callback
    void setProgressCallback(std::function<void(int, QString)> callback);
    
    // Validation
    QStringList validateExport(const QString& exportPath);
    
private:
    bool exportDatabase(const QString& exportPath);
    bool exportFiles(const QString& exportPath);
    bool createManifest(const QString& exportPath);
    bool anonymizeData();
    bool compressExport(const QString& exportPath);
    
    qint64 m_profileId;
    QSqlDatabase m_sourceDb;
};
```

### 2. Profile Archive Class

```cpp
class ProfileArchiver
{
public:
    // Archive a profile (move to profile folder)
    bool archiveProfile(qint64 profileId);
    
    // Restore from archive
    bool restoreProfile(const QString& archivePath);
    
    // List available archives
    QList<ArchiveInfo> listArchives(qint64 profileId);
    
    struct ArchiveInfo {
        QString path;
        QDateTime archivedDate;
        qint64 sizeBytes;
        int sessionCount;
        QDateTime firstSession;
        QDateTime lastSession;
    };
    
private:
    bool updateProfileStatus(qint64 profileId, const QString& status);
    bool validateArchive(const QString& archivePath);
};
```

### 3. Database Export Functions

```cpp
// Export profile and all related data
bool exportProfileData(QSqlDatabase& sourceDb, QSqlDatabase& targetDb, qint64 profileId)
{
    targetDb.transaction();
    
    try {
        // 1. Export profile record
        exportTable(sourceDb, targetDb, "profiles", 
                   QString("id = %1").arg(profileId));
        
        // 2. Export user_info
        exportTable(sourceDb, targetDb, "user_info", 
                   QString("profile_id = %1").arg(profileId));
        
        // 3. Export doctor_info  
        exportTable(sourceDb, targetDb, "doctor_info",
                   QString("profile_id = %1").arg(profileId));
        
        // 4. Export preferences
        exportTable(sourceDb, targetDb, "profile_preferences",
                   QString("profile_id = %1").arg(profileId));
        
        // 5. Export machines
        QList<qint64> machineIds = getMachineIds(sourceDb, profileId);
        for (qint64 machineId : machineIds) {
            exportMachineData(sourceDb, targetDb, machineId);
        }
        
        targetDb.commit();
        return true;
        
    } catch (...) {
        targetDb.rollback();
        return false;
    }
}

bool exportMachineData(QSqlDatabase& sourceDb, QSqlDatabase& targetDb, qint64 machineId)
{
    // Export machine record
    exportTable(sourceDb, targetDb, "machines",
               QString("id = %1").arg(machineId));
    
    // Get all sessions for this machine
    QList<qint64> sessionIds = getSessionIds(sourceDb, machineId);
    
    for (qint64 sessionId : sessionIds) {
        exportSessionData(sourceDb, targetDb, sessionId);
    }
    
    return true;
}

bool exportSessionData(QSqlDatabase& sourceDb, QSqlDatabase& targetDb, qint64 sessionId)
{
    // Export session record
    exportTable(sourceDb, targetDb, "sessions",
               QString("id = %1").arg(sessionId));
    
    // Export all related tables (foreign keys)
    exportTable(sourceDb, targetDb, "session_settings",
               QString("session_id = %1").arg(sessionId));
    
    exportTable(sourceDb, targetDb, "session_channels",
               QString("session_id = %1").arg(sessionId));
    
    exportTable(sourceDb, targetDb, "session_summaries",
               QString("session_id = %1").arg(sessionId));
    
    exportTable(sourceDb, targetDb, "session_slices",
               QString("session_id = %1").arg(sessionId));
    
    exportTable(sourceDb, targetDb, "respiratory_events",
               QString("session_id = %1").arg(sessionId));
    
    return true;
}

// Generic table export helper
bool exportTable(QSqlDatabase& sourceDb, QSqlDatabase& targetDb,
                const QString& tableName, const QString& whereClause)
{
    QSqlQuery sourceQuery(sourceDb);
    QString sql = QString("SELECT * FROM %1").arg(tableName);
    if (!whereClause.isEmpty()) {
        sql += QString(" WHERE %1").arg(whereClause);
    }
    
    if (!sourceQuery.exec(sql)) {
        qWarning() << "Failed to read from" << tableName;
        return false;
    }
    
    while (sourceQuery.next()) {
        // Build INSERT statement
        QSqlRecord record = sourceQuery.record();
        QSqlQuery insertQuery(targetDb);
        
        // Generate INSERT with all fields
        insertRecord(insertQuery, tableName, record);
    }
    
    return true;
}
```

---

## Privacy Options

### Anonymization Strategy

**Personal Information to Handle:**
- User name, first name, last name
- Date of birth
- Address, phone, email
- Doctor information
- Patient ID
- Serial numbers (optional)

**Anonymization Methods:**

1. **Hash Method** (preserves relationships):
   ```cpp
   QString hashValue(const QString& value) {
       return QString(QCryptographicHash::hash(
           value.toUtf8(), 
           QCryptographicHash::Sha256
       ).toHex());
   }
   ```

2. **Replacement Method** (more readable):
   ```cpp
   // Replace with generic values
   firstName -> "User"
   lastName -> "Name"
   email -> "user@example.com"
   serialNumber -> "SERIAL_" + hash(original).left(8)
   ```

3. **Removal Method** (most private):
   ```cpp
   // Set sensitive fields to NULL
   UPDATE user_info SET 
       first_name = NULL,
       last_name = NULL,
       dob = NULL,
       address = NULL,
       phone = NULL,
       email = NULL
   WHERE profile_id = ?
   ```

---

## File Organization

### Tech Support Export Package
```
ProfileExport_JohnDoe_20251226.zip
├── export_manifest.json          # Export metadata
├── profile_data.db               # Exported database
├── files_manifest.txt            # List of files
├── Machines/                     # Machine data files
│   └── [Machine folders]
├── Screenshots/                  # Optional
└── README.txt                    # Instructions for support
```

### Archive Format (in profile folder)
```
{ProfileFolder}/
├── [Current active files]
└── Archives/
    └── Archive_20251226_203000/
        ├── archive_manifest.json
        ├── profile_data.db
        ├── files_manifest.txt
        └── Machines/
            └── [Archived machine data]
```

---

## User Interface Integration

### 1. Profile Manager Integration

**Profile Selector Dialog:**
```
[Profile List]
  Profile: John Doe
    [Open] [Export...] [Archive...] [Delete]
    
Export Options Dialog:
  ○ Tech Support Package
    □ Remove personal information
    □ Include screenshots
    
  ○ Archive Profile
    □ Remove from active database
    Archive location: [Browse...]
    
  [Export] [Cancel]
```

### 2. Help Menu Integration

```
Help Menu:
  ...
  Export Profile for Support...
  Archive Management...
  ...
```

### 3. Progress Dialog

```
Exporting Profile: John Doe

Progress: [=================>      ] 65%

Current Step: Copying session files...
Files processed: 1234 / 1890
Size: 45.2 MB / 68.5 MB

[Cancel]
```

---

## Import/Restore Process

### Tech Support Import

```cpp
class ProfileImporter
{
public:
    // Validate export package
    ValidationResult validateExport(const QString& exportPath);
    
    // Import for analysis (read-only)
    bool importForAnalysis(const QString& exportPath, 
                          const QString& tempLocation);
    
    // Full import (merge into database)
    bool importProfile(const QString& exportPath,
                      const QString& newUsername);
    
    struct ValidationResult {
        bool valid;
        QString oscarVersion;
        QString exportDate;
        int sessionCount;
        qint64 totalSize;
        QStringList warnings;
        QStringList errors;
    };
};
```

### Archive Restore

```cpp
bool ProfileArchiver::restoreProfile(const QString& archivePath)
{
    // 1. Validate archive
    if (!validateArchive(archivePath)) {
        return false;
    }
    
    // 2. Read manifest
    ArchiveManifest manifest = readManifest(archivePath);
    
    // 3. Check for conflicts (username exists?)
    if (profileExists(manifest.username)) {
        // Prompt for new username or merge strategy
    }
    
    // 4. Restore database records
    restoreDatabaseData(archivePath);
    
    // 5. Restore files
    restoreFiles(archivePath);
    
    // 6. Update profile status to 'active'
    updateProfileStatus(manifest.profileId, "active");
    
    // 7. Verify restoration
    return validateRestoration(manifest.profileId);
}
```

---

## Error Handling

### Common Errors

1. **Insufficient Disk Space**
   - Check available space before export
   - Estimate required space from database + files

2. **Database Locked**
   - Close any open sessions
   - Wait for running operations

3. **Missing Files**
   - Log warnings for missing files
   - Continue with available data
   - Include missing files list in manifest

4. **Corruption Detection**
   - Verify database integrity before export
   - Calculate checksums for all files
   - Validate on import

### Error Recovery

```cpp
struct ExportResult {
    bool success;
    QString outputPath;
    qint64 totalBytes;
    int filesExported;
    QStringList warnings;
    QStringList errors;
    
    // For partial success
    bool isPartial() const {
        return !warnings.isEmpty() || !errors.isEmpty();
    }
};
```

---

## Security Considerations

### 1. Data Protection
- Use secure file permissions on exports
- Optional encryption for sensitive data
- Clear warnings about data sensitivity

### 2. Temporary Files
- Clean up temporary files after export
- Use secure temp directories
- Shred sensitive data if anonymized

### 3. Access Control
- Require confirmation for export operations
- Log all export/archive operations
- Optional password protection for archives

---

## Testing Strategy

### Test Cases

1. **Export Validation**
   - Single machine profile
   - Multiple machines profile
   - Profile with thousands of sessions
   - Profile with missing files
   - Empty profile

2. **Privacy Testing**
   - Verify anonymization
   - Ensure no data leaks
   - Test all privacy levels

3. **Restore Testing**
   - Restore to same system
   - Restore to different system
   - Restore with conflicts
   - Partial restore scenarios

4. **Edge Cases**
   - Very large profiles (100+ GB)
   - Profiles with special characters in names
   - Concurrent access during export
   - Interrupted export/restore

---

## Performance Considerations

### Optimization Strategies

1. **Streaming Export**
   - Don't load entire dataset into memory
   - Process in chunks
   - Update progress frequently

2. **Parallel Processing**
   - Copy files in parallel
   - Use thread pool for file operations
   - Keep database operations serial

3. **Compression**
   - Compress during export (not after)
   - Use efficient compression (zstd or lz4)
   - Balance compression vs. speed

### Estimated Times

Based on profile size:
- Small (< 1 GB, <1000 sessions): 30 seconds
- Medium (1-10 GB, 1000-10000 sessions): 2-5 minutes  
- Large (10-100 GB, 10000+ sessions): 10-30 minutes
- Very Large (> 100 GB): 30+ minutes

---

## Implementation Phases

### Phase 1: Basic Export (MVP)
- Export database records for single profile
- Copy associated files
- Create simple manifest
- Basic compression
- **Timeline:** 2 weeks

### Phase 2: Privacy & UI
- Anonymization options
- User interface integration
- Progress dialog
- Error handling
- **Timeline:** 2 weeks

### Phase 3: Archive Features
- Archive to profile folder
- Restore from archive
- Archive management UI
- **Timeline:** 1 week

### Phase 4: Advanced Features
- Encryption
- Cloud upload integration
- Differential backups
- Automated archiving
- **Timeline:** 3 weeks

---

## Alternative Approaches

### Option A: Full Database Export + Filter
**Pros:** Simple, uses existing SQLite tools
**Cons:** Large file size, privacy concerns

### Option B: JSON Export
**Pros:** Human-readable, platform-independent
**Cons:** Large file size, slower parsing

### Option C: Custom Binary Format
**Pros:** Compact, fast
**Cons:** Complex, maintenance burden

**RECOMMENDATION: Option 1 (Selective SQLite Backup)**
- Best balance of functionality and simplicity
- Native database support
- Efficient and reliable

---

## Future Enhancements

1. **Automated Backups**
   - Schedule regular profile archives
   - Rotate old archives
   - Cloud sync integration

2. **Selective Export**
   - Export date range only
   - Export specific machines
   - Export for research (anonymized)

3. **Profile Merging**
   - Merge multiple profiles
   - Handle duplicate sessions
   - Resolve conflicts

4. **Export Templates**
   - Save export configurations
   - Quick export presets
   - Batch export multiple profiles

---

## Conclusion

The recommended approach provides:

✅ **Reliable** - Uses native SQLite functionality  
✅ **Efficient** - Minimal overhead, good performance  
✅ **Flexible** - Supports multiple use cases  
✅ **Safe** - Privacy options, data validation  
✅ **Maintainable** - Clean architecture, well-tested  

This design enables both tech support troubleshooting and user archive management while maintaining data integrity and respecting user privacy.
