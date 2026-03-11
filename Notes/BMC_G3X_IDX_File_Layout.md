# BMC G3X `.idx` File Layout

*Based on `bmcG3xDataParsing.cpp` / `bmcG3xDataParsing.h`.*
*Compare with `BMC_IDX_File_Layout.md` for legacy BMC differences.*

---

## File Detection

The parser distinguishes G3X from legacy BMC by reading the first 32 bytes of the `.idx` file.
G3X files begin with the ASCII string: `"BMC G/E/P INDEX"` (15 characters, null-terminated).
Legacy BMC files begin with `0xAAAA`.

---

## File-Level Structure

| Region | Offset | Size | Description |
|--------|--------|------|-------------|
| File magic | 0x000 | 15 B | ASCII `"BMC G/E/P INDEX"` |
| Unknown | 0x00F | 33 B | Not parsed |
| Serial Number | 0x030 | 16 B | ASCII, null-terminated, trimmed |
| Unknown | 0x040 | 8 B | Not parsed |
| Model Name | 0x048 | 16 B | ASCII, null-terminated, trimmed (default: `"Luna G3X"`) |
| Unknown | 0x058 | 1992 B | Not parsed |
| Per-day record array | 0x800 | N × 2048 B | One 2048-byte record per recorded day |

---

## Per-Day Record Layout (offsets relative to record start)

Records begin at file offset 0x800 and repeat every 2048 bytes (0x800).
A record is skipped if the first two bytes are not `0xAAAA`, or if the date is invalid.

### Section 1: Record Header (bytes 0x000–0x034)

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x000 | 2 | uint16 LE | Record magic | Must be `0xAAAA` |
| 0x002 | 6 | — | *unknown* | Not parsed |
| 0x008 | 1 | uint8 | Year | Add 1900 (not 2000 — different from legacy BMC) |
| 0x009 | 1 | uint8 | Month | 1–12 |
| 0x00A | 1 | uint8 | Day | 1–31 |
| 0x00B | 5 | — | *unknown* | Not parsed |
| 0x010 | 4 | uint32 LE | WaveStartOffset | Virtual byte offset into waveform file space |
| 0x014 | 4 | uint32 LE | WaveEndOffset | Virtual byte offset (exclusive end) |
| 0x018 | 4 | uint32 LE | WaveLength | Byte length of waveform data; record skipped if 0 |
| 0x01C | 4 | uint32 LE | EventStartOffset | Byte offset into `.evt` file |
| 0x020 | 4 | uint32 LE | EventEndOffset | |
| 0x024 | 4 | uint32 LE | EventLength | |
| 0x028 | 4 | uint32 LE | LogStartOffset | Byte offset into log file |
| 0x02C | 4 | uint32 LE | LogEndOffset | |
| 0x030 | 4 | uint32 LE | LogLength | |
| 0x034 | 76 B | — | *unknown* | Bytes 0x034–0x07F not parsed |

---

### Section 2: IT Sub-record (at record offset + 0x80)

Present only if bytes at [offset + 0x80] and [offset + 0x81] are `'I'` and `'T'`.
Offsets below are relative to the start of the IT sub-record (i.e., record + 0x80).

| IT Offset | Size | Type | Field | Notes |
|-----------|------|------|-------|-------|
| 0x00 | 1 | char | 'I' | Signature |
| 0x01 | 1 | char | 'T' | Signature |
| 0x02 | 18 B | — | *unknown* | Not parsed |
| 0x14 | 4 | uint32 LE | DurationSeconds | Session duration |
| 0x18 | 12 B | — | *unknown* | Not parsed |
| 0x24 | 1 | uint8 | SessionCount | Number of sessions |
| 0x25 | 3 B | — | *unknown* | Not parsed |
| 0x28 | 2 | uint16 LE | PressureMinHundredths | cmH2O × 100; `0xFFFF` = absent |
| 0x2A | 2 | uint16 LE | PressureMaxHundredths | `0xFFFF` = absent |
| 0x2C | 2 | uint16 LE | PressureP95Hundredths | `0xFFFF` = absent |
| 0x2E | 2 | — | *unknown* | Not parsed |
| 0x30 | 2 | uint16 LE | PressureEPAPHundredths | `0xFFFF` = absent |
| 0x32 | 138 B | — | *unknown* | Bytes 0x32–0xBB not parsed |
| 0xBC | 2 | uint16 LE | AhiX100 | AHI × 100; `0xFFFF` = absent |
| 0xBE | 2 | uint16 LE | AiX100 | AI × 100 |
| 0xC0 | 2 | uint16 LE | HiX100 | HI × 100 |
| 0xC2 | 2 | uint16 LE | OaiX100 | OAI × 100 |
| 0xC4 | 2 | uint16 LE | CaiX100 | CAI × 100 |
| 0xC6 | 2 | uint16 LE | ReraIndexX100 | RERA index × 100 |
| 0xC8 | 2 | uint16 LE | EventTotalCount | |
| 0xCA | 2 | uint16 LE | EventObstructiveCount | |
| 0xCC | 2 | uint16 LE | EventCentralCount | |
| 0xCE | 2 | uint16 LE | EventHypopneaCount | |
| 0xD0 | 2 | uint16 LE | EventReraCount | |
| 0xD2 | 2 | int16 LE | EventOtherCount | Interpreted as signed |
| 0xD4 | — | — | End of IT sub-record | Total IT sub-record: at least 0xD4 bytes |

---

### Section 3: TS Sub-record (at record offset + 0x280)

Present only if bytes at [offset + 0x280] and [offset + 0x281] are `'T'` and `'S'`.
Offsets below are relative to the start of the TS sub-record (i.e., record + 0x280).

| TS Offset | Size | Type | Field | Notes |
|-----------|------|------|-------|-------|
| 0x00 | 1 | char | 'T' | Signature |
| 0x01 | 1 | char | 'S' | Signature |
| 0x02 | 12 B | — | *unknown* | Not parsed |
| 0x0E | 2 | uint16 LE | PressureMinHundredths | cmH2O × 100; `0xFFFF` = absent |
| 0x10 | 2 | uint16 LE | PressureMaxHundredths | `0xFFFF` = absent |
| 0x12 | — | — | End of TS sub-record | Total TS sub-record: at least 0x12 bytes |

---

### Summary of Unknowns in Per-Day Record

| Range | Size | Status |
|-------|------|--------|
| 0x00F–0x02F (file header) | 33 B | Not parsed (before serial number) |
| 0x040–0x047 (file header) | 8 B | Not parsed (between serial and model) |
| 0x058–0x7FF (file header) | 1992 B | Not parsed |
| Record 0x002–0x007 | 6 B | Not parsed |
| Record 0x00B–0x00F | 5 B | Not parsed |
| Record 0x034–0x07F | 76 B | Not parsed (before IT sub-record) |
| IT 0x02–0x13 | 18 B | Not parsed |
| IT 0x18–0x23 | 12 B | Not parsed |
| IT 0x25–0x27 | 3 B | Not parsed |
| IT 0x2E–0x2F | 2 B | Not parsed |
| IT 0x32–0xBB | 138 B | Not parsed |
| Record 0x100–0x27F | ~384 B | Not parsed (between IT and TS) |
| Record 0x292–0x7FF | ~1390 B | Not parsed (after TS sub-record) |

---

## Waveform Files (.000, .001, …)

Waveform data is stored across one or more numbered files. The `.idx` file refers to waveform
data using a *virtual byte offset* that treats all files as a single linear address space:
- `fileIndex = virtualOffset / 64 MiB`
- `fileOffset = virtualOffset % 64 MiB`

Each file is up to **64 MiB** (`kG3xWaveformFileSpan`).
Waveform packets are **2048 bytes** each (`kG3xWaveformPacketSize = 0x800`).

### Waveform Packet Layout (2048 bytes)

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x000 | 2 | uint8[2] | Magic | Must be `0xAD`, `0xAA` |
| 0x002 | 2 | — | *unknown* | Not parsed |
| 0x004 | 1 | uint8 | Year | 1900 + value |
| 0x005 | 1 | uint8 | Month | |
| 0x006 | 1 | uint8 | Day | |
| 0x007 | 1 | uint8 | Hour | |
| 0x008 | 1 | uint8 | Minute | |
| 0x009 | 1 | uint8 | Second | |
| 0x00A | 2 | uint16 LE | Pressure | cmH2O in hundredths; valid range 400–3500 |
| 0x00C | 104 B | — | *unknown* | Not parsed |
| 0x074 | 2 | uint16 LE | Raw0x74 | Possibly inspiratory time in centiseconds (experimental); also used for waveform leak blend |
| 0x076 | 2 | uint16 LE | Raw0x76 | Logged in diagnostics; purpose unknown |
| 0x078 | 4 B | — | *unknown* | Not parsed |
| 0x07C | 2 | uint16 LE | Raw0x7C | Logged in diagnostics; purpose unknown |
| 0x07E | 2 | uint16 LE | Raw0x7E | Possibly expiratory time in centiseconds (experimental) |
| 0x080 | 458 B | — | *unknown* | Not parsed |
| 0x24A | 192 B | int16 LE × 96 | PressureWave | 96 samples; decoded as signed16 (or signed10 if env flag set) |
| 0x36A | 166 B | — | *unknown* | Not parsed |
| 0x510 | 94 B | int16 LE × 47 | FlowAbnormality | 47 samples; decoded as signed16 or signed10; median-baselined |
| 0x56E | 8 B | — | *unknown* | Not parsed |
| 0x576 | 192 B | int16 LE × 96 | Flow | 96 samples; clamped to ±2000; scaled /10.0 for display |
| 0x636 | 458 B | — | *unknown* | Not parsed |

All three waveform arrays (96, 47, 96 samples) are downsampled to 50 output samples for
compatibility with the legacy BMC waveform format.

**Experimental timing fields (0x74, 0x7E):** When `OSCAR_BMC_G3X_EXPERIMENTAL_TIMING_747E`
is set, the parser interprets `0x74` as inspiratory centiseconds and `0x7E` as expiratory
centiseconds. Cycle time = raw0x74 + raw0x7E. Valid range: 200–2400 centiseconds.
Respiratory rate = 6000 / cycleCentiseconds. I:E fraction = raw0x74 / cycleCentiseconds.

---

## EVT File Records

The `.evt` file contains fixed-size 32-byte (`kG3xEvtRecordSize = 0x20`) records.
Records without magic `0xAE, 0xAA` at bytes 0–1 are skipped.

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x00 | 1 | uint8 | Magic byte 0 | Must be `0xAE` |
| 0x01 | 1 | uint8 | Magic byte 1 | Must be `0xAA` |
| 0x02 | 14 B | — | *unknown* | Not parsed |
| 0x10 | 1 | uint8 | MessageType | See table below |
| 0x11 | 3 B | — | *unknown* | Not parsed |
| 0x14 | 6 B | uint8[6] | Timestamp | year-1900, month, day, hour, minute, second |
| 0x1A | 2 | uint16 LE | Value1 | Meaning depends on MessageType |
| 0x1C | 2 | uint16 LE | Value2 | Meaning depends on MessageType |
| 0x1E | 2 | — | *unknown* | Not parsed |

### EVT Message Types

| Type | Interpretation | Value1 | Value2 |
|------|---------------|--------|--------|
| 0x02 | UA (unclassified apnea) | Duration seconds | — |
| 0x03 | OSA | Duration seconds | — |
| 0x04 | CSA | Duration seconds | — |
| 0x07 | Hypopnea (subtype A) | Duration seconds | — |
| 0x08 | Hypopnea (subtype B) | Duration seconds | — |
| 0x09 | Hypopnea (subtype C) | Duration seconds | — |
| 0x0A | Unknown respiratory | — | — |
| 0x0B | Unknown | — | — |
| 0x0C | High-rate leak update | — | Raw leak (× scale → tenths L/min) |
| 0x0D | Ignored | — | — |
| 0x0E | Unknown (possibly tidal volume — unvalidated) | — | — |
| 0x0F | Unknown (possibly minute ventilation — unvalidated) | — | — |
| 0x42 | Pressure + leak | Raw leak | Pressure in hundredths cmH2O |

For respiratory events (0x02–0x0A), duration is clamped to 10–180 seconds.

**Leak source policy:** If 0x0C records are present for the day, they are used as the primary
(high-rate) leak source. Otherwise, the leak field from 0x42 records is used as fallback.

---

## Key Differences from Legacy BMC Format

| Feature | Legacy BMC | BMC G3X |
|---------|-----------|---------|
| File magic | `0xAAAA` per packet | ASCII `"BMC G/E/P INDEX"` at file start |
| Year encoding | Year - 2000 (uint8) | Year - 1900 (uint8) |
| IDX packet size | 512 bytes | 2048 bytes |
| IDX packets start | File offset 0x800 | File offset 0x800 |
| Waveform packet size | 256 bytes (0x100) | 2048 bytes (0x800) |
| Waveform file addressing | File index via `.NNN` extension, packet index × 0x100 | Virtual linear address space, 64 MiB per file |
| Machine settings in IDX | Yes (offset 0x140–0x165 per packet) | No — mode inferred from pressure min/max |
| Serial number source | USR file at offset 0x2D | IDX file at offset 0x030 |
| Model number source | USR file at offset 0x2296 | IDX file at offset 0x048 |
| Session data source | USR file (BmcUsrSession) | IDX records + EVT file |
| Waveform packet magic | `0xAAAD` | `0xAD, 0xAA` |
| Flow samples per packet | 25 | 96 (downsampled to 50) |
| Pressure wave samples | 25 | 96 (downsampled to 50) |
| Flow abnormality samples | 25 | 47 (downsampled to 50) |
| Statistics sub-records | None | IT (indices/totals) and TS (pressure range) |
