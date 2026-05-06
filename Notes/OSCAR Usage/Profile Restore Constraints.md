# Profile Restore Constraints

---
  **Current constants: CURRENT_SCHEMA_VERSION = 14, MIN_RESTORE_SCHEMA_VERSION = 12**

As of 4/19/2026.

---
####   Case 1: Backup schema > installed schema (backup is newer)

Hard reject. checkCompatibility() returns false with a clear error: "Please upgrade OSCAR before restoring." The restore dialog shows a warning box and aborts. No data is touched.

####   Case 2: Backup schema < MIN_RESTORE (backup is too old)

Hard reject. Same path — error message says the minimum supported version and suggests restoring with an older OSCAR first. No data is touched.

####   Case 3: Backup schema in [MIN, CURRENT) — older but within range

Accepted with a user-visible warning (restoredialog.cpp:548–558). The dialog warns that some settings may not restore and will be regenerated. Then restore proceeds with two safety mechanisms:

    1. Tables that no longer exist in the current schema are silently skipped — executeSqlFile() queries sqlite_master before processing each file and returns true immediately if the table is absent.
    2. New columns added since the backup receive their DEFAULT values automatically (SQLite fills in defaults when a column isn't in the INSERT list).

####   Case 4: Backup schema == CURRENT

  Normal restore, no warnings.