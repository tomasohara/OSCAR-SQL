# Running Multiple OSCAR 2.0 Instances Simultaneously

## Overview

OSCAR 2.0 intentionally supports running multiple instances at the same time, each
pointing at a different database folder. This is an uncommon scenario in normal use
but is permitted (for example, a developer comparing two databases side-by-side).

Running OSCAR 1.7.1 and OSCAR 2.0 simultaneously is fully safe — they use different
QSettings registry paths (different application names) and completely different data
formats, so they do not interfere with each other at all.

## What Is Safe

- **SQLite databases** — each instance opens its own `oscar.db` file. No shared
  file, no locking conflict.
- **Log files** — each instance writes to `<its-datadir>/logs/`. Completely separate.
- **Per-database preferences** — all user-visible settings (profiles, graph layouts,
  chart settings, etc.) are stored in each database's `app_preferences` table and are
  fully isolated.

## Shared QSettings (Potential Issues)

All OSCAR 2.0 instances share the same Windows registry hive
(`HKCU\Software\<OrgName>\OSCAR 2.0`). Concurrent reads and writes to the keys
below can produce unpredictable results.

### Global keys (shared across all databases)

| Key | Written when | Risk with simultaneous instances |
|-----|-------------|----------------------------------|
| `Settings/AppData` | Startup, first run, folder selection | **Mitigated** — see note below. |
| `RecentDatabases` | Any database open / switch / delete | **Low** — read-modify-write; a concurrent update may lose a recent entry. |
| `Fingerprint` | On close | Low — last writer wins; cosmetic only. |
| `GFXEngineSetting` | Crash recovery, `--legacy` flag, Preferences dialog | Low — only changes on crash or deliberate user action. |
| `LangSetting` | Language selection dialog | Low — only changes when user actively changes language. |
| `ShowDatabaseMenu` | Preferences dialog | Low — only changes on deliberate user action. |
| `csv_reports_version` | CSV report generation | Low — one-shot migration stamp; idempotent. |
| `OpenGLCompatibilityCheck` | Before OpenGL test on Windows | Low — transient crash-recovery flag. |

### Per-database-folder keys (stored under `<folderName>\…`)

The following keys are stored under a registry subgroup named after the database
folder leaf name (e.g. `OSCAR20_data`), so each database instance has its own copy.
Two simultaneous instances do not interfere with each other for these keys.

| Key | Written when |
|-----|-------------|
| `<folder>\MainWindow\geometry` | On close |
| `<folder>\SavedPath\<name>` | File/folder picker dialogs |
| `<folder>\ImportProfile\LastPath` | Profile import dialog |

### `Settings/AppData` — Mitigated

`GetAppData()` caches the database path in memory (via `g_appDataPath`) on first
write during startup. All code that changes the active database path calls
`SetAppData()`, which updates both the in-memory value and the registry atomically.
Once an instance has completed startup, its `GetAppData()` returns the cached value
and is immune to another instance writing a different path to the registry.

The `RecentDatabases` list is the only remaining shared-state concern for
simultaneous instances.

## Same-Folder Protection (QLockFile)

Two instances opening the **same** database folder simultaneously would cause SQLite
write conflicts and profile state corruption. OSCAR prevents this with a `QLockFile`
at `<datadir>/oscar.lock`.

At startup, after the data folder is confirmed and writable, OSCAR calls
`QLockFile::tryLock()`. If the lock is already held by another process, OSCAR
displays a warning ("This OSCAR database folder is already open in another instance
of OSCAR") and exits immediately. The lock is released automatically when the
process exits.

The lock file has `setStaleLockTime(0)` — OSCAR never treats a lock as stale.
If a crash leaves a stale lock, the user must manually delete `oscar.lock` from the
data folder.

## Practical Guidance

In the normal database-switch flow (`File ▸ Database ▸ Recent / Open / New`) the
first OSCAR instance shuts itself down before the second one opens, so there is no
true simultaneous operation and none of the above races can occur.

The only scenario where QSettings races matter is when a user manually launches a
second OSCAR executable while the first is still running **with a different data
folder**. In that case the `RecentDatabases` list may lose an entry if both instances
modify it at the same moment (e.g. one opens a database while the other is deleting
one from the list). This does not cause data corruption in CPAP session data or
profile records.

Launching a second OSCAR instance pointed at the **same** folder is blocked by the
`QLockFile` mechanism described above.

## Migration Note

On first run after upgrading to a build that includes the per-folder key changes,
OSCAR will not find a remembered window position or last-used folder paths (they were
previously stored at the global level). Window position resets to the OS default once,
and folder pickers default to Documents. These are saved correctly on next close.
