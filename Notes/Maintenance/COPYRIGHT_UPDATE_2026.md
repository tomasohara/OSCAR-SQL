# Copyright Update to 2026

**Copyright (c) 2026 The OSCAR Team**

## Overview

This document records the copyright year updates performed on December 31, 2025 to update all copyright statements in the OSCAR codebase to include the year 2026.

## Task Summary

Updated copyright statements containing "The OSCAR Team" across the entire OSCAR codebase according to the following rules:

1. **Ranges ending in 2025**: Changed to end in 2026
   - Example: `2019-2025` → `2019-2026`
   
2. **Single year 2025**: Changed to range 2025-2026
   - Example: `2025` → `2025-2026`
   
3. **Ranges ending in other years**: Updated to end in 2026
   - Example: `2020-2022` → `2020-2026`
   - Example: `2019-2024` → `2019-2026`

4. **Exclusions**: Files in "thirdparty" directories were excluded from updates

5. **Copyright strings**: Also updated copyright strings in resource (.rc) files
   - Example: `VALUE "LegalCopyright", "© 2019-2024 The OSCAR Team\0"` → `"© 2019-2026 The OSCAR Team\0"`

## Execution Details

- **Date**: December 31, 2025
- **Method**: Python script (`Tools/update_copyright_2026.py`)
- **Files Processed**: 472
- **Files Updated**: 208

## Updated File Categories

### Source Files
- Main application files (`.cpp`, `.h`)
- Database repository files
- Graph rendering files
- Loader plugin files
- Test files

### Resource Files
- Build resource files (`.rc`)

### Documentation Files
- Notes and documentation (`.md`)

### Tool Files
- Python utility scripts

## Verification

Sample files verified after update:
- `oscar/speedcheck.h`: `2025` → `2025-2026` ✓
- `oscar/main.cpp`: `2019-2025` → `2019-2026` ✓
- `oscar/tests/cryptotests.h`: `2021-2022` → `2021-2026` ✓
- `oscar/database/channel_repository.h`: `2025` → `2025-2026` ✓
- `oscar/build/OSCAR_Qt_6_10_0_MinGW_64_bit-Release/OSCAR_resource.rc`: String `2019-2024` → `2019-2026` ✓

## Script Location

The update script is located at: `Tools/update_copyright_2026.py`

This script can be referenced for future copyright updates and modified as needed for subsequent years.

## Notes

- The thirdparty directory was excluded as specified
- All updates maintained the original copyright format and structure
- The script handled various copyright formatting styles correctly
- Updates were applied to both file headers and embedded strings (e.g., in .rc files)

## Future Considerations

For the next annual copyright update:
1. Update the script year references
2. Modify patterns to change 2026 to 2027
3. Run with similar command: `python Tools/update_copyright_YYYY.py`
4. Verify sample files after execution
5. Document the updates

---

**End of Documentation**
