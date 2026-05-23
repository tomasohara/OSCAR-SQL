# Time Alignment Code Review

Reviewed: 2026-05-20
Scope: last 11 commits on `master` (`HEAD~11..HEAD`), including the
`oscar2-align-clocks` merge.

This review focused on runtime behavior in the time-alignment feature:
correction storage, migration, session/day time accessors, Daily display, and
the Time Corrections / Drift Analysis dialogs.

## Findings

### P1: Unsaved previews can remain applied after switching devices

File: `oscar/devicetimecorrectiondialog.cpp`

`onDeviceChanged()` calls `clearStagedAndRevert()` after the tree selection has
already changed:

- `onDeviceChanged()` starts at line 232.
- `clearStagedAndRevert()` rebuilds `currentMachine()` at line 435.
- `previewStaged()` applies preview rows to the staged machine at line 426.

Because `currentMachine()` now returns the newly selected device, the previous
device keeps the preview correction rows in memory. This can leave Daily graphs
showing an unsaved correction until that previous machine is rebuilt by some
later action.

Suggested fix: rebuild the previously selected machine when discarding a staged
preview. The slot already receives the `previous` item, or the dialog can keep a
`m_stagedMachine` pointer and always revert that machine.

### P1: Editing a drift history row can replace the drift model with an offset

File: `oscar/devicetimecorrectiondialog.cpp`

`populateControlsFromRow()` seeds `m_staged.id` for every selected history row,
including drift rows:

- `m_staged.id` is assigned at line 788.
- `kTypeKeys` does not include `"drift"` at line 26.
- The combo falls back to index 0, `"offset"`, at line 804.
- Save marks the original row undone and writes the current combo type at
  lines 521-524.

So selecting a drift row, nudging, and saving can mark the drift row undone and
replace it with a constant offset row over the same date range.

Suggested fix: make drift rows read-only in the Time Corrections dialog, or
reject save/nudge when `m_staged.type == "drift"`. If drift editing is desired,
it should go through Drift Analysis so the model coefficients are preserved.

### P1: Legacy `clockDrift` migration loses future coverage

File: `oscar/SleepLib/profiles.cpp`

The migration at line 1098 writes one offset row per currently loaded CPAP day,
then clears the global `clockDrift` preference at line 1113.

The old `clockDrift` preference was global and applied to every CPAP session,
including future imports. After migration, future dates and any missing
historical dates have no correction.

Suggested fix: preserve the old behavior with one open-ended row, for example
from the earliest known CPAP day forward:

```cpp
DeviceTimeCorrectionData row;
row.machineId = cpapMach->getDatabaseId();
row.dateFrom  = earliest.toString(Qt::ISODate);
row.dateTo    = "";
row.type      = "offset";
row.offsetMs  = driftMs;
row.reason    = "Migrated from legacy clockDrift preference";
corrRepo.create(row);
```

The comment also says "per-night drift rows", but the migration writes constant
`"offset"` rows.

### P2: Corrections load too late for day bucketing and summary calculation

Files:

- `oscar/SleepLib/profiles.cpp`
- `oscar/SleepLib/machine.cpp`
- `oscar/SleepLib/day.cpp`

Machines and sessions are loaded around `profiles.cpp:1015`, summaries may be
calculated at `profiles.cpp:1068`, and correction rows are loaded only after
that at `profiles.cpp:1076`.

During `Machine::AddSession()`, the code uses `s->first()` and `s->last()` for
filtering, session length, split-date selection, and combine logic before
`Day::addSession()` sets `m_night`:

- `Machine::AddSession()` checks/uses corrected accessors at lines 314, 326,
  353, 360, 373, 379, and 395.
- `Day::addSession()` sets `s->m_night = d_date` at line 120.
- `Session::correctionMs()` depends on `m_night`.

This means day bucketing uses raw timestamps, not corrections. That is a
behavioral regression from legacy `clockDrift`, especially for sessions near the
OSCAR noon boundary or with large timezone corrections.

Suggested fix: load machine correction rows before sessions are added, or provide
an AddSession path that can compute the candidate OSCAR day using the relevant
correction without depending on `m_night` already being set.

### P2: Bookmark correction can come from the wrong device

Files:

- `oscar/daily.cpp`
- `oscar/SleepLib/profiles.cpp`

Daily bookmark code uses `dday->sessions.first()->correctionMs()` at:

- `daily.cpp:2234`
- `daily.cpp:2843`
- `daily.cpp:2893`

But `Profile::GetDay(date, MT_CPAP)` only checks that the day contains a CPAP
machine at `profiles.cpp:1341`; it does not filter `Day::sessions`. On mixed
device days, `sessions.first()` can be an oximeter, position sensor, or sleep
stage session, so bookmark display/navigation can use the wrong correction.

Suggested fix: use `day->firstSession(MT_CPAP)` or loop for an enabled CPAP
session.

### P2: Date changes silently discard staged edits

Files:

- `oscar/mainwindow.cpp`
- `oscar/devicetimecorrectiondialog.cpp`

`Daily::dateLoaded` updates the open Time Corrections dialog at
`mainwindow.cpp:730`. `DeviceTimeCorrectionDialog::setDate()` then clears any
staged correction at `devicetimecorrectiondialog.cpp:87` without prompting.

Navigating Daily while the dialog is open can therefore discard an unsaved
correction.

Suggested fix: if `m_hasStagedChange` is true, either prompt Save / Discard /
Cancel before following the new date, or do not auto-follow Daily navigation
until the staged change is resolved.

### P3: Offset editor wraps or caps values above 24 hours

Files:

- `oscar/devicetimecorrectiondialog.cpp`
- `oscar/borrowingtimeedit.h`

`refreshCurrentOffset()` displays the absolute offset with
`QTime(0, 0, 0).addMSecs(...)` at line 265, which wraps modulo 24 hours.
`BorrowingTimeEdit::stepBy()` clamps to `86399` seconds at line 56.

A 25-hour correction can display as 1 hour or become impossible to enter via
stepping.

Suggested fix: either explicitly clamp with a warning or display/edit offsets as
days plus `HH:mm:ss`.

## Verification

Commands run:

```text
git diff --stat HEAD~11..HEAD
git diff --name-only HEAD~11..HEAD
git diff --check HEAD~11..HEAD
```

`git diff --check` reported trailing whitespace in added Notes/spec files, but
not in the C++ time-alignment files. I did not run a full build or automated
tests.
