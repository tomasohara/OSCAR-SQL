# Journal Database Migration Design

**Author:** OSCAR Development Team  
**Date:** 2026-01-06  
**Purpose:** Migrate MT_JOURNAL summary (.000) files to database storage  
**Schema Version:** 9 (upgrade from v8)

---

## Overview

This document describes the design and implementation for converting MT_JOURNAL machine data from file-based storage (.000 summary files) to database storage. Journal entries contain user-entered data (notes, weight, feelings, bookmarks) that cannot be recreated from backup devices, making database storage with proper backup/restore critical.

## Current State

### Journal Machine Architecture

- Every Profile has exactly **one** Journal machine (type `MT_JOURNAL`)
- Journal machine gets a randomly generated machine_id (like CPAP machines)
- Each date has one journal "session" (keyed by date)
- Journal data stored as Session settings in .000 files

### Journal Data Fields

Journal entries are stored as Session settings with these channel IDs:

| Channel ID | Name | Type | Description |
|------------|------|------|-------------|
| 0xd000 | Journal_Notes | RICHTEXT | User's daily journal notes (HTML) |
| 0x0803 | Journal_Weight | DOUBLE | Daily weight measurement |
| 0x0807 | Journal_ZombieMeter | DOUBLE | Feeling rating (1-10, renamed "Feelings") |
| 0x0808 | Bookmark_Start | INTEGER | Bookmark start times (QVariantList) |
| 0x0809 | Bookmark_End | DOUBLE | Bookmark end times (QVariantList) |
| 0x0805 | Bookmark_Notes | STRING | Bookmark labels (QStringList) |
| 0x080a | LastUpdated | DATETIME | Last modification timestamp |

### Current File Storage

- Location: `{profile}/OSCAR_Data/Summaries/{date}.000`
- Format: Qt QDataStream binary format
- One file per date with journal entry
- Contains session settings only (no events/waveforms)

---

## Database Design

### Existing Tables Used

Journal data leverages the **existing** session infrastructure:

1. **`machines` table** - Journal machine record (one per profile)
   - `machine_type = MT_JOURNAL` (value from machine_common.h)
   - `machine_id` = randomly generated (same as CPAP machines)
   - `loader_name = "Journal"`
   - `serial_number` = hexid of machine_id

2. **`sessions` table** - One session per date with journal entry
   - `session_id` = computed from first session time (same as CPAP sessions)
   - `machine_id` = foreign key to journal machine
   - `start_time` = first CPAP session time (or 8 PM if no CPAP data)
   - `end_time` = last CPAP session time (or start + 1 hour)
   - `summary_only = 1` (always true for journal)
   - `events_loaded = 0` (no events for journal)
   - **Note**: Journal session times match the CPAP day range, NOT midnight-to-midnight

3. **`session_settings` table** - Journal field values
   - Stores all journal fields (notes, weight, feelings, bookmarks, etc.)
   - `channel_id` = Journal_Notes, Journal_Weight, etc.
   - `value` = DOUBLE for numeric fields
   - `data_type` = 'string', 'int', 'float', 'datetime', 'richtext'

**Special Handling for Complex Types:**
- **Bookmarks** (QVariantList, QStringList):
  - Serialize to JSON strings before storage
  - Store in session_settings with data_type='json'
  - Deserialize when loading back to memory

### No New Tables Required

The existing session infrastructure handles journal data perfectly:
- Journal machine → `machines` table
- Daily journal entries → `sessions` table (one session per date)
- Journal fields → `session_settings` table
- Database foreign keys ensure referential integrity
- Cascade deletes work automatically

---

## Implementation Plan

### Phase 1: Database Storage (New Code)

#### 1.1 Journal Data Persistence

**File:** `oscar/SleepLib/journal.cpp`

Add functions to migrate journal data to database:

```cpp
/**
 * @brief Migrates all journal .000 files to database for a profile
 * @param profile The profile to migrate
 * @return true if successful, false otherwise
 * @note Journal machine is already in database via Profile::LoadMachineData()
 */
bool Journal::MigrateToDatabase(Profile* profile);

/**
 * @brief Migrates a single journal session from .000 file to database
 * @param machine The journal machine
 * @param date The date of the journal entry
 * @return true if successful, false otherwise
 */
bool Journal::MigrateSessionToDatabase(Machine* machine, QDate date);
```

#### 1.2 Session Settings Serialization

**File:** `oscar/SleepLib/session.cpp`

Update `Session::StoreToDatabase()` to handle complex types:

```cpp
// Serialize QVariantList to JSON for bookmarks
if (channel == Bookmark_Start || channel == Bookmark_End) {
    QVariantList list = setting.value().toList();
    QJsonArray array;
    for (const QVariant& v : list) {
        array.append(QJsonValue::fromVariant(v));
    }
    settingData.value = 0;  // Not used for JSON
    settingData.dataType = "json";
    settingData.jsonValue = QJsonDocument(array).toJson(QJsonDocument::Compact);
}

// Serialize QStringList to JSON for bookmark notes
if (channel == Bookmark_Notes) {
    QStringList list = setting.value().toStringList();
    QJsonArray array;
    for (const QString& s : list) {
        array.append(s);
    }
    settingData.value = 0;
    settingData.dataType = "json";
    settingData.jsonValue = QJsonDocument(array).toJson(QJsonDocument::Compact);
}
```

Update `Session::LoadFromDatabase()` to deserialize:

```cpp
if (settingData.dataType == "json") {
    QJsonDocument doc = QJsonDocument::fromJson(settingData.jsonValue.toUtf8());
    if (doc.isArray()) {
        QJsonArray array = doc.array();
        // Convert back to QVariantList or QStringList based on channel
    }
}
```

### Phase 2: Migration Implementation

#### 2.1 Profile Load Time Migration

**File:** `oscar/SleepLib/profiles.cpp`

Add migration call during profile load:

```cpp
bool Profile::Load(QString path, bool skipSessionData)
{
    // ... existing load code ...
    
    // Migrate journal data if needed
    Machine* journal = GetMachine(MT_JOURNAL);
    if (journal && journal->m_database_id == 0) {
        qDebug() << "Profile::Load() - Migrating journal data to database";
        Journal::MigrateToDatabase(this);
    }
    
    // ... rest of load code ...
}
```

#### 2.2 Migration Logic

```cpp
bool Journal::MigrateToDatabase(Profile* profile)
{
    qDebug() << "Journal::MigrateToDatabase() - Starting migration for" << profile->user->userName();
    
    // 1. Ensure journal machine exists in database
    Machine* journal = profile->GetMachine(MT_JOURNAL);
    if (!journal) {
        qWarning() << "Journal::MigrateToDatabase() - No journal machine found";
        return false;
    }
    
    // 2. Save machine to database (gets database ID)
    if (!journal->SaveToDatabase()) {
        qWarning() << "Journal::MigrateToDatabase() - Failed to save journal machine";
        return false;
    }
    
    // 3. Find all .000 files in Summaries directory
    QString summariesPath = journal->getSummariesPath();
    QDir dir(summariesPath);
    QStringList filters;
    filters << "*.000";
    dir.setNameFilters(filters);
    QStringList files = dir.entryList();
    
    qDebug() << "Journal::MigrateToDatabase() - Found" << files.size() << ".000 files";
    
    // 4. Migrate each file
    int migratedCount = 0;
    for (const QString& filename : files) {
        QString dateStr = filename.section(".", 0, -2);
        bool ok;
        SessionID sessionId = dateStr.toLong(&ok, 16);
        if (!ok) continue;
        
        QDate date = QDate::fromSecsSinceEpoch(sessionId);
        if (MigrateSessionToDatabase(journal, date)) {
            migratedCount++;
        }
    }
    
    qDebug() << "Journal::MigrateToDatabase() - Migrated" << migratedCount << "sessions";
    
    // 5. Optionally backup and delete .000 files (not implemented in initial version)
    
    return migratedCount > 0;
}
```

### Phase 3: Backup/Restore Updates

#### 3.1 Update Journal::BackupJournal()

**File:** `oscar/SleepLib/journal.cpp`

Modify to read from database instead of files:

```cpp
bool Journal::BackupJournal(QString filename)
{
    // ... existing XML setup code ...
    
    // Get date range from database instead of files
    Machine* journal = p_profile->GetMachine(MT_JOURNAL);
    if (!journal || journal->m_database_id == 0) {
        qWarning() << "Journal::BackupJournal() - No journal machine in database";
        return false;
    }
    
    SessionRepository sessionRepo;
    QList<SessionData> sessions = sessionRepo.findByMachine(journal->m_database_id);
    
    SessionSettingsRepository settingsRepo;
    
    for (const SessionData& sessionData : sessions) {
        QDate date = QDate::fromSecsSinceEpoch(sessionData.sessionId);
        
        // Load settings from database
        QList<SessionSettingData> settings = settingsRepo.findBySession(sessionData.id);
        
        // Extract journal fields and write to XML
        // ... existing XML writing code ...
    }
    
    // ... existing XML finalization code ...
}
```

#### 3.2 Update Journal::RestoreJournal()

**File:** `oscar/SleepLib/journal.cpp`

Modify to write to database instead of files:

```cpp
bool Journal::RestoreJournal(QString filename)
{
    // ... existing XML parsing code ...
    
    Machine* journal = p_profile->GetMachine(MT_JOURNAL);
    if (!journal) {
        journal = profile->CreateJournalMachine();
    }
    
    // Ensure in database
    if (!journal->SaveToDatabase()) {
        qWarning() << "Journal::RestoreJournal() - Failed to save journal machine";
        return false;
    }
    
    for (each day in XML) {
        // Parse XML day element
        QDate date = ...;
        
        // Create or update session in database
        Session* sess = new Session(journal, computeSessionId(date));
        sess->setMachineId(journal->m_database_id);
        
        // Set journal fields from XML
        if (hasWeight) sess->settings[Journal_Weight] = weight;
        if (hasZombie) sess->settings[Journal_ZombieMeter] = zombie;
        if (hasNotes) sess->settings[Journal_Notes] = notes;
        // ... bookmarks ...
        
        // Store to database
        sess->StoreToDatabase();
        
        delete sess;
    }
    
    // ... rest of restore code ...
}
```

### Phase 4: Schema Version Update

#### 4.1 Update Database Schema

**File:** `oscar/database/database_schema.cpp`

Add schema version 9:

```cpp
bool DatabaseSchema::upgradeToVersion9(QSqlDatabase& db)
{
    qDebug() << "DatabaseSchema: Upgrading to version 9 (Journal migration)";
    
    // No schema changes needed - journal uses existing tables
    // This version marker indicates journal data has been migrated
    
    QSqlQuery query(db);
    query.prepare("INSERT INTO schema_version (version) VALUES (9)");
    if (!query.exec()) {
        qCritical() << "Failed to update schema version to 9:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "DatabaseSchema: Successfully upgraded to version 9";
    return true;
}
```

#### 4.2 Update SESSION_SETTINGS_REPOSITORY

**File:** `oscar/database/session_settings_repository.h`

Add JSON support to SessionSettingData:

```cpp
struct SessionSettingData
{
    qint64 id = 0;
    qint64 sessionId = 0;
    int channelId = 0;
    double value = 0.0;
    QString dataType;       // 'int', 'float', 'bool', 'string', 'datetime', 'richtext', 'json'
    QString jsonValue;      // NEW: For serialized QVariantList/QStringList
    QDateTime createdAt;
};
```

Update database operations to handle JSON field.

---

## Migration Strategy

### Automatic Migration

1. **On Profile Load:**
   - **Note:** Journal machine is ALREADY saved to database by `Profile::LoadMachineData()`
   - Check if journal **sessions** exist in database (not machine - machine already there!)
   - If no sessions in database but .000 files exist, trigger migration
   - Load all .000 files from Summaries directory
   - Create database records for each journal session
   - **Database-only mode:** Journal data will ONLY exist in database after migration
   - .000 files remain on disk but are NOT used or updated after migration

2. **Verification:**
   - Compare record counts (files vs database)
   - Log any migration errors
   - Provide XML export/import as backup/restore mechanism (not .000 files)

### Database-Only Mode

**IMPORTANT:** After migration, journal data is **database-only**:
- Journal sessions are NO LONGER saved to .000 files
- Journal data is read from and written to database exclusively
- .000 files from before migration remain on disk (for safety) but are ignored
- XML export/import is the backup/restore mechanism for journal data
- Session::Store() should skip writing .000 files for MT_JOURNAL sessions
- Loader should skip reading .000 files if sessions already exist in database

### Testing Plan

1. **Unit Tests:**
   - Test journal session creation in database
   - Test complex type serialization (bookmarks)
   - Test migration from .000 files

2. **Integration Tests:**
   - Load profile with existing journal .000 files
   - Verify all data migrated correctly
   - Test backup/restore with database storage
   - Verify UI displays journal data correctly

3. **Manual Tests:**
   - Create test profile with journal entries
   - Backup journal to XML
   - Delete database
   - Restore journal from XML
   - Verify all data restored

---

## Implementation Checklist

- [x] Add JSON serialization support to SessionSettingData
- [x] Update session_settings table to include json_value column (schema v9)
- [ ] Implement Journal::MigrateToDatabase()
- [ ] Implement Journal::MigrateSessionToDatabase()
- [ ] Update Session::StoreToDatabase() for JSON types
- [ ] Update Session::LoadFromDatabase() for JSON types
- [ ] **Modify Session::Store() to skip .000 file writes for MT_JOURNAL sessions**
- [ ] **Modify Journal loader to skip .000 file reads if sessions in database**
- [ ] Update Journal::BackupJournal() to read from database
- [ ] Update Journal::RestoreJournal() to write to database
- [ ] Add migration trigger in Profile::Load()
- [ ] Add migration logging and error handling
- [ ] Test migration with real journal data
- [ ] Test that new journal entries go to database only (not .000 files)
- [ ] Update documentation

---

## Rollout Plan

### Release N (Current)
- Implement database storage for journal
- Automatic migration on profile load
- Keep .000 files for safety
- Update backup/restore to use database

### Release N+1 (Next)
- Optional: Add UI to manually trigger migration
- Optional: Add cleanup tool to delete old .000 files
- Monitor for migration issues

### Release N+2 (Future)
- Remove .000 file reading code (if no issues)
- Database-only mode for journal

---

## Risk Mitigation

1. **Data Loss Prevention:**
   - Never delete .000 files automatically
   - Always verify database write before marking migration complete
   - Provide manual export/import as backup

2. **Migration Failures:**
   - Log all migration errors with details
   - Allow partial migration (some dates succeed, some fail)
   - Provide retry mechanism

3. **Complex Data Types:**
   - Test bookmark serialization thoroughly
   - Handle edge cases (empty lists, special characters)
   - Validate JSON parsing

---

## Notes

- Journal machine gets randomly generated machine_id (created with `new Machine(profile, 0)` which generates random ID)
- Journal sessions have summary_only = 1 (no events/waveforms)
- Journal session times align with CPAP day boundaries (first to last session, or 8 PM default)
- Journal date-to-session mapping same as CPAP sessions
- Database foreign keys handle cascade deletes automatically
- JSON storage allows future field additions without schema changes

---

**Document Version:** 1.0  
**Schema Version:** 9 (upgrade from v8)  
**Status:** Design Complete, Implementation Pending
