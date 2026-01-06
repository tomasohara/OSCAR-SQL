# Performance Instrumentation Quick Start Guide

**Copyright (c) 2026 The OSCAR Team**

## Purpose

This guide shows you how to use the performance instrumentation to identify bottlenecks during SD card imports.

## Quick Start

### Step 1: Enable Instrumentation

Edit `oscar/SleepLib/performance_timer.h` and uncomment this line:

```cpp
//#define ENABLE_PERFORMANCE_INSTRUMENTATION
```

Change to:

```cpp
#define ENABLE_PERFORMANCE_INSTRUMENTATION
```

### Step 2: Rebuild OSCAR

```bash
# From oscar-code directory
qmake oscar/oscar.pro
make
```

### Step 3: Import SD Card Data

1. Launch OSCAR
2. Insert SD card or select SD card folder
3. Import data (File -> Import Data)
4. Wait for import to complete

### Step 4: Review Performance Report

Check the debug output (console or log file) for a report like this:

```
=================================================================
=== PERFORMANCE REPORT ===
=================================================================
Total Measured Time: 45.234 seconds

TIMING DATA (sorted by total time):
Operation                                  |    Count |  Total(s) |   Avg(ms) |   Min(ms) |   Max(ms) |     %
---------------------------------------------------------------------------------------------------
Session::StoreEventsToDatabase             |      120 |    28.456 |   237.13  |   145.23  |   523.45  |  62.9%
...
```

### Step 5: Analyze Results

Look for:
- Operations taking >20% of total time (biggest bottlenecks)
- Operations called many times with small avg (batching opportunities)
- High max times (variability issues)

## Adding Instrumentation to New Code

### Scope-Based Timing (Recommended)

```cpp
void MyClass::myFunction()
{
    PERF_TIMER_SCOPE("MyClass::myFunction");
    
    // Your code here
    // Timer automatically starts when entering function
    // and stops when leaving (RAII pattern)
}
```

### Manual Start/Stop

```cpp
void MyClass::complexFunction()
{
    PERF_TIMER_START("MyClass::complexFunction::Phase1");
    // Phase 1 code
    PERF_TIMER_STOP("MyClass::complexFunction::Phase1");
    
    PERF_TIMER_START("MyClass::complexFunction::Phase2");
    // Phase 2 code
    PERF_TIMER_STOP("MyClass::complexFunction::Phase2");
}
```

### Counting Events

```cpp
// Count number of sessions imported
PerformanceTimer::instance().increment("SessionsImported");

// Count bytes processed
PerformanceTimer::instance().increment("BytesProcessed", dataSize);
```

### Reporting Results

```cpp
// At end of import operation
PERF_TIMER_REPORT();

// To start fresh for next import
PERF_TIMER_RESET();
```

## Where to Add Instrumentation

### High-Level Operations
- `MachineLoader::finishAddingSessions()` - Overall import
- `Session::Store()` - Per-session save
- `Machine::SaveToDatabase()` - Machine save

### Database Operations
- `Session::StoreToDatabase()` - Session metadata
- `Session::StoreEventsToDatabase()` - Event data
- All `*Repository::create()` and `saveBatch()` methods

### Data Processing
- `EventDataRepository::storeEventListData()` - Binary storage
- `EventDataRepository::compressIfBeneficial()` - Compression
- `Session::percentile()` - Percentile calculations
- `Session::UpdateSummaries()` - Summary calculations

## Disabling Instrumentation

To disable (for release builds):

```cpp
// In performance_timer.h, comment out:
#define ENABLE_PERFORMANCE_INSTRUMENTATION
```

Or add to .pro file:
```qmake
# For debug builds only
CONFIG(debug, debug|release) {
    DEFINES += ENABLE_PERFORMANCE_INSTRUMENTATION
}
```

## Performance Impact

When **enabled**:
- Minimal overhead (<1% typical)
- Uses QElapsedTimer (high-resolution)
- Thread-safe (uses QMutex)

When **disabled**:
- Zero overhead (macros expand to nothing)
- No code generated

## Example Output Interpretation

```
Session::StoreEventsToDatabase    |  120 | 28.456 | 237.13 | 145.23 | 523.45 | 62.9%
```

Means:
- Called 120 times (once per session)
- Total 28.456 seconds (62.9% of measured time)
- Average 237ms per call
- Minimum 145ms (fastest session)
- Maximum 523ms (slowest session - investigate!)

**Action**: This is the #1 bottleneck. Focus optimization here.

### Understanding Timer Hierarchy

**Important**: When a function with `PERF_TIMER_SCOPE` calls another function that also has `PERF_TIMER_SCOPE`, each timer measures independently but the parent includes the child's time:

```
Session::Store                    |  120 | 30.000 | 250.00 |  ...  | ...   | 65.0%
  Session::StoreToDatabase        |  120 | 25.000 | 208.33 |  ...  | ...   | 54.0%
    Session::StoreDB::Settings    |  120 |  2.000 |  16.67 |  ...  | ...   |  4.3%
```

**How to read this:**
- `Session::Store` took 30s total (includes everything it does)
- `Session::StoreToDatabase` took 25s (called by Store, included in the 30s)
- `Session::StoreDB::Settings` took 2s (called by StoreToDatabase, included in the 25s)
- Time in `Store` **excluding** called functions: 30s - 25s = 5s
- Time in `StoreToDatabase` **excluding** Settings: 25s - 2s = 23s

**Key Point**: Timers measure **wall-clock time** including all called functions. To find time spent **only** in a function (exclusive time), subtract the times of functions it calls.

This hierarchical view helps you:
1. Identify high-level bottlenecks (which operations are slow)
2. Drill down into details (where within those operations is time spent)
3. Find unmeasured code (if sub-timers don't add up to parent time)

```
EventDataRepository::DBInsert      | 2400 |  1.234 |   0.51 |   0.15 |   5.23 |  2.7%
```

Means:
- Called 2400 times (20 EventLists × 120 sessions)
- Only 2.7% of time but many small calls
- Average 0.51ms per INSERT

**Action**: Good candidate for batching - could combine into fewer larger operations.

## Common Patterns

### Pattern 1: Database Transaction Wrapping

```cpp
void ImportData()
{
    PERF_TIMER_SCOPE("ImportData::Total");
    
    PERF_TIMER_START("ImportData::BeginTransaction");
    database.transaction();
    PERF_TIMER_STOP("ImportData::BeginTransaction");
    
    PERF_TIMER_START("ImportData::ProcessSessions");
    for (session : sessions) {
        session->save();
    }
    PERF_TIMER_STOP("ImportData::ProcessSessions");
    
    PERF_TIMER_START("ImportData::CommitTransaction");
    database.commit();
    PERF_TIMER_STOP("ImportData::CommitTransaction");
    
    PERF_TIMER_REPORT();
}
```

### Pattern 2: Repository Instrumentation

```cpp
qint64 SessionRepository::create(const SessionData& data)
{
    PERF_TIMER_SCOPE("SessionRepository::create");
    
    PERF_TIMER_START("SessionRepository::create::PrepareQuery");
    QSqlQuery query(getDatabase());
    query.prepare("INSERT INTO sessions ...");
    PERF_TIMER_STOP("SessionRepository::create::PrepareQuery");
    
    PERF_TIMER_START("SessionRepository::create::BindValues");
    query.bindValue(":field1", data.field1);
    // ... more bindings ...
    PERF_TIMER_STOP("SessionRepository::create::BindValues");
    
    PERF_TIMER_START("SessionRepository::create::Execute");
    query.exec();
    PERF_TIMER_STOP("SessionRepository::create::Execute");
    
    return query.lastInsertId().toLongLong();
}
```

## Tips

1. **Start Broad, Then Narrow**: Instrument high-level functions first, then add detail
2. **Use Consistent Naming**: `Class::Method::Phase` format helps
3. **Don't Over-Instrument**: Too many timers makes reports hard to read
4. **Focus on Loops**: Operations in loops multiply impact
5. **Measure Before Optimizing**: Profile first, optimize second

## Troubleshooting

**Q: I don't see a performance report**

A: Check that:
- ENABLE_PERFORMANCE_INSTRUMENTATION is defined
- You called PERF_TIMER_REPORT() 
- Debug output is visible (console or log file)

**Q: Times seem wrong**

A: Check for:
- Unmatched START/STOP pairs
- Forgot to STOP a timer
- Timer name typos (creates separate entry)

**Q: Report is empty**

A: Make sure:
- At least one PERF_TIMER_* macro was executed
- Code path was actually executed during test

## See Also

- `IMPORT_PERFORMANCE_ANALYSIS.md` - Detailed bottleneck analysis
- `performance_timer.h` - Implementation details
- Qt Documentation: QElapsedTimer, QMutex

---
**Last Updated**: 2026-01-05
**Author**: OSCAR Development Team
