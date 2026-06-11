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
- Oximetry columns are `NULL` → written as empty fields by `exportcsv.cpp`'s
  value handler (no special casing needed).
- No changes to filters, joins, ordering, or any other report.

## Implementation notes

- **Reload trigger:** `system_reports.orf` is imported "when a new version of
  OSCAR is detected" (file header; `reports_initializer.cpp`). Editing the file
  alone will not refresh an existing database. During implementation, confirm
  how to force a re-import for testing (version bump or a dev/force path) so the
  edited queries actually take effect — verify before claiming the change works.
- **Verification:** run each edited report from the Export CSV dialog (or via
  the standalone query) against a profile that has CPAP-only data and one with
  oximetry; confirm new columns populate, oximetry blanks where expected, and
  the relabeled Day percentile headers read `90th`.

## Out of scope

- Aggregated report variants.
- Schema changes / recomputing stored summaries.
- Fixing the day-vs-session percentile inconsistency (flagged → separate issue).
