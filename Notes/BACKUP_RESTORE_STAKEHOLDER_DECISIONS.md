# Profile Backup/Restore - Stakeholder Decisions
**Date:** 2026-02-18 (updated)
**Status:** Approved
**Copyright:** Copyright (c) 2026 The OSCAR Team

---

## Review Questions - Answers

### 1. Should we support backup of individual machines (not just whole profile)?
**Decision:** NO - Only full profile backup is needed at this time.

**Rationale:** Backing up profiles for transfer is the critically needed component. Individual machine backup adds complexity without clear use case.

---

### 2. Should we implement encryption in Phase 1 or Phase 2?
**Decision:** POSTPONE - No encryption in Phase 1 or Phase 2.

**Rationale:** Keep MVP simple. Warn users that backup files contain sensitive unencrypted data and should be stored securely.

---

### 3. Should backup be accessible from UI or command-line (or both)?
**Decision:** UI ONLY for Phase 1.

**Rationale:** Command-line interface is optional and not required at this time. Focus on GUI user experience.

---

### 4. What should default conflict resolution be (rename vs abort)?
**Decision:** RENAME - Default to renaming the restored profile.

**Rationale:** Safest option that allows restore to proceed without data loss. Format: `username_restored_<timestamp>`

---

### 5. Should we support .001 file export for backward compatibility?
**Decision:** NO - No support for .001 files.

**Rationale:** All data is now in database. Legacy .001 file support adds unnecessary complexity.

---

### 6. Should users be able to export only a date range of sessions (partial export)?
**Decision:** YES — Date range selection is included in Phase 1.

**Rationale:** A key use case is sharing a subset of data (e.g., a few days or weeks) with a clinician or researcher without exposing the entire history. Exporting a short window also dramatically reduces file size and transfer time.

**Scope:**
- Date range applies to **high-volume tables only**: `daily_summaries`, `sessions`, `session_settings`, `session_channels`, `session_channel_values`, `respiratory_events`, `session_summaries`, `session_slices`, `event_lists`, `event_data`
- **Profile-level tables are always exported in full** (not date-filtered): `profiles`, `user_info`, `doctor_info`, `profile_preferences`, `channels`, `machines`
- Default selection is "Everything" (full profile export; existing behaviour unchanged)
- UI uses the same pattern as ExportCSV: a quick-range combo (Everything / Most Recent Day / Last Week / Last Fortnight / Last Month / Last 6 Months / Last Year / Custom) plus start/end `QDateEdit` widgets with calendar popups coloured for days with data
- Partial export filenames include the date range: `profile_backup_<username>_<startdate>_<enddate>_<timestamp>.oscar`
- Manifest records `export_options.is_partial`, `export_options.date_range.start_date`, and `export_options.date_range.end_date`
- Restoring a partial package **adds** the exported sessions to the target database; it does not require or expect a complete profile history

---

### 7. Should users be able to blank personal information before export (privacy mode)?
**Decision:** YES — Privacy mode is included in Phase 1.

**Rationale:** A user sharing data with a clinician or a developer for debugging should not be forced to expose personal details (name, DOB, address, etc.). A simple checkbox at export time covers this without requiring full encryption.

**Scope:**
- A checkbox "Replace personal information with blanks" on the BackupDialog (default: unchecked)
- When checked, **all data fields** in `user_info` and `doctor_info` are replaced with empty strings / NULL before writing to the SQL export. The `id` and `profile_id` columns are preserved for structural integrity
- The manifest records `export_options.privacy_applied: true` so the recipient knows personal data was removed
- Privacy mode can be combined with a date range export
- Privacy mode does **not** affect any other tables

---

## Additional Requirements & Constraints

### Schema Version
**Current Schema: v13** (not v8 as originally documented)

**Impact:** Update all references in documentation and code to reflect v13 as current schema.

---

### BLOB Data Compression

**Decision:** User BLOB data is most likely NOT compressed in database (for performance reasons).

**Implications:**
- BLOBs stored uncompressed in database for fast read/write
- BLOBs exported as-is (uncompressed) to SQL files
- Package-level ZIP compression provides file size reduction
- Uncompressed BLOB data likely to be >40-60% of total backup size (not 40-60% as originally estimated)

**Future Consideration:** Optional BLOB compression during backup is uncertain - postpone this decision.

---

### Cross-Platform Compatibility

**Requirement:** Must be able to restore to computer with different Endian style.

**Implementation Notes:**
- Qt's QDataStream handles endianness automatically when configured
- SQLite stores data in a standardized format (big-endian for integers)
- Hex-encoded BLOBs in SQL are endian-neutral
- Verify restore works between Windows (little-endian) and MacOS/Linux (can be either)

**Testing Required:**
- Test backup on Windows, restore on Mac
- Test backup on Mac, restore on Windows  
- Test backup on Linux (little-endian), restore on Linux (big-endian if available)

---

### Error Handling & User Notification

**Requirement:** All errors and qCritical conditions MUST be reported to user by UI.

**Implementation:**
- Every error path must emit signal to UI
- UI must display error in dialog (not just status bar)
- Critical errors require user acknowledgment (OK button)
- Error messages must be:
  - Clear and non-technical where possible
  - Actionable (tell user what to do)
  - Include error codes for support purposes

**Examples:**
```
✓ Good: "Backup failed: Not enough disk space. Please free up at least 500 MB and try again."
✗ Bad: "errno=28"

✓ Good: "Cannot restore profile: Username 'JohnDoe' already exists. Would you like to rename the restored profile to 'JohnDoe_restored'?"
✗ Bad: "Unique constraint violation on profiles.username"
```

---

### Alternative Approach: Full Database Backup

**Decision:** NOT of interest at this time.

**Rationale:** The SQLite backup API approach (backing up entire database) doesn't support:
- Individual profile backup
- Profile transfer between databases
- ID remapping for conflict resolution

**Conclusion:** SQL export approach is correct for the stated requirements.

---

### Security & Privacy

**Decision:** No security features implemented at this time.

**Requirement:** WARN user that backup contains sensitive data with no encryption or obfuscation.

**Implementation:**
- Display warning dialog before backup starts
- Warning text:
  ```
  WARNING: Backup Security
  
  The backup file will contain all your personal and medical data in 
  unencrypted form, including:
  - Personal information (name, date of birth, address)
  - Medical data (all CPAP sessions, events, statistics)
  - Doctor information
  
  Please store backup files securely:
  ✓ Use encrypted file systems or drives
  ✓ Store in secure locations only
  ✗ Do not email backup files
  ✗ Do not store on unsecured cloud storage
  
  [checkbox] I understand and will store my backup securely
  
  [Continue]  [Cancel]
  ```

---

## Updated Requirements Summary

### Must Have (Phase 1)
- ✓ Backup complete profile to .oscar file
- ✓ Restore profile from .oscar file
- ✓ Date range export — partial backup of selected sessions (decision #6)
- ✓ Privacy mode — blank personal data fields before export (decision #7)
- ✓ Handle username conflicts (rename by default)
- ✓ All data preserved (100% fidelity)
- ✓ Transactional (atomic) restore
- ✓ UI-based operation (File menu)
- ✓ Progress feedback
- ✓ All errors shown to user in dialogs
- ✓ Cross-platform compatible (different endianness)
- ✓ Security warning before backup
- ✓ Support schema v13

### Explicitly Excluded (Phase 1)
- ✗ Individual machine backup
- ✗ Encryption/password protection
- ✗ Command-line interface
- ✗ .001 file support
- ✗ BLOB compression during export
- ✗ Full database backup (SQLite API approach)

### Postponed (Future)
- Encryption support
- Command-line interface
- Selective backup by machine or data type
- Incremental backup
- Automated/scheduled backups
- Cloud integration
- BLOB compression optimization

---

## Performance Expectations

Given that uncompressed BLOBs are >40-60% of total size:

### Backup Size Estimates
- 1 year data (~365 sessions): 200-400 MB uncompressed, 100-250 MB compressed (ZIP)
- 5 years data (~1,825 sessions): 1-2 GB uncompressed, 500MB-1GB compressed (ZIP)
- 10 years data (~3,650 sessions): 2-4 GB uncompressed, 1-2 GB compressed (ZIP)

### Backup Time Estimates (with larger files)
- 1 year data: 60-120 seconds (was 30-60s)
- 5 years data: 3-5 minutes (was 2-4 min)
- 10 years data: 8-12 minutes (was 5-8 min)

### Restore Time Estimates
- 1 year data: 90-150 seconds (was 45-90s)
- 5 years data: 4-7 minutes (was 3-5 min)
- 10 years data: 10-15 minutes (was 6-10 min)

**Note:** Times will vary based on disk speed, CPU, and amount of BLOB data.

---

## Implementation Priorities

### Priority 1 (Critical)
1. Core backup/restore functionality
2. Date range export (partial backup)
3. Privacy mode (blank personal data)
4. Cross-platform compatibility (endianness)
5. Error handling and user notification
6. Security warning

### Priority 2 (Important)
1. Progress feedback
2. UI polish (BackupDialog with date range + privacy controls)
3. Conflict resolution
4. Validation and testing

### Priority 3 (Nice to Have)
1. Performance optimization
2. Backup size estimation
3. Package info viewer

---

## Testing Requirements

### Critical Tests
1. **Endianness Testing**
   - Backup on Windows → Restore on Mac
   - Backup on Mac → Restore on Windows
   - Verify data integrity (checksums, counts)

2. **Large Dataset Testing**
   - Test with 5+ years of real data
   - Monitor memory usage
   - Verify performance targets

3. **Error Handling Testing**
   - Verify all errors shown in UI dialogs
   - Test all error paths
   - Verify error messages are user-friendly

4. **Conflict Resolution Testing**
   - Username conflict → rename
   - Multiple conflicts
   - Verify new username format

5. **Date Range Testing**
   - Full export (Everything) — all sessions exported
   - Partial export — only sessions within selected range exported
   - Profile-level tables (machines, channels, prefs) always fully exported
   - Partial export filename includes date range component
   - Manifest `is_partial` and `date_range` fields correct
   - Restore partial package — sessions merged into existing profile

6. **Privacy Mode Testing**
   - Privacy mode off — user_info and doctor_info exported normally
   - Privacy mode on — all data fields blanked; id/profile_id preserved
   - Manifest `privacy_applied` field set correctly
   - Privacy mode combined with date range export

---

## Documentation Requirements

### User Documentation
- How to backup (with security warning explanation)
- How to restore (with conflict handling)
- Where to store backups securely
- Troubleshooting common issues

### Developer Documentation
- Endianness handling
- Error reporting patterns
- Testing procedures

---

## Sign-off

**Approved By:** Stakeholder
**Date:** 2026-01-06 (original); 2026-02-18 (decisions #6 and #7 added)
**Next Action:** Proceed with Phase 1 implementation

---

**End of Document**
