#!/usr/bin/env python3
"""
Hex dump utility for .001 files to diagnose parsing issues

Usage: python hexdump_001.py <filename.001> <start_offset> <num_bytes>
"""

import sys
import struct
import zlib
from pathlib import Path

def read_header(f):
    """Read the 42-byte header"""
    header_data = f.read(42)
    header = {}
    offset = 0
    
    header['magic'] = struct.unpack('<I', header_data[offset:offset+4])[0]
    offset += 4
    header['version'] = struct.unpack('<H', header_data[offset:offset+2])[0]
    offset += 2
    header['fileType'] = struct.unpack('<H', header_data[offset:offset+2])[0]
    offset += 2
    header['machineId'] = struct.unpack('<I', header_data[offset:offset+4])[0]
    offset += 4
    header['sessionId'] = struct.unpack('<I', header_data[offset:offset+4])[0]
    offset += 4
    header['sessionStart'] = struct.unpack('<q', header_data[offset:offset+8])[0]
    offset += 8
    header['sessionEnd'] = struct.unpack('<q', header_data[offset:offset+8])[0]
    offset += 8
    header['compressionMethod'] = struct.unpack('<H', header_data[offset:offset+2])[0]
    offset += 2
    header['machineType'] = struct.unpack('<H', header_data[offset:offset+2])[0]
    offset += 2
    header['uncompressedSize'] = struct.unpack('<i', header_data[offset:offset+4])[0]
    offset += 4
    header['crc16'] = struct.unpack('<H', header_data[offset:offset+2])[0]
    
    return header

def qUncompress(data):
    """Decompress Qt qCompress format"""
    expected_size = struct.unpack('>I', data[:4])[0]
    decompressed = zlib.decompress(data[4:])
    return decompressed

def hex_dump(data, start_offset, num_bytes, base_offset=0):
    """Print hex dump of data"""
    end_offset = min(start_offset + num_bytes, len(data))
    
    print(f"\nHex dump from offset {start_offset} to {end_offset} (base offset: {base_offset}):")
    print(f"{'Offset':<10} {'Hex':^48} {'ASCII':<16} {'Decimal'}")
    print("-" * 90)
    
    for i in range(start_offset, end_offset, 16):
        chunk = data[i:min(i+16, end_offset)]
        
        # Hex representation
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        hex_str = hex_str.ljust(48)
        
        # ASCII representation
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        ascii_str = ascii_str.ljust(16)
        
        # Decimal for first 4 bytes
        if len(chunk) >= 4:
            int32_le = struct.unpack('<i', chunk[:4])[0]
            uint32_le = struct.unpack('<I', chunk[:4])[0]
            dec_str = f"i32:{int32_le:11d} u32:{uint32_le:10d}"
        elif len(chunk) >= 2:
            int16_le = struct.unpack('<h', chunk[:2])[0]
            dec_str = f"i16:{int16_le}"
        else:
            dec_str = f"u8:{chunk[0]}"
        
        print(f"{base_offset+i:<10d} {hex_str} {ascii_str} {dec_str}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python hexdump_001.py <filename.001> [start_offset] [num_bytes]")
        print("\nDumps decompressed data block from a .001 file")
        print("Default: dumps first 512 bytes")
        sys.exit(1)
    
    filename = sys.argv[1]
    start_offset = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    num_bytes = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    
    filepath = Path(filename)
    if not filepath.exists():
        print(f"Error: File not found: {filename}")
        sys.exit(1)
    
    with open(filename, 'rb') as f:
        header = read_header(f)
        
        print(f"File: {filepath.name}")
        print(f"Version: {header['version']}")
        print(f"Compression: {header['compressionMethod']}")
        print(f"Uncompressed size: {header['uncompressedSize']}")
        
        data_block = f.read()
        
        if header['compressionMethod'] > 0:
            print("Decompressing...")
            data_block = qUncompress(data_block)
            print(f"Decompressed to {len(data_block)} bytes")
        
        hex_dump(data_block, start_offset, num_bytes)
        
        # Also show some interpreted structures at key offsets
        if start_offset <= 0:
            print("\n=== Channel count ===")
            channel_count = struct.unpack('<h', data_block[0:2])[0]
            print(f"Offset 0: Channel count = {channel_count}")

if __name__ == '__main__':
    main()
