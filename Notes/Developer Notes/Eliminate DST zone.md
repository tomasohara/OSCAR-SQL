# DST Zone Field — Elimination Report

## What it is

A `QCheckBox` named `DSTcheckbox` with label "DST Zone" in `oscar/newprofile.ui` (row 4, col 0 of the profile form grid).

Stored under preference key `STR_UI_DST = "DST"` in the `UserInfo` class.

---

## Complete list of touch points

### UI
| File | Lines | Role |
|---|---|---|
| `oscar/newprofile.ui` | ~235–245 | Widget definition |
| `oscar/newprofile.cpp` | 290 | Save: `setDaylightSaving(ui->DSTcheckbox->isChecked())` |
| `oscar/newprofile.cpp` | 454 | Load: `ui->DSTcheckbox->setChecked(daylightSaving())` |

### Profile layer (`profiles.h`)
| Location | Role |
|---|---|
| Line 340: `const QString STR_UI_DST = "DST"` | Preference key constant |
| Line 495: `initPref(STR_UI_DST, false)` | Default value (false) in `UserInfo` constructor |
| Line 509: `bool daylightSaving() const` | Getter |
| Line 522: `void setDaylightSaving(bool ds)` | Setter |

### Database layer
| File | Lines | Role |
|---|---|---|
| `oscar/database/user_info_repository.h` | 39 | `bool dstEnabled` member of `UserInfoData` struct |
| `oscar/database/user_info_repository.cpp` | 40–41 | INSERT SQL includes `dst_enabled` column |
| `oscar/database/user_info_repository.cpp` | 81 | UPDATE SQL includes `dst_enabled = ?` |
| `oscar/database/user_info_repository.cpp` | 121 | SELECT SQL includes `dst_enabled` |
| `oscar/database/user_info_repository.cpp` | 55, 95 | `dstEnabled ? 1 : 0` bound to INSERT/UPDATE |
| `oscar/database/user_info_repository.cpp` | 144 | `dstEnabled = query.value(12).toInt() != 0` on SELECT |
| `oscar/database/user_info_repository.cpp` | 171 | `data.dstEnabled = userInfo->daylightSaving()` (profile → DB) |
| `oscar/database/user_info_repository.cpp` | 215 | `userInfo->setDaylightSaving(data.dstEnabled)` (DB → profile) |
| `oscar/database/database_schema.cpp` | 512 | `dst_enabled INTEGER` in CREATE TABLE for `user_info` |
| `oscar/database/database_schema.h` | 60 | Current schema is v14 |

### Translations
All 25 language translation files contain a `<message>` entry for the "DST Zone" source string (sourced from `newprofile.ui` line 243). These become obsolete strings on the next `lupdate` run — no manual editing required.

### Backup/Restore
The backup exports the entire `user_info` table row as SQL INSERT statements. `dst_enabled` is included automatically as part of the row. No special handling — removing it from the schema will silently zero-fill during restore from older backups (SQLite default for missing columns).

---

## Is the value ever consumed (does it affect any logic)?

**No. It is dead code.**

`daylightSaving()` is called in exactly two places beyond storage:
1. `newprofile.cpp:454` — to populate the checkbox on display (read-back only)
2. `user_info_repository.cpp:171` — to write to the database (round-trip only)

It is **never read** by:
- Any loader (`resmed_loader`, EDF parsers, BMC loaders, CMS50 loaders, etc.)
- Any timestamp calculation
- Any display or reporting code
- `EDFInfo::localNoDST` / `TZ_offset` — these are initialized from
  `QTimeZone::systemTimeZone().offsetFromUtc(QDateTime::currentDateTime())` at startup,
  with no reference to the profile's DST flag

The mentions of "DST" in `bmcDataParsing.cpp`, `bmcG3xDataParsing.cpp`, `cms50_loader.cpp`,
`md300w1_loader.cpp`, and `gSummaryChart.cpp` are unrelated code comments about handling
clock transitions or developer queries — none reference the profile field.

---

## Consequences of eliminating it

**None functionally.** The field does nothing. Eliminating it:

1. **Cleans up a confusing UI element** that implies OSCAR adjusts for DST (it doesn't —
   it relies on the system timezone via `EDFInfo::localNoDST`).

2. **Requires a schema migration to v15**: `ALTER TABLE user_info DROP COLUMN dst_enabled`
   (supported in SQLite 3.35+, which Qt6's bundled SQLite satisfies). Old backups
   (schema 12–14) that contain `dst_enabled` in their SQL restore without issue — the
   restore logic already handles extra columns gracefully.

3. **Requires updating `user_info_repository`** INSERT/UPDATE/SELECT queries and the
   `UserInfoData` struct (8 sites, all mechanical).

4. **Removes the getter/setter from `profiles.h`** and the `STR_UI_DST` constant.

5. **Translation entries** become obsolete strings on next `lupdate` — they disappear
   automatically; no manual editing of `.ts` files required.

6. **`exportPrivacyPreferences()`** in `profile_backup.cpp` blanks `UserInfo` keys
   including any `"DST"` key during privacy export. Removing the key from `profiles.h`
   also removes it from that sweep — no separate change needed there.

---

## Conclusion

Safe to remove with zero risk to data or functionality. It is a stored-but-never-read field.
