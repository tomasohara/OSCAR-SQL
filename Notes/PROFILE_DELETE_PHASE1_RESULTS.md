# Profile Deletion Phase 1 - Test Results

**Copyright (c) 2026 The OSCAR Team**

## Test Results Summary

**Test Date**: 2026-01-14  
**Phase**: Phase 1 (Deferred FK + WAL Checkpointing)

---

## Performance Results

### Small Profile Test
- **Sessions**: < 100
- **Deletion Time**: 5 seconds
- **Status**: ✅ EXCELLENT - Meets expectations

### Large Profile Test  
- **Sessions**: ~1,200
- **Data Size**: ~2.2 GB
- **Records**: ~880,000 (estimated)

**Timing Breakdown**:
| Component | Time | Notes |
|-----------|------|-------|
| Database deletion | 9 minutes | Down from 15 min baseline |
| File deletion | 2 minutes | Already optimized |
| **Total** | **11 minutes** | Down from ~17 min total |

**Improvement**: 40% faster (database component)

---

## Analysis

### What Worked Well ✅
1. **Small profiles**: Instant deletion (5 seconds)
2. **WAL checkpointing**: Likely helped with space reclamation
3. **No errors**: Clean compilation and execution
4. **Database integrity**: Assumed maintained (no reported issues)

### What Didn't Meet Expectations ⚠️
**Expected improvement**: 67-80% (15 min → 3-5 min)  
**Actual improvement**: 40% (15 min → 9 min)  
**Gap**: 27-40% less improvement than projected

### Why the Gap?

Several possibilities:

#### 1. Deferred Foreign Keys Not Batching Effectively
SQLite's `defer_foreign_keys` may not batch CASCADE deletes as efficiently as expected for:
- Complex cascade chains (profile → machines → sessions → 8 child tables)
- Large BLOB data (event_data table with 2GB of compressed data)
- Deeply nested relationships

#### 2. Index Maintenance Still Expensive
Even with batching, SQLite must:
- Update indexes on each child table during deletion
- Process 880,000+ records across multiple tables
- Maintain composite indexes during bulk deletes

#### 3. BLOB Deletion Overhead
The `event_data` table contains ~2 GB of compressed BLOB data:
- ~20,700 BLOB records to delete
- Each BLOB deletion requires freelist management
- BLOB fragmentation may slow processing

#### 4. SQLite Version Limitations
The effectiveness of `defer_foreign_keys` can vary by SQLite version and build options.

---

## Conclusion

**Phase 1 Status**: ✅ PARTIAL SUCCESS
- Achieved 40% improvement (significant)
- Better than no optimization
- Safe and stable implementation
- BUT: Not meeting 67-80% target

---

## Recommendation: Proceed to Phase 2

Phase 2 would bypass CASCADE entirely and provide:

### Direct Table Deletion Benefits
1. **Full control over deletion order**
   - Delete largest tables first (session_channel_values, event_data)
   - Minimize index maintenance overhead
   - Process in optimal batch sizes

2. **Batch operations with IN clauses**
   ```sql
   DELETE FROM session_channel_values 
   WHERE session_channel_id IN (
     SELECT id FROM session_channels 
     WHERE session_id IN (1,2,3,...,100)
   );
   ```
   Process 100 sessions at a time instead of one-by-one

3. **Progress reporting**
   - User-visible progress bar
   - Show "Deleting session data: 45% complete"
   - Much better UX than 9-minute black box

4. **Temporary index dropping**
   ```sql
   -- Drop indexes before bulk delete
   DROP INDEX IF EXISTS idx_session_channel_values_lookup;
   -- Perform bulk deletes (faster without index maintenance)
   -- Recreate index (if needed, though not for deletion)
   ```

### Estimated Phase 2 Performance
With direct deletion optimizations:
- **Target time**: 2-4 minutes (60-75% improvement from original)
- **More predictable**: Less dependent on SQLite's CASCADE implementation
- **Better UX**: Progress bar instead of waiting

---

## Phase 2 Implementation Effort

**Complexity**: Moderate  
**Risk**: Low (well-tested pattern)  
**Time to implement**: 2-3 hours  
**Code size**: ~200 lines

**Files to modify**:
- `profile_repository.cpp`: Add `removeOptimizedDirect()` method
- `profile_repository.h`: Add method declaration
- `profileselector.cpp`: Add progress callback support

---

## Alternative: Hybrid Approach

Keep Phase 1 optimizations (WAL checkpointing, cache size) and add:

### Quick Win: Batch Session Deletion

Instead of letting CASCADE handle everything, manually delete sessions in batches:

```cpp
// Phase 1.5: Batch session deletion
bool ProfileRepository::removeFaster(qint64 id)
{
    // ... Phase 1 setup (WAL checkpoint, transaction, PRAGMAs) ...
    
    // NEW: Get all session IDs for this profile
    QList<qint64> sessionIds;
    query.prepare(
        "SELECT s.id FROM sessions s "
        "JOIN machines m ON s.machine_id = m.id "
        "WHERE m.profile_id = ?"
    );
    query.bindValue(0, id);
    query.exec();
    while (query.next()) {
        sessionIds.append(query.value(0).toLongLong());
    }
    
    // Delete sessions in batches of 100
    const int batchSize = 100;
    for (int i = 0; i < sessionIds.size(); i += batchSize) {
        QStringList batch;
        for (int j = i; j < qMin(i + batchSize, sessionIds.size()); j++) {
            batch.append(QString::number(sessionIds[j]));
        }
        
        // Delete all child data for this batch
        QString inClause = batch.join(",");
        
        // Biggest table first
        query.exec(QString(
            "DELETE FROM session_channel_values "
            "WHERE session_channel_id IN ("
            "  SELECT id FROM session_channels WHERE session_id IN (%1)"
            ")"
        ).arg(inClause));
        
        query.exec(QString(
            "DELETE FROM event_data "
            "WHERE eventlist_id IN ("
            "  SELECT id FROM event_lists WHERE session_id IN (%1)"
            ")"
        ).arg(inClause));
        
        // Then delete the sessions (will CASCADE to smaller tables)
        query.exec(QString("DELETE FROM sessions WHERE id IN (%1)").arg(inClause));
    }
    
    // Finally delete profile (will CASCADE to profile-level tables)
    query.exec("DELETE FROM profiles WHERE id = ?");
    
    // ... Phase 1 cleanup (restore PRAGMAs, commit, WAL checkpoint) ...
}
```

**Estimated improvement**: 50-60% (15 min → 6-7 min)  
**Implementation time**: 30 minutes  
**Risk**: Very low

---

## Decision Matrix

| Approach | Time Savings | Implementation | Risk | UX |
|----------|-------------|----------------|------|-----|
| Phase 1 (current) | 40% | ✅ Done | Low | Basic |
| Phase 1.5 (batch) | 50-60% | 30 min | Very Low | Basic |
| Phase 2 (full) | 60-75% | 2-3 hours | Low | Excellent |

---

## Recommendation

**Option A** (Conservative): Deploy Phase 1 as-is
- 40% improvement is still valuable
- 9 minutes is much better than 15 minutes
- Zero additional development time
- **Best for**: Quick win, risk-averse

**Option B** (Balanced): Implement Phase 1.5
- 50-60% improvement achievable
- Only 30 minutes additional work
- Minimal risk (well-tested batch pattern)
- **Best for**: Maximum value/effort ratio

**Option C** (Optimal): Implement Phase 2
- 60-75% improvement achievable
- Progress bar provides much better UX
- More maintainable (explicit vs CASCADE)
- **Best for**: Long-term optimal solution

---

## Next Steps

### If staying with Phase 1:
1. ✅ Mark as complete
2. ✅ Document in release notes
3. ✅ Monitor production usage

### If proceeding to Phase 1.5:
1. [ ] Implement batch session deletion
2. [ ] Test with large profile
3. [ ] Compare results

### If proceeding to Phase 2:
1. [ ] Implement direct deletion with progress
2. [ ] Add progress callback to UI
3. [ ] Test thoroughly
4. [ ] Deploy

---

**Test Completed By**: User testing  
**Results Documented**: 2026-01-14  
**Status**: Phase 1 deployed, evaluating next steps
