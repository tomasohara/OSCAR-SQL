# Profile Backup/Restore Feature - Documentation Index
**Date:** 2026-01-06  
**Status:** Ready for Implementation  
**Copyright:** Copyright (c) 2026 The OSCAR Team

---

## Overview

This folder contains the complete design and implementation specifications for the Profile Backup/Restore feature for OSCAR. This feature allows users to backup complete profiles (including all session data and waveforms) to a portable .oscar file and restore them to any OSCAR installation.

---

## Document Index

### 1. BACKUP_RESTORE_STAKEHOLDER_DECISIONS.md
**Purpose:** Approved stakeholder decisions and requirements  
**Status:** ✅ APPROVED  
**Read This First!**

Contains answers to design questions and additional requirements:
- Schema v9 support
- No encryption (with security warnings)
- UI-only interface
- Rename as default conflict resolution
- Cross-platform endianness compatibility
- Error handling requirements
- Updated performance expectations

**👉 Start here to understand approved scope and requirements**

---

### 2. PROFILE_BACKUP_RESTORE_DESIGN.md
**Purpose:** Comprehensive technical design specification  
**Status:** ⚠️ NEEDS UPDATE - Currently reflects v8, needs v9 updates  

Contains:
- Backup/restore process flows
- Package format (.oscar file structure)
- SQL export format with examples
- Class designs (ProfileBackup, ProfileRestore)
- Error handling strategy
- Security considerations
- Schema compatibility matrix
- Testing strategy

**Note:** This document needs minor updates to reflect stakeholder decisions (schema v9, BLOB compression notes, etc.). Use in conjunction with BACKUP_RESTORE_STAKEHOLDER_DECISIONS.md.

---

### 3. BACKUP_RESTORE_IMPLEMENTATION_PLAN.md
**Purpose:** Detailed implementation roadmap  
**Status:** ⚠️ NEEDS UPDATE - Needs v9 and stakeholder decision updates

Contains:
- 5-phase implementation plan with milestones
- Detailed task breakdown
- Code structure and file organization
- Testing requirements
- Timeline estimates (3-4 weeks)
- Risk mitigation strategies
- Success criteria

**Note:** Use this as implementation guide, but incorporate decisions from BACKUP_RESTORE_STAKEHOLDER_DECISIONS.md.

---

## Key Requirements Summary

### What We're Building
✅ **Profile Backup:** Export complete profile to .oscar file (ZIP format)  
✅ **Profile Restore:** Import profile from .oscar file to database  
✅ **Conflict Handling:** Automatically rename if username exists  
✅ **Data Integrity:** 100% fidelity, transactional restore  
✅ **Cross-Platform:** Works across Windows/Mac/Linux with different endianness  
✅ **Security:** Warning dialogs about unencrypted data  
✅ **UI Integration:** File menu → Backup Profile / Restore Profile  

### What We're NOT Building (Phase 1)
❌ Individual machine backup  
❌ Encryption/password protection  
❌ Command-line interface  
❌ .001 file support  
❌ BLOB compression during export  
❌ Full database backup (SQLite API)  

---

## Technical Highlights

### Architecture
- **Database-Only**: All data in SQLite (schema v9), including waveforms as BLOBs
- **SQL Export**: Export to SQL INSERT statements with ID placeholders
- **BLOB Handling**: Hex-encoded BLOBs (X'...') for portability
- **ID Remapping**: Automatic remapping of auto-increment IDs during restore
- **Transactions**: Atomic restore with automatic rollback on error

### Package Format
```
profile_backup_<username>_<timestamp>.oscar  (ZIP file)
├── manifest.json                # Metadata, checksums, statistics
└── database/
    ├── profile.sql
    ├── user_info.sql
    ├── doctor_info.sql
    ├── preferences.sql
    ├── channels.sql
    ├── daily_summaries.sql
    └── machines/
        └── machine_<id>_sessions/
            ├── sessions.sql
            ├── session_settings.sql
            ├── session_channels.sql
            ├── session_channel_values.sql
            ├── respiratory_events.sql
            ├── session_summaries.sql
            ├── session_slices.sql
            ├── event_lists.sql
            └── event_data.sql           # Waveform BLOBs (large)
```

### Key Technologies
- **Qt6**: QtCore, QtSql for database operations
- **SQLite**: Native database with ACID transactions
- **QuaZip** (or Qt built-in): ZIP compression
- **JSON**: Manifest format
- **SHA256**: Checksums for integrity verification

---

## Implementation Timeline

### Phase 1: Core Functionality (2-3 weeks)
- Week 1: Infrastructure (BackupManifest, SqlExporter, base classes)
- Week 2: Backup implementation
- Week 2-3: Restore implementation (ID remapping, transactions)
- Week 3: UI integration (dialogs, menu items)
- Week 3-4: Testing and documentation

### Total Effort
**23-28 days** (single developer, full-time)

---

## Testing Requirements

### Critical Tests
1. **Endianness Compatibility**
   - Windows ↔ Mac
   - Windows ↔ Linux
   - Verify data integrity across platforms

2. **Large Datasets**
   - 5+ years of real data
   - Performance verification
   - Memory usage monitoring

3. **Error Handling**
   - All errors shown in UI dialogs
   - User-friendly messages
   - Proper rollback on failures

4. **Conflict Resolution**
   - Username conflicts handled via rename
   - Multiple restore cycles
   - Verification of renamed profiles

---

## Performance Expectations

### Backup Sizes (with uncompressed BLOBs)
- 1 year: 100-250 MB (compressed ZIP)
- 5 years: 500MB-1GB (compressed ZIP)
- 10 years: 1-2 GB (compressed ZIP)

### Operation Times
- Backup 1 year: 60-120 seconds
- Restore 1 year: 90-150 seconds
- Backup 5 years: 3-5 minutes
- Restore 5 years: 4-7 minutes

**Note:** BLOB data is stored uncompressed in database for performance. Only ZIP compression is applied at package level.

---

## Security & Privacy

### Data Sensitivity
Backup files contain:
- Personal information (name, DOB, address, phone, email)
- Medical data (all CPAP sessions, AHI, respiratory events)
- Doctor information
- Password hashes

### Security Warning (Required)
Before each backup, display warning dialog:
```
WARNING: Backup Security

The backup file will contain all your personal and medical data
in unencrypted form, including:
- Personal information (name, date of birth, address)
- Medical data (all CPAP sessions, events, statistics)
- Doctor information

Please store backup files securely:
✓ Use encrypted file systems or drives
✓ Store in secure locations only
✗ Do not email backup files
✗ Do not store on unsecured cloud storage

[✓] I understand and will store my backup securely

[Continue]  [Cancel]
```

---

## File Organization

### Implementation Files
```
oscar/database/backup/
├── profile_backup.h           # Backup class
├── profile_backup.cpp
├── profile_restore.h          # Restore class
├── profile_restore.cpp
├── backup_manifest.h          # Manifest handler
├── backup_manifest.cpp
├── sql_exporter.h             # SQL export utility
└── sql_exporter.cpp

oscar/
├── backupdialog.h            # Backup UI
├── backupdialog.cpp
├── backupdialog.ui
├── restoredialog.h           # Restore UI
├── restoredialog.cpp
└── restoredialog.ui
```

### Documentation Files
```
Notes/
├── BACKUP_RESTORE_README.md                      # This file
├── BACKUP_RESTORE_STAKEHOLDER_DECISIONS.md       # Approved requirements
├── PROFILE_BACKUP_RESTORE_DESIGN.md              # Technical design
└── BACKUP_RESTORE_IMPLEMENTATION_PLAN.md         # Implementation guide
```

---

## Next Steps

### For Developers
1. ✅ Review BACKUP_RESTORE_STAKEHOLDER_DECISIONS.md
2. ✅ Review PROFILE_BACKUP_RESTORE_DESIGN.md
3. ✅ Review BACKUP_RESTORE_IMPLEMENTATION_PLAN.md
4. ⏭️ Set up development environment (Qt6, SQLite tools)
5. ⏭️ Begin Phase 1, Milestone 1.1 (create file structure)
6. ⏭️ Follow implementation plan milestones

### For Project Managers
1. ✅ Stakeholder decisions documented
2. ✅ Requirements approved
3. ⏭️ Assign developer(s)
4. ⏭️ Set target delivery date (3-4 weeks from start)
5. ⏭️ Schedule weekly progress reviews

### For Testers
1. ⏭️ Wait for Phase 1 completion
2. ⏭️ Review testing strategy in design document
3. ⏭️ Prepare cross-platform test environments
4. ⏭️ Collect large test datasets (5+ years of data)
5. ⏭️ Plan endianness compatibility tests

---

## Questions or Issues

### GitLab
Use GitLab issues for:
- Implementation questions
- Bug reports
- Feature requests
- Design clarifications

### Documentation Updates
This documentation will be updated as implementation progresses to reflect:
- Actual implementation decisions
- Lessons learned
- Performance measurements
- Known limitations

---

## Change Log

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-06 | 1.0 | Initial documentation set created |
| 2026-01-06 | 1.1 | Stakeholder decisions incorporated |

---

**Status:** 📋 DESIGN COMPLETE - READY FOR IMPLEMENTATION  
**Next Milestone:** Begin Phase 1 implementation  
**Contact:** Development team via GitLab

---

**End of Document**
