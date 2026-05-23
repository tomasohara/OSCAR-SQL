# Time Alignment Feature — Code Review

**Reviewed:** 2026-05-20
**Scope:** Last 11 commits on master (a962b74c..6544a8a0), oscar2-align-clocks branch merge
**Reviewer:** Claude (Opus 4.7) on behalf of Guy

Files reviewed:
- `oscar/SleepLib/machine.{cpp,h}`, `machine_common.h`, `session.{cpp,h}`, `day.cpp`, `profiles.{cpp,h}`
- `oscar/database/database_schema.{cpp,h}`, `device_time_correction_repository.{cpp,h}`
- `oscar/devicetimecorrectiondialog.{cpp,h,ui}`, `driftanalysisdialog.{cpp,h,ui}`,
  `driftplotwidget.{cpp,h}`, `timealignmentwelcomedialog.{cpp,h}`, `borrowingtimeedit.h`
- `oscar/daily.{cpp,h}`, `mainwindow.{cpp,h}`, `Graphs/*` minor changes, `oscar.pro`

---

## Critical issues

### 1. Clicking a drift row in the history table → nudge → Save deletes the drift row

**File:** `oscar/devicetimecorrectiondialog.cpp:783–833, 504–530`

`populateControlsFromRow()` sets `m_staged.id = row.id` for *any* clicked row, including
drift rows. Since `kTypeKeys` does not contain `"drift"`,
`kTypeKeys.indexOf("drift") == -1` and the combo defaults to `"offset"`. The user sees
`offset, +0` (drift rows have `offsetMs = 0`) and may nudge. On Save,
`onSaveStaged` re-reads type from the combo (now `"offset"`) and calls

```cpp
repo.markUndone(m_staged.id);                 // undoes the drift row
repo.upsertTyped(... "offset", m_staged.offsetMs);  // inserts new constant offset
```

So the drift model gets silently destroyed and replaced by a constant offset over the
drift row's date range — potentially decades of open-ended data.

`btnDeleteRow` is correctly disabled for drift rows in `updateControlStates()`, but the
Save path is not protected.

**Suggested fix:** In `onHistoryRowSelected`, bail out (or just show a read-only display)
if `row.type == "drift"`. Alternative: in `populateControlsFromRow`, when
`row.type == "drift"`, leave `m_staged.id = 0` so Save can't undo it.

---

### 2. Bookmark / event-tree drift uses an arbitrary session's correction, not the CPAP's

**File:** `oscar/daily.cpp:2232–2235, 2842–2844, 2891–2893`

Three places do this pattern:

```cpp
Day * dday = p_profile->GetDay(previous_date, MT_CPAP);
if (dday && !dday->sessions.isEmpty()) drift = dday->sessions.first()->correctionMs();
```

`Profile::GetDay(date, MT_CPAP)` returns the Day if that Day *contains* a MT_CPAP machine
— it does **not** filter `Day::sessions`. `Day::sessions` is the combined list across all
machine types on that date. `sessions.first()` may be an oximeter, position sensor, or
sleep-stage session, and its correction is unrelated to the CPAP's.

This breaks bookmark timestamp display whenever a day contains a non-CPAP device that
sorts first in the list.

**Suggested fix:**

```cpp
if (dday) {
    for (Session* s : dday->sessions) {
        if (s->type() == MT_CPAP) { drift = s->correctionMs(); break; }
    }
}
```

---

### 3. Legacy `clockDrift` migration loses corrections for future imports

**File:** `oscar/SleepLib/profiles.cpp:1099–1132`

The migration writes one single-night `"offset"` row per existing CPAP day, then sets
`clockDrift = 0`:

```cpp
for (const QDate& d : cpapMach->day.keys()) {
    corrRepo.upsertOffset(cpapMach->getDatabaseId(), d.toString(Qt::ISODate), driftMs);
}
```

The legacy `clockDrift` was a global constant applied to **every** CPAP session,
including future imports. After migration, sessions on dates not already in
`cpapMach->day.keys()` (future imports, future dates) get no correction. Behaviour
silently changes after migration for anyone who had `clockDrift` set.

The comment ("Migrate legacy clockDrift preference to per-night **drift rows**") is also
wrong — these are constant `"offset"` rows, not drift-model rows.

**Suggested fix:** Write a single open-ended row covering the user's data and forward:

```cpp
QDate earliest = *std::min_element(cpapMach->day.keyBegin(), cpapMach->day.keyEnd());
DeviceTimeCorrectionData row;
row.machineId = cpapMach->getDatabaseId();
row.dateFrom  = earliest.toString(Qt::ISODate);
row.dateTo    = "";   // open-ended
row.type      = "offset";
row.offsetMs  = driftMs;
row.reason    = "Migrated from legacy clockDrift preference";
corrRepo.create(row);
```

Single row, future-proof, intent-preserving.

---

## Important issues

### 4. Day classification on session load ignores corrections

**Files:** `oscar/SleepLib/session.cpp`, `oscar/SleepLib/day.cpp:120`, `oscar/SleepLib/machine.cpp:297–429`

`m_night` is only set in `Day::addSession`. But `Machine::AddSession` calls `s->first()`,
`s->last()`, and `s->last() - s->first()` at lines 314/326/353 — *before*
`dd->addSession(s)` at line 429. At those points `m_night == QDate()` (null), so
`Machine::correctionMs()` returns 0.

Result: day classification (`pickDate`, splitEpoch logic, combine-sessions logic) uses
raw uncorrected times. The old code applied `clockDrift` for MT_CPAP at these spots.
After migration, the behavior changes for users who previously had `clockDrift`
configured. For users with multi-hour timezone corrections, sessions could be filed
into a different day than under the old code.

The cached `m_firstchan`/`m_lastchan` *do* correctly store raw values and apply
correction at access time, so this is purely a day-bucketing concern, not a stale-cache
issue. But it's a behavioral regression worth a conscious decision.

---

### 5. Inconsistent "open-ended" representation

**File:** `oscar/devicetimecorrectiondialog.cpp:347–370`

`effectiveDateRange()` returns:
- `dateTo = ""` (NULL in DB) for open-ended **timezone** rows
- `dateTo = "2099-12-31"` for open-ended rows of **all other types**

`Machine::correctionMs()` and `findActive` happen to handle both, but:
- Data dated after 2099-12-31 silently loses corrections for non-timezone open-ended rows.
- Two representations of the same semantic concept → easy to introduce bugs when
  filtering, comparing, or migrating.

**Suggested fix:** Standardize on NULL (empty string in the wire format) for all
"open-ended" rows; update the bookkeeping in `closeOpenTimezoneRows` and similar to
work the same for all types.

---

### 6. Offset display & step widget cap at 24 hours

**Files:** `oscar/devicetimecorrectiondialog.cpp:265`, `oscar/borrowingtimeedit.h:56`

```cpp
// devicetimecorrectiondialog.cpp:265
ui->offsetTimeEdit->setTime(QTime(0, 0, 0).addMSecs(qAbs(displayMs)));

// borrowingtimeedit.h:56
newSecs = qMin(newSecs, 86399);
```

`QTime::addMSecs` wraps modulo 24h. `BorrowingTimeEdit` caps at 86399 seconds. A 25-hour
offset displays as 1h. Stacked travel + DST + drift can plausibly exceed 24h.

**Suggested fix:** Either clamp + show a warning, or render days separately (e.g.
"1d 02:00:00").

---

### 7. Daily date change discards staged correction silently

**Files:** `oscar/mainwindow.cpp:730–732`, `oscar/devicetimecorrectiondialog.cpp:87–105`

```cpp
// mainwindow.cpp
connect(daily, &Daily::dateLoaded, this, [this](QDate date) {
    if (m_correctionDialog) m_correctionDialog->setDate(date);
});

// devicetimecorrectiondialog.cpp setDate()
if (m_hasStagedChange)
    clearStagedAndRevert();    // silent loss of unsaved work
```

If the user has staged work and navigates the Daily view, their work is lost without
warning.

**Suggested fix:** Prompt (Save / Discard / Cancel) when `m_hasStagedChange` is true and
`setDate()` is called with a different date. Or simply don't auto-follow when there's
staged work.

---

## Minor issues

### 8. Stale comment on schema migration version

`oscar/database/database_schema.cpp:1456`:
```cpp
// Device time corrections table (schema version 16)
```
The migration is v17 (createSchema also runs it as part of v17).

---

### 9. `c1 + 1.0` slope encoding is a fragile sentinel

`oscar/SleepLib/machine.cpp:651` and `oscar/driftanalysisdialog.cpp:323`:

```cpp
// onUseDrift stores:
modelRow.c1 = m_fitSlope + 1.0;

// correctionMs recovers:
total -= row.c0Ms + qint64((row.c1 - 1.0) * double(t_noon));
```

The `+ 1.0` is used so `c1 != 0.0` distinguishes drift rows from constant rows. But the
schema already has a `type` column with `'drift'` as one value. Discriminate on
`type == "drift"` instead; store the raw slope in `c1`. Removes a non-obvious
convention and a tiny precision loss.

---

### 10. SQL float compared to integer

`oscar/database/device_time_correction_repository.cpp:97, 105, 173`:
```sql
AND c1 = 0
```
Works in SQLite (REAL 0.0 compares equal to integer 0), but brittle if any operation
ever produces a tiny non-zero c1 for what should be a constant row. Use `c1 = 0.0` for
clarity, or `(c1 IS NULL OR c1 = 0)` if we ever allow NULL. Better still: switch the
predicate to `type != 'drift'` (matches concern #9).

---

### 11. Linear regression precision for `tNoonMs` ~ 1.7×10¹²

`oscar/driftanalysisdialog.cpp:247–267` uses the un-centered formula:

```cpp
double denom = n * sxx - sx * sx;
double slope = (n * sxy - sx * sy) / denom;
```

For evenly spaced dates with `tNoonMs ≈ 1.7e12`, both terms in `denom` are ~3.85e29,
and the difference (the variance signal) is ~3.3e25. Double precision gives 15-16
significant digits, so we keep ~10 digits in the difference — fine for a 30-day window
of well-behaved data, but precision degrades for short windows or noisy data.

**Suggested fix:** Center tNoonMs around its mean before fitting:

```cpp
double tMean = sx / n;
for (auto& pt : pts) {
    sxx += (pt.tNoonMs - tMean) * (pt.tNoonMs - tMean);
    sxy += (pt.tNoonMs - tMean) * pt.offsetMs;
}
double slope = sxy / sxx;
double c0    = yMean - slope * tMean;
```

---

### 12. No outlier rejection in the drift fit

A single bad night with a multi-hour offset (manual clock change, DST forgetfulness)
will severely distort the slope. Standard mitigations: IQR-based filtering before fit,
or iteratively reweighted least squares. May be a planned future feature; flagging
in case it isn't.

---

### 13. Duplicate `rebuildMachine` helper

`oscar/devicetimecorrectiondialog.cpp:202–218` and `oscar/driftanalysisdialog.cpp:118–134`
contain identical logic. Move to a static helper (e.g. `Machine::reloadCorrectionsFromDb()`)
or a free function.

---

### 14. Sign-convention ambiguity in drift workflow

The corrections dialog adds `offsetMs` to the device's raw time (positive = device runs
forward). The drift fit, however, treats the reference device's `offsetMs` entries as
the value `cpap_raw − reference_raw` (positive = CPAP ahead of reference). After the
fit, the reference's offset entries are absorbed (`markUndone`) and the CPAP gets the
drift correction.

The math is internally consistent given this workflow, but the *user's* mental model
when entering offsets on the reference device is "I'm correcting the oximeter's clock"
— not "I'm describing how the CPAP runs ahead of the oximeter." A user who thinks of
themselves as adjusting the oximeter will get a sign-flipped drift model.

Not a code bug. Worth adding a clear note to the welcome dialog text or the drift
analysis dialog about what the reference-device offsets mean.

---

## Strengths

- Schema migration is properly transactional with `IF NOT EXISTS` and per-step rollback.
- `m_correctionCache` is correctly cleared by `rebuildCorrections()`.
- `m_firstchan` / `m_lastchan` store **raw** values and apply correction at access time —
  no risk of a cached value being stuck with a stale correction.
- The repository uses parameterized queries throughout (no SQL injection surface).
- `closeEvent` and `setDate` revert staged-but-uncommitted changes on the live Machine
  via `rebuildMachine`, so closing the dialog doesn't leave preview state behind.
- The drift dialog explicitly closes prior open-ended drift rows when a new fit is
  committed (`onUseDrift` lines 302–305) — good handling of model history.
- `correctionMs()` correctly sums all matching rows, so corrections stack
  (e.g. drift + offset).

---

## Recap

| # | Severity | Area | Summary |
|---|----------|------|---------|
| 1 | Critical | Dialog | Save flow on a clicked drift row destroys the drift model |
| 2 | Critical | Daily | Bookmark drift reads wrong session's correction in mixed-device days |
| 3 | Critical | Migration | Legacy clockDrift migration writes per-day rows, drops future-day coverage |
| 4 | Important | Load path | Day classification uses raw times because `m_night` isn't set in time |
| 5 | Important | Dialog | "Open-ended" stored as `""` for timezone, `"2099-12-31"` elsewhere |
| 6 | Important | Dialog | Time edit wraps at 24h |
| 7 | Important | UX | Daily date change silently discards staged correction |
| 8 | Minor | Docs | Stale "(schema version 16)" comment for v17 migration |
| 9 | Minor | Encoding | `c1 + 1.0` sentinel is fragile; use `type='drift'` instead |
| 10 | Minor | SQL | `c1 = 0` compares REAL to INTEGER |
| 11 | Minor | Math | Un-centered LSQ formula loses precision for short windows |
| 12 | Minor | Math | No outlier rejection in drift fit |
| 13 | Minor | Refactor | `rebuildMachine` duplicated across two dialogs |
| 14 | Minor | UX/Docs | Sign convention for reference-device offsets is non-obvious |
