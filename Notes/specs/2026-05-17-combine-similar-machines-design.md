# Design: "Combine similar machines" preference for Changes to Device Settings report

**Date:** 2026-05-17
**Status:** Approved, ready for implementation plan
**Scope:** OSCAR 2.0 only. No backport to 1.7.1.

## Motivation

The Statistics page's *Changes to Device Settings* report (a.k.a. rxchanges)
groups adjacent days that share the same machine + mode + pressure relief +
pressure settings into a single row. Users who own multiple physical devices
of the same brand frequently swap between them (e.g., travel unit vs. home
unit, warranty replacements). When the swapped devices offer identical
therapy settings, they are therapeutically interchangeable, so producing two
separate rows for what is effectively the same prescription is noise.

This feature adds an opt-in preference that relaxes the merge predicate from
"same machine object" to "same brand", and replaces the per-machine Device
column with a brand-only label when enabled.

The default is **off** because brand-level equivalence is a presumption, not
a guarantee — users should opt in only when they have confirmed that their
multiple devices are interchangeable for their purposes.

## Requirements

### Preferences dialog

- Appearance panel → "Other Visual Settings" group.
- New `QCheckBox` named **`combineSimilarMachines`**.
  - Label: `Combine similar machines`
  - Tooltip: `Combine machines with same capabilities in Changes to Device Settings report`
  - Default: unchecked.
- Position: immediately **before** the existing `includeSerial` checkbox.

### Persisted setting

- Stored via `AppSetting` (same mechanism as `includeSerial`).
- Preference key: `CombineSimilarMachines`.
- Default value: `false`.

### Cache invalidation

- When the user clicks **OK** on the Preferences dialog AND the value of
  `combineSimilarMachines` changed compared to the value before the dialog
  was opened: delete `RXChanges.cache` from the profile data folder and
  trigger `mainwin->GenerateStatistics()` so the next view rebuilds.
- This matches the existing pattern in `mainwindow.cpp:2516` (purge path)
  and the `alternatingColorsCombo` handling already present in the
  preferences save path.

### rxchanges merge logic (statistics.cpp)

- When `combineSimilarMachines()` is **true**: two adjacent days merge into
  the same rxchanges row if their machines have the **same brand string**
  (`Machine::brand()`) in addition to the existing predicates (mode,
  pressure, pressure relief).
- When `combineSimilarMachines()` is **false**: behaviour is unchanged —
  merge requires identical `Machine*`.
- Brand comparison is on the raw brand string only (e.g. `"ResMed"`). No
  machine-type filtering — the existing mode/pressure predicates already
  prevent nonsensical merges across therapy modalities.

### rxchanges Device column display (statistics.cpp)

When rendering a row in `GenerateRXChanges()`:

| `combineSimilarMachines` | `includeSerial` | Device column shows                  |
| ------------------------ | --------------- | ------------------------------------ |
| false                    | false           | `model (modelnumber)`     *(today)*  |
| false                    | true            | `model (modelnumber) [serial]` *(today)* |
| true                     | (ignored)       | `brand`                              |

When Combine is ON, `includeSerial` is silently ignored for this report.
The Device Information table elsewhere on the Statistics page is unaffected
and continues to show serial numbers per machine.

## Implementation surface

Files expected to change:

| File                                | Change                                                   |
| ----------------------------------- | -------------------------------------------------------- |
| `oscar/preferencesdialog.ui`        | Add `combineSimilarMachines` checkbox before `includeSerial` |
| `oscar/preferencesdialog.cpp`       | Load checkbox; save with cache-invalidation side effect  |
| `oscar/SleepLib/appsettings.h`      | Add key constant, getter, setter                         |
| `oscar/SleepLib/appsettings.cpp`    | `initPref(STR_AS_CombineSimilarMachines, false)`         |
| `oscar/statistics.cpp`              | Update merge predicate (~lines 413, 613); update Device column rendering (~line 1345) |

No new files. No changes to `oscar.pro`. No DB schema change. No
`RXChanges.cache` file-format change (still serialises a single Machine
loaderName/serial per RXItem; merged rows store the first contributing
machine's reference).

## Behaviour details

### Which Machine* is stored on a merged row

When Combine is ON and adjacent days from different physical machines
merge into one RXItem, the RXItem's `machine` field holds the **first**
machine that began that run. Subsequent same-brand days extend the run
without overwriting the pointer. Since the Device column for merged rows
only renders `brand()`, the specific stored pointer is immaterial as long
as it is non-null and points to a machine of the correct brand — which is
guaranteed by the merge predicate.

### Cache file format compatibility

`RXChanges.cache` is invalidated by deletion when the toggle changes, so
the on-disk format is unchanged. Caches written by older builds remain
readable; they simply get discarded the first time a user toggles the new
setting.

### Interaction with other report consumers

`GenerateRXChanges()` is the single source for both the on-screen
Statistics page and the printed report (`reports.cpp` calls into the same
HTML). Both paths therefore pick up the new behaviour automatically. No
separate print-path change is needed.

## Out of scope

- No change to the **Device Information** table on the Statistics page;
  it continues to show one row per physical device with serial numbers.
- No backport to OSCAR 1.7.1.
- No `Notes/BUG_FIXES.md` entry — this is a feature, not a bug fix.
- No Preferences UI logic to grey out `includeSerial` when Combine is ON;
  the interaction is "silently ignored", per design decision.
- No persistence of the combine-mode flag inside the cache header
  (single-mechanism invalidation by deletion is sufficient).

## Open questions

None at design time.
