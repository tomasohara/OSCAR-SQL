# Design: Purge Range of Days

**Date:** 2026-05-16  
**Status:** Approved

## Summary

Add a "Purge Range of Days" action under Data → Advanced that lets the user delete
data for a contiguous date range in one operation, with the same per-day granularity
as the existing "Purge Current Selected Day" feature.

---

## Menu placement

- Location: **Data → Advanced**, immediately after the "Purge Current Selected Day" submenu.
- New action name: `actionPurgeRangeOfDays`
- Label: `Purge Range of Days`
- Added in `oscar/mainwindow.ui`.

---

## Dialog — `PurgeRangeDaysDialog`

**New files:** `oscar/purgerangedaysdialog.h`, `oscar/purgerangedaysdialog.cpp`  
**Added to:** `oscar/oscar.pro`

### Inputs

| Control | Type | Default | Constraint |
|---|---|---|---|
| Start date | `QDateEdit` | `initialDate` passed to constructor | — |
| End date | `QDateEdit` | `initialDate` passed to constructor | ≥ start date |
| Data type | `QButtonGroup` (6 radio buttons) | CPAP selected | Mutually exclusive |

### Data type radio buttons (same labels as single-day purge menu)

| Label | `MachineType` passed to `purgeDayData()` |
|---|---|
| CPAP | `MT_CPAP` |
| Oximetry | `MT_OXIMETER` |
| Sleep Stage | `MT_SLEEPSTAGE` |
| Position | `MT_POSITION` |
| All except Notes | `MT_UNKNOWN` |
| All including Notes | `MT_JOURNAL` |

### Buttons

- **OK** — validates end ≥ start; if invalid, shows inline error and does not close.
- **Cancel** — closes without action.

### Confirmation

After OK, before any data is touched, a `QMessageBox` asks:

> *Purge \<type\> data from \<start\> to \<end\> (\<N\> days). Are you absolutely sure?*

`Yes` proceeds; `No` returns to the dialog.

---

## Core purge logic refactor

### New private helper: `purgeDayData(QDate date, MachineType type)`

Extracted from the existing `purgeDay()`. Contains only destructive work — no UI calls:

1. Calls `p_profile->GetDay(date, MT_UNKNOWN)` to get the Day record.
2. Iterates sessions; collects those matching `type` (same filter logic as today).
3. For each matched session: calls `sess->Destroy()` then `delete sess`.
4. Calls `mach->SaveSummaryCache()` for each affected machine.
5. Updates `cpap->setPurgeDate()` if a CPAP session was purged and the date is earlier than the current purge date.
6. Returns `true` if any sessions were purged, `false` otherwise.

Also removes the RX cache and Summaries XML (as today) when a CPAP session is purged.

### Refactored `purgeDay(MachineType type)`

Becomes a thin wrapper around `purgeDayData()`:

1. Gets date from `daily->getDate()`.
2. Calls `daily->Unload(date)`.
3. Calls `purgeDayData(date, type)`.
4. If nothing purged: returns early (same as today).
5. Recalculates / invalidates daily summary for the date.
6. Handles journal notes editor clear if `type == MT_JOURNAL`.
7. Calls `daily->clearLastDay()` then `daily->LoadDate(date)`.
8. Reloads overview, welcome, statistics.

Behaviour is identical to today.

---

## Range purge — `on_actionPurgeRangeOfDays_triggered()`

### Flow

1. Opens `PurgeRangeDaysDialog(daily->getDate(), this)` (pre-filled with current Daily view date).
2. If rejected: returns.
3. Shows confirmation `QMessageBox` with range and type.
4. If rejected: returns.
5. Calls `daily->Unload()`.
6. Creates a modal `QProgressDialog` (min=0, max=number of days in range, parent=`this`).
7. Loops `date` from `startDate` to `endDate` inclusive:
   - Calls `purgeDayData(date, type)`.
   - Advances progress bar by 1.
   - Calls `QApplication::processEvents()` to keep UI responsive.
   - If user clicked Cancel on progress dialog: breaks out of loop.
8. After loop (or cancel): recalculates / invalidates daily summaries for all dates
   that had data purged.
9. Reloads UI: `daily->clearLastDay()`, `daily->LoadDate(currentDate)`, 
   `overview->ReloadGraphs()`, `welcome->refreshPage()`, `GenerateStatistics()`.
10. If no data was found for any day in the range: informs the user with a
    `QMessageBox::information`.

### Cancel behaviour

Days purged before the user clicks Cancel remain purged. There is no rollback.
This matches the behaviour of other destructive operations in OSCAR.

---

## Files changed

| File | Change |
|---|---|
| `oscar/mainwindow.ui` | Add `actionPurgeRangeOfDays` action and menu item |
| `oscar/mainwindow.h` | Declare slot and `purgeDayData()` helper |
| `oscar/mainwindow.cpp` | Implement slot, refactor `purgeDay()`, implement `purgeDayData()` |
| `oscar/purgerangedaysdialog.h` | New — dialog class declaration |
| `oscar/purgerangedaysdialog.cpp` | New — dialog class implementation |
| `oscar/oscar.pro` | Add new dialog source files |
