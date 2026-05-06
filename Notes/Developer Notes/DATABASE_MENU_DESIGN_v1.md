See ==> for my comments

# File / Database Menu — Design Discussion

**Status:** Draft for review
**Date:** 2026-05-01

This document captures the design conversation around adding a `File ▸ Database`
submenu (New / Open / Recent / Delete) intended primarily for support staff and
developers, not end users on their own treatment data.

---

## What I found in the current code

- `GetAppData()` (`oscar/SleepLib/preferences.cpp:70`) reads a single QSettings
  key `Settings/AppData`. There is one OSCAR data root per install.
  
- `DatabaseManager::initialize(GetAppData() + "/oscar.db")` is called in
  `oscar/main.cpp:825`, before `p_pref` is constructed (`main.cpp:848`). Most of
  OSCAR is built around the assumption that this stays valid for the life of
  the process.
  
- `GetLogDir() = GetAppData() + "/logs"` and the debug log file is opened once
  in `LogThread::logToFile()` (`logger.cpp:95`). There is `shutdownLogger()`
  but no "rotate to a new location" path.
  
- Preferences are loaded from `GetAppData() + "/Preferences.xml"` and
  `Profiles` is `GetAppData() + "/Profiles"`. `layoutSettings/`,
  `connections/`, etc. are also under `GetAppData()`.
  
- `RestartApplication(...)` (`mainwindow.cpp:2008`) is what `Change Data Folder`
  and `Change Language` already use — `CloseProfile()` → save prefs →
  `QProcess::startDetached(self, args)` → exit.
  
  ==> Change Data Folder is orphan code -- nothing triggers it. I don't know why it was orphaned.
  
- The QSettings/registry application name is `"OSCAR 2.0"` (`main.cpp:531`).
  It is not scoped per-database today.

---

## Comments on the proposal

### 1. Push back on the "no restart" goal — at least for v1

OSCAR has a lot of process-lifetime singletons holding open handles to the
active database and profile (`DatabaseManager`, `p_pref`, `p_profile`,
`AppSetting`, the logger `LogThread`, `profileSelector`, daily/overview/welcome
views, MachineLoader caches, `DeviceConnectionManager`'s `devices.xml`, the
WAL/SHM files, layout caches, scheduled timers). The existing
`Change Data Folder` deliberately restarts because of exactly this. Trying to
do it without restart is doable but it is a long bug tail of "why is X still
showing the old DB's value." I would ship Phase 1 as restart-based (matches
`Change Data Folder`), and only build the in-process tear-down/rebuild later if
you decide it is worth it. For your stated audience (support + devs), a
2-second restart is fine.

==> ok on requiring restart in first version

### 2. Push back on renaming `Profiles` → `tribble-profiles`

That works but it forces a one-time, irreversible rename on every existing
installation when they first run the new build. Anti-virus, OneDrive,
permissions, or a stale lock on a single subfolder turns into a silent data
loss risk. 

==> I am not concerned about these issues as OSCAR 2.0 is currently used only by beta testers. We hope to release it around the end of May.

Cleaner alternative: each database lives in its **own subfolder**,
where the subfolder *is* the database name. The existing `<AppData>` becomes
the parent that contains one or more such subfolders.

```
<DatabasesRoot>/
   default/                   ← legacy data lives here after one-time move
      oscar.db
      Preferences.xml
      Profiles/<username>/...
      logs/
      layoutSettings/
      sddata/  (etc.)
   tribble/
      oscar.db
      Preferences.xml
      Profiles/...
      logs/
      ...
```

Why this is better:

- No naming collisions inside one folder, so nothing has to change about how
  `Profiles`, `logs`, `Preferences.xml`, `layoutSettings/`, `connections/`,
  etc. are laid out *underneath* a database root.
- `GetAppData()` keeps returning a single folder; it just points at the active
  DB's subfolder. The vast majority of code keeps compiling and working with
  **zero** changes.
- One-time migration is cheap and safe: if `<AppData>/oscar.db` exists at the
  legacy location, move it (and the companion folders) into
  `<AppData>/default/` and rewrite the QSettings key. If the move fails
  partway, you can detect it and roll back.

==> Agreed this is safer from program view. My thought was that developers and support people might want to put different sets of data received from different users in different locations. For example, I'll keep my personal data on my SSD but might well want to put data I have received for investigation on a NAS where space is cheaper.

### 3. Don't put the file dialog at an arbitrary OS location

Letting "New" open a generic OS Save dialog means a user can put `tribble.db`
in `D:\Random\` and now `tribble-profiles` and `tribble-logs` (or the
subfolder, in my model) end up there too — possibly across drives, behind
permission walls, or on removable media that is not always mounted. Constrain
New/Open to a single managed `DatabasesRoot` directory, plus a separate
"Change Databases Root..." action for users who really want to move the whole
set. Much smaller surface to test.

==> understand easier testing but see my previous comment.

### 4. Registry scoping — don't blanket-prefix

Splitting QSettings keys by DB name is a footgun: things like graphics engine
(`GFXEngineSetting`), `OpenGLCompatibilityCheck`, theme, language, and
`Settings/AppData` itself are genuinely machine-global. Per-database state
already mostly lives in `Preferences.xml` or in the DB itself, which moves
with the subfolder. The minimal addition I would make:

- `Settings/DatabasesRoot` (parent dir)
- `Settings/ActiveDatabase` (subfolder name)
- `RecentDatabases/0..9` (list of `{root, name}` pairs)

That is it. Don't migrate existing keys.

==> ok. There may be some data in the registry that could better be in the preferences table (there is no preferences.xml file any more -- all preference data is in the database).

### 5. Delete: agree, disallow on the active DB

Plus: refuse to delete if `oscar.db-shm` / `-wal` exist *and* a
"try-open-exclusive" fails — that catches "another OSCAR instance has it
open." Confirmation dialog should explicitly list what will be deleted (DB
file + Profiles + logs + layoutSettings + sddata) with a "yes, delete
everything" checkbox. No partial deletes — too easy to leave inconsistent
state.

==> Agreed, no partial deletes. fyi: layoutsettings is now in database.

### 6. Things to settle before coding

- What happens to `actionChange_Data_Folder`? With the new model that becomes
  "Change Databases Root" (move *all* of them). Keep as a separate action, or
  fold into Database/Settings? My vote: rename, keep separate.
  
  ==> actionChange_Data_Folder is unused orphan code.
  
- Backup files in `database/backup/` are produced from the active DB.
  Switching DB doesn't break that, but a "Backup This Database" vs "Backup
  Active Database" wording question shows up. Probably fine to leave alone.
  
  ==> Oh, and I forgot that default screenshots may go there too.
  
- Auto-import / scan timers — anything periodic that touches the DB needs to
  pause during switch. With restart-based switching, this is free.
  
  ==> ok on restart for v1.

---

## Proposed design

### Menu (in `mainwindow.ui`)

Add a `Database` submenu under `File`, before `Change Data Folder`:

```
File ▸ Database ▸ New...
                  Open...
                  Recent ▸ <up to 10 entries>
                  ----
                  Delete...
```

Always-visible — hidden settings annoy support people and developers, and
Delete is well-guarded.

==> looks good and what I had in mind.

### Identity

Active DB identified by `(databasesRoot, dbName)`. Display string: `dbName` in
the title bar, full path in tooltip.

### New flow

1. Modal dialog: name (validated against existing subfolder names + filesystem
   reserved chars), optional "create in different root" button.

2. Save current state → close DB/profile.

3. Create `<root>/<name>/` and minimal scaffolding (empty `Profiles` dir,
   `logs` dir).

4. Update QSettings active key. Append to Recent.

5. `RestartApplication("--database", "<name>")`.

   ==> currently have "--datadir <path to database>"

### Open flow

Same as New but pick from existing subfolders within DatabasesRoot. (No need
to navigate to `.db` file — the subfolder name is the identity.)

### Recent

A `QAction` per recent DB, click → save → set active → restart. Manage list
with QSettings; cap at 10; remove dead entries lazily on display.

==> good, especially removal of dead entries. (I have written code to do that decades ago and am still surprised that it isn't standard behavior)

### Delete flow

Dialog enumerates subfolders in DatabasesRoot, excludes the active one, shows
size / profile-count. On confirm, delete the entire subfolder. No restart
needed (active DB unchanged).

### Command-line

Add `--database <name>` (or shorter `--db`). On startup, if present, override
the QSettings active value before computing `GetAppData()`.

==> essentially exists now with --datadir

---

## Implementation plan (Phase 1, restart-based)

**Step 1 — folder model + migration (the load-bearing change)**

- Add a "databases root" abstraction in `preferences.cpp`. `GetAppData()`
  becomes `GetDatabasesRoot() + "/" + GetActiveDatabaseName()`. Both new
  helpers read QSettings with sensible defaults.
- Add a one-time migration in `main.cpp` (early, before
  `DatabaseManager::initialize`): if `<AppData>/oscar.db` exists *and* no
  subfolders look like database subfolders, move `oscar.db` +
  `Preferences.xml` + `Profiles` + `logs` + `layoutSettings` + `connections` +
  `sddata` into `<AppData>/default/`, then rewrite QSettings to point at the
  new layout. Atomic-as-possible: do it as a series of renames within the
  same volume; fail loud if any single rename fails (do not continue with
  half a move).
- Test by launching against a copy of an existing user's data dir.

**Step 2 — wire active-DB into startup**

- Parse `--database <name>` in `main.cpp` (alongside the existing arg loop
  near line 557 / 575). If present, set QSettings `Settings/ActiveDatabase`
  before anything else uses `GetAppData()`.
- Verify all the early callers (`migrateFromOSCAR`, `Profiles::Scan`, logger,
  `DeviceConnectionManager`) come out the right place.

**Step 3 — UI**

- Edit `mainwindow.ui` to add the `Database` submenu and the four actions.
- Implement a `DatabaseDialog` class (one dialog with three modes:
  New / Open / Delete). Lives in `oscar/database/` — proposed name
  `database_management_dialog.{h,cpp}`. Validation, listing, confirmation
  all here.
- Add slots in `mainwindow.{h,cpp}`:
  - `on_actionDatabaseNew_triggered()`
  - `on_actionDatabaseOpen_triggered()`
  - `on_actionDatabaseDelete_triggered()`
  - `populateRecentDatabasesMenu()` (called from `updateMenu` / startup)
  - `switchToDatabase(rootDir, name)` — the common path: save → set QSettings
    → `RestartApplication("--database", name)`.

**Step 4 — recent list**

- Add `RecentDatabases::add(root, name)` / `entries()` / `prune()` helpers
  (small static class or free functions in `database_manager.cpp`).
- Populate dynamically in `updateMenu()`.

**Step 5 — Delete safety**

- Refuse if `name == GetActiveDatabaseName()`.
- Refuse if a `try-open exclusive` on `oscar.db` fails (covers "other
  instance has it open").
- Confirm dialog showing exact paths to be removed and a typed `<name>`
  confirmation for extra safety on Delete (matches industry convention).

**Step 6 — log / `devices.xml` on the active DB only**

Already automatic: `GetLogDir()` flows through `GetAppData()`. Nothing to
change.

**Step 7 — release notes & bug log discipline**

- Document the one-time migration in user-facing release notes.
- Add an entry in `Notes/BUG_FIXES.md` only if any preexisting bugs surface
  during the work.

**Step 8 — schema**

None. The DB schema does not change. No migration needed inside the DB.

### Files I would touch (Phase 1)

- `oscar/SleepLib/preferences.cpp` / `.h` — `GetAppData()`, new helpers
- `oscar/main.cpp` — arg parsing, one-time folder migration,
  `DatabaseManager::initialize` path
- `oscar/mainwindow.ui` / `mainwindow.h` / `mainwindow.cpp` — menu, slots,
  recent list
- `oscar/database/database_management_dialog.{h,cpp}` — new files (add to
  `oscar.pro`)
- `oscar/database/database_manager.cpp` — minor: log the active-DB name on
  initialize
- `Notes/BUG_FIXES.md` — only if needed

I would skip the in-process switch for v1 and revisit only if
`RestartApplication` proves annoying in real support use.

### Phase 2 (only if you decide to pursue it)

In-process switch would need a documented `Close()` path on every singleton
(`DatabaseManager.close` exists; logger needs `relocateLog()`; prefs need full
reload; profile views need full destroy/recreate; loaders need a registry
reset; `AppSetting` needs reload). Estimate: 2–3 weeks of careful work and a
thorough crash-test pass — much more than Phase 1.

---

## Open questions

1. Folder model: subfolder-per-database (my Alternative A) or your
   name-prefix model? I have a strong preference for A but it is your call.

   ==> I am very uncertain about your alternative because of how I expect these features will be used. I like the idea of having multiple "database roots" even less. But I will think more about this.

2. Restart-based switch in v1, with no-restart deferred?

   ==> ok

3. Should `Database` be a submenu under `File` or a top-level menu? I would
   put it under `File` next to `Change Data Folder`.

   ==> Agreed a submenu under File. Change Data Folder does not exist in menu system and would logically be the same as Open.

4. Always-visible or gated behind an "advanced" preference? I would say
   always-visible.

   ==> Agreed

5. Any data outside `GetAppData()` that I am forgetting? (I checked
   `main.cpp`, `mainwindow.cpp`, `preferences.cpp`, `logger.cpp` — nothing
   else jumped out, but you would know about anything custom.)

   ==> I will give this more thought.

Once those are settled I can produce a more detailed step-by-step diff plan.
