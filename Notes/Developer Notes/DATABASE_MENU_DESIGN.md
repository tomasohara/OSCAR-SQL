# File / Database Menu — Design

**Status:** Design v2 (refined after first review)
**Date:** 2026-05-01

This document specifies the addition of a `File ▸ Database` submenu
(New / Open / Recent / Delete) intended primarily for support staff and
developers, not end users on their own treatment data.

The first-pass design (v1, with reviewer comments) is preserved in
`DATABASE_MENU_DESIGN_v1.md` for reference.

---

## Decisions made

1. **Switch mechanism:** Phase 1 uses an application restart, the same path
   that `--datadir` and `Change Language` already use. In-process switching
   is deferred to a possible Phase 2.
2. **Folder model — folder-as-database, no fixed root:** every database is
   identified by the absolute path to a folder that contains `oscar.db` plus
   its companions. No "DatabasesRoot" parent concept. Multiple databases may
   live wherever the user wants them — same drive, different drives, NAS,
   removable media — and OSCAR remembers them via a Recent list.
3. **No folder renaming:** existing data already lives in folder-as-database
   form; nothing needs to move on first run. The current `GetAppData()` is
   simply added to the Recent list.
4. **Menu placement:** `File ▸ Database ▸ New / Open / Recent / Delete`.
   Always visible. Delete is well-guarded.
5. **Command-line:** reuse the existing `--datadir <path>` flag for restart-
   to-target-database. No new flag.
6. **Orphan cleanup:** the hidden, unused `actionChange_Data_Folder`
   (`mainwindow.cpp:317`) and its handler (`mainwindow.cpp:2629`) are removed
   as part of this work. The new `Open` action subsumes its purpose.
7. **QSettings additions only — no key migration:**
   - `Settings/ActiveDatabase` — absolute path to the active database folder
   - `RecentDatabases/...` — list of absolute folder paths (cap 10). The
     displayed label is the leaf folder name, computed at display time.
   - The legacy `Settings/AppData` key is kept; it tracks the active path so
     `GetAppData()` and `--datadir` continue to work unchanged.

---

## Background facts (verified against the current code)

- `GetAppData()` (`oscar/SleepLib/preferences.cpp:70`) reads QSettings
  `Settings/AppData`. There is one active OSCAR data root per process.
- `DatabaseManager::initialize(GetAppData() + "/oscar.db")` is called in
  `oscar/main.cpp:825`, **before** `p_pref` is constructed
  (`main.cpp:848`).
- `--datadir <path>` already exists (`main.cpp:634`) and writes the chosen
  path into `Settings/AppData` directly. The folder-as-database model is
  effectively this flag's behaviour, exposed through the UI.
- `GetLogDir() = GetAppData() + "/logs"`. The debug log file is opened once
  in `LogThread::logToFile()` (`logger.cpp:95`).
- `RestartApplication(...)` (`mainwindow.cpp:2008`) is the existing
  save → close → `QProcess::startDetached(self, args)` → exit pattern.
- The QSettings/registry application name is `"OSCAR 2.0"` (`main.cpp:531`)
  and is **not** scoped per-database. It will stay that way.
- `actionChange_Data_Folder` is orphan: hidden in `mainwindow.cpp:317`, with
  a still-present but unreachable handler at `mainwindow.cpp:2629`.

### What actually lives under `GetAppData()`

In OSCAR 2.0 the active data folder contains:

- `oscar.db` (+ its `-shm` / `-wal` siblings while open)
- `Profiles/<username>/...`, including `Profiles/<machine>/Backup/` which
  holds the SD-card backups (the "SD data" mentioned in earlier discussion)
- `logs/` (debug log + `connections/devices.xml`)
- screenshots and any backup files written by the Backup feature

`Preferences.xml` and `layoutSettings/` no longer exist as files —
preferences moved to the `app_preferences` table and layouts to their own
tables in schema v14. Nothing else needs to be enumerated for migration.

---

## Design

### Identity

A database is identified by the **absolute path to its folder**. Wherever
a label is shown (Recent menu, Delete confirmation), it is computed at
display time as `QFileInfo(path).fileName()` — the leaf folder name. No
separate "display name" field is stored.

The title bar is unchanged: it shows the open profile name when a profile
is open, otherwise just `OSCAR` and version, matching current behaviour.

### Menu (in `mainwindow.ui`)

Add under the existing `File` menu, positioned where `Change Data Folder`
currently sits:

```
File ▸ Database ▸ New...
                  Open...
                  Recent ▸ <up to 10 entries, dead entries pruned lazily>
                  ----
                  Delete...
```

### New flow

1. OS folder-picker scoped to "select or create an empty folder".
2. Validate: folder is empty, writable, and not already known to OSCAR.
3. Save current state → `CloseProfile()` → `p_pref->Save()`.
4. Update `Settings/ActiveDatabase` to the new path. Append to
   `RecentDatabases`.
5. `RestartApplication(false, "--datadir", "<path>")` — the new instance
   creates `oscar.db` + scaffolding via the normal startup path.

### Open flow

1. OS folder-picker.
2. Save → set active → `RestartApplication(false, "--datadir", "<path>")`.
   - If the folder already contains `oscar.db`, the new instance opens it.
   - If the folder is empty (or has no `oscar.db`), the new instance
     creates one — same path as New, just without a prior dialog. OSCAR's
     existing startup logic handles this when `--datadir` points at an
     empty/new folder.
3. Update Recent.

New and Open are nearly the same operation. The distinction is that New's
folder-picker is constrained to empty folders, while Open accepts either
an existing OSCAR folder or an empty one. We could collapse them into a
single action, but they read more clearly as separate menu items.

### Recent

A `QAction` per recent DB, populated by `populateRecentDatabasesMenu()`,
called from `updateMenu()` and after every switch. Click → save → set
active → restart. The Recent list is stored in QSettings, capped at 10,
and pruned lazily when entries are displayed (drop entries whose folder no
longer exists or no longer contains `oscar.db`).

### Delete flow

1. Dialog enumerates Recent entries; the active one is disabled.
2. Per entry: show path, on-disk size, profile count.
3. On confirm: type the leaf folder name as an extra check, then delete
   the **entire folder** (no partial deletes).
4. Refuse to delete if a `try-open exclusive` on the target's `oscar.db`
   fails — covers "another OSCAR instance has it open."
5. Remove the entry from Recent.
6. No restart needed (active database unchanged).

---

## Implementation plan (Phase 1, restart-based)

**Step 1 — first-run seeding (replaces the old "folder migration" step)**

- On first launch with the new build, if `RecentDatabases` is empty and
  `GetAppData()` resolves to a folder that contains `oscar.db`, add that
  folder's path to Recent.
- No file movement, no folder renaming. The existing layout already
  satisfies the folder-as-database model.

**Step 2 — wire active-DB into startup**

- The existing `--datadir <path>` parser already does the right thing.
  Confirm it runs before `DatabaseManager::initialize`. (It does — the arg
  loop is at `main.cpp:632`-ish, before line 825.)
- On startup, if `Settings/ActiveDatabase` is present and `--datadir` was
  not given, copy `ActiveDatabase` into `Settings/AppData` so `GetAppData()`
  returns the right path. This is what keeps "remember which DB was last
  open" working across launches.

**Step 3 — UI**

- Edit `mainwindow.ui`: add the `Database` submenu under `File` and the
  four actions. Remove `actionChange_Data_Folder` from the UI.
- Implement a single management dialog with three modes (New, Open as
  fallback only — usually we'll go straight to the OS folder-picker for
  Open, and Delete). Proposed: `oscar/database/database_management_dialog.{h,cpp}`.
- Add slots in `mainwindow.{h,cpp}`:
  - `on_actionDatabaseNew_triggered()`
  - `on_actionDatabaseOpen_triggered()`
  - `on_actionDatabaseDelete_triggered()`
  - `populateRecentDatabasesMenu()` (called from `updateMenu()` and on
    construction)
  - `switchToDatabase(path)` — common path: save → write QSettings →
    `RestartApplication(false, "--datadir", path)`.
- Remove the orphan `on_actionChange_Data_Folder_triggered()` handler
  (`mainwindow.cpp:2629`) and the visibility-hiding line at
  `mainwindow.cpp:317`.

**Step 4 — Recent list helpers**

- Add a small `RecentDatabases` helper (free functions or a thin static
  class) somewhere in `oscar/database/` with:
  - `add(const QString& path)`
  - `entries()` — returns `QStringList`, prunes dead entries
  - `remove(const QString& path)`
  - `setActive(const QString& path)` — also writes `Settings/AppData`
- All persistence via `QSettings`.

**Step 5 — Delete safety**

- Refuse if target path equals `Settings/ActiveDatabase`.
- Refuse if `try-open exclusive` on `<target>/oscar.db` fails.
- Confirm dialog: lists exact paths to be removed, requires the user to
  type the leaf folder name, single "Delete everything" button.
- Use `QDir::removeRecursively()` and surface errors clearly if some files
  remain locked.

**Step 6 — orphan cleanup**

- Remove `actionChange_Data_Folder` from `mainwindow.ui`.
- Remove the visibility-hiding line at `mainwindow.cpp:317`.
- Remove `on_actionChange_Data_Folder_triggered()` (`mainwindow.cpp:2629`)
  and its declaration in `mainwindow.h`.

**Step 7 — release notes & bug log**

- Add a brief entry to user-facing release notes describing the new
  Database submenu.
- Add to `Notes/BUG_FIXES.md` only if any preexisting bugs surface during
  the work.

**Step 8 — schema**

None. The DB schema does not change.

### Files to touch

- `oscar/SleepLib/preferences.cpp` / `.h` — minor: ensure `GetAppData()`
  honours `Settings/ActiveDatabase` if present (or alternatively, write
  `Settings/AppData` from the active path during startup).
- `oscar/main.cpp` — first-run Recent seeding; copy ActiveDatabase →
  AppData when no `--datadir` given.
- `oscar/mainwindow.ui` / `.h` / `.cpp` — menu additions, slot
  implementations, orphan removal, title-bar update.
- `oscar/database/database_management_dialog.{h,cpp}` — new files; add to
  `oscar/oscar.pro`.
- `oscar/database/recent_databases.{h,cpp}` — new files (or merge into
  `database_manager`); add to `oscar/oscar.pro`.
- `oscar/database/database_manager.cpp` — log the active DB path on
  initialize.
- `Notes/BUG_FIXES.md` — only if needed.

---

## Phase 2 (deferred — only if restart proves annoying)

In-process switching would need an explicit `Close()` path on every
process-lifetime singleton:

- `DatabaseManager::close()` — exists.
- Logger — needs a new `relocateLog(newDir)` so the debug log can be
  closed and reopened at the new active path.
- `p_pref` / `AppSetting` — full reload from the new database's
  `app_preferences` table.
- `p_profile` — reload via `Profiles::Scan()` then rebuild
  `profileSelector`.
- Daily / Overview / Welcome views — destroy and recreate.
- MachineLoader caches — registry reset.
- `DeviceConnectionManager` — close `connections/devices.xml`, reopen at
  the new path.

Estimate: 2–3 weeks of careful work and a thorough crash-test pass — much
more than Phase 1.

---

## Remaining open items

None. Anything else under `GetAppData()` is handled implicitly by the
restart — the new instance starts cold against the new active path, so
no per-subsystem audit is needed for Phase 1. Surprises will surface
during implementation and testing.
