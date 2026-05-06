# QColor RGB Constructor Conversion

**Date:** January 1, 2026  
**Copyright:** (c) 2026 The OSCAR Team

## Overview

Converted all QColor string constructors (`QColor("#xxxxxx")`) to use the faster RGB constructor format (`QColor(r, g, b)`) throughout the OSCAR codebase. This addresses compiler warnings and improves performance.

## Changes Made

### Files Modified

1. **oscar/common_gui.cpp**
   - `QColor("#b254cd")` → `QColor(178, 84, 205)` (COLOR_ClearAirway)
   - `QColor("#ff4040")` → `QColor(255, 64, 64)` (COLOR_VibratorySnore)
   - `QColor("#404040")` → `QColor(64, 64, 64)` (COLOR_FlowLimit)
   - `QColor("#40c0c0")` → `QColor(64, 192, 192)` (COLOR_LeakFlag)
   - `QColor("#e0e0e0")` → `QColor(224, 224, 224)` (COLOR_UserFlag1)
   - `QColor("#c0c0e0")` → `QColor(192, 192, 224)` (COLOR_UserFlag2)

2. **oscar/Graphs/glcommon.h**
   - `QColor("#40c0ff")` → `QColor(64, 192, 255)` (COLOR_Aqua)

3. **oscar/SleepLib/schema.cpp**
   - `QColor("#40c0ff")` → `QColor(64, 192, 255)` (CPAP_Obstructive, CPAP_AllApnea)
   - `QColor("#404040")` → `QColor(64, 64, 64)` (CPAP_FlowLimit, OXI_Plethy)
   - `QColor("#585858")` → `QColor(88, 88, 88)` (CPAP_FLG)

## Conversion Method

Hex color codes were converted to RGB values using the following formula:
- Red component: First two hex digits
- Green component: Middle two hex digits  
- Blue component: Last two hex digits

Example:
- `#40c0ff` = RGB(64, 192, 255)
  - 40 (hex) = 64 (decimal)
  - c0 (hex) = 192 (decimal)
  - ff (hex) = 255 (decimal)

## Benefits

1. **Performance:** RGB constructor is faster than string parsing
2. **Compiler Warnings:** Eliminates deprecation warnings in newer Qt versions
3. **Code Clarity:** RGB values are more explicit than hex strings
4. **Consistency:** Aligns with Qt best practices

## Testing

All changes should be verified by:
1. Compiling the code to ensure no errors
2. Running OSCAR to visually verify colors remain unchanged
3. Checking that no new warnings are generated

## Notes

- All color values remain visually identical to the original hex codes
- No functional changes were made, only constructor format changes
- This conversion affects approximately 13 color definitions across 3 files
