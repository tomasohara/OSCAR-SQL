# Design: Surface stored fields in the Day & Session CSV reports

**Date:** 2026-06-10
**Status:** Approved design — pending spec review before implementation.
**Author:** OSCAR dev session.

## Goal

Add fields that are **already stored** in the database but not yet shown in the
two non-aggregated CSV reports. Pure column surfacing — **no new calculations,
no aggregation, no schema change**. We do not compute any value we don't already
have stored.

## Scope

Edit only two report definitions in `oscar/docs/system_reports.orf`:

- `Daily Summaries / by Day`
- `Session Summaries / by Session`

The aggregated variants (`by Week`, `by Month`, and the Session `by Day/Week/
Month`) are **out of scope** — adding avg/percentile fields there would require
computing weighted averages or averaging percentiles, which violates the
"don't calculate what we don't have" rule.

## Background — current state

Source of truth for system reports: `oscar/docs/system_reports.orf`, imported
into the `report_tree` table. Tables involved:

- `daily_summaries` — one row per (profile, date); already aggregates a day.
- `session_summaries` — one row per session.

Today the `by Day` report shows 13 columns and `by Session` only 8, while both
tables store substantially more. See the column tables below for the gap.

## Percentile finding (important)

The stored "95th" columns are **not** what their name implies, and the two
levels are **inconsistent**:

- **Session** (`session.cpp:3389/3401`): `pressure95th = percentile(CPAP_Pressure, 0.95)`
  and `leakTotal95th = percentile(CPAP_LeakTotal, 0.95)` → a **true 95th**.
- **Day** (`daily_summary_repository.cpp:302/308`): `pressure95th = day->p90(CPAP_Pressure)`
  where `Day::p90()` = `percentile(code, 0.90F)` (day.cpp:483) → actually the **90th**.

Neither honors the user's percentile *preference* (`prefCalcPercentile()`); both
are hardcoded (90 day / 95 session). Minor: day uses `CPAP_Leak` (leak rate),
session uses `CPAP_LeakTotal` (total leak) — different channels.

**Decision (approved):** label honestly.
- Day report: relabel existing `Pressure_95th` → `Pressure_90th`,
  `Leak_95th` → `Leak_90th`.
- Session report: new `Pressure_95th` / `Leak_95th` are genuinely 95th.

**Flagged, out of scope:** the day-vs-session percentile-level mismatch (90 vs 95)
and the leak-channel difference are a stored-data inconsistency. File a separate
GitLab issue to align them (ideally honoring the user's percentile preference)
during implementation; do **not** fix here.

## `by Day` — final columns

One row per day from `daily_summaries ds`. **Bold** = new or relabeled.

| # | CSV header | Source | Notes |
|---|---|---|---|
| 1 | **Date** | `ds.date` | renamed from `Period` (this report only) |
| 2 | AHI | `ROUND(ds.ahi,2)` | |
| 3 | RDI | `ROUND(ds.rdi,2)` | |
| 4–8 | OA / UA / H / CA / RERA | the five `*_count` columns | |
| 9 | Pressure_Avg | `ROUND(ds.pressure_avg,2)` | |
| 10 | **Pressure_Min** | `ROUND(ds.pressure_min,2)` | new |
| 11 | **Pressure_Max** | `ROUND(ds.pressure_max,2)` | new |
| 12 | **Pressure_90th** | `ROUND(ds.pressure_95th,2)` | relabeled (stored p90) |
| 13 | Leak_Avg | `ROUND(ds.leak_total_avg,2)` | |
| 14 | **Leak_Max** | `ROUND(ds.leak_total_max,2)` | new |
| 15 | **Leak_90th** | `ROUND(ds.leak_total_95th,2)` | relabeled (stored p90) |
| 16 | **Leak_Unint_Avg** | `ROUND(ds.leak_unintentional_avg,2)` | new |
| 17 | **SpO2_Avg** | `ROUND(ds.spo2_avg,1)` | new; blank if no oximetry |
| 18 | **SpO2_Min** | `ROUND(ds.spo2_min,1)` | new; blank if no oximetry |
| 19 | **Pulse_Avg** | `ROUND(ds.pulse_avg,1)` | new; blank if no oximetry |
| 20 | **Pulse_Min** | `ROUND(ds.pulse_min,1)` | new; blank if no oximetry |
| 21 | **Pulse_Max** | `ROUND(ds.pulse_max,1)` | new; blank if no oximetry |
| 22 | Hours | `ROUND(ds.mask_on_hours,2)` | mask-on hours |
| 23 | **Total_Hours** | `ROUND(ds.total_hours,2)` | new |
| 24 | **Session_Count** | `ds.session_count` | new |
| 25 | **Compliant** | `ds.is_compliant` | new; 0/1 |

WHERE/ORDER unchanged (`profile_id`, date range, `ORDER BY ds.date`).

## `by Session` — final columns

One row per session from `session_summaries ss` joined to `sessions s` /
`machines m`. **Bold** = new.

| # | CSV header | Source | Notes |
|---|---|---|---|
| 1 | Date | `date(s.start_time/1000 - 43200,'unixepoch','localtime')` | OSCAR day |
| 2 | **Start** | `time(s.start_time/1000,'unixepoch','localtime')` | new; session time-of-day |
| 3 | AHI | `ROUND(ss.ahi,2)` | |
| 4 | **RDI** | `ROUND(ss.rdi,2)` | new |
| 5–8 | OA / UA / H / CA | the four `*_count` columns | |
| 9 | **RERA** | `ss.rera_count` | new |
| 10 | **Pressure_Avg** | `ROUND(ss.pressure_avg,2)` | new (no pressure today) |
| 11 | **Pressure_Min** | `ROUND(ss.pressure_min,2)` | new |
| 12 | **Pressure_Max** | `ROUND(ss.pressure_max,2)` | new |
| 13 | **Pressure_95th** | `ROUND(ss.pressure_95th,2)` | new; genuine 95th |
| 14 | **Leak_Avg** | `ROUND(ss.leak_total_avg,2)` | new (no leak today) |
| 15 | **Leak_Max** | `ROUND(ss.leak_total_max,2)` | new |
| 16 | **Leak_95th** | `ROUND(ss.leak_total_95th,2)` | new; genuine 95th |
| 17 | **SpO2_Avg** | `ROUND(ss.spo2_avg,1)` | new; blank if no oximetry |
| 18 | **SpO2_Min** | `ROUND(ss.spo2_min,1)` | new; blank if no oximetry |
| 19 | **Pulse_Avg** | `ROUND(ss.pulse_avg,1)` | new; blank if no oximetry |
| 20 | Hours | `ROUND(ss.mask_on_hours,2)` | mask-on hours |
| 21 | **Hours_Used** | `ROUND(ss.hours_used,2)` | new; total session hours |
| 22 | Machine | `m.model` | |

WHERE/ORDER unchanged (`m.profile_id`, date range, `s.enabled = 1`,
machine-type exclusion, `ORDER BY s.start_time`). `session_summaries` has no
`pulse_min`/`pulse_max`, so the Session report gets only `Pulse_Avg`.

## Conventions

- Numeric values rounded to 2 dp, except SpO2/Pulse to 1 dp.
- **Oximetry blanking:** `daily_summaries`/`session_summaries` store **0.0**
  (not NULL) for SpO2/Pulse when there is no oximetry. To deliver blank cells
  (and avoid a misleading "0.0% SpO2"), wrap those columns in
  `NULLIF(col, 0)` → NULL → empty CSV field. This is display suppression, not a
  calculation; 0 never occurs as a real SpO2/pulse reading. `Leak_Unint_Avg`
  is **not** wrapped — 0 is a legitimate value there.
- No changes to filters, joins, ordering, or any other report.

## Implementation notes

- **Reload mechanism (confirmed in `reports_initializer.cpp`):**
  1. `getSystemReportsFilePath()` reads the Qt **resource** `:/docs/system_reports.orf`
     first, so the file is embedded in the executable — **a rebuild is required**
     after editing `oscar/docs/system_reports.orf`.
  2. `checkAndUpdateReportVersion()` only re-imports system reports when the OSCAR
     **version string changes** (saved as `csv_reports_version` in
     `app_preferences`, category `Reports`). User custom reports are preserved.
  3. On a real release the version bump auto-updates every user. **To test on an
     unchanged dev version**, clear the marker so it re-imports on next launch:
     `DELETE FROM app_preferences WHERE category='Reports' AND key='csv_reports_version';`
- **Verification status:** both queries verified by direct SQL against a real
  `oscar.db` — Day 25 cols, Session 22 cols, new fields populate, oximetry
  blanks for CPAP-only profiles and shows real values for oximetry profiles,
  Day headers read `90th`, Session `95th`. The in-app path (rebuild + re-import)
  must be exercised in QtCreator to confirm end-to-end.

## Out of scope

- Aggregated report variants.
- Schema changes / recomputing stored summaries.
- Fixing the day-vs-session percentile inconsistency (flagged → separate issue).
