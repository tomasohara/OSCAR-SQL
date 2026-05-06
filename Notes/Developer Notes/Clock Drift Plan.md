# Port 1.7.1 Session Alignment Patch to OSCAR 2.0

## Context

A merge request against OSCAR 1.7.1 (Brian Kloppenborg, 2026-04-18, `c:\OSCAR\117.patch`) replaces the global CPAP `clockDrift` preference with per-machine, per-night time offsets, adjustable via a new modeless `SessionAlignmentDialog`. In 1.7.1 the data is persisted in `machines.xml`; in OSCAR 2.0 (fully DB-centric as of schema v14) it must live in SQLite and flow through `MachineRepository`.

The goal: deliver the same user-facing feature in 2.0 (Align Data menu item, Align button in Daily toolbar, modeless nudge dialog, one-time migration of existing `clockDrift` values) while fixing four real bugs in the 1.7.1 patch and adapting storage/persistence to 2.0's DB architecture. Scope decisions (confirmed with user):
- Drift is applied at graph-render only (stats, CSV, MinutesAtPressure keep raw device times — matches 1.7.1 patch intent).
- Storage is a new table with a schema v14→v15 migration.

## Bugs in the 1.7.1 patch (to fix in the 2.0 port)

1. **Migration runs before data is loaded.** `Profile::OpenMachines` iterates `m->day.keys()` — but `Machine::day` is populated later during loader runs. Net effect in 1.7.1: `cpap->setClockDrift(0)` zeroes the setting with no per-night data written. Users silently lose their drift.
2. **OSCAR-day vs calendar-day mismatch.** The dialog writes offsets keyed by `previous_date` (OSCAR day: noon-split). The render path reads them via `QDateTime::fromMSecsSinceEpoch(sess->realFirst()).date()` — a calendar date. A session starting at 2 AM is stored under the previous OSCAR day's key but looked up under today's calendar date → offset silently does nothing.
3. **Every nudge rewrites all machine state.** `p_profile->StoreMachines()` on every button click is tolerable for 1.7.1's single-file XML; in 2.0 it would rewrite the entire `machines` table. Needs a targeted single-row UPSERT.
4. **Dialog does not follow day navigation.** `setDay` is called only when the dialog is first shown. If the user clicks prev/next day in Daily while the modeless dialog is open, the dialog keeps showing the old day.

Secondary items (documented, not bugs):
- **UX inconsistency** (by design): graphs show offset-corrected time; hover tooltips and exports show raw device time.
- `cpap->setClockDrift(0)` is not explicitly persisted after migration — 2.0 port persists via `Save()` immediately.

## Storage design

New table `machine_time_offsets`, introduced by a schema **v14 → v15** migration (purely additive; `MIN_RESTORE_SCHEMA_VERSION` stays at 12).

```sql
CREATE TABLE IF NOT EXISTS machine_time_offsets (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    machine_id  INTEGER NOT NULL,
    profile_id  INTEGER NOT NULL,
    date        TEXT    NOT NULL,          -- OSCAR day (noon-split), ISO YYYY-MM-DD
    offset_ms   INTEGER NOT NULL,
    updated_at  TEXT    DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(machine_id, date),
    FOREIGN KEY(machine_id) REFERENCES machines(id) ON DELETE CASCADE,
    FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_mto_machine_date ON machine_time_offsets(machine_id, date);
CREATE INDEX IF NOT EXISTS idx_mto_profile     ON machine_time_offsets(profile_id);
```

## Drift-site disposition (complete inventory)

Drift is applied **only at graph-render time**. Every current `clockDrift` site in 2.0:

| File:line | Action |
|---|---|
| `session.cpp:1406, 1627, 1674, 3459, 3470` (`SearchValue`, `first(id)`, `last(id)`, `first()`, `last()`) | **Remove** drift block. Raw timestamps only — matches 1.7.1 patch. |
| `Graphs/gFlagsLine.cpp:351` | Move `drift` inside per-session loop; `drift = sess->machine()->sessionOffsetMs(sess)` when `sess->type()==MT_CPAP`, else `0`. |
| `Graphs/gLineChart.cpp:537` | Same per-session pattern. |
| `Graphs/gLineOverlay.cpp:84` | Same per-session pattern. |
| `Graphs/MinutesAtPressure.cpp:549` | Same per-session pattern. (Missed by 1.7.1 patch description; same shape, must be converted or the MinutesAtPressure chart will drift-skew.) |
| `daily.cpp:834` (`UpdateEventsTree`) | Move drift inside per-session loop; per-session lookup. |
| `daily.cpp:2201, 2718, 2768` (bookmark paths) | Day-scoped, not session-scoped — use `p_profile->GetMachine(MT_CPAP)->timeOffset(previous_date)`. |
| `machine.cpp:358` | Delete dead `//int drift=...` line. |
| `preferencesdialog.ui:724, 759, 776, 789` | **Remove** "CPAP Clock Drift" group entirely. |
| `preferencesdialog.cpp:303-309, 996` | **Remove** the read/write block. |
| `profiles.h` / `CPAPSettings::clockDrift` accessor | **Keep** for one release — migration must read the legacy value. Document as deprecated. |

## Canonical date-for-session helper (fixes bug #2)

Both writer (dialog) and readers (graphs) must agree on the date key. Refactor the OSCAR-day logic out of `Machine::AddSession` (currently inline at `machine.cpp:355-385`) into a shared helper:

```cpp
// Machine::dateForSession(Session* s) const — returns OSCAR day
QDateTime dt = QDateTime::fromMSecsSinceEpoch(s->realFirst());
QTime split = (s->summaryOnly() && profile->session->lockSummarySessions())
                ? QTime(12,0,0)
                : profile->session->daySplitTime();
QDate d = dt.date();
if (dt.time() < split) d = d.addDays(-1);
return d;

// Machine::sessionOffsetMs(Session* s) const
return (s->type() == MT_CPAP) ? timeOffset(dateForSession(s)) : 0;
```

`AddSession` is refactored to call `dateForSession` for its own date assignment, keeping one source of truth. The close-session coalescing logic (`machine.cpp:371-398`) stays where it is — it adjusts the stored OSCAR-day key but is not reused by the render path.

Dialog writes offsets keyed by `previous_date` (already the OSCAR day as displayed in Daily). Render reads via `sessionOffsetMs(sess)`. Keys match.

## MachineRepository additions

```cpp
// machine_repository.h (new methods)
QMap<QDate, qint64> getTimeOffsets(qint64 machineDbId);
qint64 getTimeOffset(qint64 machineDbId, const QDate& date);     // 0 if absent
bool   upsertTimeOffset(qint64 machineDbId, qint64 profileId,
                        const QDate& date, qint64 offsetMs);     // single-row INSERT ... ON CONFLICT DO UPDATE
bool   deleteTimeOffset(qint64 machineDbId, const QDate& date);
bool   deleteAllTimeOffsets(qint64 machineDbId);
```

All prepared statements; parameterised. `upsertTimeOffset` with `offset_ms == 0` performs a DELETE (the in-memory map also treats 0 as absent).

## Machine class additions

```cpp
// machine.h
QMap<QDate, qint64> m_timeOffsets;        // private cache
qint64 timeOffset(QDate d) const { return m_timeOffsets.value(d, 0); }
void   setTimeOffset(QDate d, qint64 ms);     // updates cache + single-row UPSERT, no full save
void   clearTimeOffset(QDate d);              // updates cache + DELETE
const QMap<QDate, qint64>& timeOffsets() const { return m_timeOffsets; }
QDate  dateForSession(Session* s) const;
qint64 sessionOffsetMs(Session* s) const;
```

`setTimeOffset` / `clearTimeOffset` call `MachineRepository` directly (solves bug #3 — no `p_profile->StoreMachines()` on each nudge). No `changed` flag manipulation needed; offsets live outside the `machines` row.

## Cache population and migration hook

**Cache population** — `Profile::loadMachinesFromDatabase(profiles.cpp ~358)` after each `CreateMachine`:

```cpp
m->m_timeOffsets = MachineRepository::instance().getTimeOffsets(m->getDatabaseId());
```

**Data migration from `clockDrift`** — new `Profile::migrateClockDriftToTimeOffsets()` called from `Profile::LoadMachineData()` at the **bottom** (after `calculateDailySummaries()` around `profiles.cpp:1073`). At that point every CPAP machine's `day` map is fully populated (fixes bug #1):

```cpp
if (profile_preferences_has("timeoffsets.migrated")) return;
if (cpap->clockDrift() == 0) { set_pref("timeoffsets.migrated", true); return; }
qint64 ms = qint64(cpap->clockDrift()) * 1000LL;
for (Machine* m : m_machlist) {
    if (m->type() != MT_CPAP) continue;
    for (QDate d : m->day.keys()) {
        m->setTimeOffset(d, ms);  // writes via repository
    }
}
cpap->setClockDrift(0);
Save();                            // persist zeroed preference now (bug #6)
set_pref("timeoffsets.migrated", true);  // one-shot guard, stored in profile_preferences
```

The `"timeoffsets.migrated"` flag is a **profile-scoped** preference (not app-global) so multi-profile systems migrate independently.

## SessionAlignmentDialog integration (fixes bug #4)

New files: `oscar/session_alignment_dialog.{h,cpp,ui}`. UI mirrors the 1.7.1 dialog (device dropdown; -1h/-1m/-1s; offset label; +1s/+1m/+1h; Reset; Apply Last Offset; Apply All Previous Offsets; Close).

- Window flags `Qt::Tool | Qt::WindowStaysOnTopHint`, `setModal(false)`, parented to `MainWindow`.
- Stored state: `QPointer<Daily> m_daily`, `QDate m_date`, `QPointer<Machine> m_machine`.
- **New signal `Daily::dateChanged(QDate)`** emitted at end of `Daily::Load()` (after `previous_date = date;` ~`daily.cpp:1926`). Dialog connects to it; slot refreshes `m_date`, re-populates combobox, updates label. Dialog stays open across day navigation.
- Device dropdown: populated from `day->machines.values()` (Day's `QHash<MachineType, Machine*>` at `day.h:311`).
- Each nudge: `m_machine->setTimeOffset(m_date, current + delta)` → in-memory + single-row UPSERT → `m_daily->GraphView->redraw()` → refresh label. **No `StoreMachines()` call** (bug #3 fix).
- Apply Last Offset / Apply All Previous: same helpers as 1.7.1 patch, reading `m->timeOffsets()` and writing via `setTimeOffset`.
- `MainWindow::on_actionAlignData_triggered` caches a single `SessionAlignmentDialog*` member; on subsequent clicks `show(); raise(); activateWindow();`.

Toolbar: add Align `QPushButton` to the Daily bottom toolbar near `graphCombo` in `daily.ui` (auto-connects to `Daily::on_alignSessions_clicked`). Data menu: add `actionAlignData` to `menu_Data` in `mainwindow.ui` between the import separator and `action_Rebuild_Oximetry_Index` (~line 2544).

## Files to modify

| File | Change |
|---|---|
| `oscar/database/database_schema.h` | Bump `CURRENT_SCHEMA_VERSION` to 15; update version-history comment. |
| `oscar/database/database_schema.cpp` | New `createMachineTimeOffsetsTable()`; call it from `createSchema`; new `migrateV14ToV15()`; add branch in `upgradeSchema`. |
| `oscar/database/machine_repository.{h,cpp}` | Five new methods (see above). |
| `oscar/SleepLib/machine.h` | `m_timeOffsets`, accessors, `dateForSession`, `sessionOffsetMs`. |
| `oscar/SleepLib/machine.cpp` | Implement accessors; refactor `AddSession` date-logic to call `dateForSession`; delete commented-out `//int drift=...` at line 358. |
| `oscar/SleepLib/profiles.cpp` | Populate `m_timeOffsets` after each `CreateMachine` in `loadMachinesFromDatabase`; add `migrateClockDriftToTimeOffsets()` called from end of `LoadMachineData`. |
| `oscar/SleepLib/session.cpp` | Remove drift blocks at `1406-1410, 1627-1668, 1674-1700, 3454-3473`. |
| `oscar/Graphs/gFlagsLine.cpp`, `gLineChart.cpp`, `gLineOverlay.cpp`, `MinutesAtPressure.cpp` | Per-session `sessionOffsetMs` lookup inside each session loop. |
| `oscar/daily.cpp` | Same for `UpdateEventsTree`; bookmark paths use `previous_date` and `GetMachine(MT_CPAP)->timeOffset(...)`; emit `dateChanged` after `previous_date = date`; add `on_alignSessions_clicked` slot. |
| `oscar/daily.h` | `signals: void dateChanged(QDate);` and `SessionAlignmentDialog* m_alignDialog=nullptr;` forward-decl. |
| `oscar/mainwindow.ui` | Add `actionAlignData` in Data menu. |
| `oscar/mainwindow.{h,cpp}` | `on_actionAlignData_triggered` — create/show modeless dialog. |
| `oscar/preferencesdialog.ui` | Remove CPAP Clock Drift group (line ~724-795). |
| `oscar/preferencesdialog.cpp` | Remove read/write lines (303-309, 996). |
| `oscar/daily.ui` | Add Align `QPushButton` near `graphCombo`. |
| `oscar/session_alignment_dialog.{h,cpp,ui}` | New files — modeless dialog per §above. |
| `oscar/oscar.pro` | Register the three new files (SOURCES/HEADERS/FORMS). |
| `Notes/DATABASE_SCHEMA_REFERENCE.md` | Document `machine_time_offsets` table and bump schema to v15. |
| `Notes/BUG_FIXES.md` | Log the four 1.7.1 patch bugs and their 2.0 fixes. |
| `Htmldocs/release_notes.html` | Ask before editing (per project convention). |

## Critical files to read before implementation

- `oscar/database/database_schema.cpp` — v13→v14 migration block (~line 1618) as template for v15.
- `oscar/database/machine_repository.cpp` — prepared-statement and upsert patterns to mirror.
- `oscar/SleepLib/machine.cpp` lines 292-425 — `AddSession` OSCAR-day logic to refactor into `dateForSession`.
- `oscar/SleepLib/profiles.cpp` `loadMachinesFromDatabase` and `LoadMachineData` — the two migration insertion points.
- `oscar/SleepLib/session.cpp` lines 1400-1700 and 3450-3475 — drift blocks to remove.
- `oscar/daily.cpp` current uses of `clockDrift` (8 sites) — conversion pattern reference.
- `c:\OSCAR\117.patch` — original 1.7.1 patch, especially `sessionalignmentdialog.{h,cpp,ui}` for dialog layout.

## Verification

1. **Fresh install path.** New v15 profile, import one CPAP night. Nudge +1s in dialog → graph shifts 1 s. Confirm one row in `machine_time_offsets`. Restart OSCAR — offset persists and re-applies.
2. **v14 → v15 upgrade with non-zero clockDrift.** Open existing v14 profile with `clockDrift != 0`. Schema advances. At end of `LoadMachineData`, migration populates rows for every OSCAR-day that has a CPAP `Day`. `clockDrift` becomes 0 and is saved. Restart — migration does not re-run (`"timeoffsets.migrated"` guard). Graphs look identical to pre-upgrade.
3. **OSCAR-day boundary (bug #2 regression test).** Session that starts at 2 AM local. In Daily, navigate to the *previous* OSCAR day (where the session renders). Nudge +60 s. Waveform shifts 60 s. Navigate forward one day — no shift applied.
4. **Single-row persistence (bug #3 regression).** Enable `QSqlDatabase` query logging. Confirm each nudge produces exactly one INSERT/UPDATE on `machine_time_offsets` and zero on `machines`.
5. **Day navigation with dialog open (bug #4 regression).** Open dialog on date X → shows X's offset. Click Daily "next day" → dialog label switches to Y's offset without closing.
6. **Multi-machine profile.** Two CPAPs in a profile; dropdown switches between them; offsets are independent per machine.
7. **Apply Last / Apply All Previous.** Nudge on day N, navigate to day N+1, click Apply Last → day N+1 receives N's value. Switch machines and click Apply All Previous → every machine with a prior offset gets its most-recent value applied at the current date.
8. **Reset.** Reset button deletes the row; `timeOffset()` returns 0; graph re-renders at raw device time.
9. **Schema-downgrade restore.** v14 backup restores cleanly into v15 DB (`machine_time_offsets` simply empty). v12 backup still restores (MIN unchanged).
10. **Machine deletion cascade.** Delete a CPAP machine → `ON DELETE CASCADE` removes its offsets. No orphans.
11. **Stats/export parity.** AHI, CSV export, MinutesAtPressure chart unchanged before vs. after a nudge — by design drift is render-only.
12. **Release-notes / tooltip caveat.** Document that hover tooltips show raw device time while graph X-axis shows corrected time.
