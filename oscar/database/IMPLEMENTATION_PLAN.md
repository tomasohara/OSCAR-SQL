# Phase 1 Implementation Plan: Profiles & Machines Database

## Overview
This document outlines the step-by-step implementation of the first phase: migrating Profile and Machine data to SQLite database.

## Files to Create

### 1. Database Core (`oscar/database/`)
- **database_manager.h** - Database connection manager (singleton)
- **database_manager.cpp** - Implementation
- **database_schema.h** - Schema definitions and SQL statements
- **database_schema.cpp** - Schema creation implementation

### 2. Repository Layer (`oscar/database/`)
- **profile_repository.h** - Profile CRUD operations
- **profile_repository.cpp** - Implementation
- **machine_repository.h** - Machine CRUD operations
- **machine_repository.cpp** - Implementation

### 3. Migration Layer (`oscar/database/`)
- **migration_manager.h** - Handles XML to database migration
- **migration_manager.cpp** - Implementation

## Database Schema

### profiles Table
```sql
CREATE TABLE IF NOT EXISTS profiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    data_folder TEXT NOT NULL,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

### machines Table
```sql
CREATE TABLE IF NOT EXISTS machines (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    machine_id INTEGER NOT NULL,
    loader_name TEXT NOT NULL,
    machine_type INTEGER NOT NULL,
    brand TEXT,
    model TEXT,
    series TEXT,
    serial_number TEXT,
    model_number TEXT,
    last_imported TEXT,
    purge_date TEXT,
    data_version INTEGER DEFAULT 0,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    UNIQUE(profile_id, machine_id)
);
```

### schema_version Table
```sql
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

## Integration Points

### 1. Application Startup (main.cpp or mainwindow.cpp)
```cpp
// Early in startup sequence:
DatabaseManager::instance().initialize(GetAppData() + "/oscar.db");

// Check if migration needed:
if (MigrationManager::needsMigration()) {
    MigrationManager::migrateProfiles();
    MigrationManager::migrateMachines();
}
```

### 2. Profile Loading (profiles.cpp)
```cpp
// Instead of loading from Profile.xml:
ProfileRepository repo;
Profile* profile = repo.findByUsername(username);
```

### 3. Machine Loading (profiles.cpp)
```cpp
// Instead of loading from machines.xml:
MachineRepository repo;
QList<Machine*> machines = repo.findByProfile(profile);
```

## Implementation Steps

### Step 1: Create Database Manager
- Singleton pattern for thread-safe access
- SQLite connection with WAL mode
- Auto-initialization at startup
- Error handling and logging

### Step 2: Create Schema
- Define all tables
- Create indexes
- Enable foreign keys
- Schema versioning support

### Step 3: Create Repositories
- ProfileRepository: CRUD operations for profiles
- MachineRepository: CRUD operations for machines
- Clean, simple API

### Step 4: Create Migration Manager
- Parse Profile.xml (existing format)
- Parse machines.xml (existing format)
- Insert into database tables
- Handle errors gracefully

### Step 5: Integration
- Modify oscar.pro to include new files
- Update Profile class to optionally use database
- Update Machine loading to optionally use database
- Add command-line flag: --use-database

### Step 6: Testing
- Test with clean install (no existing data)
- Test with existing Profile.xml and machines.xml
- Test profile switching
- Test machine listing

## Error Handling

All database operations will:
1. Check for null database connection
2. Use transactions where appropriate
3. Log errors with qWarning/qCritical
4. Return clear success/failure indicators
5. Never crash - graceful degradation

## Backward Compatibility

During Phase 1:
- File-based storage still works
- Database is optional (--use-database flag)
- Both systems can coexist
- Original XML files are NOT deleted

## Next Steps After Phase 1

Once profiles and machines are working:
- Phase 2: Convert UI to read from database
- Phase 3: Convert ResMed loader to write to database
- etc.

## Questions to Clarify

1. Where is GetAppData() defined? (Need to know exact path)
2. Should we add --use-database flag or auto-detect?
3. Should migration happen silently or show progress dialog?
4. Keep original XML files as backup after migration?

Let me know if you'd like me to proceed with implementation!
