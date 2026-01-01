# OSCAR .001 File Analyzer

Copyright (c) 2026 The OSCAR Team

## Overview

The `analyze_001_file.py` script analyzes OSCAR .001 events files and provides detailed information about the file structure, channels, eventlists, and data content.

## Requirements

- Python 3.6 or higher
- Standard library only (no external dependencies)

## Usage

```bash
python analyze_001_file.py <filename.001>
```

### Example

```bash
python analyze_001_file.py ae590239.001
```

## Output Format

The script produces output in the following format:

```
File: ae590239.001
Session: 2024-12-30 22:30:00 to 2024-12-31 06:45:00
Channels: 12

Channel: CPAP_Pressure (0x10110001)
  EventList 0: Waveform
    Count: 98400 samples
    Rate: 20.0 ms/sample (50.0 Hz)
    Duration: 1968.0 seconds (32.8 minutes)
    Range: 4.0 to 15.8 cmH2O
    Data: 196800 bytes (192.19 KB)
    
Channel: CPAP_Obstructive (0x10120001)
  EventList 0: Events
    Count: 23 events
    Range: 10.2 to 45.6 seconds
    Data: 138 bytes
    
Channel: OXI_SPO2 (0x20110002)
  EventList 0: Waveform
    Count: 29520 samples
    Rate: 1000.0 ms/sample (1.0 Hz)
    Range: 88.0 to 98.0 %
    Data: 59040 bytes (57.66 KB)

Total bytes of data: 256978 bytes (250.95 KB)
File size: 78542 bytes (76.70 KB)
```

## Features

- **Complete parsing**: Reads all header fields and data blocks
- **Decompression support**: Handles Qt qCompress compression automatically
- **Data byte counting**: Tracks and totals all data bytes for each eventlist
- **Human-readable output**: Formats durations, frequencies, and file sizes
- **Channel name mapping**: Converts channel IDs to readable names
- **Range calculation**: Calculates actual min/max values using gain and offset
- **Error handling**: Validates file format and provides detailed error messages

## Implementation Details

The script follows the .001 Events File Format Documentation and implements:

1. **Header parsing** (42 bytes)
   - Magic number, version, file type
   - Session start/end timestamps
   - Compression method
   - Machine ID and type

2. **Data block parsing**
   - Channel metadata (channel IDs, eventlist counts)
   - EventList metadata (count, type, rate, gain, offset, range, dimension)
   - Raw data arrays (primary data, optional second field, time deltas)

3. **Data size calculation**
   - Primary data: count × 2 bytes (int16)
   - Second field: count × 2 bytes (if present)
   - Time deltas: count × 4 bytes (uint32, for non-waveform types)

4. **Value conversion**
   - Converts raw int16 values to actual values using: `actual = (raw × gain) + offset`
   - Calculates timestamps for waveforms and events
   - Formats durations and frequencies

## Supported Channels

The script recognizes all common OSCAR channel types:

- **CPAP channels**: Pressure, FlowRate, Leak, Respiratory Rate, etc.
- **Event channels**: Obstructive, Hypopnea, ClearAirway, Apnea, RERA, etc.
- **Oximetry channels**: SPO2, Pulse, Perfusion, Motion, etc.
- **Unknown channels**: Displays hex ID for unrecognized channels

## Error Handling

The script validates:
- File existence
- File format (must be type 1 - events)
- File version (must be >= 6)
- Decompression integrity
- Data structure consistency

## Exit Codes

- `0`: Success
- `1`: Error (file not found, invalid format, parse error, etc.)

## Design Notes

This script is designed for:

1. **Diagnostic purposes**: Understanding .001 file structure and content
2. **Verification**: Confirming data integrity and compression ratios
3. **Documentation**: Providing examples for third-party developers
4. **Debugging**: Troubleshooting CPAP data import issues

The script does NOT:
- Modify files
- Export data to other formats
- Perform clinical analysis
- Replace OSCAR's built-in import functionality

## See Also

- `.001 Events File Format Documentation-revised.md` - Complete technical specification
- OSCAR source code: `oscar/SleepLib/` - C++ implementation
- GitLab: https://gitlab.com/oscar-team/OSCAR-code
