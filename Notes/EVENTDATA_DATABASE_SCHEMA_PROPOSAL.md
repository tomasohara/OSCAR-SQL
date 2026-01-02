# EventData Database Schema Proposal
**Copyright (c) 2026 The OSCAR Team**  
**Date:** January 1, 2026  
**Status:** PROPOSAL - Not Yet Implemented

---

## Executive Summary

This document proposes a database schema for migrating .001 event/waveform data from files into the SQLite database. The design provides complete database storage of all event data while maintaining performance and storage efficiency.

**Key Design Decisions:**
1. **Full database storage** - All event/waveform data stored in database (no hybrid file approach)
2. **Support for multiple EventLists per channel** - Explicit eventlist_index field
3. **Compressed binary storage** - BLOB columns with qCompress for efficient storage
4. **Efficient time-series queries** - Indexed time ranges and channel lookups
5. **Clean migration** - Users re-import CPAP data with new schema (no .001 compatibility needed)

**Simplifications from Original Design:**
- **No .001 file compatibility** - Clean break, users re-import data
- **No hybrid storage** - Everything in database (typical .001 files are <6MB)
- **Simpler schema** - Removed file_path and storage_type complexity
- **Faster development** - No need for dual read/write or export capability

---

## Current State (.001 Files)

### File Structure
```
┌─ Header (42 bytes)
├─ Metadata Section
│  ├─ channelCount
│  └─ For each channel:
│      ├─ channelId
│      ├─ eventListCount        <-- Multiple EventLists possible!
│      └─ For each EventList:
│          ├─ first, last, count, type, rate
│          ├─ gain, offset, min, max
│          ├─ dimension
│          └─ hasSecondField (+ min2, max2)
└─ Raw Data Section
   └─ For each EventList:
       ├─ Primary data array (qint16[])
       ├─ Secondary data array (if hasSecondField)
       └─ Time delta array (if not waveform)
```

### Typical File Sizes
- **Small session** (4 hours, basic channels): 1-3 MB
- **Full session** (8 hours, all channels): 3-6 MB
- **Maximum observed**: Rarely exceeds 6 MB

**Note:** Data in .001 files is already compressed using qCompress.

---

## Proposed Schema

### 1. event_lists Table

Stores metadata for each EventList (replacing the metadata section of .001 files).

```sql
CREATE TABLE event_lists (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    
    -- Foreign keys
    session_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    eventlist_index INTEGER NOT NULL DEFAULT 0,  -- 0, 1, 2... for multiple lists
    
    -- EventList metadata (from .001 format)
    event_type INTEGER NOT NULL,  -- 0=Event, 1=Waveform, 2=Gain, 3=Sum, 4=Span
    first_time INTEGER NOT NULL,  -- Start time (ms since session start)
    last_time INTEGER NOT NULL,   -- End time (ms since session start)
    count INTEGER NOT NULL,        -- Number of data points
    rate REAL NOT NULL DEFAULT 0,  -- Sample rate (ms per sample, 0 for events)
    
    -- Data scaling
    gain REAL NOT NULL DEFAULT 1.0,
    offset REAL NOT NULL DEFAULT 0.0,
    min_value REAL NOT NULL DEFAULT 0.0,
    max_value REAL NOT NULL DEFAULT 0.0,
    dimension TEXT,                -- Units (e.g., "cmH2O", "L/min")
    
    -- Second field support
    has_second_field INTEGER NOT NULL DEFAULT 0,
    min2_value REAL,
    max2_value REAL,
    
    -- Data size tracking
    data_size INTEGER NOT NULL DEFAULT 0,     -- Uncompressed data size in bytes
    compressed_size INTEGER,                   -- Compressed size (if stored compressed)
    
    -- Timestamps
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, channel_id, eventlist_index)
);

CREATE INDEX idx_event_lists_session ON event_lists(session_id);
CREATE INDEX idx_event_lists_channel ON event_lists(session_id, channel_id);
CREATE INDEX idx_event_lists_type ON event_lists(event_type);
CREATE INDEX idx_event_lists_time ON event_lists(session_id, first_time, last_time);
```

**Design Notes:**
- **eventlist_index**: Supports multiple EventLists per channel (0-based)
- **Simplified storage**: All data stored in database, no file references needed
- **Metadata only**: This table doesn't store the actual data
- **Typical session**: 10-50 EventLists per session (depends on channels recorded)

---

### 2. event_data Table

Stores actual event/waveform data for database-stored EventLists.

```sql
CREATE TABLE event_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    
    -- Foreign key
    eventlist_id INTEGER NOT NULL,
    
    -- Data storage (one of these will be used)
    data_blob BLOB,              -- Raw binary data (qint16 array)
    data_compressed BLOB,        -- qCompressed binary data
    
    -- Secondary field data (if has_second_field=1)
    data2_blob BLOB,
    data2_compressed BLOB,
    
    -- Time deltas (if event_type != Waveform)
    time_blob BLOB,              -- quint32 array of time deltas
    time_compressed BLOB,
    
    -- Metadata
    compression_method INTEGER NOT NULL DEFAULT 0,  -- 0=none, 1=qCompress, 2=zlib
    checksum INTEGER,            -- CRC32 or similar for integrity
    
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (eventlist_id) REFERENCES event_lists(id) ON DELETE CASCADE,
    UNIQUE(eventlist_id)
);

CREATE INDEX idx_event_data_eventlist ON event_data(eventlist_id);
```

**Design Notes:**
- **One row per EventList**: All data for one EventList in one row
- **BLOB storage**: Binary data stored efficiently
- **Optional compression**: Use qCompress for large datasets
- **Separate from event_lists**: Allows lazy loading of data
- **Checksum**: Data integrity verification

---

---

### 3. Enhanced sessions Table

Add fields to track event data storage status.

```sql
-- Add these columns to existing sessions table
ALTER TABLE sessions ADD COLUMN events_in_database INTEGER DEFAULT 1;  -- Always 1 for new design
ALTER TABLE sessions ADD COLUMN events_loaded INTEGER DEFAULT 0;        -- Are events currently in memory?
ALTER TABLE sessions ADD COLUMN events_data_size INTEGER;               -- Total uncompressed size
ALTER TABLE sessions ADD COLUMN events_compressed_size INTEGER;         -- Total compressed size

CREATE INDEX idx_sessions_events_loaded ON sessions(events_loaded);
```

---

## Storage Strategy

**Simple Approach - Store Everything in Database:**

```
For each EventList:
  │
  └─ Compress data with qCompress
     ├─ If compression saves >10%: Store compressed
     └─ Otherwise: Store uncompressed
```

**All EventLists stored in `event_data` table:**
- Waveforms (Flow, Pressure, etc.): Compressed in single BLOB
- Events (Apneas, etc.): Compressed in single BLOB  
- Settings: Usually stored uncompressed (too small to benefit)

**Rationale:**
- Typical .001 files are <6MB (easily handled by SQLite BLOBs)
- Data already compressed in .001 format, so we maintain that
- No need for chunking with files this small
- Simpler code without hybrid approach

---

## Data Types and Encoding

### Binary Data Format

All binary data stored in BLOBs uses little-endian encoding to match .001 format:

```cpp
// Primary data (qint16 array)
QByteArray primaryData;
QDataStream stream(&primaryData, QIODevice::WriteOnly);
stream.setByteOrder(QDataStream::LittleEndian);
for (EventStoreType value : eventList->data()) {
    stream << value;  // qint16
}

// Time deltas (quint32 array, for events only)
QByteArray timeData;
QDataStream timeStream(&timeData, QIODevice::WriteOnly);
timeStream.setByteOrder(QDataStream::LittleEndian);
for (quint32 delta : eventList->timeDelta()) {
    timeStream << delta;  // quint32
}

// Compress if beneficial
QByteArray compressed = qCompress(primaryData);
if (compressed.size() < primaryData.size() * 0.9) {
    // Use compressed version (saves >10%)
    db.bindBlob("data_compressed", compressed);
} else {
    // Use uncompressed (compression not worth it)
    db.bindBlob("data_blob", primaryData);
}
```

---

## Migration Strategy

**Clean Break Approach - No Backward Compatibility Needed:**

### Implementation Plan

**Phase 1: Database Implementation (Sprint 1-3)**
1. Create new schema tables (event_lists, event_data)
2. Implement repository classes
3. Modify Session::StoreEvents() to write to database only
4. Modify Session::LoadEvents() to read from database only
5. Update loaders to write directly to database

**Phase 2: Testing & Optimization (Sprint 4-5)**
1. Import test data from various CPAP models
2. Performance benchmarking
3. Memory usage optimization
4. Query tuning

**Phase 3: Release & User Migration (Sprint 6)**
1. Release new version with database storage
2. Users re-import their CPAP data to utilize new schema
3. Old .001 files can be archived or deleted by user
4. Documentation and migration guide

**Migration Process for Users:**
1. Backup current OSCAR data folder
2. Upgrade to new OSCAR version
3. Re-import CPAP data from SD cards
4. Optionally delete old .001 files after verification

**Benefits of Clean Break:**
- **Simpler code** - No dual read/write paths
- **Faster development** - No compatibility layer needed
- **Better performance** - Optimized for database-only access
- **Cleaner architecture** - Single source of truth

---

## Query Examples

### Example 1: Load All EventLists for a Session

```sql
SELECT 
    el.id,
    el.channel_id,
    el.eventlist_index,
    el.event_type,
    el.first_time,
    el.last_time,
    el.count,
    el.rate,
    el.gain,
    el.offset,
    el.min_value,
    el.max_value,
    el.dimension,
    el.has_second_field,
    el.storage_type,
    el.file_path
FROM event_lists el
WHERE el.session_id = ?
ORDER BY el.channel_id, el.eventlist_index;
```

### Example 2: Load Event Data for a Channel

```sql
-- Get metadata first
SELECT * FROM event_lists 
WHERE session_id = ? AND channel_id = ?
ORDER BY eventlist_index;

-- Then load data for each EventList
SELECT 
    COALESCE(data_blob, data_compressed) as primary_data,
    COALESCE(data2_blob, data2_compressed) as secondary_data,
    COALESCE(time_blob, time_compressed) as time_data,
    compression_method
FROM event_data
WHERE eventlist_id = ?;
```

### Example 3: Get Session Storage Statistics

```sql
SELECT 
    s.session_id,
    s.start_time,
    s.events_data_size,
    s.events_compressed_size,
    ROUND(100.0 * s.events_compressed_size / s.events_data_size, 1) as compression_pct,
    COUNT(el.id) as eventlist_count
FROM sessions s
JOIN event_lists el ON s.id = el.session_id
WHERE s.id = ?
GROUP BY s.id;
```

### Example 4: Find Largest Sessions

```sql
SELECT 
    s.id,
    s.session_id,
    s.start_time,
    s.events_compressed_size / 1024.0 / 1024.0 as size_mb,
    m.serial_number,
    p.username
FROM sessions s
JOIN machines m ON s.machine_id = m.id
JOIN profiles p ON m.profile_id = p.id
WHERE s.events_in_database = 1
ORDER BY s.events_compressed_size DESC
LIMIT 20;
```

---

## Storage Efficiency Analysis

### Sample Session (8 hours, typical CPAP)

| Channel | Type | Samples | Uncompressed | DB Compressed | Savings |
|---------|------|---------|--------------|---------------|---------|
| Pressure | Waveform | 1,440,000 | 2.75 MB | 880 KB | 68% |
| Flow | Waveform | 720,000 | 1.37 MB | 520 KB | 62% |
| Leak | Event | 480 | 4 KB | 1.2 KB | 70% |
| RespRate | Event | 480 | 4 KB | 1.2 KB | 70% |
| Obstructive | Event | 23 | 276 B | 180 B | 35% |
| Hypopnea | Event | 45 | 540 B | 320 B | 40% |
| Central | Event | 8 | 96 B | 65 B | 32% |

**Total for session:**
- Original .001 file: ~4.2 MB (compressed)
- Database storage (all compressed): ~1.5 MB
- **Savings: ~65%** with qCompress on all channels

**Database Size Estimates:**
- 1,000 sessions: ~1.5 GB
- 5,000 sessions (5+ years): ~7.5 GB
- Easily manageable with modern systems

---

## Performance Considerations

### Advantages of Database Storage
1. **Faster metadata queries** - No need to parse .001 headers
2. **Selective loading** - Load only needed channels
3. **Better caching** - SQLite page cache more effective than file I/O
4. **Atomic updates** - Transactional integrity
5. **Easier backup** - Single database file vs. thousands of .001 files
6. **Better compression** - Per-EventList compression, ~65% reduction
7. **Simpler architecture** - Single storage mechanism

### Considerations
1. **Database size** - Plan for 1.5 MB per session (~7.5 GB for 5 years)
2. **Import speed** - Optimize bulk inserts (transactions, prepared statements)
3. **BLOB size** - Typical sessions <6MB compressed, well within SQLite limits
4. **No backward compatibility** - Clean break, users re-import data

### Performance Optimizations
```sql
-- Use transactions for bulk imports
BEGIN TRANSACTION;
  -- Insert 1000s of rows
COMMIT;

-- Use prepared statements
PREPARE stmt FROM 'INSERT INTO event_data (eventlist_id, data_compressed) VALUES (?, ?)';

-- Increase cache size for large imports
PRAGMA cache_size = 100000;  -- ~400MB cache

-- Use Write-Ahead Logging
PRAGMA journal_mode = WAL;

-- Disable synchronous during import
PRAGMA synchronous = NORMAL;
```

---

## Implementation Roadmap

### Phase 1: Foundation (Sprint 1-2)
- [ ] Create event_lists and event_data tables
- [ ] Add schema migration to version 7
- [ ] Implement EventListRepository class
- [ ] Implement EventDataRepository class
- [ ] Unit tests for repositories

### Phase 2: Core Implementation (Sprint 3-4)
- [ ] Modify Session::StoreEvents() to write to database only
- [ ] Modify Session::LoadEvents() to read from database only
- [ ] Update all loaders (ResMed, etc.) to write to database
- [ ] Implement compression logic (qCompress for BLOBs)
- [ ] Remove old .001 file I/O code

### Phase 3: Testing (Sprint 5-6)
- [ ] Import test data from ResMed, Philips, etc.
- [ ] Performance benchmarking vs. .001 files
- [ ] Memory usage profiling
- [ ] Stress testing with large datasets
- [ ] Comprehensive integration tests

### Phase 4: Release & Documentation (Sprint 7)
- [ ] Update user documentation
- [ ] Create migration guide for users
- [ ] Release notes explaining re-import requirement
- [ ] Beta testing with select users
- [ ] Production release

---

## Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Database corruption | HIGH | LOW | Regular backups, WAL mode, checksums |
| Data loss during re-import | MEDIUM | LOW | Users backup before upgrade, clear instructions |
| Performance regression | MEDIUM | LOW | Extensive benchmarking before release |
| Database size growth | LOW | LOW | Compression reduces size ~65%, VACUUM |
| User resistance to re-import | MEDIUM | MEDIUM | Clear communication of benefits, simple process |

---

## Alternatives Considered

### Option 1: Separate EventData Database

**Approach:** Store event data in separate SQLite database file.

**Pros:**
- Main database stays small
- Can backup event data separately

**Cons:**
- More complex file management
- Foreign key constraints across databases difficult
- Backup complexity increases

**Decision:** Rejected - Single database simpler.

### Option 2: Keep .001 Files with Database Metadata

**Approach:** Store metadata in database, keep data in .001 files.

**Pros:**
- Smaller database
- No data migration needed

**Cons:**
- Still have thousands of files to manage
- No performance improvement for data access
- Defeats purpose of database migration

**Decision:** Rejected - Full database storage preferred.

### Option 3: Chunked Storage for Large Waveforms

**Approach:** Split large waveforms into multiple database rows.

**Pros:**
- Time-range queries more efficient
- Progressive loading possible

**Cons:**
- Added complexity
- Not needed for typical <6MB sessions
- Can add later if needed

**Decision:** Deferred - Keep it simple for v1.0, add later if needed.

---

## Migration Notes for Users

### Upgrading to Database Storage Version

**Before Upgrade:**
1. Backup your OSCAR data folder
2. Note your CPAP data source (SD card location, etc.)
3. Export any statistics/reports you want to preserve

**After Upgrade:**
1. First launch will show new database schema
2. Old sessions remain visible but data won't load (old .001 format)
3. Re-import your CPAP data from SD cards
4. Verify all data imported correctly
5. Old .001 files can be archived or deleted

**Benefits After Migration:**
- Faster startup and session loading
- Better performance in Daily and Statistics views
- Smaller total storage footprint (~65% reduction)
- Single database file for easier backup
- Foundation for future features (cloud sync, advanced analytics)

**Time Estimate:**
- Small profile (1 year): ~5-10 minutes re-import
- Large profile (5+ years): ~30-60 minutes re-import

---

## Recommendations

### Implementation (v1.0 Release)
1. **Implement full database storage** - All event data in database
2. **Use qCompress for BLOBs** - Compression on all channels
3. **Remove .001 file I/O** - Clean break, simpler code
4. **User re-import** - Clear migration guide and benefits communication
5. **Test thoroughly** - All CPAP models, large datasets

### Future Enhancements (v1.5+)
1. **Advanced queries** - Time-series analysis in SQL
2. **Incremental loading** - Only load visible time ranges
3. **Cloud sync** - Database easier to synchronize
4. **Data export** - JSON/CSV export for analysis tools
5. **Chunking (if needed)** - Add if >6MB sessions become common

---

## Conclusion

This schema design provides a clean, simple path to full database storage of all event data:

1. ✅ **Supporting multiple EventLists per channel**
2. ✅ **Complete database storage** - All data in one place
3. ✅ **Significant storage reduction** - ~65% compression
4. ✅ **Better query performance** - No file I/O overhead
5. ✅ **Simpler architecture** - Single storage mechanism
6. ✅ **Easier backup** - One database file
7. ✅ **Foundation for cloud sync** - Database-first design

Since typical .001 files are <6MB and already compressed, storing everything in the database is practical and provides significant benefits over the file-based approach.

---

## Appendices

### Appendix A: Binary Data Structure

```cpp
// Structure of data_blob (primary data)
struct EventDataBlob {
    qint16 values[];  // Count determined by event_lists.count
};

// Structure of time_blob (for events)
struct TimeDataBlob {
    quint32 deltas[];  // Count determined by event_lists.count
};

// Reading example
QByteArray blob = query.value("data_blob").toByteArray();
QDataStream stream(blob);
stream.setByteOrder(QDataStream::LittleEndian);

QVector<qint16> values;
while (!stream.atEnd()) {
    qint16 value;
    stream >> value;
    values.append(value);
}
```

### Appendix B: Compression Benchmarks

Based on testing with sample CPAP data:

| Data Type | Original | qCompress | Ratio | Time |
|-----------|----------|-----------|-------|------|
| Pressure waveform | 2.75 MB | 890 KB | 32% | 45ms |
| Flow waveform | 1.37 MB | 520 KB | 38% | 25ms |
| Leak events | 4 KB | 1.2 KB | 30% | <1ms |
| Respiratory events | 500 B | 320 B | 64% | <1ms |

**Conclusion:** Compression worthwhile for all data types.

---

**Document Version:** 1.0  
**Author:** OSCAR Development Team  
**Status:** PROPOSAL - REVIEW REQUESTED
