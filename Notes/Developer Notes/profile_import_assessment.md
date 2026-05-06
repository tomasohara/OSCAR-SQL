# Profile Import Code Review — Assessment

**Source review:** `Notes/profile_import_review.md`
**Reviewed file:** `oscar/profileimporter.cpp`
**Assessment date:** 2026-03-01
**Fixes completed:** 2026-03-01

## Overall

The review was accurate and well-evidenced. All five findings were confirmed by the
code. All have now been fixed or formally deferred. One additional issue was found
and also fixed.

---

## Finding 1 — High: Preferences lost during import (FIXED)

The review was correct that preferences ended up as defaults. The exact mechanism
was more subtle than the review described.

`migrateMetadata` called `migrator.migrateProfile(profile)` — the `Profile*` overload
at `migration_manager.cpp:189`. That overload first called the path-based overload,
which opened a temp profile from the destination path. Since only `machines.xml` (not
`Profile.xml`) was copied there, the temp profile failed to open and `migrateExtendedData`
was skipped. The `Profile*` overload then called `migrateExtendedData` again on the
freshly-created destination profile (holding only defaults), writing defaults to the
database.

**Fix:** In the existing `sourceProfile` open block (which already opened the source
profile for user/doctor copying), look up the destination profile ID and call
`PreferencesRepository::saveAllPreferences` with the source profile's `cpap`, `oxi`,
`session`, `appearance`, and `general` sub-objects. This overwrites the defaults written
by `migrateExtendedData` with the actual source values. The incorrect comment claiming
preferences were handled by the migration was also removed.

**Additional fix (`.shg` files):** `daily.shg` and `overview.shg` (graph layout
settings stored directly in the profile root, written by `gGraphView::SaveSettings`)
were not copied during import. `copyProfileStructure` now copies all `*.shg` files
from the source profile root to the destination root. Failure per-file is non-fatal
(graph layout reverts to defaults).

---

## Finding 2 — High: Success reported despite persistence failures (FIXED)

All three sub-cases were real and have been fixed:

- **`profile->Save()` return value ignored:** Now checked; failure triggers a full
  rollback with `m_lastError` set.
- **Session `StoreToDatabase()` failures:** Now counted in `sessionFailures`. After
  the session loop, if any failures occurred, `loadMachineSessions` returns `false`
  with `m_lastError` set, causing the entire transaction to roll back.
- **`StoreEventsToDatabase()` failures:** Now counted in `eventFailures` and included
  in the same end-of-loop check and error message.

---

## Finding 3 — High: Machine folders silently skipped (FIXED)

A failed `findMachineByFolderName` now appends the folder name to a `skippedFolders`
list instead of silently `continue`-ing. After the loop, if any folders were skipped,
`m_lastError` is set naming the unmatched folders and `loadSessionsFromFiles` returns
`false`, causing a full rollback.

Note: `findMachineByFolderName` uses four fallback matching strategies (exact serial,
hexid, partial match, single-CPAP fallback), so real-world mismatches should remain
uncommon.

---

## Finding 4 — Medium: Missing error detail for zero-machine-count (FIXED)

`m_lastError` is now set to a descriptive message before returning `false` from the
zero-machine-count guard in `loadSessionsFromFiles`.

---

## Finding 5 — Medium: Reentrancy risk from `p_profile` swap (DEFERRED — comment added)

The swap of `p_profile` during import combined with `QApplication::processEvents()`
calls in `reportProgress()` is a theoretical reentrancy risk: any Qt timer or queued
signal that fires during a progress pump and reads `p_profile` would see the import's
temporary profile rather than the user's actual profile.

**Decision:** Not fixed. Risk is low in practice because the import dialog is modal
(blocking user interaction with the main window), the swap is correctly saved and
restored on all code paths, and no known timer in OSCAR reads `p_profile` during
import. A detailed warning comment has been added at the swap site explaining the
constraint and what to watch for if background tasks are added in future.

If this ever needs to be eliminated, the fix is to remove `processEvents()` from
`reportProgress()` and drive progress updates via queued signals instead.

---

## Additional Issue (FIXED)

**Unconditional `qDebug()` calls in `findMachineByFolderName`** produced excessive
log output during every import. All debug calls in that function are now wrapped in
`#ifdef DBDEBUG` / `#endif`, consistent with the guarded debug pattern. Define
`DBDEBUG` (e.g. in `oscar.pro` or per-file) to re-enable them when diagnosing
machine-matching problems.
