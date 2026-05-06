# ResMed Leak95 Investigation Summary (2026-04-16)

## Context

Users reported that OSCAR Leak Rate statistics (especially 95th percentile) can be much higher than ResMed MyAir/ResScan values, sometimes close to 2x.

Investigation target:

- Determine whether OSCAR has an error in leak percentile calculations.
- Separate day-level differences from period (30/90/365 day) differences.

## What Was Compared

- OSCAR (daily and period statistics)
- ResScan (ResMed)
- MyAir (ResMed)
- Source data from `Blue Dragon 2025` profile (multiple databases and date ranges)

## Key Findings

1. Day-level agreement can be exact.
- Example: `2026-03-29` had matching values across OSCAR and ResScan:
  - Leak median `0.0`
  - Leak 95th `6.0`

2. Day-level mismatches also occur for a subset of days.
- Example: `2026-04-02`
  - OSCAR day p95 (from PLD): `2.4`
  - ResScan/MyAir (from STR): `1.2`
- Added diagnostics showed many days where PLD-derived day p95 > STR day p95.
- In the sampled diagnostic log set, differences were commonly quantized (most often `+1.2`, then `+2.4`).

3. Period-level method difference is large and systematic.
- OSCAR period Leak95 currently uses a percentile over all leak samples in the date range.
- MyAir/ResScan period behavior appears much closer to an average of daily STR Leak95 values.
- This alone can create large differences, even before PLD-vs-STR day bias is considered.

## Strong Evidence From Period Diagnostics

For one import run:

- 30-day window:
  - `OSCAR_period_p95=4.8`
  - `OSCAR_day_p95_wavg=3.776`
  - `OSCAR_day_p95_avg=3.931`
  - `STR_p95_wavg=3.355`
  - `STR_p95_avg=3.559`
- 90-day window:
  - `OSCAR_period_p95=6.0`
  - `OSCAR_day_p95_wavg=4.643`
  - `OSCAR_day_p95_avg=4.504`
  - `STR_p95_wavg=4.226`
  - `STR_p95_avg=4.094`
- 365-day window:
  - `OSCAR_period_p95=8.4`
  - `OSCAR_day_p95_wavg=7.151`
  - `OSCAR_day_p95_avg=7.163`
  - `STR_p95_wavg=6.440`
  - `STR_p95_avg=6.452`

Interpretation:

- `OSCAR_period_p95` is consistently higher than STR averages.
- Averaging OSCAR daily p95 reduces the gap substantially.
- STR averages are still lower than OSCAR daily averages, consistent with day-level PLD-vs-STR differences.

## Conclusion

There is no single universal "bug" explanation. The observed inflation vs MyAir/ResScan is primarily a stacked effect:

1. Metric definition mismatch:
- OSCAR period Leak95 = global percentile of all period samples.
- MyAir/ResScan appears closer to average of daily STR Leak95 values.

2. ResMed source-channel mismatch on some days:
- OSCAR day p95 is derived from PLD waveform data.
- ResMed-reported day p95 appears STR-derived.
- PLD-derived day p95 is often higher for affected days.

These effects are ResMed-specific and should not be assumed for all manufacturers.

## User-Facing Explanation (Suggested)

"OSCAR and MyAir/ResScan are using different methods for long-range Leak95. OSCAR calculates a percentile over all leak samples in the selected period, while MyAir/ResScan appears to use an average of daily leak percentiles from ResMed summary data. On some ResMed days, the PLD-derived daily percentile used by OSCAR is also higher than the STR summary percentile used by ResMed tools. Together, this can make OSCAR period Leak95 look much higher, sometimes near 2x."

## Code Status

Temporary investigation diagnostics were left in source but disabled (commented out / `#if 0`) in:

- `oscar/SleepLib/loader_plugins/resmed_loader.cpp`

Disabled diagnostics include:

- Day-level STR vs PLD p95 mismatch logging
- Period-level OSCAR vs STR aggregate comparison logging

