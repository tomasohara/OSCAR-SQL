# OSCAR Database - Design Philosophy and Performance

---

## Data Storage Philosophy

**⚡ Database-Only Approach (v8+):**
- **Database**: ALL data including waveforms, events, metadata, settings, summaries, daily aggregates
- **Files**: DEPRECATED - .001 files no longer used for event/waveform data

**Rationale:**
- SQLite excellent for structured metadata, queries, AND binary data (BLOBs)
- Database storage provides ACID transactions, referential integrity, and atomic operations
- Compressed BLOBs achieve 40-60% compression, comparable to .001 file format
- Single-file database simplifies backup, restore, and data management
- Eliminates file synchronization issues between database and .001 files
- Daily summaries enable lightning-fast reports without loading sessions

**Migration:** Existing .001 files are read once during upgrade, then data is migrated to event_data table and .001 files can be deleted.

---

## Performance Considerations

- **Indexes** optimize common query patterns
- **Foreign keys** ensure referential integrity
- **Cascade deletes** simplify data management
- **Session summaries** provide fast statistics per session
- **Daily summaries** provide ultra-fast statistics per day (20-100x faster)
- **BLOB compression** reduces database size by 40-60% for waveform data ⚡ NEW IN v8
- **Transactional safety** ensures data integrity across all operations ⚡ NEW IN v8

---

## Schema v6 Highlights

The daily_summaries table (new in v6) provides:
- **Pre-calculated daily statistics** for all CPAP metrics
- **Automatic population** during data load
- **35 statistics per day** including AHI, RDI, pressure, leak, oximetry
- **Fast queries** for Overview and Statistics screens (<100ms vs 2-3 seconds)
- **One row per profile-day** (machine_id removed in v16; see v16 highlights below)
- **Cache invalidation** via sessions_hash field

---

## Schema v7 Highlights 🐛 **BUG FIX**

The session_channel_values table (new in v7) provides:
- **Fixes critical bug** where value/time summaries were not persisted to database
- **Stores m_valuesummary and m_timesummary** hash data for each channel
- **Enables accurate weighted averages** and time-based statistics
- **Impact**: Without this table, weighted averages defaulted to simple averages, causing significant inaccuracies in pressure statistics and other metrics
- **Automatic migration** from schema version 6 to 7
- **Note**: Existing sessions will need to be re-saved to populate this data (happens automatically on next import)

---

## Schema v8 Highlights ⚡ **MAJOR CHANGE - DATABASE-ONLY MODE**

The event_lists and event_data tables (new in v8) provide revolutionary database-only storage:

**What Changed:**
- **Eliminates .001 files** - All waveform/event data now stored in database BLOBs
- **Two new tables**: event_lists (metadata) and event_data (binary data)
- **Compressed storage** - qCompress level 9 achieves 40-60% compression
- **Transactional integrity** - All data protected by ACID transactions
- **Simpler data model** - Single database file instead of database + thousands of .001 files

**Benefits:**
- **Data integrity** - Foreign key constraints ensure waveform data consistency with sessions
- **Atomic operations** - Import/delete operations are fully transactional
- **Simplified backup** - Single database file contains ALL user data
- **Better performance** - Eliminates file system overhead for thousands of small files
- **Cross-platform** - No file path issues, permissions problems, or filename limitations
- **Checksum verification** - CRC16 checksums ensure data integrity

**Migration Strategy:**
- **Automatic** - Existing .001 files read once on first load after upgrade
- **Data preserved** - All waveform/event data migrated to event_data table
- **Backward compatible** - Can still read old .001 files if database migration fails
- **File cleanup** - After successful migration, .001 files can be safely deleted

**Storage Efficiency:**
- Compression ratio: 40-60% (comparable to .001 format)
- Typical session: 50-200 KB compressed in database
- Overhead per EventList: ~200 bytes metadata in event_lists table
- Net result: Similar or smaller database size compared to .001 files

**Performance Impact:**
- Load times: Comparable to .001 file loading
- Memory usage: Unchanged (data still decompressed to memory)
- Query flexibility: Can now query waveform metadata without loading full data
- Transaction safety: Significantly improved vs file-based storage

**Note**: Re-import CPAP data after upgrade to migrate from .001 files to database storage.

---

## Schema v9 Highlights 📝 **ENHANCEMENT**

The `json_value` column in `session_settings` (added in v9) provides:

**What Changed:**
- **New column**: Added `json_value TEXT` column to `session_settings` table
- **Complex data support**: Enables storing structured/complex data as JSON
- **Journal migration**: Supports migrating journal-type data to the database

**Use Cases:**
- **Bookmark data**: User-created bookmarks with timestamps and descriptions
- **Session notes**: Rich text or structured notes attached to sessions
- **Custom fields**: Any complex settings that don't fit the simple value model
- **Inter-session data**: Data spanning multiple sessions (e.g., sleep diary entries)

**Benefits:**
- **Flexibility**: JSON format accommodates diverse data structures
- **Future-proofing**: Supports new data types without schema changes
- **Consolidation**: Moves more data from files into the database
- **Query capability**: JSON fields can be queried using SQLite's JSON functions

**Migration Impact:**
- **Automatic upgrade**: Existing databases get the new column added
- **Backward compatible**: Old data without json_value continues to work
- **No data loss**: Existing session_settings records remain unchanged

---

## Schema v10 Highlights 🔧 **SEMANTIC FIX**

The schema v10 changes provide semantic correctness for apnea event classification:

**What Changed:**
- **Renamed field**: `central_count` → `unclassified_count` in both `session_summaries` and `daily_summaries` tables
- **New field**: Added `clear_airway_count` to `session_summaries` table
- **Data preservation**: Existing `central_count` data copied to `unclassified_count` during migration
- **Backward compatibility**: Old `central_count` column retained but ignored by new code

**Why This Change:**
- **Semantic accuracy**: Most CPAP machines cannot distinguish between true Central Apneas and other unclassified events
- **Correct terminology**: "Unclassified Apnea" (UA) is more accurate than assuming all are "Central Apnea" (CA)
- **Clear Airway support**: Enables proper tracking of Clear Airway Apneas (CAA) which some advanced machines can detect

**Impact:**
- **AHI calculation unchanged**: Both CA and UA events count toward AHI
- **Better reporting**: Users see more accurate event classifications
- **Future-proofing**: Supports advanced machines that can differentiate event types

**Migration:**
- **Automatic**: Schema upgrade copies central_count → unclassified_count
- **No data loss**: All existing event counts preserved
- **UI updates**: Reports and statistics screens updated to use new terminology

---

## Schema v11 Highlights 📊 **NEW FEATURE - CSV EXPORT REPORTS**

The schema v11 changes introduced database-driven CSV export reports (later redesigned in v13):

**What Changed:**
- **New tables**: Added `reports` and `report_contents` tables
- **Macro-based queries**: SQL templates with runtime substitution (#PROFILE_ID, #START_DATE, #END_DATE)
- **Report varieties**: Each report supports multiple aggregation levels (Days, Weeks, Months)

**Note:** These tables were replaced by `report_tree` in v13. See Schema v13 Highlights.

---

## Schema v12 Highlights 🔧 **DENORMALIZATION AND CLEANUP**

**What Changed:**
- **Profile ID denormalization**: Added `profile_id` column to `session_settings`, `session_channels`, `session_summaries`, and `event_lists` for direct profile-level queries without joining through machines/sessions
- **Respiratory events enriched**: Added `profile_id` and `channel_id` to `respiratory_events`
- **Channels type**: Added `type INTEGER` column to `channels`
- **Sessions simplified**: Removed `events_file` and `summary_file` columns (legacy .001 file references no longer needed)
- **No-migration policy introduced**: Schema version mismatch now requires a fresh database and data reimport. Incremental migrations are no longer supported.

**Why Denormalization:**
- Enables efficient queries like "all sessions for profile X" without joining through machines
- Allows direct profile-level filtering on event_lists, session_summaries, and respiratory_events
- Significant query performance improvement for reporting screens

**Impact of No-Migration Policy:**
- Users upgrading from v11 or earlier must reimport their CPAP data
- Eliminates complex migration code and reduces maintenance burden
- Ensures data integrity — no partial or broken upgrade states

---

## Schema v13 Highlights 🌲 **REPORT TREE REDESIGN**

**What Changed:**
- **Replaced `reports`/`report_contents`** (v11) with a single self-referencing `report_tree` table
- **Hierarchical structure**: Root nodes → Folders → Report leaf nodes
- **System/User split**: `source` column distinguishes OSCAR-managed vs user-created nodes
- **External orf file**: System reports loaded from `system_reports.orf` on each startup (allows report updates without schema changes)

**Benefits:**
- **Flexibility**: Supports unlimited nesting depth (folders within folders)
- **Separation**: System and user reports clearly delineated
- **Maintainability**: System reports updated via `.orf` file, not schema migrations
- **Simpler schema**: One table instead of two for the same functionality

**Tree Initialization:**
- At first install: root nodes and system reports populated from `.orf` file
- At startup: system reports refreshed if OSCAR version changed; user reports untouched
- User-created reports: preserved across all OSCAR upgrades

---

## Schema v14 Highlights 🗄️ **FILE-TO-DB MIGRATION**

**What Changed:**
- **app_preferences table**: Replaces legacy `Preferences.xml`
  - Global application settings stored in database
  - data_type values: 'string', 'int', 'float', 'bool', 'datetime', 'date', 'time', 'blob'
  - Seeding: XML file imported on first launch after upgrade, then deleted

- **graph_layouts table**: Replaces `layoutSettings/*.shg` and per-profile `.shg` files
  - Unified storage for both named layouts (shared, cross-profile) and per-profile current layouts
  - Row discrimination by (profile_id IS NULL, is_current)
  - data: Raw QDataStream binary (magic 0x41756728, version 5+)
  - Seeding: Legacy files imported on first launch after upgrade, then deleted

- **profile_preferences enhancement**: Added `blob_value BLOB` column
  - Supports binary data in preferences (e.g., serialized objects)

**Benefits:**
- **Single source of truth**: All preferences in database instead of scattered files
- **Atomic operations**: Backup/restore includes all settings
- **Cross-profile sharing**: Named layouts can be used by all profiles
- **Simpler deployment**: No file path or permission issues

---

## Schema v15 Highlights 🧹 **CLEANUP**

**What Changed:**
- **Dropped dst_enabled column** from `user_info`
  - Stored but never read by application
  - Cleaned up in migration

- **Deleted orphaned DST rows** from `profile_preferences`
  - Legacy DST preference entries removed

---

## Schema v16 Highlights 🔧 **DAILY SUMMARIES — REMOVE PER-MACHINE DIMENSION**

**What Changed:**
- Dropped `machine_id` column, its FK to `machines`, and the
  `idx_daily_summaries_profile_machine` index from `daily_summaries`.
- Natural key changed from `UNIQUE(profile_id, date, machine_id)` to
  `UNIQUE(profile_id, date)`.
- `DailySummaryRepository` API methods that previously took an optional
  `machineId` parameter (`findByProfileAndDate`, `findRange`,
  `calculateAndStore`, `calculateAndStoreFromDay`, `exists`) now take only
  the profile/date arguments.
- Restore code's special-case for remapping `daily_summaries.machine_id`
  was removed; v15 backups still restore cleanly because the existing
  `validColumns` filter (built from `PRAGMA table_info`) silently drops
  the column.

**Why:**
A `daily_summaries` row is a profile-day rollup. The `Day` object it
mirrors already aggregates across all machines that contributed sessions
to that OSCAR day (one CPAP plus zero or more oximetry/auxiliary devices).
The `machine_id` column was a design mistake: every caller passed `0`
intending "combined", but the calculation code silently rewrote that to
the first enabled CPAP session's machine id, contradicting the original
"NULL = combined" documentation. The corresponding read-side helpers
(`findByProfileAndDate` with default args, `findRange`, `exists`) queried
`machine_id IS NULL` and so could never find the rows the writer stored.
None of those finders had any in-tree callers, which is why the mismatch
was invisible.

**Migration:**
`migrateV15ToV16` rebuilds the table (SQLite cannot drop a column inside
a UNIQUE constraint without a rebuild). Data is copied via
`INSERT OR REPLACE` ordered to prefer machine-bound rows over NULL rows
on the (profile_id, date) key. In current deployments every existing row
already maps cleanly to a unique (profile_id, date) tuple, so the dedup
is defensive only.
