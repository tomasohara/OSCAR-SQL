#!/usr/bin/env python3
"""
OSCAR .001 Events File Analyzer

Copyright (c) 2026 The OSCAR Team

This script analyzes OSCAR .001 events files and provides detailed information
about the file structure, channels, and data content.

Usage:
    python analyze_001_file.py <filename.001>

Requirements:
    Python 3.6+
"""

import sys
import struct
import zlib
from datetime import datetime
from pathlib import Path


# Channel ID to name mapping (from schema.cpp)
CHANNEL_NAMES = {
    # Pressure related
    0x110C: 'CPAP_Pressure',
    0x110D: 'CPAP_IPAP',
    0x1110: 'CPAP_IPAPLo',
    0x1111: 'CPAP_IPAPHi',
    0x110E: 'CPAP_EPAP',
    0x111C: 'CPAP_EPAPLo',
    0x111D: 'CPAP_EPAPHi',
    0x11A7: 'CPAP_EEPAP',
    0x11A8: 'CPAP_EEPAPLo',
    0x11A9: 'CPAP_EEPAPHi',
    0x110F: 'CPAP_PS',
    0x111A: 'CPAP_PSMin',
    0x111B: 'CPAP_PSMax',
    0x1020: 'CPAP_PressureMin',
    0x1021: 'CPAP_PressureMax',
    0x1022: 'CPAP_RampTime',
    0x1023: 'CPAP_RampPressure',
    0x1027: 'CPAP_Ramp',
    0x11A4: 'CPAP_PressureSet',
    0x11A5: 'CPAP_IPAPSet',
    0x11A6: 'CPAP_EPAPSet',
    # Flags/Events
    0x1000: 'CPAP_CSR',
    0x1028: 'CPAP_PB',
    0x1001: 'CPAP_ClearAirway',
    0x1002: 'CPAP_Obstructive',
    0x1003: 'CPAP_Hypopnea',
    0x1004: 'CPAP_Apnea',
    0x1010: 'CPAP_AllApnea',
    0x1005: 'CPAP_FlowLimit',
    0x1006: 'CPAP_RERA',
    0x1007: 'CPAP_VSnore',
    0x1008: 'CPAP_VSnore2',
    0x100a: 'CPAP_LeakFlag',
    0x1158: 'CPAP_LargeLeak',
    0x100b: 'CPAP_NRI',
    0x100c: 'CPAP_ExP',
    0x100d: 'CPAP_SensAwake',
    0x101e: 'CPAP_UserFlag1',
    0x101f: 'CPAP_UserFlag2',
    0x1024: 'CPAP_UserFlag3',
    # Oximetry
    0x1800: 'OXI_Pulse',
    0x1801: 'OXI_SPO2',
    0x1802: 'OXI_Plethy',
    0x1805: 'OXI_Perf',
    0x1803: 'OXI_PulseChange',
    0x1804: 'OXI_SPO2Drop',
    # Waveforms
    0x1100: 'CPAP_FlowRate',
    0x1101: 'CPAP_MaskPressure',
    0x1102: 'CPAP_MaskPressureHi',
    0x1103: 'CPAP_TidalVolume',
    0x1104: 'CPAP_Snore',
    0x1105: 'CPAP_MinuteVent',
    0x1106: 'CPAP_RespRate',
    0x1107: 'CPAP_PTB',
    0x1108: 'CPAP_Leak',
    0x1109: 'CPAP_IE',
    0x110A: 'CPAP_Te',
    0x110B: 'CPAP_Ti',
    0x1112: 'CPAP_RespEvent',
    0x1113: 'CPAP_FLG',
    0x1114: 'CPAP_TgMV',
    0x1115: 'CPAP_MaxLeak',
    0x1116: 'CPAP_AHI',
    0x1117: 'CPAP_LeakTotal',
    0x1118: 'CPAP_LeakMedian',
    0x1119: 'CPAP_RDI',
    # Position
    0x2990: 'POS_Orientation',
    0x2991: 'POS_Inclination',
    0x2992: 'POS_Movement',
    # Other
    0x1025: 'RMS9_MaskOnTime',
    0x1026: 'CPAP_SummaryOnly',
    0x1200: 'CPAP_Mode',
    0x1201: 'CPAP_SteadyBreathing',
    0x1202: 'CPAP_SteadyBreathingFlag',
    # Manufacturer specific
    0x1210: 'BMC_PressureWave',
    0x1211: 'BMC_FlowAbnormality',
    0x1212: 'BMC_IE_Ratio',
}

# EventList type names (from C++ enum: EVL_Waveform=0, EVL_Event=1)
EVENTLIST_TYPES = {
    0: 'Waveform',
    1: 'Events',
    2: 'Gain',
    3: 'Sum',
    4: 'Span',
}


def read_uint8(data, offset):
    """Read unsigned 8-bit integer (little-endian)"""
    value = struct.unpack('<B', data[offset:offset+1])[0]
    return value, offset + 1


def read_int8(data, offset):
    """Read signed 8-bit integer (little-endian)"""
    value = struct.unpack('<b', data[offset:offset+1])[0]
    return value, offset + 1


def read_uint16(data, offset):
    """Read unsigned 16-bit integer (little-endian)"""
    value = struct.unpack('<H', data[offset:offset+2])[0]
    return value, offset + 2


def read_int16(data, offset):
    """Read signed 16-bit integer (little-endian)"""
    value = struct.unpack('<h', data[offset:offset+2])[0]
    return value, offset + 2


def read_uint32(data, offset):
    """Read unsigned 32-bit integer (little-endian)"""
    value = struct.unpack('<I', data[offset:offset+4])[0]
    return value, offset + 4


def read_int32(data, offset):
    """Read signed 32-bit integer (little-endian)"""
    value = struct.unpack('<i', data[offset:offset+4])[0]
    return value, offset + 4


def read_int64(data, offset):
    """Read signed 64-bit integer (little-endian)"""
    value = struct.unpack('<q', data[offset:offset+8])[0]
    return value, offset + 8


def read_double(data, offset):
    """Read double precision float (little-endian)"""
    value = struct.unpack('<d', data[offset:offset+8])[0]
    return value, offset + 8


def read_bool(data, offset):
    """Read boolean (1 byte)"""
    if offset >= len(data):
        raise ValueError(f"Attempting to read bool at offset {offset} but data length is {len(data)}")
    value = struct.unpack('<B', data[offset:offset+1])[0]
    return bool(value), offset + 1


def read_qstring(data, offset, debug=False):
    """
    Read Qt QString format (Qt 4.6+ / QDataStream version Qt_4_6)
    Format: int32 size (number of BYTES of UTF-16 data), then UTF-16LE data
    Note: Size is in bytes, NOT characters! (Changed in Qt 4.2+)
    """
    if offset + 4 > len(data):
        raise ValueError(f"Cannot read QString size at offset {offset} (need 4 bytes, have {len(data) - offset})")
    
    size, offset = read_int32(data, offset)
    
    if debug:
        print(f"DEBUG:       QString size value: {size} (0x{size & 0xFFFFFFFF:08x})")
    
    if size == -1 or size == 0xFFFFFFFF:  # Null string
        return "", offset
    
    if size == 0:  # Empty string
        return "", offset
    
    # Sanity check on size (size is in bytes)
    if size < 0 or size > 2000000:  # More than 2MB is suspicious
        raise ValueError(f"QString size {size} at offset {offset-4} seems invalid (too large or negative)")
    
    # Size is already in bytes for Qt 4.2+, no need to multiply by 2
    if offset + size > len(data):
        raise ValueError(f"QString requires {size} bytes but only {len(data) - offset} bytes available at offset {offset}")
    
    utf16_data = data[offset:offset+size]
    offset += size
    
    # Decode UTF-16LE to string
    string = utf16_data.decode('utf-16-le', errors='replace')
    
    if debug:
        print(f"DEBUG:       QString result: '{string}' ({len(string)} characters, {size} bytes)")
    
    return string, offset


def read_int16_array(data, offset, count):
    """Read array of signed 16-bit integers"""
    array = []
    for _ in range(count):
        value, offset = read_int16(data, offset)
        array.append(value)
    return array, offset


def read_uint32_array(data, offset, count):
    """Read array of unsigned 32-bit integers"""
    array = []
    for _ in range(count):
        value, offset = read_uint32(data, offset)
        array.append(value)
    return array, offset


def read_header(f):
    """Read the 42-byte header from file"""
    header_data = f.read(42)
    if len(header_data) < 42:
        raise ValueError("File too short - invalid header")
    
    header = {}
    offset = 0
    
    header['magic'], offset = read_uint32(header_data, offset)
    header['version'], offset = read_uint16(header_data, offset)
    header['fileType'], offset = read_uint16(header_data, offset)
    header['machineId'], offset = read_uint32(header_data, offset)
    header['sessionId'], offset = read_uint32(header_data, offset)
    header['sessionStart'], offset = read_int64(header_data, offset)
    header['sessionEnd'], offset = read_int64(header_data, offset)
    header['compressionMethod'], offset = read_uint16(header_data, offset)
    header['machineType'], offset = read_uint16(header_data, offset)
    header['uncompressedSize'], offset = read_int32(header_data, offset)
    header['crc16'], offset = read_uint16(header_data, offset)
    
    return header


def qUncompress(data):
    """
    Decompress Qt qCompress format
    Format: 4 bytes big-endian uncompressed size + zlib compressed data
    """
    if len(data) < 4:
        raise ValueError("Compressed data too short")
    
    # First 4 bytes are big-endian uncompressed size
    expected_size = struct.unpack('>I', data[:4])[0]
    
    # Decompress the rest using zlib
    try:
        decompressed = zlib.decompress(data[4:])
    except zlib.error as e:
        raise ValueError(f"Decompression failed: {e}")
    
    # Verify size
    if len(decompressed) != expected_size:
        print(f"Warning: Decompression size mismatch (expected {expected_size}, got {len(decompressed)})")
    
    return decompressed


def parse_data_block(data, file_version=10, debug=False):
    """Parse the data block containing all channel metadata and raw data"""
    offset = 0
    channels = []
    
    if debug:
        print(f"DEBUG: Data block size: {len(data)} bytes")
        print(f"DEBUG: File version: {file_version}")
    
    # Read channel count
    channel_count, offset = read_int16(data, offset)
    if debug:
        print(f"DEBUG: Channel count: {channel_count}, offset now: {offset}")
    
    # PHASE 1: Read all metadata
    metadata_list = []
    for i in range(channel_count):
        if debug:
            print(f"DEBUG: === Phase 1 - Reading channel {i+1}/{channel_count}, offset: {offset} ===")
        
        channel_id, offset = read_uint32(data, offset)
        eventlist_count, offset = read_int16(data, offset)
        
        if debug:
            channel_name = CHANNEL_NAMES.get(channel_id, f"Unknown_{channel_id:08x}")
            print(f"DEBUG:   Channel {i+1}: {channel_name} (ID: 0x{channel_id:08x}), EventLists: {eventlist_count}")
        
        eventlists = []
        for j in range(eventlist_count):
            if debug:
                print(f"DEBUG:   Reading eventlist {j+1}/{eventlist_count}, offset: {offset}/{len(data)}")
            
            el = {}
            el['first'], offset = read_int64(data, offset)
            el['last'], offset = read_int64(data, offset)
            el['count'], offset = read_int32(data, offset)
            el['type'], offset = read_int8(data, offset)
            el['rate'], offset = read_double(data, offset)
            el['gain'], offset = read_double(data, offset)
            el['offset_val'], offset = read_double(data, offset)
            el['min'], offset = read_double(data, offset)
            el['max'], offset = read_double(data, offset)
            
            if debug:
                print(f"DEBUG:     Metadata read: count={el['count']}, type={el['type']}, rate={el['rate']:.2f}, gain={el['gain']}, min={el['min']}, max={el['max']}")
                print(f"DEBUG:     Before QString, offset: {offset}/{len(data)}")
                # Show next 20 bytes as hex
                hex_preview = ' '.join(f'{b:02x}' for b in data[offset:min(offset+20, len(data))])
                print(f"DEBUG:     Next 20 bytes: {hex_preview}")
            
            el['dimension'], offset = read_qstring(data, offset, debug=debug)
            
            if debug:
                print(f"DEBUG:     After dimension, offset: {offset}/{len(data)}")
                hex_preview = ' '.join(f'{b:02x}' for b in data[offset:min(offset+24, len(data))])
                print(f"DEBUG:     Next 24 bytes (should be hasSecondField bool): {hex_preview}")
            
            if offset >= len(data):
                raise ValueError(f"Ran out of data reading hasSecondField for channel {i+1}, eventlist {j+1}. Offset: {offset}, Data length: {len(data)}")
            
            el['hasSecondField'], offset = read_bool(data, offset)
            
            if debug:
                print(f"DEBUG:     hasSecondField: {el['hasSecondField']}, offset: {offset}")
            
            if el['hasSecondField']:
                el['min2'], offset = read_double(data, offset)
                el['max2'], offset = read_double(data, offset)
            
            eventlists.append(el)
        
        metadata_list.append({
            'channelId': channel_id,
            'eventlists': eventlists
        })
    
    # PHASE 2: Read all raw data (in same order!)
    if debug:
        print(f"DEBUG: === PHASE 2: Reading raw data arrays ===")
        print(f"DEBUG: Starting at offset: {offset}/{len(data)}")
    
    for ch_idx, channel_meta in enumerate(metadata_list):
        channel_id = channel_meta['channelId']
        eventlists = channel_meta['eventlists']
        
        if debug:
            channel_name = CHANNEL_NAMES.get(channel_id, f"Unknown_{channel_id:08x}")
            print(f"DEBUG: === Phase 2 - Channel {ch_idx+1}: {channel_name} (ID: 0x{channel_id:08x}), {len(eventlists)} eventlists ===")
        
        for el_idx, el_meta in enumerate(eventlists):
            count = el_meta['count']
            el_type = el_meta['type']
            
            if debug:
                print(f"DEBUG:   EventList {el_idx+1}: count={count}, type={el_type}, offset={offset}/{len(data)}")
            
            # Calculate data size for this eventlist
            data_bytes = 0
            
            # Read primary data array
            if offset + (count * 2) > len(data):
                raise ValueError(f"Not enough data for primary array: need {count*2} bytes at offset {offset}, have {len(data)-offset} bytes left")
            
            primary_data, offset = read_int16_array(data, offset, count)
            el_meta['data'] = primary_data
            data_bytes += count * 2  # Each int16 is 2 bytes
            
            # Read second field if present
            if el_meta['hasSecondField']:
                second_data, offset = read_int16_array(data, offset, count)
                el_meta['data2'] = second_data
                data_bytes += count * 2
            
            # Read time deltas for Events type (type 1), NOT for Waveforms (type 0)
            # C++ enum: EVL_Waveform=0, EVL_Event=1
            if el_type == 1:  # Events have time deltas
                time_deltas, offset = read_uint32_array(data, offset, count)
                el_meta['time_deltas'] = time_deltas
                data_bytes += count * 4  # Each uint32 is 4 bytes
            
            el_meta['data_bytes'] = data_bytes
        
        channels.append({
            'channelId': channel_id,
            'eventlists': eventlists
        })
    
    return channels, offset


def format_bytes(bytes_val):
    """Format byte size as bytes/KB/MB"""
    if bytes_val < 1024:
        return f"{bytes_val} bytes"
    elif bytes_val < 1024 * 1024:
        return f"{bytes_val} bytes ({bytes_val/1024:.2f} KB)"
    else:
        return f"{bytes_val} bytes ({bytes_val/1024:.2f} KB, {bytes_val/(1024*1024):.2f} MB)"


def format_duration(seconds):
    """Format duration in seconds as human-readable string"""
    if seconds < 60:
        return f"{seconds:.1f} seconds"
    elif seconds < 3600:
        minutes = seconds / 60
        return f"{seconds:.1f} seconds ({minutes:.1f} minutes)"
    else:
        hours = seconds / 3600
        minutes = (seconds % 3600) / 60
        return f"{seconds:.1f} seconds ({hours:.1f} hours, {minutes:.1f} minutes)"


def analyze_file(filename):
    """Analyze a .001 events file and print detailed information"""
    filepath = Path(filename)
    
    if not filepath.exists():
        print(f"Error: File not found: {filename}")
        return False
    
    file_size = filepath.stat().st_size
    
    try:
        with open(filename, 'rb') as f:
            # Read header
            header = read_header(f)
            
            # Validate header
            if header['fileType'] != 1:
                print(f"Error: Not an events file (fileType={header['fileType']})")
                return False
            
            if header['version'] < 6:
                print(f"Error: File version too old ({header['version']})")
                return False
            
            # Read and decompress data block
            data_block = f.read()
            
            if header['compressionMethod'] > 0:
                print(f"(Decompressing data...)")
                data_block = qUncompress(data_block)
            
            # Parse data block (enable debug if needed)
            debug_mode = '--debug' in sys.argv
            channels, bytes_read = parse_data_block(data_block, file_version=header['version'], debug=debug_mode)
            
            # Convert timestamps
            session_start = datetime.fromtimestamp(header['sessionStart'] / 1000.0)
            session_end = datetime.fromtimestamp(header['sessionEnd'] / 1000.0)
            
            # Calculate total data bytes
            total_data_bytes = 0
            for channel in channels:
                for el in channel['eventlists']:
                    total_data_bytes += el['data_bytes']
            
            # Print output
            print(f"\nFile: {filepath.name}")
            print(f"Session: {session_start.strftime('%Y-%m-%d %H:%M:%S')} to {session_end.strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"Channels: {len(channels)}")
            print()
            
            for ch_num, channel in enumerate(channels, 1):
                channel_id = channel['channelId']
                channel_name = CHANNEL_NAMES.get(channel_id, f"Unknown_{channel_id:08x}")
                eventlist_count = len(channel['eventlists'])
                
                print(f"Channel {ch_num}: {channel_name} (0x{channel_id:08x})")
                print(f"  EventLists: {eventlist_count}")
                
                for idx, el in enumerate(channel['eventlists']):
                    el_type_name = EVENTLIST_TYPES.get(el['type'], f"Type_{el['type']}")
                    print(f"  EventList {idx}: {el_type_name}")
                    print(f"    Count: {el['count']} {'samples' if el['type'] == 0 else 'events'}")
                    
                    if el['type'] == 0:  # Waveform (type 0)
                        rate_ms = el['rate']
                        if rate_ms > 0:
                            freq_hz = 1000.0 / rate_ms
                            duration_sec = (el['count'] * rate_ms) / 1000.0
                            print(f"    Rate: {rate_ms:.1f} ms/sample ({freq_hz:.1f} Hz)")
                            print(f"    Duration: {format_duration(duration_sec)}")
                    
                    # Calculate actual range from data (only if we have data)
                    if el.get('data') and len(el['data']) > 0:
                        gain = el['gain']
                        offset_val = el['offset_val']
                        
                        actual_values = [(raw * gain) + offset_val for raw in el['data']]
                        actual_min = min(actual_values)
                        actual_max = max(actual_values)
                        
                        dimension = el['dimension']
                        if dimension:
                            print(f"    Range: {actual_min:.1f} to {actual_max:.1f} {dimension}")
                        else:
                            print(f"    Range: {actual_min:.1f} to {actual_max:.1f}")
                    
                    print(f"    Data: {format_bytes(el['data_bytes'])}")
                    print()
            
            print(f"Total bytes of data: {format_bytes(total_data_bytes)}")
            print(f"File size: {format_bytes(file_size)}")
            
            return True
            
    except Exception as e:
        print(f"Error parsing file: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python analyze_001_file.py <filename.001> [--debug]")
        print()
        print("This script analyzes OSCAR .001 events files and provides")
        print("detailed information about channels, eventlists, and data content.")
        print()
        print("Options:")
        print("  --debug    Enable debug output to diagnose parsing issues")
        sys.exit(1)
    
    filename = sys.argv[1]
    success = analyze_file(filename)
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
