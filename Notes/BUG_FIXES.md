# OSCAR Bug Fix Log

Notable bugs found and fixed during development/investigation.

---

## 2026-04-30 - Overview graph heights reset when File/Preferences OK clicked (#108)

**File:** `oscar/overview.cpp` — `Overview::RebuildGraphs()`

**Symptom:** After the user manually adjusts graph heights on the Overview page, opening
File/Preferences and clicking OK (even with no changes) resets all heights to the default.

**Root cause:** `mainwindow.cpp` unconditionally calls `overview->RebuildGraphs(true)` after
the Preferences dialog closes. Inside `RebuildGraphs`, `GraphView->LoadSettings("Overview")`
restored the correct heights, but the `if (reset)` block that follows immediately called
`GraphView->resetLayout()` (twice), overwriting every graph height with the global default.

**Fix:** Moved `LoadSettings("Overview")` and `settingsLoaded = true` to after the
`if (reset)` block, so saved heights are applied last and are not clobbered by `resetLayout()`.

---

## 2026-04-30 - Overview pressure graph scaled to setting instead of observed pressure (#104)

**File:** `oscar/Graphs/gPressureChart.cpp` / `gPressureChart.h`

**Symptom:** On the Overview page, the pressure bar chart's top bar and Y-axis maximum
were always driven by the machine's IPAPHi (or equivalent) pressure setting ceiling,
not by the pressure actually observed during therapy. For bilevel auto machines the top
bar showed the setting even when observed peak pressure was meaningfully lower.

**Root cause:** `populate()` called `addSlice(CPAP_IPAPHi)` for all auto/variable bilevel
modes, which reads `day->settings_max(CPAP_IPAPHi)` — the setting, not observed data.

**Fix:** Added `addObservedIPAPMax()` which reads `Day::Max(CPAP_Pressure)` (peak of the
combined pressure waveform stored in the session summary), falling back to
`Day::Max(CPAP_IPAP)` then to the setting if neither has data. All five
`addSlice(CPAP_IPAPHi)` calls in `populate()` replaced with `addObservedIPAPMax()`.
Added `Maxy()` override returning `m_maxy + 1` for 1 cmH2O headroom above the bars.

---

## 2026-04-29 - Extend line cursor through Event Flags graph (#102)

**File:** `oscar/Graphs/gFlagsLine.cpp` (`gFlagsGroup::paint`)

**Symptom:** The green vertical line cursor on the Daily page did not extend through the
Event Flags graph, breaking visual continuity.

**Root cause:** `gFlagsGroup::paint()` never implemented the cursor line drawing that
`gLineChart` and `gOverviewGraph` both have. No architectural barrier — simply an omission.

**Fix:** Added the standard 10-line cursor block (same pattern as `gLineChart::paint`)
after the outline rect is drawn, so the green line renders on top of the graph border.

---

## 2026-04-29 - Show calendar day in italic if it has a bookmark (#99)

**File:** `oscar/daily.cpp` (`Daily::UpdateCalendarDay`)

**Feature:** Calendar days that have at least one bookmark are now displayed in italic,
consistent with the existing visual encoding (bold = journal data, underline = position
data, colour = CPAP/oximeter presence).

**Implementation:** Retained the `Day*` pointer from `FindDay(date, MT_JOURNAL)` and
added a `hasbookmarks` check using `settingExists(Bookmark_Start)` plus a non-empty
list test on the journal session. `setFontItalic(true)` is applied to the
`QTextCharFormat` when the check passes.

---

## 2026-04-29 - Welcome page hides Oximetry block when oxi data comes from CPAP

**File:** `oscar/welcome.cpp` (`Welcome::GenerateOxiHTML`)

**Symptom:** The Welcome page's oximetry info block (icon, frame, "Most recent
Oximetry data...") was hidden for users whose CPAP machine has built-in oximetry,
because no dedicated `MT_OXIMETER` machine exists.

**Root cause:** `GetMachines(MT_OXIMETER)` returned an empty list, so
`haveoximeterdata` stayed false and the block was hidden. `LastDay(MT_OXIMETER)`
likewise returned an invalid date for the report link.

**Fix:** When no dedicated oximeter machine has data, fall back to checking
`channelAvailable("SPO2"|"Pulse")`. If oxi channels are present, set
`haveoximeterdata = true` with `oxiSourceType = MT_CPAP`. For the displayed date,
walk back from the last CPAP day to find one with SPO2/Pulse data; otherwise use
`LastDay(MT_CPAP)`. Mirrors the fix to `Statistics::GenerateCPAPUsage`.

---

## 2026-04-29 - Statistics page hides Oximeter section when oxi data comes from CPAP

**Files:** `oscar/statistics.h`, `oscar/statistics.cpp`
(`StatisticsRow::value`, `Statistics::GenerateCPAPUsage`)

**Symptom:** The "Oximeter Statistics" block on the Statistics page was not shown for
users whose CPAP machine has built-in oximetry (oxi channels stored in MT_CPAP
sessions rather than a dedicated MT_OXIMETER machine).

**Root cause:** The section-skip guard called `countDays(MT_OXIMETER, ...)` which
returns 0 when there is no dedicated oximeter machine, so `skipsection = true` hid
the entire block. All downstream `calcWavg/calcMin/etc.(channel, MT_OXIMETER, ...)`
calls also returned zero/null because `GetGoodDay(date, MT_OXIMETER)` cannot find
days whose only sessions are of type MT_CPAP.

**Fix:** Before the row-rendering loop in `GenerateCPAPUsage`, detect which machine
type actually holds oximetry data: `MT_OXIMETER` if a dedicated oximeter has data in
the report range, `MT_CPAP` if the SPO2/Pulse channels are available in CPAP sessions
but no dedicated oximeter data exists. This `oxiSourceType` is then substituted for
`MT_OXIMETER` in the `SC_HEADING` day-count check, the `SC_DAYS_HEADER` data lookups,
and passed as a new `typeOverride` parameter to `StatisticsRow::value()`, which uses
it instead of the row's nominal `type` for all Profile-level calc calls.

---

## 2026-04-29 - Viatom import silently fails after 1.7.1 migration (#97)

**Files:** `oscar/SleepLib/profiles.cpp` (`Profile::CreateMachine`),
`oscar/SleepLib/machine.cpp` (`Machine::SaveToDatabase`)

**Symptom:** After migrating a profile from OSCAR 1.7.1, importing Viatom oximeter
data appeared to succeed ("Imported 5 sessions") but all sessions were silently
discarded. All oximeter plots on the Daily page showed as disabled. Debug log
contained `UNIQUE constraint failed: machines.profile_id, machines.machine_id`
followed by "Machine not in database yet, skipping database storage" for every session.

**Root cause:** The 1.7.1 migration stored the Viatom machine with `serial_number=""`
in the DB (machines.xml had no serial element). `loadMachinesFromDatabase()` indexed
it in `MachineList["Viatom"][""]`. The Viatom loader called `CreateMachine()` with
`serial="25C2303495"` — not found under the empty key — so a new `Machine` object was
created with the same `m_id` but `m_database_id=0`. `SaveToDatabase()` tried to INSERT,
hitting `UNIQUE(profile_id, machine_id)`. With `m_database_id==0`, `Session::Store()`
logged a warning and returned without saving any events.

**Fix 1 (`CreateMachine`):** After the folder-scan determines `id` is non-zero, scan
the loader's existing `MachineList` entries by `m_id`. If found under a different serial
key, re-index it to the new serial and return the existing machine (preserving
`m_database_id`).

**Fix 2 (`SaveToDatabase`):** When `findBySerialLoaderAndProfile()` returns no match,
fall back to `findByProfileAndMachineId()`. If that finds a machine with the same
loader, adopt its DB ID and update the serial in the DB.

---

## 2026-04-28 - viatom_loader: SpO2=100 triggers spurious "Untested Data" warning

**File:** `oscar/SleepLib/loader_plugins/viatom_loader.cpp`

**Symptom:** Importing from a WellUe/Viatom OsRing_S oximeter triggered the
"Your Viatom device generated data that OSCAR has never seen before" warning
dialog, even though the data was valid.

**Root cause:** The SpO2 valid range check was `61–99%`. The OsRing_S can
record `SpO2=100`, which is a legitimate reading but fell outside the check,
producing an `UNEXPECTED_VALUE` warning.

**Fix:** Raised the upper bound from 99 to 100 in both `ParseFileViatom` (the
main Viatom path) and `ParseFilePOD2` (the POD2 path).

---

## 2026-04-27 - daily_summaries: drop bogus per-machine dimension (schema v16, #95)

**Files:** `oscar/database/database_schema.{h,cpp}`,
`oscar/database/daily_summary_repository.{h,cpp}`,
`oscar/database/backup/profile_restore.cpp`,
`oscar/SleepLib/profiles.cpp`, `oscar/mainwindow.cpp`,
`Notes/DATABASE_SCHEMA_REFERENCE.md`

**Symptom:** None visible to users — but the schema was internally
inconsistent and three repository finders were unreachable from any call
site that used their default arguments.

**Root cause:** `daily_summaries` was defined with `machine_id` as part of
its natural key (`UNIQUE(profile_id, date, machine_id)`), even though a
row is a profile-day rollup that already aggregates across every machine
contributing sessions to the OSCAR day. Every caller passed `machineId =
0` intending "combined", but `calculateFromDay()` silently rewrote that
to the first enabled CPAP session's machine id; `create()` then stored
the row with that id rather than NULL. Meanwhile `findByProfileAndDate`,
`findRange`, and `exists` translated `machineId = 0` into
`WHERE machine_id IS NULL`, so they could never read back what the writer
had stored. The mismatch was invisible because none of those three
finders had any in-tree callers.

**Fix (schema v15 → v16):**
- Dropped `machine_id` column, its FK to `machines`, and
  `idx_daily_summaries_profile_machine`.
- Changed `UNIQUE(profile_id, date, machine_id)` to
  `UNIQUE(profile_id, date)`.
- `migrateV15ToV16` rebuilds the table (SQLite cannot drop a column
  inside a UNIQUE constraint without a rebuild). The data copy is
  `INSERT OR REPLACE` ordered to prefer machine-bound rows over NULL ones
  on the (profile_id, date) key — defensive only; current databases have
  exactly one row per (profile_id, date) already.
- `DailySummaryRepository` API simplified: the optional `machineId`
  parameter is gone from `findByProfileAndDate`, `findRange`,
  `calculateAndStore`, `calculateAndStoreFromDay`, and `exists`.
  `DailySummaryData::machineId` removed.
- Backup compatibility: v15 backups restore cleanly because
  `executeSqlFile()`'s existing `validColumns` filter (built from
  `PRAGMA table_info`) silently drops columns absent from the v16
  schema. The dead FK-remap special case for
  `daily_summaries.machine_id` was removed.

**Side effect:** Deleting a machine no longer touches `daily_summaries`
(previously the FK was `ON DELETE SET NULL`). This is consistent with
the new "rollup is a profile-day, not a machine-day" semantics.

---

## 2026-04-27 - CSV Export Wizard: rename/description not persisted to database (#94)

**Files:** `oscar/database/report_tree_model.{h,cpp}`,
`oscar/exports/report_exporter.cpp`

**Symptom:** Renaming a user report/folder or editing a description in the
CSV Export Wizard appeared to succeed, but changes were lost on close/reopen.

**Root cause (rename):** `ReportTreeModel` did not override `setData()`.
When Qt commits an inline edit (F2 or right-click → Rename), it calls
`QAbstractItemModel::setData()` with `Qt::EditRole`. Without the override,
the base `QStandardItemModel::setData()` ran — updating only the in-memory
item text — while `renameNode()` (which writes to DB) was never called.

**Root cause (description):** `onEditDescription()` discarded the bool
return value of `updateDescription()`, so silent DB failures showed no error.

**Fix:** Override `setData()` in `ReportTreeModel` to detect `Qt::EditRole`
changes on user non-root nodes, persist the new name to the database, then
fall through to the base class to update the model text. Added
`QMessageBox::warning` in `onEditDescription()` when `updateDescription()`
returns false.

---

## 2026-04-26 - Purge oximetry: stale stats remain in daily_summaries (#91)

**Files:** `oscar/mainwindow.cpp` — `MainWindow::purgeDay()`,
`MainWindow::on_actionPurgeCurrentDaysOximetry_triggered()`

**Symptom:** After purging oximetry data for a day, `daily_summaries.spo2_avg`,
`pulse_avg`, `pulse_min`, `pulse_max`, and `has_oximetry` retain the old values.
Statistics pages continue to show oximetry stats that no longer exist.

**Root cause:** `Session::Destroy()` calls `SessionRepository::remove()` which
cascade-deletes `session_summaries` (has `ON DELETE CASCADE` from sessions).
However, `daily_summaries` has no FK relationship to sessions — it is only linked
to profiles and machines — so deleting sessions has no effect on it. Neither purge
code path called `DailySummaryRepository::calculateAndStoreFromDay()` or
`invalidateDate()` after destroying the sessions.

**Fix:** After the session-destroy loop in both purge paths, look up the updated
`Day*` for the affected date. If sessions remain (e.g. CPAP data still present),
call `calculateAndStoreFromDay()` to recalculate the row via INSERT OR REPLACE.
If no sessions remain (day was entirely purged), call `invalidateDate()` to delete
the now-stale row.

---

## 2026-04-26 - SpO2 and pulse data missing from daily_summaries after oximetry import (#89)

**Files:** `oscar/mainwindow.cpp` — `MainWindow::importNonCPAP()`;
`oscar/oximeterimport.cpp`

**Symptom:** After importing oximetry data (via the oximeter wizard or file-based
loaders), `daily_summaries.spo2_avg`, `pulse_avg`, `pulse_min`, `pulse_max`, and
`has_oximetry` remain 0/false even though data is correctly in `session_channels`
and `session_summaries`.

**Root causes:**

1. `calculateDailySummaries()` is called only once at profile load when
   `existingCount == 0`. After any subsequent import no code calls it again, so
   the `daily_summaries` rows for affected dates are never updated with oximetry
   stats. (`mach->day[date]` and `profile->daylist[date]` point to the same Day
   object, so a single call sees combined CPAP+oxi sessions; `INSERT OR REPLACE`
   updates existing rows idempotently.)

2. `oximeterimport.cpp` called `count()`, `Min()`, and `Max()` for OXI_SPO2 and
   OXI_Pulse but not `avg()` or `wavg()`, leaving those fields as 0 in both
   `session_channels` and, consequently, `daily_summaries.spo2_avg`/`pulse_avg`.

**Fixes:**

- `importNonCPAP()`: call `p_profile->calculateDailySummaries()` after a
  successful import (`res > 0`). Covers Viatom, Dreem, Zeo, Somnopose.
- `oximeterimport.cpp`: add `session->avg()` and `session->wavg()` for
  OXI_Pulse and OXI_SPO2 before `mach->Save()`, then call
  `p_profile->calculateDailySummaries()` after `StoreMachines()`.

---

## 2026-04-25 - session_channels p95/median stored as 0 for OXI_Pulse and other channels (#88)

**Files:** `oscar/SleepLib/machine.cpp` — `Machine::Save()`;
`oscar/SleepLib/session.cpp` — `Session::StoreToDatabase()`, `Session::updateCountSummary()`

**Symptom:** `session_channels.p95` and `session_channels.median` contain 0 for OXI_Pulse
(channel_id 6144) and potentially other channels even though data was correctly imported.

**Root causes:**

1. **Primary — overwrite on re-save:** `Machine::Save()` called `StoreToDatabase()` on
   every session unconditionally. When `Profile::Save()` triggered `Machine::Save()` (e.g.
   user changes preferences), events were not loaded in memory. `StoreToDatabase()` deleted
   the existing `session_channels` rows and re-inserted them with p95=0.

2. **Missing fallback in `StoreToDatabase()`:** When events weren't loaded, the code
   unconditionally set p95=0 even when `m_valuesummary`/`m_timesummary` were already
   populated (loaded from the DB). No attempt was made to use these loaded summaries.

3. **EVL_Event single-event list:** For change-only compressed channels where the value
   never changed during the session, `ToTimeDelta()` creates a 1-event EventList. The loop
   in `updateCountSummary()` never runs for cnt==1, leaving `valsum` empty.
   `calculatePercentiles()` then returns `valid=false` → p95=0.

4. **EVL_Waveform multi-EventList time accumulation:** When a channel has multiple
   EventLists (e.g. separated by recording gaps), `updateCountSummary()` iterated the
   global cumulative `valsum` for the timesum calculation on each EventList. Counts from
   earlier EventLists were double-counted into subsequent EventLists' time weights,
   giving inflated (wrong) p95 values when gaps exist.

**Fixes:**

- `Machine::Save()`: skip `StoreToDatabase()` for sessions where `sessionRowId() > 0`
  and `!changed()`. New sessions (rowId==0) and sessions explicitly marked changed still
  always save.
- `StoreToDatabase()`: added `hasSummaryData` check — if events aren't loaded but
  `m_valuesummary` and `m_timesummary` are populated (DB-loaded), use them via
  `calculatePercentiles()` instead of writing 0.
- `updateCountSummary()` EVL_Event: after the loop, if `valsum` is still empty (cnt==1),
  add `lastraw` to valsum with a time weight of 1 so `calculatePercentiles()` can return
  the single constant value as the percentile.
- `updateCountSummary()` EVL_Waveform: use a per-EventList `localValsum` for the timesum
  calculation so each EventList contributes only its own sample counts to the time weights.

---

## 2026-04-25 - importNonCPAP() ran without a database transaction (#87)

**File:** `oscar/mainwindow.cpp` — `MainWindow::importNonCPAP()`

**Symptom:** Imports of Zeo, Dreem, Somnopose, and Viatom data ran outside any
database transaction. A failure mid-import would leave the database in a partially
written state with no way to roll back.

**Root cause:** `importCPAP()` wraps the entire import in `dbMgr.transaction()` /
`dbMgr.commit()` with rollback on failure, but `importNonCPAP()` had no equivalent
wrapping — it called `loader.Open()` and `ctx->Commit()` with no transaction guard.

**Fix:** Added `dbMgr.transaction()` before `loader.Open()` in `importNonCPAP()`,
with the same failure handling as `importCPAP()`: error dialog + early return on
transaction-start failure, rollback + critical dialog on commit failure. Also added
the `lastImported` timestamp update (inside the transaction) that `importCPAP()` already
performs, which was also missing.

---

## 2026-04-24 - nightly-build.bat corrupts itself when git stash runs mid-execution (#83)

**File:** `Building/Windows/nightly-build.bat`, `Building/Windows/nightly-notify.ps1`

**Symptom:** Running `nightly-build.bat` when the file had local modifications produced
garbled commands (e.g. `l buildall-qt6.bat`) and failed immediately.

**Root cause:** `git stash` inside the script restored the old version of the file on
disk. Since cmd.exe reads batch files by byte offset, the interpreter then read from
the wrong offset in the restored (differently-sized) file.

**Fix:** Moved `git stash`/`pop` to before the fetch step so the script is never
modified mid-execution. Added a self-modification guard (skipped when running a copy
from outside the repo). Added `--ignore-cr-at-eol` to guard's `git diff` to avoid
false positives from CRLF/LF differences. Added logging to `C:\OSCAR\nightly-build.log`
and a modal failure notification via `nightly-notify.ps1`.

---

## 2026-04-23 - layoutSettings folder not removed after import when source has no saved layouts

**File:** `oscar/main.cpp` — `importLegacyNamedLayouts()`

After importing from OSCAR 1.7.1, the `layoutSettings` folder persisted in OSCAR 2.0's app
data when the source had a `.descriptions.txt` file but no `.shg` layout files. The function
removes `.descriptions.txt` only inside the `if (allOk)` block that runs after committing
`.shg` entries to the DB. When no `.shg` files exist for a view, `entries.isEmpty()` causes
an early `continue`, skipping `descFile.remove()`. The file remains, the folder is non-empty,
and the `dir.rmdir()` cleanup at the end cannot remove it.

Fix: call `descFile.remove()` in the `entries.isEmpty()` path before `continue` so the
descriptions file is always cleaned up.

---

## 2026-04-23 - Bulk import creates empty layoutSettings folder in app data

**File:** `oscar/profileimporter.cpp` — `copyLayoutSettings()`

After a bulk import from OSCAR 1.7.1, an empty (or near-empty) `layoutSettings` folder
was left in the OSCAR 2.0 app data directory. The source folder existed but contained only
a `.txt` file with no saved layouts. `QDir().mkpath(destLayoutPath)` was called
unconditionally after confirming the source folder exists, before checking whether any
files actually needed to be copied. The destination folder was created even when no files
required copying.

Fix: moved `mkpath()` inside the copy loop, guarded by a `destCreated` flag so the
destination directory is created only when the first file actually needs to be copied there.

---

## 2026-04-23 - Crash deleting a profile whose directory is missing

**File:** `oscar/profileselector.cpp` — `on_buttonDestroyProfile_clicked()`

A cancelled (or otherwise failed) profile import can leave a row in the `profiles` table while
the profile directory is absent. `Profiles::Scan()` skips such profiles (marks them `missing` and
does not add them to the in-memory `Profiles::profiles` map). `updateProfileList()` still shows
them because it queries the DB directly. Clicking Delete on such a profile called
`Profiles::profiles[name]`, which — because `QHash::operator[]` inserts a default value on a
missing key — silently returned `nullptr`. The immediately following `profile->Get(...)` call
dereferenced that null pointer → crash (`QHash::find` via `Preferences::Get` at
`profileselector.cpp:487`).

Fix: use `Profiles::profiles.value(name, nullptr)` to avoid inserting a null entry. When the
returned pointer is null, resolve the profile path directly from the database via
`ProfileRepository::resolvePath()`. Guard the password-check block with `if (profile && ...)`.
`removeDirWithProgress()` already handles a non-existent directory safely (returns true), so the
rest of the deletion flow works unchanged.

---

## 2026-04-23 - Fix profile import failing with "cannot commit - no transaction is active"; cancellation leaves incomplete DB data

**Files:** `oscar/SleepLib/preferences.cpp`, `oscar/profileimporter.cpp`

`Preferences::Save()` and `Preferences::Open()` called `db.transaction()` / `db.commit()` /
`db.rollback()` directly on the raw `QSqlDatabase` object, bypassing
`DatabaseManager::m_inTransaction` tracking. When `ProfileImporter::importProfile()` called
`migrateAppSettings()` → `p_pref->Save()` while the outer metadata transaction was active, the
direct `db.commit()` committed the outer transaction. `DatabaseManager` still believed
`m_inTransaction = true`, so when `importProfile()` then tried its own commit, SQLite correctly
reported "cannot commit - no transaction is active". The code path for a failed commit did not
call `ProfileRepository::remove()`, so the already-committed metadata (profile record, machines,
user info) remained in the DB with no directory and no sessions. This manifested as two user-
visible bugs: (1) an error message on every import attempt, and (2) after cancellation of a bulk
import, the first profile appeared in the DB with incomplete data.

Fix 1 (`preferences.cpp`): use `ownTransaction = !dbMgr.inTransaction()` in both `Save()` and
`Open()`. When an outer transaction is already active the writes join it; when called standalone
the function starts and commits its own transaction as before.

Fix 2 (`profileimporter.cpp`): move the `profileId` lookup to BEFORE the metadata commit so the
ID is available on the failure path. Add a defensive `cleanupRepo.remove(profileId)` call in the
commit-failure path to remove any metadata that may have been committed via a bypassed path.

---

## 2026-04-22 - Fix restore failure for pre-v15 backups containing removed columns

**Files:** `oscar/database/backup/profile_restore.cpp`

`executeSqlFile()` reconstructed INSERT statements verbatim from backup SQL files and
passed them to SQLite unchanged. When a backup from schema v12–v14 contained a column
that was subsequently removed (e.g. `user_info.dst_enabled` dropped in v15,
`sessions.events_file`/`summary_file` dropped in v12), SQLite rejected the INSERT with
"table has no column named X", aborting the entire restore. The existing table-level
guard (`sqlite_master` check) handled missing *tables* but not missing *columns*.
Fix: query `PRAGMA table_info(<table>)` once per file to build a valid-column set; skip
any column in the backup INSERT that is absent from the set. This closes the latent bug
for all past and future column removals without needing per-migration restore patches.

---

## 2026-04-22 - Remove dead DST Zone field (schema v15)

**Files:** `oscar/newprofile.ui`, `oscar/newprofile.cpp`, `oscar/SleepLib/profiles.h`,
`oscar/database/user_info_repository.{h,cpp}`, `oscar/database/database_schema.{h,cpp}`,
`Notes/DATABASE_SCHEMA_REFERENCE.md`

The "DST Zone" checkbox in the New Profile dialog was stored and persisted to the
`user_info.dst_enabled` database column, but was never read by any loader, timestamp
calculation, or display code. `EDFInfo::localNoDST` (the only DST-related mechanism)
derives its offset from the system timezone at startup, independently of this field.
Removed the checkbox, `STR_UI_DST` constant, `daylightSaving()`/`setDaylightSaving()`
accessors, `dstEnabled` struct member, all SQL references, and the database column.
Schema bumped from v14 to v15; migration drops the column via `ALTER TABLE DROP COLUMN`.

---

## 2026-04-21 - Eight issues from Claude code-review of schema v14 additions

**Files:** `oscar/database/app_preferences_repository.{h,cpp}`, `oscar/database/database_manager.cpp`, `oscar/database/database_schema.cpp`, `oscar/SleepLib/preferences.cpp`, `oscar/Graphs/gGraphView.cpp`, `oscar/main.cpp`

**Bug 1 — Stale sibling column after scalar ↔ blob type switch (`app_preferences`)**
Symptom: `save()` upserted `value`/`data_type` but left `blob_value` from any prior `saveBlob()` call intact (and vice-versa). Although `data_type` identifies the authoritative column on read, the stale column wastes row width and surprises direct SQL queries.
Fix: Added `blob_value = NULL` to `save()`'s UPDATE clause; added `value = NULL` to `saveBlob()`'s UPDATE clause.

**Bug 2 — `importLegacyNamedLayouts` aborted entire view on single unreadable file**
Symptom: If one `.shg` file couldn't be opened, `readOk = false` propagated to `allOk`, causing DB rollback for the entire view — all successfully-read layouts were discarded and left permanently in legacy form.
Fix: Skip unreadable files individually with a `qWarning`, removing `readOk`. Also skip version-0 files (corrupt or under 6 bytes) rather than importing garbage that silently fails to deserialize.

**Fix 3 — Dead migration code after `return true` in `upgradeSchema`**
~285 lines of v3–v11 migration code were unreachable after the `return true` at the end of active migration logic. Deleted.

**Fix 4 — Qt5 `#if QT_VERSION` guards in `Preferences::Save()` XML path**
Three conditional blocks using `QVariant::Type` / `QVariant::Invalid` / `ts.setCodec` were unnecessary in the Qt6-only build. Removed; kept Qt6 branch inline.

**Fix 5 — `gGraphView::SaveSettings` silently swallowed DB save failures**
No warning was emitted when `repo.saveCurrentLayout()` returned false; `openOk` was set but callers had no console signal. Added `qWarning` on failure.

**Fix 6 — `PRAGMA journal_mode = WAL` result not checked**
`exec()` succeeds even if SQLite stays in delete mode (e.g. on a network filesystem). The pragma returns the resulting mode as a row. Now reads the result and emits `qWarning` if it isn't "wal".

**Style 7 — `dataTypeFromVariant`/`variantFromString` were non-static instance methods**
Neither method accesses instance state. Declared `static` in the header.

**Style 8 — `hasData()` used `toInt()` for a `COUNT(*)` result**
Changed to `toLongLong()` to match the id type and other count sites.

---

## 2026-04-19 - Four bugs from data-migration code review

**Files:** `oscar/SleepLib/preferences.cpp`, `oscar/main.cpp`, `oscar/saveGraphLayoutSettings.cpp`

**Bug 1 — Crash during XML→DB migration leaves preferences permanently half-seeded**
Symptom: A power failure mid-import caused the `app_preferences` table to be partially populated. On the next launch `rows.isEmpty()` returned false so the code loaded the partial data and never looked at `Preferences.xml` again, silently losing the remaining keys.
Fix: Wrapped the `Open()` seeding loop in a `db.transaction()`/`db.commit()`. A crash now rolls back to an empty table so the next launch falls through to XML seeding again.

**Bug 2 — Erased app preferences resurrected from DB on next launch**
Symptom: `Preferences::Erase()` removed a key from the in-memory hash, but `Save()` only UPSERTed — it never deleted rows. Erased keys (e.g. `STR_AppName`, `STR_GEN_SkipLogin`) were reloaded from the DB on the next `Open()`.
Fix: Changed the DB path in `Save()` to delete-then-reinsert the whole `"general"` category inside a transaction, so erased keys are not carried forward.

**Bug 3 — No transactions around legacy layout import loops**
Symptom: `importLegacyNamedLayouts()` and `importLegacyProfileLayouts()` wrote to the DB one row per file with no transaction; a crash left partial DB state with some source files already deleted.
Fix: Restructured both functions to two phases — read all files into memory first, then write all to DB inside a single `db.transaction()`; delete source files only after `db.commit()`.

**Bug 4 — Layout description truncation appended "..." beyond the allowed length**
Symptom: In `SaveGraphLayoutSettings::itemChanged()`, a description exactly one character over `maxDescriptionLen` (80) had `"..."` appended, yielding 84 characters instead of being capped at 80.
Fix: Changed `desc.append("...")` to `desc = desc.left(maxDescriptionLen - 3) + "..."`.

---

## 2026-04-19 - Restore dialog blocked UI for several minutes during Validate

**Files:** `oscar/restoredialog.{h,cpp}`, `oscar/oscar.pro`

**Symptom:** Clicking Validate in the Restore Profile dialog caused the UI to freeze for several minutes (extraction + SHA-256 hash of all .sql files ran on the main thread).

**Root cause:** `ProfileRestore::validatePackage()` was called synchronously in `on_validateButton_clicked()`, blocking the Qt event loop until the ZIP extraction and checksum computation finished.

**Fix:** Moved `validatePackage()` to a thread-pool worker via `QtConcurrent::run`. The dialog immediately shows an indeterminate progress bar and disables all controls. `QFutureWatcher<bool>::finished` fires on the main thread when done, where the rest of the validation UI logic runs unchanged. Added `concurrent` to `QT +=` in oscar.pro.

---

## 2026-04-20 - Six issues from Gemini code-review of schema v14 migration

**Files:** `oscar/database/database_schema.cpp`, `oscar/database/graph_layouts_repository.cpp`, `oscar/main.cpp`, `oscar/Graphs/gGraphView.cpp`, `oscar/saveGraphLayoutSettings.{h,cpp}`

**Issue 1 — `migrateV13ToV14` had no transaction wrapper**
Symptom: If any of the four DDL/DML steps succeeded but a later step failed, the DB was left with v13 schema version but v14 tables already created — relying on idempotency to recover on retry.
Fix: Wrapped the migration body in `db.transaction()` / `db.commit()` / `db.rollback()`.

**Issue 2 & 3 — `peekVersion` lambda duplicated; importers lacked `static`**
Symptom: Identical lambda duplicated in `importLegacyNamedLayouts()` and `importLegacyProfileLayouts()`; both functions had external linkage.
Fix: Extracted to a `static int peekShgVersion(const QByteArray&)` file-scope helper; added `static` to both importer functions.

**Issue 4 — `loadNamedLayout` selected `is_current` but silently skipped it**
Symptom: SELECT included `is_current` at column index 3 but the mapping code jumped directly to `q.value(4)` for `description`, reading the wrong column if ordering ever changed.
Fix: Removed `is_current` from the SELECT; shifted `description`/`format_version`/`data` to indices 3/4/5.

**Issue 5 — `openOk` global not set in DB branches of `SaveSettings`/`LoadSettings`**
Symptom: Callers reading `openOk` after a DB-path call would see stale state from the previous file operation.
Fix: Set `openOk` to the DB operation result in both `SaveSettings` and `LoadSettings` DB branches.

**Issue 6 (style) — Misleading comment and vestigial stub**
Fix: Corrected `saveCurrentLayout` comment (removed "preserve description" which doesn't apply to current-layout upserts); removed no-op `createSaveFolder()` definition from `saveGraphLayoutSettings.cpp` and declaration from `.h`; added `extern` linkage explanation to `gVversion` declaration; added `qWarning` to DB-not-open paths in load methods.

---

## 2026-04-19 - Four bugs from Codex code-review of schema v14 migration

**Files:** `oscar/database/database_schema.cpp`, `oscar/database/app_preferences_repository.cpp`, `oscar/database/backup/profile_backup.cpp`, `oscar/database/backup/profile_restore.cpp`

**Bug 1 — Fresh v14 databases missing `blob_value` column in `profile_preferences`**
Symptom: `createProfilePreferencesTable()` did not include `blob_value BLOB`, but the v13→v14 migration adds it via `ALTER TABLE`. Any code writing `blob_value` to a fresh install would fail with "no such column".
Fix: Added `blob_value BLOB` to the `CREATE TABLE` DDL in `createProfilePreferencesTable()`.

**Bug 2 — QDateTime app preferences saved in Qt::TextDate format, read back with Qt::ISODate**
Symptom: `AppPreferencesRepository::save()` used `value.toString()` which for QDateTime produces Qt::TextDate (e.g. "Sat Apr 19 12:00:00 2026"). `variantFromString()` parsed with `Qt::ISODate`, which fails, returning an invalid QDateTime. Affected `UpdatesLastChecked` and any other datetime pref.
Fix: Added explicit ISO 8601 serialization for QDateTime/QDate/QTime types in `save()`.

**Bug 3 — `graph_layouts` not included in profile backup/restore**
Symptom: Per-profile current daily/overview layouts (rows with `profile_id = this profile`) were silently dropped on backup and not restored, losing the user's layout customizations.
Fix: Added `graph_layouts` export to `exportProfileMetadata()` in `profile_backup.cpp` and added it to the `restoreOrder` list in `profile_restore.cpp`.

**Bug 4 — `createGraphLayoutsTable()` returns true even if unique indexes fail**
Symptom: Both partial unique index creations used `qWarning()` + fall-through on failure. The schema version could advance to 14 without the indexes. Repository upserts using `ON CONFLICT` on those indexes would then insert duplicate rows instead of updating.
Fix: Changed both index creation failure paths to `qCritical()` + `return false`.

---

## 2026-04-19 - Schema v14: Preferences.xml and graph layouts migrated to DB

**Files:** `oscar/database/database_schema.{h,cpp}`, `oscar/database/database_manager.cpp`, `oscar/database/app_preferences_repository.{h,cpp}`, `oscar/database/graph_layouts_repository.{h,cpp}`, `oscar/SleepLib/preferences.cpp`, `oscar/Graphs/gGraphView.{h,cpp}`, `oscar/saveGraphLayoutSettings.{h,cpp}`, `oscar/main.cpp`, `oscar/oscar.pro`

**Change:** Schema bumped from v13 to v14. Two new tables replace filesystem files:
- `app_preferences` — replaces `Preferences.xml` (global app preferences singleton)
- `graph_layouts` — replaces `layoutSettings/*.shg` (named layouts) and per-profile `daily.shg`/`overview.shg` (current layouts)
Also: `blob_value BLOB` column added to `profile_preferences` for future use.

**Migration:** `DatabaseManager` now calls `DatabaseSchema::upgradeSchema()` (formerly a stub) when the DB version is below current. `migrateV13ToV14()` is additive-only (CREATE TABLE IF NOT EXISTS + ALTER TABLE ADD COLUMN).

**Legacy file import:** Preferences.xml is imported once on first launch (in `Preferences::Open()` fallthrough) then deleted. Named `.shg` files in `layoutSettings/` are imported by `importLegacyNamedLayouts()` in `main.cpp` then deleted. Per-profile `daily.shg`/`overview.shg` are imported lazily in `gGraphView::LoadSettings()` on first load then deleted.

**Initialization order fix:** DB initialization in `main.cpp` moved to before `p_pref->Open()` so the DB routing in `Preferences::Open()` sees an open database.

---

## 2026-04-19 - Debug log flushed to disk on every message (crash-safe logging)

**Files:** `oscar/logger.cpp` — `LogThread::appendClean()`, `LogThread::run()`

**Symptom:** On crash, recent debug messages were lost because they were buffered in `LogThread::buffer` (a QList) and in the `QTextStream` — neither was flushed before the process died.

**Root cause:** `appendClean()` only enqueued messages; `run()` dequeued them asynchronously. Any messages not yet dequeued at crash time were lost. Additionally, the `QTextStream` write had no `QFile::flush()` call to push data past the C library buffer.

**Fix:** Moved the file write into `appendClean()`, under the existing `strlock`, with `Qt::endl` (flushes stream) plus `m_logFile->flush()` (flushes C library buffer to kernel). The `run()` thread now only emits `outputLog()` for UI display. The OS writes kernel-buffered data to disk on process exit, so a normal crash loses nothing.

---

## 2026-04-17 - OpenProfile rollback on late failure now fully cleans partial state

**Files:** `oscar/mainwindow.cpp` — `OpenProfile()`

**Symptom:** A defensive failure path could still leave a partially opened profile in memory if `OpenProfile()` aborted after `p_profile` assignment and data load had already started. This was most visible in the active-page guard checks.

**Root cause:** Although `ProgressDialog` cleanup had been added, late returns in `OpenProfile()` did not consistently roll back loaded profile data and partially created pages.

**Fix:** Added a pre-open sanity guard to abort before assigning `p_profile` when page objects are unexpectedly still active, and introduced a single rollback helper (`abortOpenProfile`) for late failures. The rollback now:
- closes/deletes progress dialog,
- deletes any partially created Welcome/Daily/Overview pages,
- unloads machine data, removes lock, and nulls `p_profile`,
- and calls `ensureCleanDatabaseState()`.

---

## 2026-04-17 - Daily constructor: skip graphs for channels absent from loaded data

**Files:** `oscar/daily.cpp` — `Daily::Daily()`

**Symptom:** Daily page constructor always created graph objects for every possible channel (Prisma, BMC, PRS1-specific, Zeo, Position, Oximeter, etc.) regardless of whether the loaded profile contained any data for those channels. This wasted memory and had latent null-pointer risk if any channel's graph entry was ever missing from the creation loops but referenced in the hardcoded AddLayer calls.

**Root cause:** The `cpapcodes[]` and `oximetercodes[]` creation loops had no availability check. All ~25 hardcoded `graphlist[schema::channel[CODE].code()]->AddLayer(...)` calls after the loops used `operator[]` (which inserts null on miss) rather than `.value()` (which returns null without inserting), so a missing graph entry would crash.

**Fix:** Before the creation loops, build a `QSet<ChannelID>` from all machines' `availableChannels(0)`. Skip graph creation in both loops for channels not in the set (empty set = no data loaded = create all). Changed all hardcoded `graphlist[...]->AddLayer(...)` calls to use `graphlist.value(...)` with null guards so absent graphs are safely skipped.

---

## 2026-04-17 - Short/ignored sessions leaked in UnloadMachineData

**Files:** `oscar/SleepLib/profiles.cpp` — `Profile::UnloadMachineData()`

**Symptom:** Memory allocated for short sessions (under the "ignore sessions shorter than" threshold, default 5 minutes) was never freed when closing a profile. Each open/close cycle leaked one Session object per short session in the profile.

**Root cause:** `Machine::AddSession()` adds every session to `sessionlist` unconditionally (to prevent re-importing), but returns early without adding it to a `Day` when the session is below the ignore threshold. `UnloadMachineData()` called `sessionlist.clear()` which empties the QHash without deleting the Session pointers. Sessions in Days were freed correctly via `Day::~Day()`, but these day-less sessions had no other owner.

**Fix:** Before clearing `sessionlist`, collect all Session pointers that are in a Day (via `mach->day`), then explicitly delete any sessions in `sessionlist` not in that set.

---

## 2026-04-17 - Remove High Resolution Mode (Qt5-only feature)

**Files:** `oscar/highresolution.h`, `oscar/highresolution.cpp` (deleted), `oscar/main.cpp`, `oscar/oscar.pro`, `oscar/preferencesdialog.cpp`, `oscar/preferencesdialog.ui`

**Symptom:** Preferences > Appearance showed a disabled "Enable High Resolution Mode" checkbox with no function in Qt6.

**Root cause:** The feature was Qt5-only — in Qt6, high DPI scaling is always on. The checkbox was already disabled/forced-checked in Qt6 builds, but the entire module and its UI element remained.

**Fix:** Deleted `highresolution.h`/`.cpp`, removed the checkbox from `preferencesdialog.ui`, and removed all `#if QT_VERSION < 6` guarded references in `main.cpp` and `preferencesdialog.cpp`. The setting was stored in a separate file (`hiResolutionMode.txt`), not in `Preferences.xml`, so profile import is unaffected.

---

## 2026-04-17 - ProgressDialog left open on error returns in OpenProfile

**Files:** `oscar/mainwindow.cpp` — `OpenProfile()`

**Symptom:** Two defensive checks in `OpenProfile()` (active `daily` or `overview` object detected) called `return false` without closing or deleting the `ProgressDialog` that had already been shown via `progress->open()`. The dialog would remain visible on screen with no way to dismiss it.

**Root cause:** Both early-return paths at the `if (daily)` and `if (overview)` guards branched out before the normal `progress->close(); delete progress;` at the end of the function.

**Fix:** Added `progress->close(); delete progress;` before each `return false`.

---

## 2026-04-17 - Daily::leakchart member variable never initialised

**Files:** `oscar/daily.cpp` — `Daily::Daily()`, `oscar/daily.h`

**Symptom:** The class member `gLineChart *leakchart` (intended to allow preference-driven threshold resets) was never assigned in the constructor. A local variable of the same name shadowed it, leaving the member holding an indeterminate pointer for the life of the object.

**Root cause:** Refactoring introduced `gLineChart *leakchart = new gLineChart(...)` as a local variable in the constructor, shadowing the class member declared in the header. The class member was never assigned or initialised.

**Fix (option 2B):** Removed the type prefix from the constructor declaration, turning it into an assignment to `this->leakchart`. Also added `leakchart = nullptr` near the other member initialisations at the top of the constructor body for safety.

---

## 2026-04-17 - Redundant repaint() calls in Welcome::refreshPage()

**Files:** `oscar/welcome.cpp` — `refreshPage()`

**Symptom:** Five explicit `repaint()` calls forced immediate synchronous redraws of the Welcome page toolbar buttons after each call to `setEnabled()`. Qt already schedules a deferred paint event when widget state changes, making these calls redundant overhead on every profile open.

**Root cause:** Unnecessary calls added; Qt handles repainting automatically when widget state changes.

**Fix:** Removed all five `repaint()` calls.

---

## 2026-04-17 - CProgressBar heap-allocated on every Overview range change

**Files:** `oscar/overview.cpp` — `on_rangeCombo_activated()`

**Symptom:** `CProgressBar` was heap-allocated with `new` and explicitly deleted at the end of every range-change handler, adding unnecessary heap churn on a frequently called code path.

**Root cause:** No reason for heap allocation; `CProgressBar` is not a QWidget subclass and has no parent-ownership requirements.

**Fix:** Changed to stack allocation. Removed `new`/`delete`; replaced `->` member access with `.`.

---

## 2026-04-16 - Purge machine reappears after restart

**Files:** `oscar/mainwindow.cpp` — `purgeMachine()`

**Symptom:** After purging a device via Data/Advanced/Purge all device data, the machine reappeared in the list after restarting OSCAR (session data was gone, but the machine record persisted).

**Root cause:** `purgeMachine()` called `p_profile->DelMachine(mach)` which removes the machine from the in-memory list only. It never called `MachineRepository::remove()` to delete the machine record from the SQLite database.

**Fix:** Capture `mach->getDatabaseId()` before deleting the object, then call `MachineRepository::remove(dbId)` to delete the database record.

---

## 2026-04-16 - Feelings (ZombieMeter) imported with wrong values / displayed incorrectly

**Files:** `oscar/profileimporter.cpp` — `migrateJournalFromSource()`; `oscar/daily.cpp` — `setup_ZombieUIWidgets()`, `on_ZombieSlider_valueChanged()`, `on_Units10_100_clicked()`

**Symptom:** After a 1.7.1 → 2.0 import, Feelings values on the Daily Notes tab showed wrong numbers.

**Root cause:** OSCAR 1.x stored the Feelings field (`Journal_ZombieMeter`) in two mixed encodings: an old 0–10 scale (where 5 was a "not set" sentinel) and a newer 0–100 scale stored as value+20. The importer wrote these raw values into the 2.0 database without normalising them. The display code in `setup_ZombieUIWidgets` applied a runtime decoding (< 20 → ×10, ≥ 20 → −20) to handle both formats on the fly, but this was fragile and produced confusing results for the user.

**Fix (import):** In `migrateJournalFromSource()`, after loading each journal session, normalise `Journal_ZombieMeter` to a clean 0–100 range before storing: values ≥ 20 have 20 subtracted (strip the offset); value 5 is converted to 0 (old "not set" sentinel); all other values (0–4, 6–10) are multiplied by 10. A result of 0 removes the key entirely (not set).

**Fix (display):** Removed the runtime decoding from `setup_ZombieUIWidgets` — values are now stored as 0–100 directly, so the function uses `qBound(0, zombieValue, 100)` unchanged. Removed the `+ZombieModeOffset` offset from `on_ZombieSlider_valueChanged()` and `on_Units10_100_clicked()` so newly entered values are also stored as 0–100.

**Note:** Users with existing OSCAR 2.0 feelings data must reimport from 1.7.1 to correct stored values.

---

## 2026-04-15 - Overview BMI graph missing or unpopulated

**File:** `oscar/daily.cpp` — `Daily::set_JournalWeightValue()`

**Symptom:** The BMI graph on the Overview page either did not appear at all, or appeared but showed no data points.

**Root cause:** `set_JournalWeightValue()` saved `Journal_Weight` to the journal session but never saved `Journal_BMI`. BMI was only calculated and displayed in the UI label (`set_BmiUI`). Since `Journal_BMI` was never persisted, `day->settingExists(Journal_BMI)` always returned false in `gOverviewGraph::SetDay()`, so `m_goodcodes` stayed all-false, `m_empty` stayed true, and `gGraphView` skipped the graph entirely. Users with older data where BMI had been stored saw the graph appear but all new days had no data points.

**Fix:** In `set_JournalWeightValue()`, after saving weight, also calculate and save `Journal_BMI` (using `user_height_cm`) when weight > 0 and height is available. When weight is cleared, also erase `Journal_BMI` from journal settings.

Also fixed in `profileimporter.cpp::migrateJournalFromSource()`: during the 1.7.1 → 2.0 import, the importer now reads the source user's height and backfills `Journal_BMI` into each journal session that contains `Journal_Weight` before storing it to the database. This ensures historical weight data produces a populated BMI graph immediately after import.

---

## 2026-04-13 - Blank progress dialog during Resync Device Detected Events

**File:** `oscar/mainwindow.cpp` — `MainWindow::doReprocessEvents()`

**Symptom:** When enabling Custom CPAP User Event flagging and selecting "Resync Device Detected Events", a blank popup appeared and stayed on screen for the ~1 minute duration of the operation.

**Root cause:** `doReprocessEvents()` created and opened a `ProgressDialog` but never called `QApplication::processEvents()` inside the processing loop, so Qt's event loop had no opportunity to paint the dialog or update its progress bar. `doRecompressEvents()` (the equivalent function for recompression) correctly called both `progress.setProgressValue(++idx)` and `QApplication::processEvents()` per day.

**Fix:** Added `int idx = 0;` before the loop and `progress.setProgressValue(++idx); QApplication::processEvents();` at the end of each day iteration, matching the pattern in `doRecompressEvents()`.

---

## 2026-04-14 - Statistics end-date drift and purge-all notes recreation

**Files:** `oscar/SleepLib/profiles.cpp`, `oscar/daily.h`, `oscar/daily.cpp`, `oscar/mainwindow.cpp`

**Symptom 1 (Statistics):** On the Statistics page, the reporting period end date (and "Most Recent" period end) could be later than the CPAP database range end date. In affected cases, the extra day had no CPAP data.

**Root cause 1:** `Profile::FirstGoodDay(MT_*)` / `Profile::LastGoodDay(MT_*)` treated no-data typed ranges as valid by falling back through `FirstDay(mt)`/`LastDay(mt)` behavior. For types with no data (notably oximeter), this let overall profile bounds (which can be extended by Journal days) leak into statistics report-date calculations.

**Fix 1:** Added inverted-range guards in both methods:
- `FirstGoodDay(MT_*)` now returns invalid when `FirstDay(mt) > LastDay(mt)`.
- `LastGoodDay(MT_*)` now returns invalid when `LastDay(mt) < FirstDay(mt)`.

**Symptom 2 (Purge):** Daily page "Data -> Advanced -> Purge selected day -> Delete all include Notes" could appear to delete notes, but notes reappeared immediately.

**Root cause 2:** After purge deleted the journal session, Daily reload called `Unload(previous_date)`. The notes editor still contained pre-purge text, so unload re-saved/recreated the journal entry.

**Fix 2:** Added `Daily::clearJournalNotesEditor()` and call it from `MainWindow::purgeDay()` when purging `MT_JOURNAL`, before `LoadDate(date)`, so reload cannot re-create the deleted note.

## 2026-04-14 - Statistics report-date stale across profile switches

**Files:** `oscar/statistics.h`, `oscar/statistics.cpp`, `oscar/mainwindow.cpp`

**Symptom:** For users with multiple profiles, the statistics report end date could show a stale value when switching profiles, if both profiles' `lastGoodDay()` happened to match the cached value from the previous profile.

**Root cause:** `Statistics::updateReportDate()` uses two file-scope `QDate` statics (`lastdate`, `firstdate`) to skip redundant recalculations. These statics are process-global and not reset between profile switches, so the early-return could fire on the first `GenerateStatistics()` call for a newly-opened profile, leaving the new profile's `statReportDate` unrefreshed.

**Fix:** Added `Statistics::resetReportDate()` which invalidates both statics, and call it in `MainWindow::OpenProfile()` immediately after `p_profile = prof`, ensuring the first `updateReportDate()` call for each new profile always does a full refresh.

---

## 2026-04-09 - Code review fixes: mainwindow.cpp

**Files:** `oscar/mainwindow.cpp`

**Fix 1 — ProgressDialog leak in zip exporters:** `on_actionCreate_Card_zip_triggered()`, `on_actionCreate_Log_zip_triggered()`, and `on_actionCreate_OSCAR_Data_zip_triggered()` all allocated a `ProgressDialog` on the heap but never deleted it. Added `prog->close(); delete prog;` after `z.Close()` in each function.

**Fix 2 — Dead code in `finishCPAPImport()`:** Removed a large block of commented-out session-save code with confusing stale comments (`(?)savesession`). Session saving is now handled by `ctx->Commit()` inside `importCPAP()`.

**Fix 3 — Syntax noise in `SetupGUI`:** Removed a stray extra semicolon (`; ;`) and trailing `};` in the `m_clinicalMode` check block.

**Fix 4 — Removed Win32 fallback in `purgeMachine()`:** The `#ifdef Q_OS_WIN` block used `SetFileAttributes`/`RemoveDirectory` to attempt removal of the machine directory. Since the intent is to leave the directory intact if it is non-empty (i.e. has unexpected contents), the Win32 code added no value over the cross-platform `QDir::rmdir()`. Replaced with a single `qWarning()` on failure.

---

## 2026-04-09 - Bulk import progress bar stayed at 0; single import caused OS "hung" reports

**Files:** `oscar/main.cpp`, `oscar/profileimporter.cpp`

**Symptom 1:** During bulk startup import, the progress bar value never advanced until an entire profile was
complete, making it appear frozen (especially for single-profile users where it went 0→100 with no
intermediate movement).

**Root cause:** The `progressChanged` signal lambda in `migrateFromOSCAR()` updated only the label text;
it never called `progress.setValue()`. The dialog range was `0..profileList.size()*100`, so only the
coarse per-profile increments at `i*100` and `(i+1)*100` moved the bar.

**Fix:** Changed dialog range to `0..100` (per-profile). The lambda now calls `progress.setValue(current)`
so the bar advances through all import stages (0→10→20…→100) for each profile. The label still shows
"n of n profiles" for overall context. Bar resets to 0 at the start of each new profile.

**Symptom 2:** During single-profile import (File → Profiles → Import Profile from OSCAR) the UI could
appear hung to the OS, because `QApplication::processEvents()` was only called every 10 sessions and
there were no UI yields before the `calculateSummaries()`, `profile->Save()`, or `dbMgr.commit()` calls.

**Fix (first pass):** Reduced session update interval from 10→5. Added `reportProgress()` calls at key
phases.

**Further fix:** Added `QApplication::processEvents()` per file in `copyDirectoryRecursively` to keep
the UI alive while the Backup folder is being copied. Split the single large import transaction into
three smaller ones: (1) profile metadata only (committed before session loading), (2) one transaction
per machine for session data (so each commit is small), (3) calculateSummaries + profile->Save(). On
failure after partial commits `ProfileRepository::remove()` cascade-deletes the partial profile from
the database before filesystem cleanup.

---

## 2026-04-09 - Statistics page shows labels but no data columns (regression from 4bbbf9b8)

**Files:** `oscar/statistics.cpp`

**Symptom:** Statistics page shows row headings (Total Days, AHI, Leak, etc.) but all data columns are absent. Reported against imported 1.7.1 profiles but affects all profiles.

**Root cause:** Commit `4bbbf9b8` changed `if (row.calc == SC_HEADING)` to `if (row.calc == SC_HEADING && summaryInfo.size() > 0)` to guard against a crash on zero-session profiles. However, `summaryInfo.size()` returns `numDisabledsessions` (count of disabled sessions), which is 0 for any profile with all sessions enabled (the normal case). The heading block — which builds the `periods` list of column time-ranges — was therefore never entered, leaving `periods` empty and all data rows with no columns.

**Fix:** Restored `if (row.calc == SC_HEADING)` and added a proper date-validity guard: if `summaryInfo.first()` or `summaryInfo.last()` is invalid (true only for a zero-data profile), set `skipsection = true` and `continue`. This preserves the crash protection while restoring column display for normal profiles.

---

## 2026-04-09 - OAuth2 crash: delete-sender-in-slot in OAuth2Handler::onNewConnection

**Files:** `oscar/network/oauth2_handler.cpp`

**Symptom:** OSCAR crashed immediately after the user completed both Google OAuth web pages. Stack trace showed `doActivate<false>` / `QAbstractSocketPrivate::emitReadyRead`.

**Root cause:** `onNewConnection()` connected a lambda to `QTcpSocket::readyRead`. The lambda called `stopServer()`, which does `delete m_server`. Because `QTcpServer::nextPendingConnection()` returns a socket whose parent is the server, deleting the server also deleted the socket. This happened while Qt's signal machinery was still executing the `readyRead` emission on that socket — destroying the sender mid-emission crashes `doActivate`.

**Fix:** Moved `stopServer()` and `exchangeCodeForToken()` (and all validation) into a `QTimer::singleShot(0, ...)` callback so they execute after the `readyRead` slot returns and signal emission is complete.

---

## 2026-04-09 - OSCAR 1.x → 2.0 import: missing layoutSettings and preferences ✓ tested

**Files:** `oscar/profileimporter.cpp`, `oscar/profileimporter.h`

**Symptoms:**
1. After importing an OSCAR 1.x profile, the `layoutSettings` folder (saved graph layouts) was missing in OSCAR 2.0.
2. Session settings such as "do not import sessions before date" (`IgnoreOlderSessions`/`IgnoreOlderSessionsDate`) reverted to defaults after import.
3. App-level tab preferences (`OpenTabAtStart`, `OpenTabAfterImport`) were not carried over.

**Root causes:**

1. **layoutSettings not copied** — The `layoutSettings` folder lives in the OSCAR 1.x data root (two levels above the profile folder), not inside the profile folder. The importer only handled files within the profile folder. Fix: new `copyLayoutSettings()` method computes the source data root and copies files not already present in the destination.

2. **Profile preferences overwritten on save** — `profileimporter.cpp` correctly saved source preferences to the "session"/"cpap"/etc. DB categories via `prefRepo.saveAllPreferences()`. However, the subsequent `profile->Save()` call invoked `saveProfilePreferencesToDatabase()` on the *destination* profile (which held defaults), writing those defaults to the "profile" DB category. Since `loadExtendedDataFromDatabase()` treats the "profile" category as authoritative (it runs last and overwrites), source settings were lost on next open. Fix: copy `sourceProfile->p_preferences` into `profile->p_preferences` (skipping `DataFolder`, `UserName`, `VersionString`) before `profile->Save()`.

3. **App-level preferences not migrated** — All user-configurable settings stored in `AppWideSetting` / `p_pref` (Preferences.xml at the data root) — including `AutoOpenLastUsed`, `OpenTabAtStart`, `OpenTabAfterImport`, graph appearance settings, etc. — are not inside the profile and were not migrated. Fix: new `migrateAppSettings()` method opens the source `Preferences.xml` and bulk-copies all keys to the live `p_pref`, skipping OSCAR 2.0-specific tracking values (`VersionString`, `Profile`, `Skipped*Version`, `UpdatesLastChecked`).

---

## 2026-04-08 - Qt5→Qt6 locale regression in date/time display

**Files:** `oscar/overview.cpp`, `oscar/daily.cpp`, `oscar/Graphs/gGraphView.cpp`

**Symptom:** In Qt6, `QDateTime::toString(format)` uses the C locale, so month/day names (MMM, MMMM, dddd) always appear in English regardless of the user's system locale.

**Findings (audit 2026-04-08):**

1. **overview.cpp:445** — `dt.toString("dd MMM yyyy (dddd)")` — date label on Overview page.
2. **daily.cpp:2482** — `dt.toString("MMM dd HH:mm:ss.zzz")` — date display on Daily page.
3. **gGraphView.cpp:885** — `dt.toString("MMM dd yyyy")` — log panel entry.
4. **gGraphView.cpp:1755,1757** — `st/et.toString("d MMM...")` — date range label (same-year and different-year cases).
5. **gGraphView.cpp:1771,1773** — `st/et.toString("d MMM...")` — range string with time (same/different year).
6. **gGraphView.cpp:1785** — `st.toString(tr("d MMM yyyy [ %1 - %2 ]")...)` — single-day range string.

**Fix:** Replaced all `dt.toString(fmt)` calls with `QLocale().toString(dt, fmt)`. `QLocale()` respects the default locale set by `QLocale::setDefault()` in `translation.cpp`.

---

## 2026-04-08 - i18n fixes from codebase audit

**Files:** `oscar/SleepLib/profiles.cpp`, `oscar/mainwindow.cpp`, `oscar/profileselector.cpp`, `oscar/overview.cpp`, `oscar/exports/report_exporter.cpp`, `oscar/translation.cpp`

**Findings (audit 2026-04-08):**

1. **profiles.cpp:842** — `QMessageBox::warning` string not wrapped in `tr()` (inside commented-out block; fixed for if it is re-enabled).
2. **mainwindow.cpp:554** — `"Opening " + profileName` not translated; changed to `tr("Opening %1").arg(profileName)`.
3. **profileselector.cpp:770** — `"Something went wrong"` not wrapped in `tr()`.
4. **overview.cpp:152** — `"[Date Widget]"` placeholder not wrapped in `tr()`.
5. **report_exporter.cpp:222,228** — Date format hardcoded as `"MM/dd/yyyy"` (US-only); replaced with `QLocale().dateFormat(QLocale::ShortFormat)`.
6. **translation.cpp:158** — Multi-language window title intentionally not `tr()`-wrapped (dialog appears before any language is loaded); added comment documenting this.

---

## 2026-04-08 - DST timestamp fixes from codebase audit

**Files:** `oscar/SleepLib/loader_plugins/bmcDataParsing.cpp`, `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp`, `oscar/SleepLib/loader_plugins/resmed_loader.cpp`, `oscar/SleepLib/machine.cpp`

**Findings (audit 2026-04-08):**

1. **bmcDataParsing.cpp:24** (`DecodeDate`) — `QDateTime(date, QTime(12,0,0))` had no explicit timezone; added `Qt::LocalTime`.
2. **resmed_loader.cpp:1453** — Mask-off end time used `QDateTime::addDays(1)` (adds exactly 86 400 s); on spring-forward night this overshoots into the next OSCAR day. Fixed by using `QDate::addDays(1)` then reconstructing the QDateTime with `EDFInfo::localNoDST`.
3. **machine.cpp:373, 275** — `AddSession` and `pickDate` compared `QTime time` against `split_time`; during fall-back the same local time appears twice and cannot be disambiguated. Replaced with epoch comparison: `s->first() < QDateTime(d2.date(), split_time, Qt::LocalTime).toMSecsSinceEpoch()`. Removed now-unused `QTime time` local variable; surviving reference at line 385 replaced with `d2.time()`.
4. **bmcDataParsing.cpp:69, 126** — In-progress and historic session `EndTimestamp` used `StartTimestamp.addDays(1)` (adds 86 400 s). Fixed with `QDate::addDays(1)` then reconstruct preserving same time-of-day and timeSpec.
5. **bmcG3xDataParsing.cpp:1902** — Same `addDays(1).addSecs(-1)` pattern on QDateTime. Fixed using `QDate::addDays(1)` then construct noon on next date minus 1 s. Also added explicit `Qt::LocalTime` to StartTimestamp construction.

**Root cause (all):** `QDateTime::addDays(N)` adds exactly N×86 400 seconds; on DST transition nights the local noon-to-noon window is 82 800 s (spring-forward) or 90 000 s (fall-back), so the computed boundary falls in the wrong OSCAR day. `QDate::addDays(N)` performs calendar arithmetic and is DST-safe.

---

## 2026-04-08 - Crash/stale-pointer fixes from codebase audit

**Files:** `oscar/SleepLib/loader_plugins/bmcDataParsing.cpp`, `oscar/SleepLib/deviceconnection.cpp`, `oscar/statistics.cpp`

**Findings (audit 2026-04-08):**

1. **bmcDataParsing.cpp:590** — `session->Waveforms.first()` called when `Waveforms` could be empty on a mid-loop split. Added `&& !session->Waveforms.isEmpty()` guard.
2. **bmcDataParsing.cpp:603** — Same crash at loop exit; added same guard to the post-loop `if`.
3. **bmcDataParsing.cpp:567–580** — `foundSettings` stored a raw pointer into `AllMachineSettings` (via `&msettings` in range-for and `&AllMachineSettings.last()`), which is invalidated by container reallocation. Replaced with copy-by-value.
4. **deviceconnection.cpp:426,438** — `SetValueEvent::id()` and `GetValueEvent::id()` called `m_keys.first()` unconditionally; default constructor leaves `m_keys` empty. Added isEmpty() guard returning `QString()`.
5. **statistics.cpp:1441** — `summaryInfo.first()/last()` called inside `SC_HEADING` branch with no check for empty; crashes on a profile with zero recorded nights. Added `summaryInfo.size() > 0` guard.

**Fix:** Guards added at each call site; `bmcDataParsing` settings lookup converted from pointer to value.

---

## 2026-04-05 - Import: Summary-only sessions show AHI/H = 0.00 after import from OSCAR 1.7.1

**Files:** `oscar/SleepLib/session.cpp` (`LoadSummaryFromFile`)

**Symptom:** After importing OSCAR 1.7.1 data, summary-only ResMed sessions (those without a `.001` events file) displayed AHI = 0.00 and Hypopnea = 0.00. Sessions with full event data imported correctly.

**Root cause (two interacting issues):**
1. `StoreSummaryStatistics()` in `resmed_loader.cpp` stores fractional event counts for summary-only sessions: `setCount(CPAP_Hypopnea, R.hi * hours)` produces e.g. 0.997 (not an integer). `SessionChannelData::count` and `SessionSummaryData::hypopneaCount` are `int`, so 0.997 truncates to 0 on storage.
2. OSCAR 1.7.1 wrote `.000` files at version ≤ 14. `LoadSummaryFromFile()` only reads `m_availableChannels` for version ≥ 15 (version 14 had a serialization bug). With an empty `m_availableChannels`, `StoreToDatabase()` skips the session_channels block entirely — `m_cph` (event rates, stored correctly) is never written to the database.
3. `LoadFromDatabase()`'s summary-only restore block can recover counts from `m_cph × hours`, but only if session_channels was stored. With no session_channels for CPAP_Hypopnea, the condition `m_cph.contains(CPAP_Hypopnea)` is false, the block is skipped, and the count stays 0.

**Fix:** At the end of `LoadSummaryFromFile()`, if `m_availableChannels` is still empty after parsing, rebuild it from the keys in `m_cnt` and `m_cph`. This ensures `StoreToDatabase()` creates session_channels records (with correct `cph` = 0.10), enabling the existing restore block in `LoadFromDatabase()` to compute `qRound(cph × hours)` = 1 on reload.

**Note:** Sessions already imported with broken data must be re-imported.

---

## 2026-04-02 - AirSense 11: Wrong icon shown on Welcome page

**Files:** `oscar/SleepLib/loader_plugins/resmed_loader.cpp`, `oscar/SleepLib/machine.cpp`

**Symptom:** Welcome page showed the default CPAP icon instead of the AirSense 11 icon.

**Root cause (3 parts):**
1. `scanProductObject()` (JSON path, `Identification.json`): used `indexOf("11")/left(idx+2)` to derive `info.series`, producing `"AirSense11"` (no space). The icon hash is keyed on `"AirSense 11"` (with space), so lookup failed and the default icon was returned.
2. `parseIdentLine()` (`.tgt` path): had no checks for `STR_ResMed_AirSense11` or `STR_ResMed_AirCurve11`; these models fell through to the S9 `else` branch.
3. `Machine::SaveToDatabase()`: when an existing machine record was found, only `machineId` was updated — `series` and other info fields were never refreshed, so stale values from old loader bugs persisted across imports.

**Fix:**
- `scanProductObject()`: replaced the `indexOf` hack with explicit `contains()` checks (case-insensitive) for each series string, matching the same constants used in the icon hash.
- `parseIdentLine()`: added `contains()` checks for AirSense 11 and AirCurve 11 before the AirSense 10 checks; all checks made case-insensitive; also handle no-space form (`"AirSense11"`).
- `SaveToDatabase()`: now updates `series`, `model`, and `modelNumber` in the existing record if any have changed, so a re-import from SD card self-corrects stale database entries.

---

## 2026-03-30 - G3X: Periodic breathing duration wrong (uint16 vs uint32)

**Files:** `bmcG3xDataParsing.cpp`

**Symptom:** PB episode durations were ~28s and ~23s instead of the correct ~159s and ~154s as reported by PAP-Link.

**Root cause:** EVT type 0x09 (PB marker) encodes duration as a uint32 at offset 0x1C (low 16 bits) combined with offset 0x1E (high 16 bits).  The code was reading only the low 16 bits (`value2`) and dividing by 1000, giving 28034/1000≈28s and 23390/1000≈23s.  The correct uint32 LE values are 159106 ms and 154462 ms (≈2:39 and ≈2:34 matching PAP-Link exactly), confirmed via raw byte dump: bytes [0x82,0x6D,0x02,0x00] = 0x00026D82 = 159106 and [0x5E,0x5B,0x02,0x00] = 0x00025B5E = 154462.

**Fix:** In `kG3xEvtTypePBMarker` case, compute `durationMs = static_cast<quint32>(value2) | (static_cast<quint32>(ReadUInt16LEPtr(rec, 0x1E)) << 16)` before dividing by 1000.

---

## 2026-03-27 - G3X: Flow limitation bars misaligned and wrong width

**Files:** `bmcDataParsing.h`, `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`

**Symptom:** FL events displayed as a near-invisible line when zoomed in (200 ms wide), with no visual correlation to individual breaths in the flow waveform.

**Root cause (two bugs):**
1. **Wrong width:** Bar was bookended at ±100 ms, giving a 200 ms display width.  The EVT value2 field (which for respiratory events carries duration in milliseconds) was read but not stored or used for FL events.  Confirmed via Python analysis of Kavolodin EVT data (5145 FL events): value2 range 0.36–3.98 s, mean 1.88 s — the device-reported inspiration duration.
2. **Wrong direction:** The bar was initially plotted forward from `ts` (correct), then changed to backward (`ts − dur` to `ts`) based on a misread of the flow waveform.  With backward plotting the bar covered expiration instead of inspiration.  Confirmed visually: `ts` is the trough at end-of-expiration / start-of-inspiration; the bar must run forward to cover the inspiratory peak where FL occurs.

**Fix:**
- Added `DurationMs` field to `BmcFlowLimitEvent`; EVT parser stores `value2` there.
- `bmc_loader.cpp` plots each FL event from `ts` to `ts + DurationMs` (forward), with a zero bookend 100 ms before `ts` to isolate from the previous event.
- Validated: FL bars now align with the inspiratory peak; severity 1/2/3 visually correlates with degree of inspiratory flattening in the flow waveform.

---

## 2026-03-25 - G3X: EVT scan misses all records when EventStartOffset is not 32-byte aligned

**Files:** `bmcG3xDataParsing.cpp`

**Symptom:** For at least one known device (Lijunjun G3 B20A, SC.75), all respiratory events (apneas, hypopneas, flow limits) are absent from the imported day because the EVT parse loop finds zero valid records.

**Root cause:** The IDX `EventStartOffset` is not guaranteed to fall on a 32-byte record boundary. When it falls mid-record (e.g., 16 bytes in), the parse loop starting exactly at `EventStartOffset` and stepping in 32-byte increments hits the second half of every record — none of which start with the required AE AA magic bytes. All records are silently skipped.

**Fix:** Round `EventStartOffset` down to the nearest 32-byte boundary before seeking. The existing AE AA magic check discards the partial record fragment at the start.

---

## ~~2026-03-25 - G3X: Replace 0x44-based PB with AASM computed PB from 0x0C breath markers~~

~~**Files:** `bmcG3xDataParsing.cpp`, `bmcg3x_loader.h`~~

~~**Symptom:** Periodic breathing was never shown in OSCAR for any G3X device. Firmware SC.72 (JCCPAP/Luna G3X) emits no EVT 0x44 records at all. On SC.74+/SC.75 the 0x44 records had not been validated and PB output was explicitly suppressed.~~

~~**Root cause:** PB detection depended entirely on firmware-emitted 0x44 event records, which are absent on SC.72. No universal PB signal existed.~~

~~**Fix:** Replaced 0x44-based detection with an AASM algorithm applied to device-classified CSA (central apnea) events from `rawRespEvents`. Each event carries a device-measured duration; startTime is derived as endTime − duration. The algorithm groups consecutive central apneas (≥3 s each) separated by ≤20 s of normal breathing into PB episodes (≥3 qualifying apneas per episode). Works on all firmware. `ExportPeriodicBreathing()` in `BmcG3xLoader` changed from `false` to `true`.~~

---

## 2026-03-25 - G3X: EVT 0x42 IPAP/EPAP fields reversed; bilevel devices showed wrong IPAP

**Files:** `bmcG3xDataParsing.cpp`

**Symptom:** For bilevel G3X devices (e.g. G3 B20A with 1 cmH2O pressure support), OSCAR displayed the same value for both `CPAP_IPAP` and `CPAP_EPAP`, equal to EPAP. IPAP was never shown.

**Root cause:** The EVT 0x42 record carries two pressure fields: `value2` (offset 0x1C) = EPAP, `unk1e` (offset 0x1E) = IPAP. The parser was treating `value2` as both IPAP and EPAP (CPAP assumption). Confirmed by comparing the Lijunjun G3 B20A (1 cmH2O PS): every 0x42 record has `unk1e` = `value2` + 100 (= +1.00 cmH2O).

**Fix:** Updated both the main EVT parse loop and the fallback scan in `ReadDateSession()` to read EPAP from `value2` and IPAP from `unk1e`. IPAP falls back to EPAP if `unk1e` is outside the plausible pressure range (handles pure CPAP devices where `unk1e` might be zero or absent).

---

## 2026-03-26 - G3X: Periodic breathing (EVT 0x44) suppressed; unknown-firmware warning added

**Files:** `bmc_loader.h`, `bmc_loader.cpp`, `bmcg3x_loader.h`, `bmcg3x_loader.cpp`

~~**Periodic breathing:** EVT type 0x44 ("PB") was being emitted to the CPAP_PB channel for all G3X sessions. The flag has not been validated against PAP-Link for any G3X firmware, and on SC.74+ firmware it produces clearly nonsensical output. Added `ExportPeriodicBreathing()` virtual method to `BmcLoader` (base returns `true` for legacy BMC). `BmcG3xLoader` overrides to `false`, which suppresses creation of the CPAP_PB event list entirely. The raw 0x44 records are still collected in the parser for future analysis.~~

**Unknown-firmware warning:** BMC G3X firmware version is read from the companion `.log` file (first 6 KB scanned for a null-terminated ASCII string starting with `"G3-2."`; offset varies by device — ~0x0420 in small G3 A20 logs, ~0x1420 in larger G3 B20A ring-buffer logs). Falls back to IDX offset `0x0345` (internal SC build string, e.g. `"G3-2.SC.72.01"`) if the `.log` is unavailable. The version is stored in `MachineInfo.properties["firmware"]` (added in `PeekInfo()`). In `Open()`, if the firmware string is non-empty and does not start with a known user-facing prefix (`"G3-2.11."` or `"G3-2.12."`, or contain `"SC.72"` / `"SC.74"` as IDX-fallback identifiers), a `QMessageBox::information` dialog is shown asking the user to send their SD card .zip to the OSCAR team. Import then continues normally.

---

## 2026-03-26 - G3X: Leak graph wrong/empty on firmware SC.74+ (Kavolodin / Patient 2); model string and firmware version also fixed

**Files:** `bmcG3xDataParsing.cpp`, `bmcDataParsing.h`

**Symptom:** On BMC G3 A20 machines with firmware SC.74+ (part-code prefix `880`, e.g. Kavolodin and Patient 2), the OSCAR leak graph showed a near-zero sparse line with only a handful of data points — clearly wrong. On the same machines, the machine model string displayed the internal part/config code (`"880A40383"`) instead of the product name (`"G3 A20"`). Firmware version was not recorded at all.

**Root cause (three bugs):**
1. **Leak field**: `kG3xOffsetLeak = 0x52A` is populated only in firmware SC.72 (prefix `110`). In firmware SC.74+ (prefix `880`) this field is always zero (0.33% non-zero, clearly noise). The correct leak field for SC.74+ is `0x568` (total mask leak; continuously populated at ~15–17 L/min baseline, scale 0.16 L/min per raw unit).
2. **Model string**: IDX offset `0x048` holds the internal part/config code (`"110A40113"` or `"880A40383"`), not the product name. The product name (`"G3 A20"`) is at IDX offset `0x100`.
3. **Firmware version**: Not read at all. The user-facing version string (e.g. `"G3-2.11.02.33"` or `"G3-2.12.54.13"`) is in the companion `.log` file; the internal SC build string (e.g. `"G3-2.SC.72.01"`) is at IDX offset `0x0345` as a fallback.

**Fix:**
- Added `FirmwareVersion` field to `BmcMachineInfo` in `bmcDataParsing.h`.
- Updated `ParseMachineInfo()` to read model from IDX `0x100` (with fallback to `0x048` if absent) and firmware version from the `.log` file (scan first 6 KB for `"G3-2."` prefix; offset varies by device). Falls back to IDX `0x0345` if `.log` is unavailable.
- Added `kG3xOffsetAlternateLeak = 0x568` constant.
- Added Phase 3.5 adaptive leak-field probe in `ReadDateSession()`: samples first 200 waveform packets; if fewer than 10% have non-zero `0x52A`, selects `0x568` for the entire day. This is robust to future firmware versions without needing hard-coded part-code lookup.
- Updated waveform loop to use `leakFieldOffset` (determined by Phase 3.5) instead of always using `kG3xOffsetLeak`.
- Scale factor `0.16 L/min per raw unit` applies to both fields.

---

## 2026-03-25 - G3X: Respiratory event duration corrected to value2 (milliseconds)

**Files:** `bmcG3xDataParsing.cpp`
**Root cause:** All respiratory event types (0x01–0x09, 0x0A) were using `value1` as duration in seconds. `value1` is actually a wrapping counter (0–999) with no duration information; `value2` encodes duration in milliseconds. Confirmed 2026-03-25 by extracting value2/1000 for a full OSCAR day and comparing against OSCAR waveform graphs: OSA durations of 27.7 s, 15.6 s; hypopnea 10.9 s; UA 11.5 s; RERA 11.3 s — all clinically plausible. `0x0B` value2/1000 = 0.4–1.1 s (sub-second, not event duration).
**Fix:** `G3xRawRespEvent.Value1` renamed to `Value2Millis`; raw event collection changed to store `value2`; duration = `round(value2 / 1000.0)` seconds, clamped to [10, 180]. RERA fixed-duration override removed.

---

## 2026-03-25 - G3X: Unclassified hypopnea (0x01) added to CPAP_Hypopnea channel

**Files:** `bmcG3xDataParsing.cpp`
**Feature:** EVT message type `0x01` confirmed as unclassified hypopnea (2026-03-25, Patient 2 B33BF114508). Added `kG3xEvtTypeUH = 0x01` constant; added to EVT loop and Phase 2 mapping → `BmcRespiratoryEventType::HYP`. Duration uses value2/1000 s clamped to 10–180 s. 58 records observed across Patient 2 nights; absent from JCCPAP.

---

## ~~2026-03-26 - G3X: PB detection expanded to include CH (central hypopnea) events~~

~~**Files:** `bmcG3xDataParsing.cpp`~~
~~**Symptom:** JCCPAP-2 (SC.72 firmware) periodic breathing episode missed. SC.72 emits one CSA plus generic/central hypopneas during a PB event; CSA-only clustering cannot form a ≥3-apnea cluster from one CSA.~~
~~**Root cause:** Phase 2b PB clustering filtered only on `kG3xEvtTypeCSA` (0x04). On CPAP devices CH (0x08, central hypopnea) also indicates absent/reduced central drive and is part of the same periodic-breathing cluster.~~
~~**Fix:** Expanded Phase 2b filter to include `kG3xEvtTypeCH` (0x08) alongside CSA. CH events continue to be reported as hypopneas in OSCAR (unchanged); only the PB episode detection input changes.~~

---

## 2026-03-24 - G3X: RERA (Respiratory Effort Related Arousal) added to CPAP_RERA channel

**Files:** `bmcDataParsing.h`, `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`
**Feature:** EVT message type `0x0A` confirmed as RERA by PAP-Link correlation on five independent events across four JCCPAP nights (2026-02-12, 2026-02-21, 2026-02-22 ×2, 2026-02-24). All five PAP-Link RERA timestamps matched a `0x0A` record within ±1 minute; two RERAs on 2026-02-22 corresponded exactly to the two `0x0A` records on that night.
**Implementation:** `RERA` added to `BmcRespiratoryEventType` enum. `kG3xEvtTypeRERA = 0x0A` constant added. Phase 2 maps `0x0A` → `BmcRespiratoryEventType::RERA`. Written to `CPAP_RERA` (EVL_Event) in `bmc_loader`. Duration initially set to a fixed 10 s; updated 2026-03-25 to use value2/1000 s (observed ~11.3 s, consistent with PAP-Link's 10-second display).

---

## 2026-03-23 - G3X: Flow Limitation (Mild/Moderate/Severe) added to CPAP_FLG channel

**Files:** `bmcDataParsing.h`, `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`
**Feature:** EVT message types 0x0E (Mild), 0x0F (Moderate), 0x10 (Severe) confirmed as flow limitation events by PAP-Link alignment on two independent sessions (G3X-2 2026-02-20: 376/22/6 vs PAP-Link many/21/5; JCCPAP 2025-12-20: 181/11/5). These types are absent from devices with no FL.
**Implementation:** `BmcFlowLimitEvent` struct (Timestamp + Grade 1/2/3) collected in EVT loop, assigned to sessions, written to `CPAP_FLG` (EVL_Event, gain=1.0) with ±100 ms zero bookends to create isolated bars. `setPhysMin/Max(0.0/3.0)` anchors the y-axis so mild (grade 1) bars are visible. Statistics (min=0, median=0, 95th=0, max=3) are correct — FL is sparse (~2% of session time).

---

## 2026-03-23 - G3X: CPAP_Pressure artificially quantised to 0.5 cmH2O steps

**Files:** `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`, `bmc_loader.h`, `bmcg3x_loader.h`
**Symptom:** The G3X Pressure graph had only 0.5 cmH2O resolution despite the machine recording pressure at 0.01 cmH2O precision.
**Root cause:** `Raw.IPAP`/`Raw.EPAP` were written using `PressureHundredthsToRawHalfCm()`, which rounds to the nearest half-cmH2O to match the legacy BMC packet format. The `bmc_loader` then applied a fixed gain of 0.5. This was appropriate for the legacy machine (which genuinely only records half-cmH2O steps) but threw away the G3X's finer precision.
**Fix:** Added virtual `PressureChannelGain()` to `BmcLoader` (default 0.5 for legacy). G3X overrides to 0.01. G3X now stores raw hundredths of cmH2O directly in `Raw.IPAP`/`Raw.EPAP`, bypassing `PressureHundredthsToRawHalfCm()`. `bmc_loader` uses `PressureChannelGain()` for all three pressure event lists.

---

## 2026-03-22 - BMC Legacy: IPAP/EPAP fields mis-labelled in packet struct

**Files:** `bmcDataParsing.h`, `bmcDataParsing.cpp`
**Symptom:** `BmcWaveformPacketStruct` had `IPAP` at offset 0x04 and `EPAP` at 0x06, which was backwards. A runtime `std::swap` was papering over the error.
**Root cause:** The documentation was incorrect — 0x04 is always the smaller value (EPAP) and 0x06 is always the larger (IPAP), confirmed across all packets on all three test machines.
**Fix:** Swapped the field names in the struct declaration in `bmcDataParsing.h` (EPAP at 0x04, IPAP at 0x06). Removed the `std::swap` calls and their comment from `bmcDataParsing.cpp`.

---

## 2026-03-20 - BMC Legacy: MaskPressure waveform channel showed garbage data

**Files:** `bmc_loader.h`, `bmc_loader.cpp`
**Symptom:** CPAP_MaskPressure waveform channel showed unrecognizable data. Separate BMC_PressureWave chart was also displayed.
**Root cause:** `Raw.MaskPressure` (kBmcExtendedWaveformSamples = 50 elements) is never initialized in the legacy BMC packet constructor — it contains garbage. The actual mask pressure waveform is in `Raw.PressureWave` (25 samples at packet offset 0x08, ÷10 = cmH2O). The separate BMC_PressureWave chart is redundant; mask pressure belongs in the standard CPAP_MaskPressure channel.
**Fix:** Changed `wMaskPressure->AddWaveform` in `bmc_loader.cpp` to use `bmcWaveform.Raw.PressureWave`. Changed `ExportPressureWaveform()` in `bmc_loader.h` base class to return `false` (no separate chart).

---

## 2026-03-20 - BMC Legacy: PressureWaveform gain wrong by factor of 10

**Files:** `bmc_loader.h`
**Symptom:** The pressure waveform displayed values 10× too high (e.g., 85 cmH2O instead of 8.5 cmH2O).
**Root cause:** `PressureWaveformGain()` returned `1.0`. Raw waveform values (int16) store pressure in 0.1 cmH2O units; gain must be `0.1` to display cmH2O. Confirmed by cross-referencing raw PressureWave values against known EPAP/IPAP settings across three SD card images (BMC Luna, BMC Luna G3, BMC G3).
**Fix:** Changed `PressureWaveformGain()` return value in `bmc_loader.h` from `1.0` to `0.1`.

---

## 2026-03-20 - BMC Legacy: Pulse rate / SpO2 from optional oximeter accessory

**Files:** `bmcDataParsing.cpp`
**Note:** SpO2 (0xCC) and pulse rate (0xCE) are zero on all currently tested SD images because those machines did not have the oximeter accessory installed. The fields are correctly mapped; data will appear when an oximeter-equipped machine's card is loaded. Both channels are expected to be present or absent together. Offset 0xBE was briefly tried as an alternate pulse source but is mechanical, not physiological.

---

## 2026-03-20 - BMC Legacy: Ti/Te computation produced values ~10× too small

**Files:** `bmc_loader.cpp`
**Symptom:** CPAP_Ti showed 95% of values below 0.21 seconds — physiologically implausible for normal breathing (~0.8–1.2 s expected inspiratory time at 12–16 BPM).
**Root cause:** `IERatioMapped` is stored as a percentage (0–100) by the `BmcWaveformPacket` constructor, but the Ti/Te block divided by 1000 (treating it as permille). For example, `IERatioMapped = 23` at RR = 12 produced Ti = (60/12) × (23/1000) = 0.115 s instead of the correct 1.15 s.
**Fix:** Changed divisors in `bmc_loader.cpp` lines 519–520 from `1000.0` to `100.0`, and `(1000 - IERatioMapped)` to `(100 - IERatioMapped)`.

---

## 2026-03-20 - BMC G3X: CPAP_Ti / CPAP_Te / I:E channels based on unconfirmed offsets

**Files:** `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`, `bmc_loader.h`, `bmcg3x_loader.h`
**Symptom:** G3X exported CPAP_Ti, CPAP_Te, CPAP_IE, BMC_IE_Ratio channels derived from waveform packet offsets 0x074 and 0x07E, which were assumed to be inspiration/expiration time in centiseconds.
**Root cause:** Analysis showed the sum of 0x074 + 0x07E is near-constant (~565 cs) regardless of respiratory rate, and neither field correlates with RR, tidal volume, minute ventilation, or pressure. They are not patient Ti/Te. The original label was incorrect.
**Fix:** Added `ExportTimingChannels()` virtual method to `BmcLoader` (default true). Overridden to false in `BmcG3xLoader`. Guarded creation and population of `wInspTime`, `wExpTime`, `wIEValue`, `wIERatio` event lists in `bmc_loader.cpp`. Suppressed IERatioMapped computation from 0x074/0x07E in `bmcG3xDataParsing.cpp`. Also consolidated duplicate Ti/Te write block into one guarded block.

---

## 2026-03-18 - Daily calendar navigation bar doesn't highlight on window focus

**Files:** `oscar/daily.ui`
**Symptom:** In OSCAR 1.7.1 (Qt 5), the calendar's month/year navigation bar changed from black-on-gray to white-on-blue when OSCAR had window focus. In 2.0 (Qt 6), it stays black-on-gray regardless of focus.
**Root cause:** Qt 5 applied the system highlight color to the navigation bar automatically. Qt 6 no longer does this, and the explicit stylesheet in `daily.ui` overrides any default behavior. Qt stylesheets don't support a `:focus` pseudo-state on parent widgets, so restoring this would require handling `QEvent::WindowActivate`/`WindowDeactivate` in code to swap the stylesheet.
**Status:** Noted; no fix applied.

---

## 2026-03-18 - Preferences dialog more vertically spread out than Qt5/Windows 10

**Files:** Preferences `.ui` file
**Symptom:** Preferences dialog is noticeably taller on Qt 6 / Windows 11 compared to Qt 5 / Windows 10.
**Root cause:** Not a bug. Qt 6 uses larger default layout margins, spacing, and widget padding. Windows 11's Segoe UI font renders with slightly different metrics, and native controls are taller for touch-friendly sizing. The combined effect makes the same `.ui` layout taller.
**Status:** Noted; no fix applied. Could be tightened via layout margins/spacing or stylesheet, but that risks affecting other platforms.

---

## 2026-03-18 - Crash on application exit

**Files:** `oscar/main.cpp`, `oscar/mainwindow.cpp`
**Symptom:** OSCAR crashes during shutdown in `QGuiApplication::~QGuiApplication` every time the application exits.
**Root cause:** Two `QApplication` instances existed simultaneously. A `QApplication app` was created at the top of `main()` (inside a Qt 6.5+ guard) to force light-mode color scheme, and then a second `QApplication mainapp` was created later for the actual event loop. Qt requires exactly one QApplication per process. When `mainapp` was destroyed on exit, it cleaned up global Qt state; then `app`'s destructor tried to clean the same state again, crashing in `QGuiApplication::~QGuiApplication`.
**Fix:** Removed the first `QApplication app` and moved the `setColorScheme(Qt::ColorScheme::Light)` call to `mainapp` immediately after its construction. Also restructured shutdown: (1) moved `Profiles::Done()` and `DestroyGraphGlobals()` from `closeEvent` to `main.cpp` after explicit `delete mainwin` so globals outlive all widgets; (2) un-parent loaders from MainWindow in `closeEvent` to avoid dual-ownership double-free; (3) removed `QCoreApplication::quit()` from `MainWindow::~MainWindow()`.

---

## 2026-03-18 - Crash when importing/exporting journal with no profile open

**Files:** `oscar/mainwindow.cpp`
**Symptom:** OSCAR crashes when user selects Journals/Import Journal (or Export) with no profile/database open.
**Root cause:** `profilePath()` (line 2528) dereferences `p_profile` without a null check. When no profile is loaded, `p_profile` is null, causing a crash in `QHash::contains`.
**Fix:** Added null guard for `p_profile` in `profilePath()`. Added early-return with warning message in both `on_actionImport_Journal_triggered()` and `on_actionExport_Journal_triggered()` when `p_profile` is null.

---

## 2026-03-18 - BMC G3X: mask-on hours now separate from hours-used

**Files:** `oscar/SleepLib/loader_plugins/bmcg3x_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.cpp`, `oscar/SleepLib/session.cpp`

**Symptom:** `session_summaries.hours_used` and `session_summaries.mask_on_hours` were identical; mask-on hours did not reflect the fact that the patient removed the mask before the machine was turned off.

**Root cause (two separate bugs):**
1. `Session::StoreSummaryToDatabase()` computed `hoursUsed = hours()`, but `hours()` returns MaskOn-slice time when slices are populated, making `hoursUsed == maskOnHours`.
2. The mask-off detector in `BmcG3xLoader::findStableEndMs` used a trailing high-leak scan; after mask removal the BMC G3X ramps pressure to minimum so leak returns to zero — the last packet has `Raw.Leak = 0` and the detector never triggered.

**Fix:**
- `session.cpp`: Changed `hoursUsed` to use `(s_last - s_first) / 3600000.0` (raw session span) instead of `hours()`.
- `bmcg3x_loader.h`: Replaced leak-based mask-off detection with flow-based detection. Scans backward through `Raw.Flow[50]` samples to find the last packet with any sample exceeding the noise floor (±25 raw). If the trailing no-flow period is ≥ 300 seconds, that packet's timestamp is the mask-off point. Validated on reference night: mask-off detected at 08:37:43, giving maskOnHrs = 8.16 vs totalHrs = 8.60 (≈ 27-minute difference).

---

## 2026-03-17 - Dark mode illegibility fixed in three windows

**Files:** `oscar/profileselector.cpp`, `oscar/daily.ui`, `oscar/oximeterimport.cpp`

**Symptom:** In system dark mode on Qt 6.2/6.4, three windows were illegible: the profile selector had a dark background, the daily page calendar widget had a dark background, and the oximeter import wizard showed white-on-white text.

**Root cause:** Qt 6.5+ provides `QApplication::styleHints()->setColorScheme()` to force light mode per-widget, but this API is unavailable on Qt 6.2/6.4. Without explicit palette overrides, these widgets inherited the system dark palette.

**Fix:**
- `profileselector.cpp`: Apply an explicit light `QPalette` (white base/window, black text) to `this` in the constructor.
- `daily.ui`: Extend the `QCalendarWidget` stylesheet with rules targeting `QAbstractItemView` (white background, black text) and `qt_calendar_navigationbar` (light grey background, black text).
- `oximeterimport.cpp`: Apply `WindowText`, `Text`, and `ButtonText` = black to `this->palette()` in the constructor so text is legible over the existing light gradient background.

---

## 2026-03-17 - Qt dialog/button translations added via `oscar_qt_*.ts` supplemental catalogs

**Files:** `oscar/translation.cpp`, `Translations/qt/oscar_qt_*.ts`, `Tools/generate_qt_dialog_translations.py`

**Symptom:** Standard Qt dialog strings such as `Open`, `Save`, `Cancel`, `Close`, and `QFileDialog` labels remained untranslated even when OSCAR itself was running in a translated language.

**Root cause:** OSCAR already had runtime support for loading supplemental `oscar_qt_*.qm` files, but the corresponding `.ts` source files did not exist. The runtime loader also truncated the language code to two letters, which prevented region-specific catalogs such as `zh_CN`, `pt_BR`, `es_MX`, and `en_UK` from being loaded by name.

**Fix:** Added trimmed Qt supplemental translation catalogs under `Translations/qt/` for every current OSCAR language, generated primarily from the local Qt 6.10.2 translation sources and supplemented with OSCAR-local strings/manual fallbacks where Qt did not ship a matching source catalog. Updated `initTranslations()` to try the full OSCAR language code first (for example `oscar_qt_pt_BR.qm`) and then fall back to the base language code if needed.

**Note:** Languages where Qt does not provide a matching source catalog (for example Afrikaans, Filipino, Greek, Norwegian Bokmal, Romanian, Thai) use best-effort supplemental translations and should be reviewed by native speakers when possible.

---

## 2026-03-17 — QMessageBox custom buttons not translated (Yes/Cancel in CPAP Data Located dialog)

**Files:** `oscar/mainwindow.cpp`, `Translations/*.ts` (27 language files)

**Symptom:** The "Yes" and "Cancel" buttons in the "CPAP Data Located" QMessageBox were not translated when OSCAR was set to a non-English language on an English OS.

**Root cause:** `tr("Yes")` and `tr("Cancel")` called in `mainwindow.cpp` look up translations in the `MainWindow` context only. These strings existed in other contexts in the `.ts` files (e.g. `QObject`, `RestoreDialog`) but were absent from the `MainWindow` context. Qt does not fall back across contexts.

**Fix:** Added "Yes" and "Cancel" to the `MainWindow` context in all 27 `.ts` files that had existing translations for those strings. Three files were not updated due to no existing translation: Czech, English (UK), Thai.

**Note:** QFileDialog button labels ("Open"/"Save"/"Cancel") remain untranslated — those are Qt-internal strings requiring `oscar_qt_xx.qm` files, which do not yet exist.

---

## 2026-03-17 — "Last Imported" column in profile selector not updating after SD card import

**Files:** `oscar/mainwindow.cpp`, `oscar/SleepLib/importcontext.cpp`

**Symptom:** The "Last Imported" column in the profile selector showed a stale date and was never updated after importing new data from an SD card.

**Root Cause:** `MachineInfo::lastimported` is set to `QDateTime::currentDateTime()` only when a `MachineInfo` is first constructed (new machine). For existing machines it is loaded from the database and never updated on subsequent imports. `Machine::SaveToDatabase()` returns early for already-registered machines without touching `lastImported`. Additionally, `ResmedLoader` calls `mach->AddSession()` directly (not via `new_sessions` or `ImportContext`), so there was no hook in the loader itself to catch the timestamp.

**Fix:** In `MainWindow::importCPAP()`, after `Open()` returns `c > 0` (sessions were imported) and inside the open DB transaction, iterate all machines in the profile matching the loader's name, set `m->info.lastimported = QDateTime::currentDateTime()`, and persist via `MachineRepository::update()`. This is the one place in the import flow guaranteed to run for all loaders regardless of how they add sessions. Also added the same fix in `ImportContext::Commit()` for the context-based path used by PRS1.

---

## ~~2026-03-15 — QFileDialog: DontUseNativeDialog required for button translation (design note)~~

~~**Files:** All call sites using `QFileDialog` throughout OSCAR.~~

~~**Background:** `QFileDialog::DontUseNativeDialog` was added to every `QFileDialog` call so that button labels (Open, Save, Cancel, etc.) are translated to the user's selected OSCAR language.~~

~~**Investigation:** We considered whether there was an alternative that would allow native OS dialogs to be used while still translating the buttons. There is not. Native dialogs are rendered entirely by the OS platform layer (e.g. COMDLG32 on Windows) and always use the OS locale. Qt has no mechanism to inject translated text into native dialogs on any supported platform. `QFileDialog::setLabelText()` is documented to have no effect on native dialogs on most platforms.~~

~~**Conclusion:** `DontUseNativeDialog` is the only correct cross-platform solution when the app language may differ from the OS language. The trade-off is that Qt-rendered dialogs have a slightly different appearance and may be marginally slower than native dialogs on some platforms.~~

---

## 2026-03-16 — BMC G3X: startup artifacts on Flow Rate, Pressure, and Pressure Trend graphs

**Files:** `oscar/SleepLib/loader_plugins/bmcg3x_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.cpp`

**Symptom:** At the left edge of the session view, Flow Rate showed a spike from y=0 to the first sample, Pressure showed a ramp from the APAP minimum (~4.0 cmH2O) up to the therapeutic value, and Pressure Trend showed the same ramp.

**Root cause:** The BMC G3X machine starts recording waveform packets immediately when powered on, before the patient puts on the mask. During this idle period, `PressureTrend` (0x76C) is held at the minimum APAP pressure (400 hundredths = 4.0 cmH2O). When the patient dons the mask, the APAP algorithm ramps pressure upward over several seconds to reach the therapeutic target. The session's `s_first` was set to the very first waveform packet, so all idle and ramp data was visible from the left graph edge.

**Fix:** Added virtual `findStableStartMs(BmcSession*)` to `BmcLoader` (base returns `StartTimestamp` unchanged). `BmcG3xLoader` overrides it with a two-phase scan: (1) skip the initial idle packets where `PressureTrend` is at the startup minimum, (2) from the first packet where pressure begins rising, advance until pressure stops rising (ramp peak). The resulting timestamp is passed to `session->really_set_first()` instead of `StartTimestamp`. The graph renderers naturally crop all earlier data: the waveform renderer skips samples before `s_first`, and the event renderer primes from the last pre-`s_first` event so step-function channels also start cleanly.

---

## 2026-03-15 — Viatom/Wellue import: SpO2 and Pulse Rate plots show "Plots Disabled" / empty after restart

**Files:** `oscar/SleepLib/loader_plugins/viatom_loader.h`

**Symptom:** After importing Viatom/Wellue oximeter data via Data → Import Viatom Data, the session appeared in the session list but SpO2 and Pulse Rate graphs showed "Plots Disabled". After restarting OSCAR, the same plots were simply empty. Debug log showed: `Session::LoadFromDatabase(): Session XXXXXXXXXX not found in database`.

**Root cause:** `Machine::SaveToDatabase()` rejects machines where both `info.serial` and `info.model` are empty (`"Cannot save machine without serial or model"`). In `ViatomLoader::newInfo()`, `model` was `QString()` (empty). `serial` is only set if the file's enclosing folder name is ≥9 characters long with the last 4 numeric (a Viatom device serial number pattern). When a user imports files from a non-serial-named folder (e.g. Downloads), both remain empty. The machine is never saved to the database, so `m_database_id` stays 0. `Session::Store()` skips all DB writes when the machine has no DB ID. After `TrashEvents()` clears in-memory data, `OpenEvents()` fails (machine not in DB), yielding 0 plot points → "Plots Disabled". On restart, `LoadSessionsFromDatabase()` finds no machine/sessions and nothing is shown.

**Fix:** Changed `newInfo()` in `viatom_loader.h` to provide a default `model` of `QObject::tr("Viatom Oximeter")`. This ensures the machine is always saved to the database even when no serial can be determined from the folder name, so session data is correctly stored and loaded.

---

## 2026-03-15 — MD300W1, Dreem, Somnopose, ZEO loaders: same empty model/serial bug as Viatom

**Files:** `oscar/SleepLib/loader_plugins/md300w1_loader.h`, `dreem_loader.h`, `somnopose_loader.h`, `zeo_loader.h`

**Symptom:** Same as the Viatom bug above — sessions would not persist to database after import.

**Root cause:** Same root cause. `newInfo()` returned empty `model` and empty `serial` in all four loaders. MD300W1 goes through the oximeter wizard; Dreem, Somnopose, and ZEO go through `importNonCPAP`. None populate serial from file data, so `Machine::SaveToDatabase()` always failed for them.

**Fix:** Added default model names: MD300W1 → `"MD300W1 Oximeter"`, Dreem → `"Dreem Headband"`, Somnopose → `"Somnopose"`, ZEO → `"Zeo Sleep Manager"`.

---

## 2026-03-12 — Statistics page month names not translated in Qt6

**Files:** `oscar/statistics.cpp`

**Symptom:** In Monthly report mode, column headers showing month names (e.g., "January", "February") were always displayed in English regardless of the active language. Worked correctly under Qt5.

**Root cause:** `QDate::toString(format)` behaviour changed between Qt5 and Qt6. In Qt5 it used the application's default locale for month/day names. In Qt6 it always uses the C locale (English). This is a documented Qt6 breaking change.

**Fix:** Changed `s.toString("MMMM<br>yyyy")` to `QLocale().toString(s, "MMMM<br>yyyy")` on line 1460, which explicitly uses the application locale and works correctly in both Qt5 and Qt6.

---

## 2026-03-12 — QDateEdit ignoreOlderSessionsDate displays incorrectly under translation

**Files:** `oscar/preferencesdialog.ui`, `Translations/Francais.fr.ts`

**Symptom:** The "Do not import sessions older than" date field in Preferences displayed strange/uneditable content when a non-English language (specifically French) was active.

**Root cause:** The `displayFormat` property (`dd MMMM yyyy`) of the `QDateEdit` widget was a plain `<string>` in the `.ui` file, so Qt's `uic` wrapped it in `tr()`. The French translation mapped it to `jj MMMM aaaa` (using French abbreviations jour/an), which are not valid Qt date format codes. Qt treated `jj` and `aaaa` as literal text, causing the widget to display literal "jj" and "aaaa" instead of numeric day and year.

**Fix:** Added `notr="true"` to the `displayFormat` string in `preferencesdialog.ui` so the format is never translated. Qt's `MMMM` token already renders month names in the application locale's language, so no translation of the format string is needed. Also corrected the French translation from `jj MMMM aaaa` to `dd MMMM yyyy`; this entry will become obsolete after the next `lupdate` run.

---

## 2026-03-12 — Purged data reappears after OSCAR restart

**File:** `oscar/SleepLib/session.cpp`, `Session::Destroy()`

**Symptom:** Purging a day (or all data) via Data → Advanced appeared to work, but after closing and restarting OSCAR the purged sessions were still present.

**Root cause:** `Session::Destroy()` only deleted the legacy `.000` summary and `.001` event files from disk, but never removed the session row from the SQLite database. Since summary file storage was moved to the database (`.000` writes are disabled), nothing was actually deleted — the files didn't exist and the DB rows remained intact. On restart OSCAR loaded sessions from the database, restoring all "purged" data.

**Fix:** Added `SessionRepository::remove(m_sessionrow_id)` call in `Session::Destroy()` before `unlinkSession()`. The DB schema uses `ON DELETE CASCADE` on all child tables (session_channels, session_slices, session_settings, event_lists, respiratory_events, etc.), so a single delete of the sessions row cleans up everything.

---

## 2026-03-12 — Rebuild-from-backup erases backup directory in BMC loaders

**Files:** `oscar/SleepLib/loader_plugins/bmc_loader.cpp`, `oscar/SleepLib/loader_plugins/bmcg3x_loader.cpp`, `Open()`

**Symptom:** "Rebuild from backup" for BMC and BMC G3x machines silently destroyed the backup data it was trying to import from, leaving the machine with no backup after the rebuild.

**Root cause:** Both `BmcLoader::Open()` and `BmcG3xLoader::Open()` unconditionally erase and recreate `mach->getBackupPath()` then copy `dirpath` into it. When rebuilding from backup, `dirpath` IS the backup directory, so the backup was wiped before any data was read.

**Fix:** Before the erase-and-copy block in both loaders, compare `QDir::cleanPath(dirpath)` to `QDir::cleanPath(backupPath)`. If they are equal, skip the backup creation entirely. `QDir::cleanPath()` normalises trailing slashes so the comparison is reliable.

---

## 2026-03-12 — Profile selector name column not translatable (locale name order)

**File:** `oscar/profileselector.cpp`, `oscar/profileselector.h`

**Symptom:** Names in the profile list and detail panel were always displayed in "Last, First" order regardless of locale. French users expect "First Last" order.

**Root cause:** Three of the four name-formatting call sites used bare `QString("%1, %2")` with no `tr()` wrapper, making them untranslatable. The fourth used `tr("Name: %1, %2")` but conflated the label and the name format in a single translation string, making it impossible for translators to change only the name order.

**Fix:** Added `ProfileSelector::formattedName(lastName, firstName)` helper that returns `tr("%1, %2").arg(lastName, firstName)`. All four call sites now use the helper. Translators can override `"%1, %2"` to `"%2 %1"` in their `.ts` file to get "First Last" order with no further code changes.

---

## 2026-03-12 — Profile selector name column sorts by display string, not by last name

**File:** `oscar/profileselector.cpp`, `oscar/profileselector.h`

**Symptom:** Clicking the name column header to sort would sort by the displayed string. In French ("First Last" order) this sorts by first name, not last name — inconsistent with English behaviour and user expectation.

**Root cause:** `MySortFilterProxyModel2::lessThan` compared `Qt::DisplayRole` data for the name column. When the display format is locale-dependent (e.g. "First Last" in French), the sort order follows the display format rather than a stable key.

**Fix:** Added `MySortFilterProxyModel2::NameSortRole` (`Qt::UserRole+3`) to carry a locale-independent `"lastname, firstname"` sort key. All three `setData` calls for column 5 now also store this key. `lessThan` detects the name column and compares by `NameSortRole` instead of `DisplayRole`. Added `ProfileSelector::nameSortKey(lastName, firstName)` static helper to produce the key consistently.

---

## 2026-03-12 — gOverviewGraph crash on mouse-over (Overview page)

**File:** `oscar/Graphs/gOverviewGraph.cpp`, `mouseMoveEvent()`

**Symptom:** OSCAR crashes when moving the mouse over the Feelings plot on the Overview page, if Feelings has only been set for one day.

**Root cause:** `d.value()` was called on a `QHash` iterator (`d = m_values.find(hl_day)`) before the guard `d != m_values.end()` was checked. When hovering over any day without a Feelings entry, `d` is the end iterator — dereferencing it is undefined behaviour and crashes.

**Stack trace frame:** `gOverviewGraph::mouseMoveEvent` at line 1079 (original), `QHash::iterator::value()`.

**Fix:** Moved `QMap<short, EventDataType> &valhash = d.value();` to inside the `if ((d != m_values.end()) && (day != nullptr))` block, so it is only evaluated when the iterator is valid. `valhash` is only used within that block, so no other changes were needed.

---

## 2026-03-16 — BMC G3X: oximetry channels continue recording after mask removal; CPAP waveforms do not

**Files:** No code change — investigation note only.

**Observation:** On the 2026-03-16 night, the Flow Rate waveform and most CPAP channels (Pressure, Mask Pressure, Tidal Volume, etc.) appear to stop at approximately 08:27, while the OSCAR session timeline and the Oximetry channels (SpO2, Pulse Rate) continue to 09:04.

**Root cause (data, not code):** The patient removed the mask at approximately 08:25–08:27. Binary evidence in the waveform file (B33BF114508.000):
- 08:25:34: Leak jumps from 0 to 15 raw units (mask seal breaking).
- 08:25:54: Flow spikes to 1159 raw units, leak reaches 1199 (mask coming off).
- 08:26:28 onward: Flow drops to 0–5 raw units (noise floor); leak stabilises at ~178–193 raw units (ambient air escaping from the running machine with no mask).
- 08:27–09:04: Flow amplitude never exceeds 18 raw units; all 60 waveform packets per minute are present and valid.

The SpO2 finger probe remained on the patient's finger, so OXI_SPO2 and OXI_Pulse continue recording valid data until 09:04. The CPAP machine also continued running (and recording) with no mask until 09:04, producing a constant-leak, near-zero-flow signature.

**Conclusion:** OSCAR is displaying the data correctly. The flow waveform is not missing — it is genuinely flat because the mask was off. The session end time of 09:04 reflects when the machine was switched off, not when therapy ended. No code change required.

---

## 2026-03-15 — "Find your CPAP data card" dialog Open/Cancel buttons not translated

**File:** `oscar/mainwindow.cpp`, `MainWindow::importCPAPData()` (around line 1285)

**Symptom:** When a non-English language was active, the Open and Cancel buttons in the "Find your CPAP data card" `QFileDialog` remained in English.

**Root cause:** `QFileDialog` defaults to the native OS file picker. Native dialogs render their own buttons outside Qt's widget and translation system, so `tr()` has no effect on them.

**Fix:** Added `w.setOption(QFileDialog::DontUseNativeDialog, true)` so Qt renders the dialog itself. Qt's own dialog widgets are fully subject to the translation system, and the buttons are translated correctly.

---

## 2026-04-09 - Network module: QTemporaryFile destructor deletes downloaded file; missing size checks and cleanup helpers

**Files:** `oscar/network/cloud_downloader.{h,cpp}`, `oscar/network/dropbox_uploader.{h,cpp}`, `oscar/network/onedrive_uploader.{h,cpp}`, `oscar/network/oauth2_handler.cpp`

**Bug 1 — CloudDownloader deleted the downloaded file on destruction**
On successful download, `onReplyFinished()` left `m_tempFile` non-null. The destructor called `m_tempFile->remove()` on it, deleting the temp file from disk even though the caller still held the path. Fixed by adding `delete m_tempFile; m_tempFile = nullptr;` in the success path (without calling `remove()`, since `autoRemove` is false).

**Bug 2 — Timestamp-based temp file name**
`start()` constructed the temp file path using `QDateTime::currentMSecsSinceEpoch()`, leaving a dead XXXXXX-template assignment above it. Replaced with `QTemporaryFile` (template `oscar_download_XXXXXX.oscar`, `setAutoRemove(false)`) for a secure, unique name.

**Bug 3 — Redundant Qt version guards in startRequest()**
The `#if QT_VERSION >= Qt6` and `#elif >= Qt5.9` branches in `startRequest()` were identical. Collapsed to a single `#if >= 5.9` / `#else` guard.

**Bug 4 — Duplicated manual cleanup in Dropbox/OneDrive uploaders**
Manual `m_reply->deleteLater(); m_reply = nullptr; delete m_file; m_file = nullptr;` was repeated across abort, error, and success branches. Added a `cleanupReply()` helper to both uploaders (matching the pattern already in `CloudUploader`) and replaced all call sites.

**Bug 5 — OneDrive uploader had no file-size guard**
`createUploadSession()` sent the entire file with no size check, despite Microsoft Graph API's 60 MiB per-chunk limit. Added a 60 MiB hard limit with a user-facing error, mirroring the Dropbox uploader's 150 MB guard.

**Bug 6 — OAuth2 token expiry persisted as ISO date string**
`saveTokens()` wrote `m_tokenExpiry.toString(Qt::ISODate)`; `loadTokens()` parsed it back with `QDateTime::fromString`. Changed to `toMSecsSinceEpoch()` / `fromMSecsSinceEpoch()` (stored as `qint64`) for unambiguous UTC round-tripping.


---

## 2026-04-19 - No way to cancel Restore, Backup, or Share Profile operations

**Files:** `oscar/database/backup/profile_backup.{h,cpp}`, `oscar/database/backup/profile_restore.{h,cpp}`, `oscar/restoredialog.{h,cpp}`, `oscar/backupdialog.{h,cpp}`, `oscar/sharedialog.{h,cpp}`

**Symptom:** Once Restore / Backup / Share was started, the user had no way to stop it. The Close button was disabled for the duration, which could be many minutes for large profiles.

**Root cause:** `ProfileBackup::createBackup()` and `ProfileRestore::restoreProfile()` ran synchronously on the main thread with no cancellation mechanism. All three dialogs disabled the Close button during the operation.

**Fix:**
- Added `requestCancel()` + `std::atomic<bool> m_cancelRequested` to both `ProfileBackup` and `ProfileRestore`.
- Added `QCoreApplication::processEvents()` calls between each major phase in `createBackup()`, and after each table-import emit in `restoreInTransaction()`, so the Cancel click is processed promptly.
- Changed all three dialogs to keep the Close button enabled during operation, relabelled "Cancel". Clicking it calls `requestCancel()` on the active object (and `abort()` on the cloud uploader if an upload is in progress). The button becomes "Cancelling..." and is disabled once clicked.
- Cancellation is treated as a non-error: no QMessageBox is shown; status label shows "Cancelled." and the UI resets normally.
- For restore: if cancelled mid-transaction, `restoreInTransaction()` rolls back cleanly before returning.
