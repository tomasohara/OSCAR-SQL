# `daily_summaries`: Removing the per-machine dimension

**Date:** 2026-04-27
**Schema baseline at time of writing:** v15 (`DatabaseSchema::CURRENT_SCHEMA_VERSION = 15`)
**Proposed target version:** v16

## 1. The mistake we want to fix

`daily_summaries` was defined with `machine_id` as part of the natural key:

```sql
CREATE TABLE daily_summaries (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id  INTEGER NOT NULL,
    date        TEXT    NOT NULL,
    machine_id  INTEGER,
    ...
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE SET NULL,
    UNIQUE(profile_id, date, machine_id)
);
```
(`oscar/database/database_schema.cpp:1016`)

A row in this table is a *Day*-level rollup. A `Day` already aggregates across
all of that day's machines (one MT_CPAP + zero or more MT_OXIMETER + optional
MT_SLEEPSTAGE / MT_POSITION). So the table should have one row per
`(profile_id, date)` — not one per `(profile_id, date, machine_id)`.

## 2. What the per-machine column actually does today

The schema says "per machine per day", but the code never uses it that way.

### 2.1 All call sites pass `machineId = 0` ("combined")
- `Profile::calculateDailySummaries()` — `oscar/SleepLib/profiles.cpp:3029`
- `MainWindow` purge-day handler — `oscar/mainwindow.cpp:2104`
- `MainWindow` clear-oximetry handler — `oscar/mainwindow.cpp:2962`
- `DailySummaryRepository::calculateRange()` — `oscar/database/daily_summary_repository.cpp:384`

The header makes `machineId` an optional default argument
(`daily_summary_repository.h:84-95`) and no caller in the tree ever passes
anything else.

### 2.2 The "combined" intent is silently broken
`DailySummaryRepository::calculateFromDay()`
(`oscar/database/daily_summary_repository.cpp:262-365`) walks the day's
sessions and, when `machineId == 0`, **rewrites it** to the database id of
the first enabled MT_CPAP session's machine:

```cpp
if (machineId == 0 && sess->type() == MT_CPAP && sess->machine()) {
    machineId = sess->machine()->getDatabaseId();
}
...
data.machineId = machineId;
```

`create()` then stores `data.machineId > 0 ? data.machineId : NULL`
(`daily_summary_repository.cpp:52`). Net effect:

- A day with any CPAP session → row keyed to that CPAP machine's id.
- A day with only oximetry → row keyed to `machine_id IS NULL`.

So the existing rows are *not* "combined per profile/date" the way every
caller intends; they're "per CPAP machine, but happen to be one per date
because a profile rarely has two CPAPs". This works by accident and
contradicts the schema documentation
(`Notes/DATABASE_SCHEMA_REFERENCE.md` line 859: "machine_id ... NULL=combined").

### 2.3 Latent bug: the read-side helpers can't find the rows they wrote
`findByProfileAndDate(profileId, date, machineId = 0)` translates
`machineId == 0` into `WHERE machine_id IS NULL`
(`daily_summary_repository.cpp:138`). But the rows were stored with the
CPAP machine's id, not NULL. So this finder, called with default args,
returns nothing for any day with CPAP data. The same is true of `findRange`
and `exists`. **These three methods currently have zero in-tree callers**
(grep confirms only declarations and self-references), which is why the
mismatch hasn't bitten anyone.

### 2.4 The `UNIQUE(profile_id, date, machine_id)` constraint is the safety net
The redundant per-machine iteration in `Profile::calculateDailySummaries()`
(`profiles.cpp:2979-3038` walks every machine then every day on each
machine, so for a profile with CPAP + Oximeter the same date is visited
twice) is rendered idempotent only because `INSERT OR REPLACE` keys on the
existing UNIQUE triple and `calculateFromDay` always picks the same CPAP
machine id from the shared `Day::sessions` list. Once `machine_id` is
removed, the new constraint becomes `UNIQUE(profile_id, date)` and the same
idempotency holds — but for the right reason.

## 3. Surface area of the change

### 3.1 Schema (`oscar/database/database_schema.cpp`)
- `createDailySummariesTable()` (line 1016) — drop `machine_id` column;
  drop the `FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE SET
  NULL`; change `UNIQUE(profile_id, date, machine_id)` to
  `UNIQUE(profile_id, date)`.
- `createIndexes()` (line 418) — drop
  `idx_daily_summaries_profile_machine`. Keep
  `idx_daily_summaries_profile_date`, `_ahi`, `_compliance`, `_date_range`.
- `database_schema.h` — bump `CURRENT_SCHEMA_VERSION` 15 → 16. Decide
  whether to bump `MIN_RESTORE_SCHEMA_VERSION` (currently 12).

### 3.2 Migration (`migrateV15ToV16`)
SQLite cannot drop a column that is referenced by an index/UNIQUE
constraint without recreating the table. Standard recipe:

```sql
BEGIN;

CREATE TABLE daily_summaries_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    date TEXT NOT NULL,
    -- ... all other columns unchanged ...
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    UNIQUE(profile_id, date)
);

INSERT OR REPLACE INTO daily_summaries_new
       (id, profile_id, date, session_count, ... , sessions_hash)
SELECT  id, profile_id, date, session_count, ... , sessions_hash
FROM    daily_summaries
ORDER BY (machine_id IS NULL) ASC, id ASC;
-- ORDER BY: prefer machine-bound rows over NULL; INSERT OR REPLACE keeps the last,
-- so we visit NULL rows first and let machine-bound rows win on the (profile_id, date) key.

DROP TABLE daily_summaries;
ALTER TABLE daily_summaries_new RENAME TO daily_summaries;

DROP INDEX IF EXISTS idx_daily_summaries_profile_machine;
-- (the other four indexes survive intact since SQLite reattaches indexes by name
-- only to the original table; verify by listing pragmas, recreate if needed.)

COMMIT;
```

In practice the dedup is a no-op for current users (one row per date), but
the migration must be defensive because users who hand-populated rows or
ran older builds may have both a NULL and a machine-bound row for the same
date.

### 3.3 Repository (`oscar/database/daily_summary_repository.{h,cpp}`)
- Remove `machineId` field from `DailySummaryData`.
- Drop `machineId` parameter from public API:
  - `findByProfileAndDate(profileId, date)`
  - `findRange(profileId, startDate, endDate)`
  - `calculateAndStore(profileId, date)`
  - `calculateAndStoreFromDay(day, profileId)`
  - `exists(profileId, date)`
- Drop the auto-resolution branch in `calculateFromDay()`
  (`daily_summary_repository.cpp:284-289`).
- `create()` SQL drops the `machine_id` column from the column list and
  bind list; `mapResultToData()` drops the `query.value("machine_id")`
  read.

### 3.4 Call sites
All four callers pass `0` already, so the diff is "delete the trailing
`, 0`". No semantic change.

### 3.5 Reports — `oscar/docs/system_reports.orf`
Existing daily/weekly/monthly queries already filter on `profile_id` and
group by date, never by `machine_id`. They are correct under both schemas.
The only practical improvement: with one row per date guaranteed, weekly
and monthly aggregates can no longer accidentally double-count a date that
ended up with both a NULL and a machine-bound row in some pathological
backup. No query edits required.

### 3.6 SQL elsewhere
- `oscar/exports/report_exporter.cpp:505,1012,1034` — uses `SELECT *` /
  `MIN(date)` / `MAX(date)`. Survives the column drop unchanged.
- `oscar/exportcsv.cpp:439,466,491,515` — `SELECT ds.<named columns> FROM
  daily_summaries ds`. None reference `machine_id`. No edits required.
- `Notes/USEFUL_QUERIES.sql:313` — table-name reference only. No edit.
- `oscar/sqleditor.ui:45` — help text mentioning the table. No edit.

### 3.7 Backup / Restore
- **Export** — `ProfileBackup::exportDailySummaries()`
  (`profile_backup.cpp:893`) uses the generic `SqlExporter` which reflects
  the live column list. Post-migration, exports will simply omit
  `machine_id`. No code change.
- **Import** — `ProfileRestore::executeSqlFile()`
  (`profile_restore.cpp:1062-1100`) special-cases `machine_id` remapping
  for the `daily_summaries` table. With v16 backups this code is dead and
  can be removed. With *legacy* (v15) backups restored into a v16 DB:
  - Either bump `MIN_RESTORE_SCHEMA_VERSION` to 16 (forces a re-import for
    anyone with an older backup — consistent with the schema migration
    policy memo for an unreleased v16), **or**
  - Add a column-drop step in restore that strips `machine_id` from any
    incoming `daily_summaries` `INSERT` when the live table no longer has
    that column. The existing per-column iteration in `executeSqlFile()`
    is well placed for this — just skip the column instead of remapping
    it.
- **Restore order** — `restoreInTransaction()` order array
  (`profile_restore.cpp:1192-1211`) doesn't change, but
  `daily_summaries` no longer needs to come after `machines`; we could
  move it earlier, though there's no benefit.

### 3.8 Documentation
- `Notes/DATABASE_SCHEMA_REFERENCE.md`
  - Update title block "Schema Version" to 16 (currently says 15).
  - Add v16 entry to the version-history table.
  - Update the `daily_summaries` `CREATE TABLE` block, data dictionary,
    and the FK / cascade-delete tables (drop `daily_summaries.machine_id
    → machines.id` row).
  - Remove the Daily Summaries Indexes entry for
    `idx_daily_summaries_profile_machine`.
  - Update the ER diagram block: the `machines (1) ─< daily_summaries (N)`
    edge goes away; only the `profiles (1) ─< daily_summaries (N)` edge
    remains.
- `Notes/Database-ER-Diagram.png` — regenerate.
- `Notes/BUG_FIXES.md` — log under a 2026-04 entry.

## 4. Risk register

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Existing testers' DBs (beta-2 shipped 2026-03-16) need migration. | Medium | Provide `migrateV15ToV16` (recipe in §3.2). The migration is purely a column drop + UNIQUE rewrite; no data values change. |
| v15 backups in users' hands won't restore cleanly into v16. | Low/Med | Either bump `MIN_RESTORE_SCHEMA_VERSION` (cleanest, aligns with the existing policy that schema-mismatched backups can require a fresh import) or add the column-skip path described in §3.7. Recommendation: bump the floor — the user base is still beta and the alternative adds restore-time edge cases for a column nobody uses correctly. |
| Hidden caller of `findByProfileAndDate` / `findRange` / `exists` that relies on the `machineId` parameter. | Low | A repo-wide grep finds none. The methods can lose the parameter without a deprecation step. |
| Reports that *should* have been per-machine break silently. | None identified | None of the system reports or CSV-export queries split by `machine_id`. The Daily/Statistics/Overview screens read `Day` objects, not this table. |
| Foreign-key cascade behaviour change. The current `ON DELETE SET NULL` from `machines` did keep summary rows alive when a machine was deleted. After the change, deleting a machine cascades through `sessions` → session-children, but `daily_summaries` keeps its row keyed only by profile + date. That's actually closer to what we want (the summary is a profile-day, not a machine-day), but it should be called out in the BUG_FIXES note so behavior is intentional. | Low | Document. |

## 5. Estimated effort

| Item | Effort |
| --- | --- |
| Schema + migration + index changes | ~1 hour |
| Repository API + struct changes | ~30 min |
| Call-site cleanup (4 sites) | ~10 min |
| Restore code update (delete special case or add column-skip) | ~30 min |
| Documentation (`DATABASE_SCHEMA_REFERENCE.md`, ER diagram, BUG_FIXES) | ~45 min |
| Manual test: fresh install, upgrade from v15, restore v15 backup, restore v16 backup | ~1 hour |
| **Total** | ~4 hours |

## 6. Recommendation

Do the change. The per-machine dimension is wrong, currently silently
broken (§2.2 and §2.3), and held together by an `INSERT OR REPLACE`
accident (§2.4). The cleanup also lets us delete one finder branch, one
restore special-case, one index, and one FK — all with no caller fallout.

Suggested sequencing:
1. Land the schema bump, migration, and repository simplification together
   (one commit — they're tightly coupled and impossible to test separately).
2. Land the doc updates in a follow-up commit so reviewers can read
   schema + migration first without the noise.
3. Bump `MIN_RESTORE_SCHEMA_VERSION` to 16 and test the restore path with
   a v15 backup to confirm the "backup too old" message fires cleanly.
