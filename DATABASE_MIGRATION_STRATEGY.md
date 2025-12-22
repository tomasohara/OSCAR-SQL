# OSCAR Database Migration Strategy

## Executive Summary

This document provides a comprehensive strategy for migrating OSCAR from its current file-based storage system to a SQLite database. The analysis identifies which files should be converted to database tables and which must remain as files, and proposes an incremental migration path that minimizes risk while maximizing benefits.

**Key Insight:** OSCAR can rebuild all session and summary data from SD card backups. This fundamentally simplifies the migration strategy - instead of migrating existing data files, we focus on converting loaders to write to the database, then users can regenerate their data from SD card backups. This approach is cleaner, safer, and leverages OSCAR's existing rebuild capability.

### OSCAR Context
- Open source application used worldwide by individuals
- Self-installed, no technical support required
- 22 platform flavors (Windows, macOS, Linux variants)
- All development by volunteers
- Users can rebuild data from original SD card backups
- Must work with single installer - no manual configuration

## Current State Analysis

### File-Based Storage Overview

OSCAR currently stores data in multiple file formats across the filesystem:

#### Profile Data Files
- **Profile.xml** - User profile information, preferences, settings
- **machines.xml** - Device metadata, serial numbers, last import dates
- **channels.dat** - User-customized channel display preferences

#### Session Data Files  
- **Summaries.xml.gz** - Compressed session summary statistics
- **Binary event files** - Detailed waveform and event data in custom binary format
- Located in: `{ProfilePath}/{MachineID}/Events/{SessionID}`

#### External Source Data Files (SD Card Data)
- **ResMed EDF files** (`.edf`, `.str`)
- **Philips Respironics** (`.000`, `.001`, etc.)
- **BMC binary files**
- **Prisma XML/EDF files**
- **Intellipap DV5/DV6 files**
- **Fisher & Paykel files**
- **Weinmann, SleepStyle, and other manufacturer formats**
- **Oximeter data** (CMS50, MD300W1, Viatom, etc.)

## Files That MUST Remain File-Based

### External Data Sources (Cannot Convert)

The following files are read directly from CPAP device SD cards by loader plugins and **CANNOT be converted to database format**:

1. **All manufacturer-specific SD card data**
   - Each CPAP manufacturer uses proprietary file formats
   - 15+ loader plugins parse these unique formats
   - Data comes from external sources (SD cards)
   - Must be read directly as files

**Rationale**: 
- These are external data sources, not internal application data
- Loader plugins are designed to parse specific file formats
- Converting would break the import process
- Original files should be preserved for backup/verification

### Backup and Export Files

- **SD card backup directories** - Keep for data recovery
- **CSV/PDF exports** - Remain as user-generated files
- **Application configuration files** - Can remain as INI/XML

## Files That SHOULD Be Migrated to Database

### 1. Profile Data (`Profile.xml`)

**Current State:**
- Location: `{ProfilePath}/Profile.xml`
- Format: XML
- Size: ~10-50 KB per profile
- Contains: User info, preferences, all settings

**Proposed Database Storage:**

```sql
CREATE TABLE profiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    data_folder TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE profile_preferences (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    category TEXT NOT NULL,
    key TEXT NOT NULL,
    value TEXT,
    value_type TEXT,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    UNIQUE(profile_id, category, key)
);

CREATE TABLE user_info (
    profile_id INTEGER PRIMARY KEY,
    first_name TEXT,
    last_name TEXT,
    dob DATE,
    height REAL,
    gender TEXT,
    email TEXT,
    phone TEXT,
    address TEXT,
    country TEXT,
    timezone TEXT,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE
);

CREATE TABLE doctor_info (
    profile_id INTEGER PRIMARY KEY,
    doctor_name TEXT,
    practice_name TEXT,
    phone TEXT,
    email TEXT,
    address TEXT,
    patient_id TEXT,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE
);
```

**Benefits:**
- Faster profile loading
- Atomic updates to preferences
- No XML parsing overhead
- Better data validation

### 2. Machine Metadata (`machines.xml`)

**Current State:**
- Location: `{ProfilePath}/machines.xml`
- Format: XML
- Size: ~5-20 KB per profile
- Contains: Device information, serial numbers, model info

**Proposed Database Storage:**

```sql
CREATE TABLE machines (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    machine_id INTEGER UNIQUE NOT NULL,
    loader_name TEXT NOT NULL,
    machine_type TEXT NOT NULL,
    brand TEXT,
    model TEXT,
    series TEXT,
    serial_number TEXT,
    model_number TEXT,
    last_imported DATETIME,
    purge_date DATE,
    data_version INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE
);

CREATE TABLE machine_properties (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    machine_id INTEGER NOT NULL,
    property_key TEXT NOT NULL,
    property_value TEXT,
    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE CASCADE,
    UNIQUE(machine_id, property_key)
);

CREATE INDEX idx_machines_profile ON machines(profile_id);
CREATE INDEX idx_machines_serial ON machines(serial_number);
CREATE INDEX idx_machines_loader ON machines(loader_name);
```

**Benefits:**
- Fast machine lookups
- Referential integrity with sessions
- Easy querying by serial number
- Tracks import history

### 3. Session Summaries (`Summaries.xml.gz`)

**Current State:**
- Location: `{ProfilePath}/{MachineID}/Summaries.xml.gz`
- Format: Compressed XML
- Size: Can be 100s of MB for long-term users
- Contains: Session metadata, channel summaries, statistics

**Proposed Database Storage:**

```sql
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    machine_id INTEGER NOT NULL,
    session_id INTEGER NOT NULL,
    first_time DATETIME NOT NULL,
    last_time DATETIME NOT NULL,
    duration_ms INTEGER,
    enabled BOOLEAN DEFAULT 1,
    summary_only BOOLEAN DEFAULT 0,
    has_events BOOLEAN DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE CASCADE,
    UNIQUE(machine_id, session_id)
);

CREATE TABLE session_settings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    setting_value TEXT,
    value_type TEXT,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, channel_id)
);

CREATE TABLE channel_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    count_value REAL,
    sum_value REAL,
    avg_value REAL,
    wavg_value REAL,
    min_value REAL,
    max_value REAL,
    percentile_90 REAL,
    percentile_95 REAL,
    median_value REAL,
    cph_value REAL,
    sph_value REAL,
    first_time DATETIME,
    last_time DATETIME,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, channel_id)
);

CREATE TABLE value_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    raw_value INTEGER NOT NULL,
    count INTEGER NOT NULL,
    time_seconds INTEGER NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, channel_id, raw_value)
);

CREATE INDEX idx_sessions_machine ON sessions(machine_id);
CREATE INDEX idx_sessions_time ON sessions(first_time);
CREATE INDEX idx_channel_summaries_session ON channel_summaries(session_id);
CREATE INDEX idx_value_summaries_session ON value_summaries(session_id);
```

**Benefits:**
- 10-50x faster session queries
- 50% reduction in memory usage
- No XML decompression overhead
- Efficient date range queries

### 4. Event Data (Binary Files)

**Current State:**
- Location: `{ProfilePath}/{MachineID}/Events/{SessionID}`
- Format: Custom binary
- Size: Can be GBs for long-term users
- Contains: Detailed waveforms, event flags, timestamps

**Proposed Database Storage:**

```sql
CREATE TABLE events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    event_list_index INTEGER NOT NULL,
    event_type TEXT NOT NULL,
    start_time DATETIME NOT NULL,
    end_time DATETIME,
    rate REAL DEFAULT 1.0,
    gain REAL DEFAULT 1.0,
    offset REAL DEFAULT 0.0,
    min_value REAL,
    max_value REAL,
    data_count INTEGER,
    compressed_data BLOB,
    time_data BLOB,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_events_session ON events(session_id);
CREATE INDEX idx_events_channel ON events(channel_id);
CREATE INDEX idx_events_time ON events(start_time);
```

**Compression Strategy:**
- Use zlib compression for waveform data
- Store in BLOB fields
- Typical compression: 60-80% size reduction
- Load on-demand when viewing daily details

**Benefits:**
- Faster event loading
- Better data integrity
- Efficient storage with compression
- Simplified backup/restore

### 5. Channel Configuration (`channels.dat`)

**Current State:**
- Location: `{ProfilePath}/channels.dat`
- Format: Binary
- Size: ~10-50 KB
- Contains: User channel display preferences

**Proposed Database Storage:**

```sql
CREATE TABLE channel_definitions (
    channel_id INTEGER PRIMARY KEY,
    code TEXT UNIQUE NOT NULL,
    fullname TEXT,
    label TEXT,
    description TEXT,
    units TEXT,
    channel_type TEXT,
    default_color TEXT,
    enabled BOOLEAN DEFAULT 1,
    show_in_overview BOOLEAN DEFAULT 1,
    display_order INTEGER
);

CREATE TABLE channel_preferences (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    custom_label TEXT,
    custom_color TEXT,
    enabled BOOLEAN DEFAULT 1,
    show_in_overview BOOLEAN,
    display_order INTEGER,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    FOREIGN KEY (channel_id) REFERENCES channel_definitions(channel_id),
    UNIQUE(profile_id, channel_id)
);
```

**Benefits:**
- Per-profile channel customization
- Easier to manage channel metadata
- No binary file parsing

### 6. Daily Aggregations (New)

**Current State:**
- Calculated on-the-fly from sessions
- Not persisted
- Recalculated every time

**Proposed Database Storage:**

```sql
CREATE TABLE days (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    date DATE NOT NULL,
    first_session_time DATETIME,
    last_session_time DATETIME,
    total_time_ms INTEGER,
    has_enabled_sessions BOOLEAN DEFAULT 0,
    machine_types TEXT,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    UNIQUE(profile_id, date)
);

CREATE TABLE day_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    day_id INTEGER NOT NULL,
    session_id INTEGER NOT NULL,
    FOREIGN KEY (day_id) REFERENCES days(id) ON DELETE CASCADE,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(day_id, session_id)
);

CREATE INDEX idx_days_profile ON days(profile_id);
CREATE INDEX idx_days_date ON days(date);
CREATE INDEX idx_day_sessions_day ON day_sessions(day_id);
CREATE INDEX idx_day_sessions_session ON day_sessions(session_id);
```

**Benefits:**
- Instant overview screen loading
- No recalculation needed
- Efficient date range queries

## Proposed Database Schema Overview

### Entity Relationship Diagram

```
┌─────────────┐
│  profiles   │
└──────┬──────┘
       │
       ├─────────────┐
       │             │
       ↓ 1:N         ↓ 1:N
┌─────────────┐   ┌──────────────┐
│  machines   │   │profile_prefs │
└──────┬──────┘   └──────────────┘
       │
       │ 1:N
       ↓
┌──────────────┐
│   sessions   │
└──────┬───────┘
       │
       ├─────────────┬─────────────┐
       │             │             │
       ↓ 1:N         ↓ 1:N         ↓ 1:N
┌────────────┐  ┌────────┐  ┌──────────────┐
│channel_    │  │ events │  │value_        │
│summaries   │  └────────┘  │summaries     │
└────────────┘              └──────────────┘

┌──────────────┐         ┌───────────────┐
│    days      │────N:N──→│ day_sessions  │
└──────────────┘         └───────────────┘
```

### Key Design Principles

1. **Foreign Key Constraints** - Maintain referential integrity
2. **Appropriate Indexing** - Optimize common query patterns
3. **BLOB Compression** - Efficient storage for large waveform data
4. **Normalized Schema** - Reduce redundancy
5. **Flexible Properties** - Key-value tables for extensibility

## Revised Migration Strategy (Loader-First Approach)

### Strategic Insight: Leverage Rebuild Capability

**OSCAR's existing ability to rebuild all session and summary data from SD card backups fundamentally changes the migration strategy.** Instead of complex data migration from XML/binary files, we:

1. Convert loaders to write to database
2. Convert UI/reporting to read from database  
3. Users re-import from their SD card backups
4. Clean, simple, and leverages existing functionality

**Key Benefits:**
- Simpler implementation (no complex XML/binary migration)
- Cleaner data (regenerated from source)
- Lower risk (uses proven rebuild functionality)
- All loaders must be converted before release anyway
- Users already have SD card backups

### Phase 1: Foundation & Test Loader (Weeks 1-4)

**Goals:**
- Set up SQLite infrastructure
- Create database schema
- Convert ONE loader (ResMed) as proof of concept
- Validate end-to-end workflow

**Tasks:**
1. Create database connection manager
2. Implement complete database schema
3. Build repository pattern classes
4. Create unit tests
5. **Convert ResMed loader to write to database**
6. Test import from ResMed SD card
7. Verify data in database tables

**Why ResMed First:**
- Most popular CPAP brand
- Complex data format (good test)
- Large user base for testing
- Representative of loader complexity

**Testing:**
- Import test ResMed SD card data
- Verify all data in database
- Test session summaries
- Test event data
- Profile basic performance

**Deliverables:**
- Working SQLite database with full schema
- Repository classes for all tables
- ResMed loader writes to database
- Test suite validating ResMed import
- Proof of concept complete

**Critical Decision Point:** If Phase 1 succeeds, proceed with converting remaining loaders. If issues arise, resolve before continuing.

### Phase 2: Core UI & Reporting (Weeks 5-8)

**Goals:**
- Convert UI components to read from database
- Maintain feature parity with file-based version
- Optimize query performance

**Tasks:**
1. Update Profile/Machine loading from database
2. Convert Daily view to use database
3. Convert Overview to use database
4. Convert Statistics to use database
5. Update Reports to use database
6. Implement caching layer
7. Performance optimization

**Data Flow:**
```
Database → Repository → UI Components
```

**Testing:**
- All UI screens work with database
- Feature parity with file version
- Performance benchmarks
- Memory profiling

**Expected Performance:**
- 10-50x faster session queries
- 50% memory reduction
- Instant overview loading

**Deliverables:**
- All UI reads from database
- Reporting functions use database
- Performance meets/exceeds file-based version
- ResMed users can fully use database version

### Phase 3: Convert All Loaders (Weeks 9-14)

**Goals:**
- Convert all 15+ loader plugins to write to database
- Ensure consistency across all loaders
- Maintain SD card backup functionality

**Loader Conversion Priority:**
1. **ResMed** (Week 9) - Already done in Phase 1
2. **Philips Respironics (PRS1)** (Week 10) - 2nd most popular
3. **Fisher & Paykel (Icon)** (Week 10) - Popular
4. **BMC** (Week 11) - Growing user base
5. **Prisma/Löwenstein** (Week 11) - European market
6. **Intellipap** (Week 12) - Specialty
7. **Weinmann** (Week 12) - European
8. **SleepStyle** (Week 13) - New Zealand/Asia
9. **Oximeters** (Week 13) - CMS50, MD300W1, Viatom
10. **Position sensors, sleep trackers** (Week 14) - Somnopose, Dreem, Zeo
11. **Other/Legacy loaders** (Week 14) - Resvent, vREM, MSeries

**Loader Conversion Template:**
```cpp
// Each loader follows same pattern:
class LoaderXYZ : public MachineLoader {
    int Open(const QString & path) override {
        // 1. Parse SD card files (UNCHANGED)
        ParseSDCardData(path);
        
        // 2. Create Session object (UNCHANGED)
        Session* session = CreateSession();
        
        // 3. Write to database (NEW)
        SessionRepository repo(DatabaseManager::instance());
        repo.create(session);
        
        // 4. Backup SD card to filesystem (UNCHANGED)
        BackupSDCard(path);
        
        return SUCCESS;
    }
};
```

**Testing Strategy:**
- Convert 2-3 loaders per week
- Test each with real SD card data
- Verify database correctness
- Validate backup still works
- Test with volunteer users

**Deliverables:**
- All loaders write to database
- SD card backups still created
- Transaction safety for all imports
- Consistent error handling across loaders

### Phase 4: User Data Transition (Weeks 15-16)

**Goals:**
- Provide tools for users to transition
- Create re-import utility
- Documentation and user guide

**Tasks:**
1. Create "Rebuild from Backup" utility
2. Implement progress indicators
3. Create user documentation
4. Video tutorial
5. FAQ and troubleshooting guide

**Rebuild Utility:**
```
┌────────────────────────────────────────────┐
│     Rebuild Data from SD Card Backups     │
├────────────────────────────────────────────┤
│                                            │
│  Profile: John Doe                         │
│                                            │
│  This will rebuild your OSCAR database     │
│  from your SD card backups.                │
│                                            │
│  Located SD card backups:                  │
│  ☑ ResMed AirSense 10 (2,487 sessions)    │
│  ☑ Contec CMS50 (412 sessions)            │
│                                            │
│  Estimated time: 12 minutes                │
│  Database size: ~450 MB                    │
│                                            │
│  Note: Your existing data will remain      │
│  as backup until you confirm success.      │
│                                            │
│  [Back]  [Start Rebuild]  [Cancel]         │
└────────────────────────────────────────────┘
```

**User Transition Flow:**
1. User installs new OSCAR version
2. First launch detects file-based data
3. Offers to rebuild from SD card backups
4. User clicks "Rebuild"
5. OSCAR re-imports all SD card backups
6. Verifies database integrity
7. User confirms data looks correct
8. Old files moved to backup folder

**Deliverables:**
- Rebuild utility
- User documentation
- Video tutorial
- Transition wizard

### Phase 5: Testing & Refinement (Weeks 17-20)

**Goals:**
- Extensive testing with volunteers
- Bug fixes and refinements
- Performance optimization
- Platform-specific testing

**Testing Areas:**
1. **Functional Testing**
   - All loaders work correctly
   - All UI features work
   - Data accuracy verified
   - SD card backup still works

2. **Performance Testing**
   - Query speed benchmarks
   - Memory usage profiling
   - Startup time measurement
   - Large dataset handling

3. **Platform Testing**
   - Windows 10/11
   - macOS (Intel & Apple Silicon)
   - Linux (Ubuntu, Fedora, etc.)
   - Build from source

4. **User Acceptance Testing**
   - Real users with real data
   - Volunteer beta testers
   - Forum feedback
   - Bug reports

**Bug Triage:**
- Critical: Blocks release
- High: Must fix before release
- Medium: Should fix if time permits
- Low: Document as known issue

**Deliverables:**
- All critical/high bugs fixed
- Performance validated
- Platform compatibility verified
- Beta test sign-off

### Phase 6: Documentation & Release (Weeks 21-24)

**Goals:**
- Complete all documentation
- Create installers for 22 platforms
- Release preparation
- Support preparation

**Tasks:**
1. **Developer Documentation**
   - Database schema documentation
   - API reference
   - Loader conversion guide
   - Contribution guidelines

2. **User Documentation**
   - User guide updates
   - Migration instructions
   - FAQ
   - Troubleshooting guide
   - Video tutorials

3. **Release Preparation**
   - Create installers for all platforms
   - Test installation on each platform
   - Update website
   - Prepare release notes
   - Forum announcements

4. **Support Preparation**
   - Forum moderator briefing
   - Known issues list
   - Quick start guide
   - Support scripts

**Release Strategy:**
- Beta release to volunteers (Week 21)
- Address critical feedback (Week 22)
- Release candidate (Week 23)
- Final release (Week 24)

**Deliverables:**
- Complete documentation
- Installers for 22 platforms
- Release notes
- Forum support ready
- Production release

## Database Implementation Details

### Technology Choices

**Database:** SQLite
- **Rationale:** 
  - Embedded, no server required
  - Cross-platform (Windows, macOS, Linux)
  - ACID compliant
  - Excellent C++ support via Qt SQL module
  - Single-file portability

**Features to Enable:**
```sql
PRAGMA foreign_keys = ON;           -- Referential integrity
PRAGMA journal_mode = WAL;          -- Write-Ahead Logging for concurrency
PRAGMA synchronous = NORMAL;        -- Balance speed and safety
PRAGMA cache_size = -64000;         -- 64MB cache
PRAGMA temp_store = MEMORY;         -- Faster temp tables
```

### Connection Management

```cpp
class DatabaseManager {
public:
    static DatabaseManager& instance();
    
    bool initialize(const QString& dbPath);
    void close();
    
    QSqlDatabase database();
    
    bool transaction();
    bool commit();
    bool rollback();
    
    bool backup(const QString& backupPath);
    bool vacuum();
    bool analyze();
    
private:
    QSqlDatabase m_database;
    QString m_connectionName;
    QMutex m_mutex;
};
```

### Repository Pattern

```cpp
template<typename T>
class Repository {
public:
    virtual bool create(T* entity) = 0;
    virtual T* findById(qint64 id) = 0;
    virtual bool update(T* entity) = 0;
    virtual bool remove(T* entity) = 0;
    virtual QList<T*> findAll() = 0;
    
protected:
    QSqlDatabase db();
    bool executeQuery(const QString& sql, const QVariantList& params);
};

class ProfileRepository : public Repository<Profile> {
public:
    Profile* findByUsername(const QString& username);
    QList<Profile*> findAll();
    // ... specific methods
};
```

### Migration Manager

```cpp
class MigrationManager {
public:
    MigrationManager(const QString& profilePath);
    
    bool migrateProfile();
    bool migrateMachines();
    bool migrateSessions(const QDate& startDate, const QDate& endDate);
    bool migrateEvents();
    
    void setProgressCallback(std::function<void(int, QString)> callback);
    
    bool validate();
    bool rollback();
    
private:
    QString m_profilePath;
    DatabaseManager& m_dbManager;
    std::function<void(int, QString)> m_progressCallback;
};
```

## Migration Complexity Assessment

### Low Complexity (Risk)
- Profile metadata - Small files, simple structure
- Machine information - Well-defined schema
- Channel definitions - Simple key-value

### Medium Complexity (Risk)
- Session summaries - Larger datasets, XML parsing
- Value summaries - Nested structures
- Daily aggregations - Calculation logic

### High Complexity (Risk)
- Event data - Very large datasets (GBs)
- Binary parsing - Custom format handling
- Compression - Performance considerations
- Loader integration - 15+ plugins to modify

## Risk Mitigation Strategies

### Data Loss Prevention
1. **Automatic Backups** - Before any migration operation
2. **Validation Steps** - Checksum verification after migration
3. **Rollback Capability** - Keep original files until verified
4. **Transaction Safety** - Use database transactions for all writes
5. **Incremental Migration** - Migrate small batches first

### Performance Issues
1. **Profiling** - Measure before and after migration
2. **Indexing** - Add indexes for common queries
3. **Caching** - Implement multi-level caching
4. **Batch Operations** - Use bulk inserts
5. **Lazy Loading** - Load event data on demand

### Compatibility Issues
1. **Dual Mode** - Support both file and database modes
2. **Version Detection** - Auto-detect data format
3. **Graceful Degradation** - Handle missing data
4. **Backward Compatibility** - Always able to read files

### User Experience
1. **Progress Indicators** - Real-time feedback
2. **Pausable Migration** - Allow interruption
3. **Clear Documentation** - Migration guide
4. **Easy Rollback** - One-click restore

## Expected Benefits

### Performance Improvements
- **Query Speed**: 10-100x faster for complex queries
- **Memory Usage**: 50-70% reduction
- **Startup Time**: 30-50% faster
- **Overview Loading**: Near-instant (100x improvement)
- **Concurrent Access**: Multiple instances possible

### Data Integrity
- **ACID Compliance**: Prevents data corruption
- **Referential Integrity**: No orphaned records
- **Transaction Safety**: Atomic operations
- **Validation**: Built-in constraints
- **Backup**: Simplified backup/restore

### Maintainability
- **Schema Evolution**: Version-controlled updates
- **Easier Debugging**: SQL queries vs binary files
- **Better Testing**: Mock databases for unit tests
- **Code Simplification**: Repository pattern cleaner than file I/O

### Future Extensibility
- **Cloud Sync**: Foundation for cloud storage
- **Advanced Analytics**: SQL-based reporting
- **Data Export**: Standard SQL tools
- **Third-party Integration**: ODBC/JDBC access
- **Mobile Apps**: Shared database format

## Migration Tools & Commands

### Command-Line Interface

```bash
# Initialize database
oscar --init-database

# Migrate profile
oscar --migrate-profile "username"

# Migrate machines
oscar --migrate-machines "username"

# Migrate sessions (all or date range)
oscar --migrate-sessions "username" [--from 2020-01-01] [--to 2024-12-31]

# Migrate events
oscar --migrate-events "username"

# Migrate everything
oscar --migrate-all "username"

# Verify migration
oscar --migrate-verify "username"

# Show migration status
oscar --migrate-status "username"

# Rollback to files
oscar --migrate-rollback "username"

# Use database mode
oscar --use-database

# Use file mode (legacy)
oscar --use-files
```

### Migration Wizard UI

```
┌─────────────────────────────────────────────┐
│     OSCAR Database Migration Wizard         │
├─────────────────────────────────────────────┤
│                                             │
│  Profile: John Doe                          │
│                                             │
│  Current Storage: File-based                │
│  Target Storage: SQLite Database            │
│                                             │
│  ☑ Backup current data                      │
│  ☑ Create database schema                   │
│  ☐ Migrate profile (0/1)                    │
│  ☐ Migrate machines (0/3)                   │
│  ☐ Migrate sessions (0/2,487)               │
│  ☐ Migrate events (0/2,487)                 │
│                                             │
│  Estimated time: 15 minutes                 │
│  Database size: ~450 MB                     │
│  (Current files: ~1.2 GB)                   │
│                                             │
│  [Back]  [Start Migration]  [Cancel]        │
└─────────────────────────────────────────────┘
```

## Testing Strategy

### Unit Tests
- Repository CRUD operations
- Data conversion accuracy
- Compression/decompression
- Query builders
- Transaction handling

### Integration Tests
- Profile loading from database
- Session import workflow
- Loader plugin integration
- Statistics calculations
- UI components

### Performance Tests
- Query benchmarks
- Memory profiling
- Startup time measurement
- Large dataset handling
- Concurrent access

### User Acceptance Tests
- Migration workflow
- Data accuracy verification
- UI responsiveness
- Error handling
- Rollback procedure

## Documentation Requirements

### User Documentation
1. **Migration Guide** - Step-by-step instructions
2. **FAQ** - Common questions and answers
3. **Troubleshooting Guide** - Error resolution
4. **Performance Comparison** - Before/after metrics
5. **Video Tutorial** - Visual walkthrough

### Developer Documentation
1. **Database Schema** - Complete ERD and table descriptions
2. **API Reference** - Repository methods and examples
3. **Migration Architecture** - System design
4. **Extension Guide** - Adding new features
5. **Testing Guide** - Running and writing tests

## Revised Timeline & Milestones

### Milestone 1: Proof of Concept (End of Week 4)
- Database infrastructure complete
- ResMed loader converted and tested
- End-to-end workflow validated
- **Go/No-Go Decision Point**

### Milestone 2: UI Conversion (End of Week 8)
- All UI components use database
- Feature parity achieved
- Performance benchmarks met
- ResMed users can use database version

### Milestone 3: All Loaders Converted (End of Week 14)
- All 15+ loaders write to database
- SD card backups still functional
- Consistent error handling
- Ready for user testing

### Milestone 4: User Transition Tools (End of Week 16)
- Rebuild utility complete
- Documentation ready
- User transition path clear
- Ready for beta testing

### Milestone 5: Testing Complete (End of Week 20)
- All critical bugs fixed
- Performance validated
- Platform compatibility verified
- Beta testing successful

### Milestone 6: Production Release (End of Week 24)
- Documentation complete
- 22 platform installers ready
- Forum support prepared
- Production release

## Critical Success Factors

### 1. Single Installer Requirement
- Database must be automatically initialized on first run
- No manual configuration required
- Works across all 22 platform variants
- Users building from source must have seamless experience

### 2. Rebuild from Backup Strategy
- Simpler than migrating old data
- Cleaner data (regenerated from source)
- Leverages existing OSCAR capability
- Users already have SD card backups
- Proven, reliable process

### 3. All-or-Nothing Loader Conversion
- **Cannot release with partial loader support**
- All loaders must be converted before release
- Test with ResMed first (proof of concept)
- Then systematically convert remaining loaders
- No migration of old XML/binary files needed

### 4. Volunteer Development Context
- Keep implementation as simple as possible
- Clear, well-documented code
- Minimize breaking changes
- Enable incremental development
- Support community contributions

### 5. Multi-Platform Support
- Must work on Windows, macOS, Linux
- Test on multiple platforms throughout development
- SQLite provides excellent cross-platform support
- Qt provides consistent database API

## Implementation Priorities

### Must Have (Release Blockers)
1. All loaders converted to database
2. All UI components read from database
3. SD card backup still works
4. Rebuild from backup utility
5. Database auto-initialization
6. Feature parity with file version
7. Installers for 22 platforms

### Should Have (High Priority)
1. Performance improvements validated
2. Comprehensive documentation
3. User transition wizard
4. Video tutorials
5. Testing on all major platforms

### Nice to Have (If Time Permits)
1. Database optimization tools
2. Advanced reporting features
3. Cloud sync preparation
4. Mobile app compatibility

## Conclusion

This revised database migration strategy leverages OSCAR's existing rebuild capability to dramatically simplify the migration process. Key points:

1. **Loader-First Approach**: Convert loaders to write to database, not migrate old data
2. **Rebuild from Backup**: Users re-import from SD card backups (existing capability)
3. **All Loaders Required**: All 15+ loaders must be converted before release
4. **Single Installer**: Must work seamlessly without manual configuration
5. **Volunteer Context**: Keep it simple, well-documented, community-friendly
6. **Production Ready**: 6-month timeline with clear milestones

**Recommended Next Step:** Start Phase 1 with ResMed loader conversion as proof of concept. Success validates the approach; failure allows us to pivot before significant investment.

---

**Document Version**: 2.0  
**Created**: December 21, 2025  
**Updated**: December 22, 2025  
**Author**: OSCAR Development Team  
**Status**: Revised Strategy Based on Rebuild Capability
