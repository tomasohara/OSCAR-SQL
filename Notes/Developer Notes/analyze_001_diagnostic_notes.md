# .001 File Format Diagnostic Notes

## Issue Summary

The provided `.001 Events File Format Documentation-revised.md` does not accurately match the actual files being produced by OSCAR. The script encounters parsing errors because the actual file structure differs from the documentation.

## Evidence from Debug Output

### Channel 1 Analysis
```
Offset 69: QString (dimension) = -1 (null string)
Offset 73: After QString
Offset 89: hasSecondField = True  
Offset 106: Next channel starts
```

**Problem**: 73 to 89 is 16 bytes, but we're only reading `hasSecondField` (1 byte). This means there are **15 unaccounted bytes** between the dimension field and hasSecondField.

### Channel 2 Analysis
```
Offset 106: Channel ID = 0x00404400
Offset 112: Next channel (EventLists = 0, so no eventlist data)
```
This spacing is correct (4 bytes channelID + 2 bytes eventlist count).

### Channel 3 Analysis  
```
Offset 112: Channel ID = 0xbeb85200 (garbage)
EventLists = 63 (suspicious)
Metadata values are nonsensical
```

The parser is completely out of sync by channel 3.

## Possible Causes

1. **Missing Fields in Documentation**: The documentation may be incomplete or outdated. Version 8 mentioned "value/time summary hashes" but didn't specify where they appear in the structure.

2. **Version Differences**: Despite the file claiming to be version 10, it may have a different structure than documented.

3. **Additional Metadata**: There may be additional fields per EventList that aren't documented (e.g., checksums, reserved bytes, flags).

## Recommended Actions

1. **Examine OSCAR Source Code**: The actual file format should be defined in the OSCAR source code, specifically in the loader classes:
   - Look in `oscar/SleepLib/` directory
   - Find EventList serialization code  
   - Check `QDataStream` read/write operations

2. **Compare with Working Parser**: If OSCAR can read these files successfully, the C++ code will show the exact field order and types.

3. **Version-Specific Handling**: The format may vary by version. Need to check version history in source code.

## Next Steps for Script Development

To fix the analyzer script, we need to:

1. Identify the 15 mystery bytes between dimension and hasSecondField
2. Determine if there are other undocumented fields
3. Potentially create version-specific parsers

**Recommendation**: Please point to the specific OSCAR source files that handle .001 file loading so we can see the actual structure.
