# Weighted Percentile Review

Date: 2026-03-13

## Scope

Reviewed the weighted percentile implementations in:

- `oscar/SleepLib/day.cpp`
- `oscar/SleepLib/profiles.cpp`
- `oscar/SleepLib/session.cpp`

Also checked `Day::rangePercentile()` in `oscar/SleepLib/day.cpp`, but that function is explicitly non-weighted and is not part of the weighted-percentile comparison.

## Summary

The weighted percentile math itself is effectively the same in all implementations. If they are given the same weighted histogram and the same gain, they should return the same result.

However, there are coding differences around fallback and gain handling that can cause different returned values in some cases.

## Matching Algorithm

The core weighted percentile algorithm matches across:

- `Day::percentile()`
- `Profile::calcPercentile()`
- `Session::percentile()`
- `Session::calculatePercentiles()`

Shared behavior:

- Build a weighted map of value -> weight
- Sort by value using `ValueCount`
- Compute:
  - `p = 100.0 * percent`
  - `nth = SN * percent`
  - `nthi = floor(nth)`
- Return the current value immediately if cumulative weight exceeds `nthi`
- Otherwise, when exactly on a bucket boundary, linearly interpolate between adjacent values using:
  - `p1 = px * (sum1 - w1 / 2.0)`
  - `p2 = px * (sum2 - w2 / 2.0)`
  - `v = v1 + ((p - p1) / (p2 - p1)) * (v2 - v1)`

Relevant locations:

- `oscar/SleepLib/day.cpp` lines 429-476
- `oscar/SleepLib/profiles.cpp` lines 2347-2394
- `oscar/SleepLib/session.cpp` lines 2464-2512
- `oscar/SleepLib/session.cpp` lines 2585-2633

## Finding 1: Missing `m_timesummary` Is Handled Differently

This is the biggest behavioral difference.

### `Day::percentile()` and `Profile::calcPercentile()`

These functions check whether `m_timesummary` exists for the channel. If it does, they use time weighting. If it does not, they fall back to `m_valuesummary` counts.

Relevant locations:

- `oscar/SleepLib/day.cpp` lines 375-409
- `oscar/SleepLib/profiles.cpp` lines 2284-2318

### `Session::percentile()` and `Session::calculatePercentiles()`

These functions require `m_timesummary` to exist. If it is missing:

- `Session::percentile()` returns `0`
- `Session::calculatePercentiles()` returns `valid = false`

Relevant locations:

- `oscar/SleepLib/session.cpp` lines 2425-2433
- `oscar/SleepLib/session.cpp` lines 2542-2550

This difference can propagate into stored session statistics. In `Session::StoreToDatabase()`, invalid multi-percentile results are written as zeros.

Relevant location:

- `oscar/SleepLib/session.cpp` lines 2962-2971

### Impact

If a channel has value summaries but no time summary, then:

- day-level and profile-level calculations will still return a percentile using count weighting
- session-level calculations will return zero or invalid

So these implementations are not guaranteed to produce the same result in that situation.

## Finding 2: Missing Gain Is Handled Differently

There is also a difference in how missing or zero gain is treated.

### `Day::percentile()`

`Day::percentile()` reads gain with:

- `gain = sess->m_gain[code];`

That uses `QHash::operator[]`. If the key is missing, the default value is `0`. That gain is then applied directly when constructing `ValueCount` entries.

Relevant locations:

- `oscar/SleepLib/day.cpp` line 377
- `oscar/SleepLib/day.cpp` line 421

### `Profile::calcPercentile()`

`Profile::calcPercentile()` normalizes a missing or zero gain to `1`.

Relevant location:

- `oscar/SleepLib/profiles.cpp` lines 2276-2278

### `Session::percentile()` / `Session::calculatePercentiles()`

These use:

- `m_gain.value(id, 1.0)`

So missing gain also defaults to `1`.

Relevant locations:

- `oscar/SleepLib/session.cpp` line 2435
- `oscar/SleepLib/session.cpp` line 2553

### Impact

If summary data exists for a channel but gain is missing, `Day::percentile()` can scale values incorrectly, potentially collapsing them to zero, while `Profile` and `Session` will still use a gain of `1`.

That means `Day` can disagree with `Profile` and `Session` even though the interpolation logic matches.

## Non-Weighted Function Checked

`Day::rangePercentile()` is a different algorithm and is explicitly non-weighted.

Relevant location:

- `oscar/SleepLib/day.cpp` lines 574-618

It should not be expected to match the weighted percentile functions.

## Conclusion

The weighted percentile formulas are consistent across the main implementations, so with the same weighted input data they should produce the same numeric result.

They are not fully behaviorally equivalent, though. The following implementation differences can cause different outputs:

1. Missing `m_timesummary`
   - `Day` and `Profile` fall back to count-weighted data
   - `Session` returns zero/invalid

2. Missing gain
   - `Day` can use a default gain of `0`
   - `Profile` and `Session` default missing gain to `1`

Because of those differences, OSCAR cannot currently guarantee that all weighted percentile calculations will always return the same number.
