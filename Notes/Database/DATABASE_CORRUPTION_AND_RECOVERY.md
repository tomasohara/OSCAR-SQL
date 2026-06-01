# OSCAR Database — Corruption Risks and Recovery Options

**Last Updated:** 2026 Q2
**Applies to:** Schema v17+ (single-file SQLite, WAL mode)

---

## Overview

SQLite is highly resistant to corruption under normal circumstances. Its WAL (Write-Ahead Logging)
mode and transaction model protect against most failure scenarios. However, certain conditions can
still leave a database in an unreadable or partially-corrupt state:

- Abrupt power loss during a write on hardware without write-back caching
- Abnormal termination, such as terminating the OSCAR process from Task Manager
- Storage media failure (bad sectors, failing SSD)
- Filesystem or OS bugs that truncate or reorder writes
- Disk-full conditions during a WAL write
- Manually copying `oscar.db` while OSCAR is running (WAL files out of sync)

When this happens, a user's entire OSCAR history is at risk — all profiles, sessions, waveforms, events, notes, and settings live in the single `oscar.db` file.

---

## OSCAR's Current Protections

OSCAR enables the following SQLite settings at every database open:

| Setting | Value | Effect |
|---------|-------|--------|
| `journal_mode` | `WAL` | Writes go to a separate WAL file first; readers never block writers |
| `synchronous` | `NORMAL` | WAL header is synced before each checkpoint; safe against OS crash, not power loss during checkpoint |
| `foreign_keys` | `ON` | Referential integrity enforced at write time |
| `PRAGMA optimize` | (on close) | Keeps query-planner statistics fresh |
| `wal_checkpoint(TRUNCATE)` | (on demand) | Merges WAL back into main file and truncates it |

**What this means in practice:** An OS crash (OSCAR killed, machine rebooted) is safe — WAL mode
recovers automatically on next open. A hard power loss during a checkpoint is the primary residual
risk with `synchronous = NORMAL`.

**Startup Check:** At startup, OSCAR checks whether the previous OSCAR session terminated normally. If not, see **Implemented: Dirty-Shutdown Integrity Check** below for how this condition is handled.

---

## Detecting Corruption

### Quick check (recommended first step)
```sql
PRAGMA quick_check;
```
Scans B-tree structure and free-list without verifying content checksums. Fast — suitable for
startup. Returns `ok` if clean. OSCAR runs a quick check automatically if the previous OSCAR session did not terminate normally. The user can also run a quick check from OSCAR Help/Troubleshooting/Check Database Integrity (although the menu item calls it an integrity check for clarity, it is a quick_check that is performed.)

### Full integrity check
```sql
PRAGMA integrity_check;
```
Checks every page, every B-tree pointer, and all table/index cross-references. Slower on large
databases but thorough. Returns `ok` if clean, or a list of error descriptions. 

### From the sqlite3 command-line tool (advanced users)
```
sqlite3 oscar.db "PRAGMA integrity_check"
```
Note: `sqlite3` is a separate command-line tool that must be downloaded and installed independently of OSCAR.

---

## Recovery Options

Listed in order of preference. Use the first option that applies.

---

### Option 1 — Restore from backup (Best Option)

If the user has backed up the OSCAR database using a system backup service, restoring oscar.db from a backup will recover the entire database including all profile data, sessions, events, and settings as of the backup date. Note that the backup must have been run with OSCAR closed, or the backup service must support Windows Volume Shadow Copy (VSS) so that open files are captured consistently.

Data lost: anything added, imported, or changed after the last backup was made. After restoring the database, import again your SD cards to include data newer than the last full backup.

**Recommendation for users:** Make a complete backup periodically using a system backup service. Make a profile backup too.

---

### Option 2 — Delete and Start Fresh, then Re-Import from SD Cards or Restore Profile Backups

If no system backup is available, delete `oscar.db` and let OSCAR create a new empty database,
then re-import from each CPAP device's SD card or restore profiles from individual profile backups (.oscar files that you have saved earlier). Restoring a profile followed by importing new SD card data will give you the most complete recovery.

Location of `oscar.db`: see `Notes/Database/OSCAR_Data Directory Contents.md`.

**What this recovers:** All session history, waveforms, and events for profiles whose SD cards are available.

**What this does NOT recover without a profile backup:**

- Daily notes entered in OSCAR
- Custom graph layouts
- Report configurations
- Doctor/user profile information

**Limitation:** Summary-only devices (e.g., AirSense 10 CPAP Basic) write only daily summaries
to the SD card. There are no waveforms or per-event records to re-import — those are permanently
lost if not in a system backup.

**Note:** In a database with multiple profiles, corruption may affect only some profiles and their
data. Since it is generally not possible to determine which profiles are damaged, starting fresh
and re-importing is cleaner than attempting partial recovery.

---

### Option 3 — SQLite `.recover` (Advanced Users Only)

SQLite 3.29+ (released 2019; bundled with Qt 6) includes a `.recover` command in the `sqlite3`
CLI tool. It reads every page it can access, skipping damaged pages, and reconstructs as much
data as possible into a new database file.

```cmd
:: Step 1 — copy the damaged file so oscar.db is not touched
copy oscar.db oscar_damaged.db

:: Step 2 — recover into a new file
sqlite3 oscar_damaged.db ".recover" | sqlite3 oscar_recovered.db

:: Step 3 — verify the recovered database
sqlite3 oscar_recovered.db "PRAGMA integrity_check"
```

This is a best-effort operation. Rows on damaged pages are lost; rows on intact pages are
preserved. The resulting database may pass `integrity_check` even if some rows are missing.

After recovery, rename `oscar_recovered.db` to `oscar.db` and restart OSCAR. OSCAR will
detect the schema version and resume normally. Note that this method does not report what
data was recovered, so it is not possible to know in advance which profiles or records were
on damaged pages and are missing from the result.

---

### Option 4 — `.dump` Fallback (Advanced Users Only)

If `.recover` is unavailable or fails, `.dump` reads the database through the SQL layer and
emits INSERT statements for every row it can reach.

```cmd
sqlite3 oscar_damaged.db .dump > oscar_dump.sql
sqlite3 oscar_recovered.db < oscar_dump.sql
```

`.dump` stops at the first page it cannot read, so it may recover less data than `.recover` on
heavily-damaged files. As with `.recover`, there is no report of what was and was not recovered.

---


## WAL and SHM File Considerations

SQLite WAL mode produces two companion files alongside `oscar.db`:

| File | Purpose |
|------|---------|
| `oscar.db-wal` | Write-Ahead Log — uncommitted or un-checkpointed writes |
| `oscar.db-shm` | Shared-memory index for WAL readers |

**Always treat these three files as a unit.** Copying only `oscar.db` while OSCAR is running
produces a logically inconsistent snapshot. The `-wal` and `-shm` files must be copied at the
same instant, or the copy must be taken with OSCAR closed.

When OSCAR closes cleanly, it checkpoints and truncates the WAL, so `-wal` and `-shm` are
typically empty or absent after a normal shutdown.

---

## Developer Reference

The sections below are for OSCAR developers, not end users.

---

## Implemented: Dirty-Shutdown Integrity Check

Running `PRAGMA quick_check` on every startup is impractical. A typical OSCAR database (500 MB – 2 GB
of waveform BLOBs) requires reading every page, which would add 1–30 seconds to startup depending on
hardware — unacceptable as a blocking operation.

The solution is a **dirty-shutdown flag** in QSettings:

1. Before `DatabaseManager::initialize()`, OSCAR writes `"db/CleanShutdown" = false` to QSettings
   and calls `settings.sync()` to ensure it reaches disk.
2. After `DatabaseManager::close()` returns on a clean exit, OSCAR writes `"db/CleanShutdown" = true`.
3. On the next startup, if the flag is still `false`, the previous session crashed.
   OSCAR runs `PRAGMA quick_check` then, and only then.
4. If `quick_check` finds problems, a dialog explains the situation and offers the user the choice
   to continue anyway or exit.
5. If `quick_check` passes (database is intact despite the crash), OSCAR proceeds silently.

**Normal-startup cost:** one QSettings read and one QSettings write — negligible.
**Crash-triggered cost:** one full database scan, paid only when there is genuine reason to suspect damage.

This approach mirrors what browsers (Chrome, Firefox) and SQLite's own hot-journal detection do.

**Key references:**
- `main.cpp`: flag read/write bracketing `DatabaseManager::instance().initialize()` and `close()`
- `DatabaseManager::checkIntegrity()` in `oscar/database/database_manager.cpp`: runs `PRAGMA quick_check`
- QSettings key: `"db/CleanShutdown"` (default `true` on first run)

### Pre-VACUUM integrity check (also implemented)

`VACUUM` (`Compress Database` menu item, `mainwindow.cpp::on_actionCompress_Database_triggered()`)
rewrites the entire database into a fresh copy. Running VACUUM on a corrupt database risks propagating
or obscuring the damage, so OSCAR now runs `PRAGMA quick_check` **before** VACUUM begins. If the check
fails, the operation is aborted and the user is directed to restore from backup.
