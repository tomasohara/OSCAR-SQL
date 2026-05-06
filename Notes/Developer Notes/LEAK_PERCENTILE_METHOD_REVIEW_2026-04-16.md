# OSCAR Leak Percentile Method Review

Date: 2026-04-16  
Scope: Review OSCAR percentile calculation paths relevant to ResMed leak mismatches and identify likely issues.

## Reproduced Facts

1. `Blue Dragon 2025` (`C:\Users\Guy\Documents\oscar20_data`), day `2026-04-02`:
- STR (`Leak.95`) raw = `1`, gain `0.02 L/s`, so `1.2 L/min`.
- OSCAR/PLD-derived `p95` = `2.4 L/min`.
- Ratio = `2.0`.

2. Same profile, day `2026-03-29`:
- STR (`Leak.95`) raw = `5`, gain `0.02 L/s`, so `6.0 L/min`.
- OSCAR and ResScan both show `6.0 L/min`.

3. For `2026-04-02` PLD files, leak signal gain is consistent (`0.02 L/s` in each file), so this specific mismatch is not caused by mixed leak gains across files.

4. Day-boundary mismatch is unlikely here: ResMed is noon-to-noon and OSCAR day grouping under this dataset is also noon-to-noon.

## Code Path Review

### A) ResMed STR leak stats are parsed but not used for p50/p95

- STR values are parsed into `R.leak50`, `R.leak95`, `R.leakmax` in:
  - `oscar/SleepLib/loader_plugins/resmed_loader.cpp:1681-1691`
- But `StoreSummaryStatistics()` only persists max, not p50/p95:
  - `oscar/SleepLib/loader_plugins/resmed_loader.cpp:2540-2544`
  - p50/p95 lines are commented.

Impact:
- OSCAR leak percentile display is computed from PLD time-series, not directly from STR device-reported `Leak.95`.
- Therefore disagreement with ResScan/MyAir is possible (and observed on `2026-04-02`).

Assessment:
- This is a product-behavior mismatch, not necessarily a pure math bug.

### B) Session percentile math

- Session percentile uses time-weighted distribution from `m_timesummary`:
  - `oscar/SleepLib/session.cpp:2410-2513`
- Multi-percentile version:
  - `oscar/SleepLib/session.cpp:2527-2645`

Assessment:
- For ResMed PLD leak data, this method appears internally consistent.
- The `2026-04-02` value (`2.4`) is reproducible from PLD-windowed samples.

### C) Day percentile aggregation has a confirmed gain-handling bug risk

- Day aggregation merges raw keys from all sessions into `wmap`, then multiplies by a single `gain` variable at the end:
  - `oscar/SleepLib/day.cpp:365-387`
  - `oscar/SleepLib/day.cpp:399-421`
- This assumes gain is identical across sessions; code only logs if different:
  - `oscar/SleepLib/day.cpp:379-383`

Why this is a real bug risk:
- Multi-day/profile percentile code already does per-session gain normalization before combining values:
  - `oscar/SleepLib/profiles.cpp:2290-2310`
- `Day::percentile` does not follow that safer pattern.

Impact:
- If same channel has different gains across sessions in one day, `Day::percentile` can mis-scale values.
- Not the root cause for the tested `2026-04-02` case (gains were consistent there), but still a real defect candidate.

### D) `updateCountSummary()` waveform branch can over-accumulate time across multiple waveform eventlists

- In waveform path, `valsum` is cumulative across eventlists, then converted to time each loop iteration:
  - `oscar/SleepLib/session.cpp:1298-1314`

Risk:
- If a channel has multiple waveform eventlists, earlier counts can be re-applied multiple times.
- Leak path uses `EVL_Event` (not this branch), so this likely does not explain current leak mismatch.

### E) Secondary numerical/robustness concerns

1. `Day::percentile` has no explicit input-range guard (`percentile < 0` / `> 1`).
2. Event-time weighting in `updateCountSummary()` uses integer-second truncation for event deltas:
   - `oscar/SleepLib/session.cpp:1289-1293`
   - Usually minor for 2-second PLD data, but still quantizes durations.

## Likely Explanation For 2026-04-02 (Most Probable)

Most likely, OSCAR and ResScan are not using the same statistic source on that day:
- ResScan/MyAir are showing STR `Leak.95 = 1.2`.
- OSCAR is showing PLD-derived percentile (`2.4`) from in-window samples.

Given the current code, this is expected behavior when STR and PLD-derived percentiles diverge.

## Recommended Developer Actions

1. Decide intended behavior for leak percentiles:
- Option A: "ResMed-compatible mode" (prefer STR p50/p95 when present).
- Option B: Keep PLD-derived percentiles as primary, but display STR p95 as a separate "Device reported" metric.

2. Fix `Day::percentile` gain mixing regardless of decision:
- Convert raw->physical per session before inserting into `wmap` (align with `profiles.cpp` logic).
- Add a unit test with two sessions with different gains for same channel/value.

3. Add a targeted regression test suite for percentile consistency:
- `STR == PLD` day (`2026-03-29`-like case).
- `STR != PLD` day (`2026-04-02`-like case).
- Multi-session mixed-gain synthetic case.

4. If adopting STR-preferred behavior:
- Implement explicit storage/use of STR p50/p95 for relevant channels in session/day stats path.
- Keep provenance labeling in UI/export ("computed from PLD" vs "device-reported STR").

## Bottom Line

- I do not see evidence that OSCAR's percentile interpolation itself is fundamentally broken for this leak case.
- I do see:
  1) an intentional/legacy source mismatch (PLD-computed vs STR-reported), and  
  2) a real day-level gain aggregation defect risk in `Day::percentile`.
- The `2026-04-02` discrepancy is best explained by (1), not noon-boundary handling or mixed PLD gain on that day.
