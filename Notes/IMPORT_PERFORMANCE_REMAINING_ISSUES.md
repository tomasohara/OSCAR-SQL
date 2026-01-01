# Import Performance - Remaining Issues

**Date:** December 31, 2025  
**Copyright:** (c) 2026 The OSCAR Team

---

## Current Status

### ✓ Completed: Database Transaction Optimization
- **Result:** 2x performance improvement
- **Implementation:** Single transaction for entire import
- **Files modified:** 6 files in database layer + mainwindow

### ⚠ Remaining Issue: 57% Slowdown Still Occurs
Despite the 2x improvement, import still slows down at approximately 57% progress.

---

## Analysis: Where Is the Remaining Bottleneck?

Since database transactions are now batched but slowdown persists at the same point, the bottleneck is likely in **post-import processing** or **cumulative operations** that happen during or after the import.

---

## Areas to Investigate

### 1. SaveSummaryCache() - HIGH PRIORITY ⭐
**Location:** Called in `finishCPAPImport()` for each machine

```cpp
void MainWindow::finishCPAPImport()
{
    // ...
    QList<Machine *> machines = p_profile->GetMachines(MT_CPAP);
    for (Machine * mach : machines) {
        mach->saveSessionInfo();
        mach->SaveSummaryCache();  // ← SUSPECT
    }
    GenerateStatistics();
    // ...
}
```

**Why This Could Cause Slowdown:**
- Writes summary data for ALL sessions every time
- File grows larger as more sessions are imported
- No incremental update mechanism
- At 57%, cache file is already substantial

**How to Investigate:**
1. Add timing logs around `SaveSummaryCache()` calls
2. Monitor file size growth of summary cache files
3. Check if it's reading/writing entire cache each time

**Potential Fix:**
- Implement incremental cache updates
- Only write changed sessions to cache
- Use SQLite for cache instead of XML/binary files

### 2. GenerateStatistics() - MEDIUM PRIORITY
**Location:** Called in `finishCPAPImport()`

```cpp
void MainWindow::GenerateStatistics()
{
    Statistics stats;
    QString htmlStats = stats.GenerateHTML();
    // Processes ALL sessions for statistics
}
```

**Why This Could Cause Slowdown:**
- Recalculates statistics for entire dataset
- Processing time grows with number of sessions
- Called after every import operation

**How to Investigate:**
1. Add timing logs around `GenerateStatistics()`
2. Measure time vs. number of sessions
3. Check if stats are needed during import

**Potential Fix:**
- Defer statistics generation until import completes
- Use incremental statistics calculation
- Cache intermediate results

### 3. Machine::saveSessionInfo() - MEDIUM PRIORITY
**Location:** Called in `finishCPAPImport()` for each machine

**Why This Could Cause Slowdown:**
- Writes session info for all sessions
- File I/O grows with dataset size

**How to Investigate:**
1. Time this operation
2. Check file sizes and I/O patterns

### 4. Session::UpdateSummaries() - LOW PRIORITY
**Location:** Called during import for each session

**Why This Could Cause Slowdown:**
- Cumulative calculation overhead
- Memory usage grows

**How to Investigate:**
1. Profile memory usage during import
2. Check for O(n²) algorithms in summary calculations

### 5. UI Updates During Import - LOW PRIORITY
**Location:** Various progress updates

**Why This Could Cause Slowdown:**
- QApplication::processEvents() calls
- Frequent UI redraws

**Potential Fix:**
- Batch UI updates
- Reduce update frequency

---

## Recommended Investigation Order

1. **Add detailed timing logs** to identify the exact bottleneck:

```cpp
void MainWindow::finishCPAPImport()
{
    QElapsedTimer timer;
    timer.start();
    
    if (daily)
        daily->Unload(daily->getDate());
    qDebug() << "Daily unload:" << timer.restart() << "ms";

    p_profile->StoreMachines();
    qDebug() << "StoreMachines:" << timer.restart() << "ms";
    
    QList<Machine *> machines = p_profile->GetMachines(MT_CPAP);
    for (Machine * mach : machines) {
        mach->saveSessionInfo();
        qDebug() << "saveSessionInfo:" << timer.restart() << "ms";
        
        mach->SaveSummaryCache();
        qDebug() << "SaveSummaryCache:" << timer.restart() << "ms";
    }

    GenerateStatistics();
    qDebug() << "GenerateStatistics:" << timer.restart() << "ms";
    
    // ... rest of function
}
```

2. **Run import with logging enabled:**
   - Import 100 sessions
   - Import 200 sessions  
   - Import 365 sessions
   - Compare timing at different stages

3. **Analyze the logs** to identify which operation slows down most

4. **Focus optimization** on the slowest operation

---

## Data Collection Template

Create a test log with this format:

```
Sessions: 100
ImportCPAP: 120s
  - loader->Open(): 100s
  - ctx->Commit(): 20s
finishCPAPImport: 30s
  - Daily unload: 1s
  - StoreMachines: 2s
  - saveSessionInfo: 5s
  - SaveSummaryCache: 15s  ← BOTTLENECK?
  - GenerateStatistics: 7s
  
Sessions: 200  
ImportCPAP: 240s
  - loader->Open(): 200s
  - ctx->Commit(): 40s
finishCPAPImport: 90s
  - Daily unload: 1s
  - StoreMachines: 3s
  - saveSessionInfo: 12s
  - SaveSummaryCache: 55s  ← GROWS NON-LINEARLY?
  - GenerateStatistics: 19s
```

---

## Quick Test: Disable Post-Processing

To quickly identify if post-processing is the issue, temporarily comment out parts of `finishCPAPImport()`:

```cpp
void MainWindow::finishCPAPImport()
{
    // if (daily)
    //     daily->Unload(daily->getDate());

    p_profile->StoreMachines();
    QList<Machine *> machines = p_profile->GetMachines(MT_CPAP);
    for (Machine * mach : machines) {
        mach->saveSessionInfo();
        // mach->SaveSummaryCache();  // ← DISABLE THIS
    }

    // GenerateStatistics();  // ← DISABLE THIS
    
    // UI updates...
}
```

If import remains fast at 57% with these disabled, you've confirmed the bottleneck.

---

## Expected Root Causes (In Order of Likelihood)

1. **SaveSummaryCache() with growing cache file** (80% likely)
2. **GenerateStatistics() recalculating everything** (15% likely)
3. **Multiple factors combining** (5% likely)

---

## Long-Term Solutions

Once bottleneck is identified:

### For SaveSummaryCache():
- Move summary cache to SQLite database
- Implement incremental updates
- Only write changed sessions
- Use database indexes for fast lookups

### For GenerateStatistics():
- Cache intermediate calculations
- Implement incremental statistics
- Defer until import fully completes
- Use background thread for processing

### For Overall Performance:
- Consider progressive UI updates
- Implement import in background thread
- Show estimated time remaining based on actual performance

---

## Notes

- The 2x improvement from transaction batching proves database access was a bottleneck
- The persistent 57% slowdown suggests a different bottleneck also exists
- Cumulative file operations (like cache writes) are prime suspects
- Need empirical data from timing logs to confirm

---

## Next Steps

1. Add timing logs to finishCPAPImport()
2. Run controlled tests with 100, 200, 365 sessions
3. Analyze timing data to identify bottleneck
4. Implement targeted fix for identified bottleneck
5. Retest to verify improvement

---

**Status:** Investigation needed - logging infrastructure recommended  
**Priority:** Medium (2x improvement already achieved)  
**Estimated effort:** 4-8 hours for investigation + fix

---

**End of Document**
