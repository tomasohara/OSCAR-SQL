# OSCAR Database Migration Strategy

## Executive Summary

This document provides a comprehensive strategy for migrating OSCAR from its current file-based storage system to a SQLite database. The analysis identifies which files should be converted to database tables and which must remain as files, and proposes an incremental migration path that minimizes risk while maximizing benefits.

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

## Incremental Migration Path

### Phase 1: Foundation (Weeks 1-2)

**Goals:**
- Set up SQLite infrastructure
- Create database schema
- Implement basic CRUD operations
- Establish testing framework

**Tasks:**
1. Create database connection manager
2. Implement schema creation scripts
3. Build repository pattern classes
4. Create unit tests
5. Add dual-mode support (database vs files)

**Deliverables:**
- Working SQLite database
- Repository classes for all tables
- Test suite passing
- Command-line flag: `--use-database`

### Phase 2: Profile & Machine Migration (Weeks 3-5)

**Goals:**
- Migrate profile and machine metadata
- Update profile loading logic
- Implement migration tools

**Tasks:**
1. Create ProfileRepository class
2. Create MachineRepository class
3. Implement XML to database converter
4. Update Profile class to use database
5. Update Machine class to use database
6. Create migration command

**Migration Flow:**
```
Profile.xml → XML Parser → ProfileRepository → Database
machines.xml → XML Parser → MachineRepository → Database
```

**Testing:**
- Migrate test profiles
- Verify all preferences preserved
- Test profile switching
- Validate machine listings

**Rollback:** Keep original XML files until validated

**Deliverables:**
- `--migrate-profile "username"` command
- Profiles load from database
- Machines list from database
- Original XML remains as backup

### Phase 3: Session Summary Migration (Weeks 6-9)

**Goals:**
- Migrate session summaries to database
- Achieve significant performance improvements
- Handle large datasets efficiently

**Tasks:**
1. Create SessionRepository class
2. Create ChannelSummaryRepository class
3. Implement Summaries.xml.gz parser
4. Batch migration with progress reporting
5. Update session loading logic
6. Implement caching layer

**Migration Strategy:**
- Progressive migration (newest sessions first)
- Date range selection
- Background processing
- Progress indicators

**Testing:**
- Migrate sessions incrementally
- Verify statistics accuracy
- Performance benchmarks
- Memory profiling

**Expected Performance:**
- 10-50x faster session queries
- 50% memory reduction
- 30% faster startup

**Deliverables:**
- `--migrate-sessions [date-range]` command
- Session summaries in database
- Daily view loads from database
- Overview statistics from database

### Phase 4: Event Data Migration (Weeks 10-13)

**Goals:**
- Migrate detailed event data
- Implement compression
- Maintain performance with large datasets

**Tasks:**
1. Create EventRepository class
2. Implement binary file parser
3. Add zlib compression
4. On-demand event loading
5. Update Daily tab to use database
6. Optimize BLOB handling

**Hybrid Approach:**
- Summaries: Always in database
- Events: Database with file fallback option
- User choice for very large datasets

**Testing:**
- Migrate event data for test sessions
- Verify waveform accuracy
- Test compression ratios
- Daily detail view validation

**Deliverables:**
- `--migrate-events` command
- Event data in database
- Daily detail view works from database
- File-based option remains available

### Phase 5: Channel Configuration (Weeks 14-15)

**Goals:**
- Migrate channel preferences
- Enable per-profile customization

**Tasks:**
1. Create ChannelRepository class
2. Parse channels.dat file
3. Migrate to channel_definitions table
4. Update channel preference UI
5. Implement preference inheritance

**Deliverables:**
- Channel preferences in database
- Per-profile customization works
- Preference UI updated

### Phase 6: Daily Aggregation Caching (Weeks 16-17)

**Goals:**
- Implement day-level caching
- Dramatic improvement in overview loading

**Tasks:**
1. Create DayRepository class
2. Calculate and store daily aggregates
3. Update overview screen logic
4. Implement incremental updates
5. Add cache invalidation

**Testing:**
- Verify daily statistics
- Test date range queries
- Overview performance benchmarks

**Expected Performance:**
- Instant overview loading
- 100x faster statistics queries

**Deliverables:**
- Days table populated
- Overview screen uses database
- Real-time performance improvement

### Phase 7: Loader Integration (Weeks 18-21)

**Goals:**
- Update loaders to write to database
- Maintain SD card backup functionality
- Ensure all loaders work correctly

**Tasks:**
1. Update MachineLoader base class
2. Modify all 15+ loader plugins
3. Implement transaction management
4. Preserve SD card backup process
5. Add import error handling

**Import Flow:**
```
SD Card (File) → Loader Plugin → Parse Data → Database Repository
       ↓                                              ↓
   (Backup to                                    (Structured
    filesystem)                                   storage)
```

**Testing:**
- Test each loader plugin
- Verify data imports correctly
- Test error handling
- Rollback testing

**Deliverables:**
- All loaders write to database
- SD card backups still created
- Robust error handling
- Transaction safety

### Phase 8: Optimization & Polish (Weeks 22-24)

**Goals:**
- Performance tuning
- User experience improvements
- Production readiness

**Tasks:**
1. Query optimization
2. Index tuning
3. Cache optimization
4. Memory profiling
5. Concurrent access testing
6. Migration wizard UI
7. Documentation
8. User guide

**Testing:**
- Full application testing
- Performance benchmarks
- Load testing
- User acceptance testing

**Deliverables:**
- Optimized database
- Migration wizard
- Complete documentation
- Production-ready system

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

## Timeline & Milestones

### Milestone 1: Foundation (End of Week 2)
- Database infrastructure complete
- Basic repositories implemented
- Test framework established

### Milestone 2: Metadata Migration (End of Week 5)
- Profiles and machines in database
- Command-line migration tools
- Initial testing complete

### Milestone 3: Session Migration (End of Week 9)
- Session summaries in database
- Performance improvements visible
- User testing begins

### Milestone 4: Event Migration (End of Week 13)
- Event data in database
- Compression working
- Full feature parity with file-based

### Milestone 5: Complete Integration (End of Week 21)
- All loaders use database
- Migration wizard complete
- Full testing complete

### Milestone 6: Production Release (End of Week 24)
- Documentation complete
- Beta testing successful
- Ready for release

## Conclusion

This database migration strategy provides a comprehensive, incremental path to modernizing OSCAR's data storage while minimizing risk and maximizing benefits. Key points:

1. **Preserve Loader Independence**: SD card files remain external, loaders continue to parse them
2. **Incremental Approach**: Users can migrate at their own pace
3. **Safety First**: Multiple backup and rollback options
4. **Significant Benefits**: 10-100x performance improvements expected
5. **Production Ready**: 6-month timeline to fully tested release

The recommended approach is to start with Phase 1-2 (profile and machine metadata) as these provide immediate value with minimal risk, then proceed incrementally based on user feedback and testing results.

---

**Document Version**: 1.0  
**Created**: December 21, 2025  
**Author**: OSCAR Development Team  
**Status**: Proposal for Review
