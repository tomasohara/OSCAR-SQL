# SessionChannelData - Where Numbers Are Computed

## Overview

The numbers in the `SessionChannelData` structure are computed in `SleepLib/session.cpp` in the `Session` class.

## Main Entry Point

**`UpdateSummaries()`** method (around line 681) - Recalculates all session statistics during data import/loading.

## Individual Calculation Methods

All located in `session.cpp`:

| Method | Purpose |
|--------|---------|
| `avg(ChannelID id)` | Simple average from raw event data |
| `wavg(ChannelID id)` | Time-weighted average using `m_timesummary` |
| `Min(ChannelID id)` / `Max(ChannelID id)` | Min/max from EventLists |
| `cph(ChannelID id)` | Count per hour (count / session hours) |
| `sph(ChannelID id)` | Sum per hour |
| `calculatePercentiles(ChannelID id)` | Calculates median, p90, p95, p995 using time-weighted percentile calculation |
| `updateCountSummary(ChannelID id)` | Builds `m_valuesummary` and `m_timesummary` hashes |

## Database Storage

**`StoreToDatabase()`** method (around line 1280) populates `SessionChannelData` structures:
- Simple values come from hash maps: `m_avg`, `m_wavg`, `m_min`, `m_max`, `m_cph`, `m_sph`, `m_gain`, `m_firstchan`, `m_lastchan`
- Percentiles are calculated on-demand via `calculatePercentiles(id)`

---

# m_valuesummary and m_timesummary Explained

## What They Are

From `session.h`:

```cpp
QHash<ChannelID, QHash<EventStoreType, EventStoreType>> m_valuesummary;
QHash<ChannelID, QHash<EventStoreType, quint32>> m_timesummary;
```

| Structure | Type | Purpose |
|-----------|------|---------|
| `m_valuesummary` | `ChannelID → (raw_value → count)` | How many times each distinct value occurred |
| `m_timesummary` | `ChannelID → (raw_value → time_ms)` | Total time (milliseconds) spent at each value |

## How They Are Built

**`updateCountSummary()`** method builds both structures:

### For Event-type data (discrete events like apneas):
- Each distinct raw value is counted
- Time is calculated as the duration between consecutive events
- Example: If OA events occur at 10s, 15s, 20s, the time at each value is the interval between events

### For Waveform data (continuous samples like pressure, leak):
- Each distinct raw value is counted
- Time = `count × rate` (sample rate in milliseconds)
- Example: If pressure=10 appears 1000 times at 40ms sample rate, time = 40,000ms

## How They Are Used

### 1. Weighted Average (`wavg()`)
```cpp
for each (value, time) in m_timesummary[channel]:
    total_time += time
    weighted_sum += value × gain × time
wavg = weighted_sum / total_time
```

### 2. Percentiles (`calculatePercentiles()`)
```cpp
// Build weight map from time summary
for each (raw_value, time_seconds) in m_timesummary[channel]:
    total_time += time_seconds
    wmap[raw_value] += time_seconds

// Sort by value
// Find position in cumulative time distribution
// Linear interpolate between bracketing values
```

### 3. Time Above Threshold (`timeAboveThreshold()`)
```cpp
for each (raw_value, time_ms) in m_timesummary[channel]:
    actual_value = raw_value × gain
    if actual_value >= threshold:
        total_time += time_ms
return total_time / 60000.0  // convert to minutes
```

## How to Use Them for New Calculations

```cpp
// Example: Calculate time spent in specific pressure ranges
double timeInPressureRange(ChannelID id, double minPressure, double maxPressure) 
{
    auto ts = m_timesummary.find(id);
    auto gain = m_gain.value(id, 1.0);
    
    if (ts == m_timesummary.end()) return 0;
    
    quint32 totalMs = 0;
    for (auto it = ts.value().begin(); it != ts.value().end(); ++it) {
        double pressure = it.key() * gain;
        if (pressure >= minPressure && pressure <= maxPressure) {
            totalMs += it.value();  // time in ms at this pressure
        }
    }
    return totalMs / 60000.0;  // return minutes
}

// Example: Calculate standard deviation
double calculateStdDev(ChannelID id)
{
    updateCountSummary(id);  // Ensure summaries are built
    
    auto vs = m_valuesummary.find(id);
    auto ts = m_timesummary.find(id);
    auto gain = m_gain.value(id, 1.0);
    
    if (vs == m_valuesummary.end() || ts == m_timesummary.end()) return 0;
    
    // Calculate mean first (could use cached m_wavg)
    double mean = wavg(id);
    
    double sumSquaredDiff = 0;
    double totalTime = 0;
    
    for (auto it = ts.value().begin(); it != ts.value().end(); ++it) {
        double value = it.key() * gain;
        double time = it.value();
        double diff = value - mean;
        sumSquaredDiff += (diff * diff) * time;
        totalTime += time;
    }
    
    return sqrt(sumSquaredDiff / totalTime);
}
```

## Key Points

1. **Raw values** in the hashes are `EventStoreType` (typically `qint16`), NOT physical values
2. **Always multiply by `m_gain[channel]`** to get actual physical values
3. **Time is in milliseconds** in `m_timesummary`
4. **Call `updateCountSummary(channel)`** before using them to ensure they're populated
5. **These are MUCH faster than iterating EventLists** - they provide O(1) lookup by value
6. **They're stored in the database** via `SessionChannelValuesRepository` so they're available after reloading

## Database Persistence

The value/time summaries are stored in the `session_channel_values` table via:
- `SessionChannelValuesRepository::saveChannelSummaries()` - saves to database
- `SessionChannelValuesRepository::loadChannelSummaries()` - loads from database

This allows fast percentile and weighted average calculations even when raw event data is not loaded in memory.

---

*Document created: February 5, 2026*
