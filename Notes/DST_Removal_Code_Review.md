# Code Review — DST Zone Removal

**Date:** 2026-04-22
**Scope:** Modified files for the DST Zone elimination task — `oscar/newprofile.ui`,
`oscar/newprofile.cpp`, `oscar/SleepLib/profiles.h`,
`oscar/database/user_info_repository.{h,cpp}`, `oscar/database/database_schema.{h,cpp}`,
`Notes/DATABASE_SCHEMA_REFERENCE.md`, `Notes/BUG_FIXES.md`, `claude.md`, and the new
design note `Notes/Eliminate DST zone.md`.

A full grep across `oscar/**` confirmed no remaining references to `dst_enabled`,
`dstEnabled`, `STR_UI_DST`, `daylightSaving`, or `DSTcheckbox`. Translation files
(`Translations/*.ts`) still contain the `DST Zone` source string — this is expected;
the next `lupdate` run will mark them obsolete automatically.

---

## Blocker

### Pre-v15 backups will fail to restore

`profile_restore.cpp::executeSqlFile()` does **not** filter out columns that exist in a
backup file but no longer exist in the current database schema. It parses
`INSERT INTO user_info (..., dst_enabled, ...) VALUES (...)` from older backups, then
reconstructs and executes the INSERT verbatim (only `id` and a handful of named
columns are filtered — see `profile_restore.cpp:973-1090`).

Concrete failure path after this change ships:

1. Any backup written at schema v12, v13, or v14 contains `dst_enabled` in
   `user_info.sql`.
2. After the v14→v15 migration drops the column, SQLite rejects the INSERT with
   `table user_info has no column named dst_enabled`.
3. `q.exec()` returns false → `executeSqlFile` returns false → the entire restore
   aborts with the error surfaced to the user.

The claim in `Notes/Eliminate DST zone.md:81-82` that "the restore logic already
handles extra columns gracefully" is not supported by the code. The comment at
`profile_restore.cpp:572` refers to *new* columns added since the backup getting
DEFAULT values (which is SQLite's own behavior for missing values in an INSERT),
**not** to removed columns being silently dropped. The only graceful handling present
today is for missing *tables* via the `sqlite_master` check at
`profile_restore.cpp:919`.

`MIN_RESTORE_SCHEMA_VERSION` remains `12` (`database_schema.h:82`), so v12/v13/v14
backups are in-scope and must restore cleanly into the v15 database.

**Recommended fix (in `profile_restore.cpp::executeSqlFile`):**

- Once per table, run `PRAGMA table_info(<table>)` and build a `QSet<QString>` of
  valid column names.
- Inside the per-column loop, `continue` when `stmt.columns.at(i)` is not in that
  set (i.e. skip the column and its value — SQLite will use the column's DEFAULT or
  NULL during INSERT).

This is a general-purpose fix and also closes the same latent bug for the v14
removals of `sessions.events_file` / `sessions.summary_file` when restoring a v12
or v13 backup.

**Do not land the v15 schema bump without this fix.**

---

## Suggestion — Clean up orphaned `"DST"` rows in `profile_preferences`

`UserInfo` inherits `PrefSettings(Profile*)`, so `STR_UI_DST = "DST"` was being
persisted to the `profile_preferences` table as well as to `user_info.dst_enabled`
(confirmed in the project memory note on Privacy Mode). Removing `initPref` /
accessors prevents future writes, but every existing profile still has a `"DST"` row
that will remain in the DB, be included in future backups (harmless — it's a
boolean), and be round-tripped through restore (recreating the orphan).

`exportPrivacyPreferences()` dumps the whole `profile_preferences` table row-by-row
with a personal-key blanklist (`profile_backup.cpp:194`). `"DST"` is not a personal
key, so it exports as-is. Not a privacy concern, just accumulated cruft.

Add a single statement to `migrateV14ToV15` alongside the `DROP COLUMN`:

```sql
DELETE FROM profile_preferences WHERE key = 'DST';
```

Cheap, matches the spirit of the removal, and keeps the database clean across
upgraded installs.

---

## What's correct

- **`database_schema.cpp::migrateV14ToV15`** — transactional, idempotent via the
  case-insensitive `"no such column"` fallback, bumps `schema_version` inside the
  same transaction, rolls back on any failure. Matches the shape of the existing
  `v13→v14` migration.
- **`createUserInfoTable`** — `dst_enabled INTEGER` removed so fresh installs skip
  the column cleanly.
- **`user_info_repository.{h,cpp}`** — `UserInfoData.dstEnabled` member removed;
  INSERT, UPDATE, SELECT SQL all consistent; column indices in `findByProfile`
  correctly renumbered (old idx 13 → new idx 12 for `password_hash`); bind count
  matches placeholder count on both INSERT (12) and UPDATE (11 + WHERE id).
- **`profiles.h`** — `STR_UI_DST` constant, `initPref` default, getter, and setter
  all removed in one pass; no orphan callers anywhere in `oscar/**`.
- **`newprofile.cpp` / `newprofile.ui`** — save (`on_nextButton_clicked`) and load
  (`edit`) references both removed. The now-empty row-4 col-0 cell in the grid is
  harmless: row-4 col-1 still holds `verticalSpacer_6`, and grid layouts tolerate
  empty cells without visual artifacts.
- **Translation files** — intentionally untouched. Next `lupdate` will mark the
  `DST Zone` source string obsolete automatically.
- **`Notes/DATABASE_SCHEMA_REFERENCE.md`** — version banner and `user_info` table
  definition both updated consistently.
- **`Notes/BUG_FIXES.md`** — entry appended per the project convention.

---

## Nits

- `claude.md` line 73: typo **"charactes"** → **"characters"**.
- `claude.md`: missing newline at end of file.
- `newprofile.ui` line 234: blank line left in place of the removed `<item>` block.
  Harmless to Qt's XML parser; cosmetically a clean delete with no gap is tidier.
- `database_schema.cpp:1405`: using `err.contains("no such column")` for idempotency
  is locale-safe (SQLite error messages are always English) but slightly brittle.
  Pre-checking with `PRAGMA table_info(user_info)` is more robust. In practice the
  defensive branch should never fire because `schema_version` gates re-runs.

---

## Recommendation

**Do not ship the v15 schema bump until `executeSqlFile` filters unknown columns.**
Everything else in the DST-removal surface is clean, mechanical, and correctly
applied. Once the restore path is hardened (and ideally the
`profile_preferences` cleanup is added to the migration), the change is safe to
release.
