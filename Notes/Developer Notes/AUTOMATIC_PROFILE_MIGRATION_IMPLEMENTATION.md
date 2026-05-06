# Automatic Profile Migration Implementation

**Copyright (c) 2026 The OSCAR Team**

## Overview

This document describes the implementation of automatic profile migration from OSCAR 1.x to OSCAR 2.0 at application startup. This feature allows users to migrate all profiles from an old file-based OSCAR_Data directory to the new SQL-based OSCAR 2.0 system in a single operation.

## Implementation Date
January 21, 2026

## Background

OSCAR 2.0 introduces a new SQL-based architecture to replace the file-based system used in OSCAR 1.x. Previously, users could only migrate one profile at a time through the menu: File → Import from OSCAR. The automatic migration feature was added to streamline the migration process when setting up OSCAR 2.0 for the first time.

## Architecture

### Components Involved

1. **main.cpp** - `migrateFromOSCAR()` function
   - Handles the overall migration workflow
   - Prompts user to select OSCAR 1.x data folder
   - Validates the source directory
   - Enumerates all profiles in the source directory
   - Coordinates the migration of each profile
   - Provides progress feedback and reports results

2. **profileimporter.h/cpp** - `ProfileImporter` class
   - Performs the actual migration for a single profile
   - Handles all phases: copying structure, loading sessions, migrating metadata, calculating summaries
   - Provides detailed progress reporting
   - Handles errors and rollback

3. **mainwindow.cpp** - `on_action_Import_OSCAR_Data_triggered()`
   - Menu-driven single profile import (unchanged)
   - Uses the same ProfileImporter class for consistency

## Implementation Details

### Function: `migrateFromOSCAR(QString destDir)`

Located in: `oscar/main.cpp`

#### Phase 1: Directory Selection and Validation

```cpp
// Prompts user to select OSCAR 1.x data folder
// Validates:
// - Has Preferences.xml
// - Has Profiles subdirectory
// - Does NOT have oscar.db (not already OSCAR 2.0)
// - Contains at least one valid profile
```

#### Phase 2: Profile Enumeration

```cpp
QList<QString> enumerateProfiles(QString sourcePath)
// Scans Profiles subdirectory
// Validates each profile by checking for machines.xml
// Returns list of valid profile directory names
```

#### Phase 3: Sequential Profile Migration

For each profile found:

1. **Create Progress Dialogs**
   - Overall progress dialog showing profile X of Y
   - Detailed progress dialog for current profile import

2. **Invoke ProfileImporter**
   ```cpp
   ProfileImporter importer;
   connect(&importer, &ProfileImporter::progressChanged,
           detailProgress, &ProgressDialog::setProgressValue);
   
   bool success = importer.importProfile(profileSourcePath, 
                                         profileDir, 
                                         detailProgress);
   ```

3. **Track Results**
   - Count successful migrations
   - Count failed migrations
   - Record failed profile names for reporting

4. **Allow Cancellation**
   - User can cancel remaining migrations
   - Already-migrated profiles remain in database

#### Phase 4: Results Reporting

Three possible outcomes:

1. **Full Success**: All profiles migrated
2. **Partial Success**: Some profiles migrated, some failed
3. **Complete Failure**: No profiles migrated

Results include:
- Number of profiles migrated
- Time elapsed
- List of failed profiles (if any)

## Code Changes

### Modified Files

#### oscar/main.cpp

**Added Includes:**
```cpp
#include "profileimporter.h"
#include "SleepLib/progressdialog.h"
```

**Modified Function:** `migrateFromOSCAR()`

**Before:**
```cpp
for (const QString& profileDir : profileList) {
    // migrate profileDir
    qDebug() << "Migrating profile" << profileDir;
}
// report total time and number of profiles migrated
```

**After:**
```cpp
// Create progress dialog
QProgressDialog progress(...);

int profilesSucceeded = 0;
int profilesFailed = 0;
QStringList failedProfiles;

for (int i = 0; i < profileList.size(); i++) {
    // Create ProfileImporter
    ProfileImporter importer;
    
    // Connect progress signals
    connect(&importer, &ProfileImporter::progressChanged,
            detailProgress, &ProgressDialog::setProgressValue);
    
    // Perform migration
    bool success = importer.importProfile(profileSourcePath, 
                                         profileDir, 
                                         detailProgress);
    
    // Track results
    if (success) {
        profilesSucceeded++;
    } else {
        profilesFailed++;
        failedProfiles.append(profileDir);
    }
}

// Report comprehensive results with message boxes
```

## User Experience Flow

1. **First Launch Detection**
   - OSCAR detects empty or non-existent data directory
   - Prompts: "Migrate Data from OSCAR 1.x?"

2. **Source Selection**
   - User clicks OK to migrate
   - File dialog: "Choose the OSCAR 1.x data folder to migrate"
   - Can click Cancel to skip migration

3. **Validation**
   - OSCAR validates selected folder is OSCAR 1.x
   - Shows error if invalid and allows retry
   - Enumerates all profiles found

4. **Migration Progress**
   - Overall progress: "Migrating profile: ProfileName (X of Y)"
   - Detailed progress: Shows current operation within profile
   - User can cancel remaining migrations

5. **Results**
   - Success: "Successfully migrated N profile(s) in X seconds"
   - Partial: "Migrated N successfully, but M failed"
   - Failure: "Failed to migrate any profiles"
   - Lists failed profiles if any

## Error Handling

### Validation Errors
- Invalid source directory → User can retry or cancel
- No profiles found → User can retry or cancel
- Already OSCAR 2.0 directory → Warning, user can retry

### Migration Errors
- Profile import failure → Logged, counted, continues to next profile
- Database errors → Handled by ProfileImporter rollback mechanism
- File system errors → Reported in error message

### User Cancellation
- Cancels remaining profiles only
- Already-migrated profiles remain in database
- No data loss

## Testing Recommendations

### Test Cases

1. **Normal Migration**
   - Source: OSCAR 1.x with 2-3 profiles
   - Expected: All profiles migrate successfully

2. **Empty Source**
   - Source: Directory with no profiles
   - Expected: Error message, allows retry

3. **Invalid Source**
   - Source: Random directory
   - Expected: Error message, allows retry

4. **Partial Failure**
   - Source: Mix of valid and corrupt profiles
   - Expected: Valid profiles migrate, corrupt ones reported

5. **User Cancellation**
   - Cancel after 1 profile of 3
   - Expected: 1 profile in database, others skipped

6. **OSCAR 2.0 Source**
   - Source: Already OSCAR 2.0 directory
   - Expected: Error message, does not migrate

## Performance Considerations

- Migration time depends on:
  - Number of profiles
  - Amount of session data in each profile
  - Storage speed (HDD vs SSD)
  
- Progress feedback every 20 files during copy operations
- Database transactions used for efficiency
- Typical time: 30-120 seconds per profile with full data

## Future Enhancements

Potential improvements:

1. **Parallel Migration**
   - Migrate multiple profiles simultaneously
   - Would require thread-safe database operations

2. **Selective Migration**
   - Allow user to choose which profiles to migrate
   - Checkbox list of found profiles

3. **Resume Migration**
   - Detect partially migrated profiles
   - Offer to continue incomplete migrations

4. **Backup Creation**
   - Automatically create backup of OSCAR 1.x data
   - Optional setting

## Related Documentation

- `PROFILE_EXPORT_IMPORT_DESIGN.md` - Overall import/export architecture
- `DATABASE_SCHEMA_REFERENCE.md` - Database structure
- `profileimporter.h` - ProfileImporter class documentation

## Revision History

| Date | Author | Description |
|------|--------|-------------|
| 2026-01-21 | The OSCAR Team | Initial implementation |

## Notes

- This feature is only triggered on first launch or when data directory is empty
- Does not affect existing single-profile import menu option
- All migrated profiles use the same ProfileImporter code path
- Migration is one-way: OSCAR 1.x → OSCAR 2.0 only
