# BMC `.idx` File Layout

*Based on `bmcDataParsing.cpp` / `bmcDataParsing.h` — legacy BMC format only (not G3X).*

---

## File-Level Structure

| Region | Offset | Size | Description |
|--------|--------|------|-------------|
| Pre-data area | 0x0000 | 0x800 bytes | Unknown / unused by parser |
| Packet array | 0x0800 | N × 512 bytes | One 512-byte packet per recorded day, read sequentially to EOF |

Packets are read in a single pass: each 512-byte block is parsed **twice** from offset 0 — once as a `BmcIdxEntry` (waveform pointer data) and once as a `BmcMachineSettings` (therapy settings).

---

## Per-Packet Layout (offsets relative to packet start)

### Section 1: Common Header (bytes 0x000–0x006, shared by both parsers)

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x000 | 2 | uint16 LE | Packet magic | Must be `0xAAAA`; throws if wrong |
| 0x002 | 2 | uint16 LE | Entry index | Sequential packet number |
| 0x004 | 1 | uint8 | Year | Add 2000 |
| 0x005 | 1 | uint8 | Month | 1–12 |
| 0x006 | 1 | uint8 | Day | 1–31 |

---

### Section 2: Waveform Pointers (BmcIdxEntry — bytes 0x007–0x014)

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x007 | 6 | — | *skipped* | Unknown |
| 0x00D | 2 | uint16 LE | StartOffsetPacket | Waveform file packet index; byte offset = value × 0x100 |
| 0x00F | 2 | uint16 LE | StartFileIndex | Waveform file number (0–999 → extension `.000`–`.999`) |
| 0x011 | 2 | uint16 LE | NextOffsetPacket | Packet index of next session's start |
| 0x013 | 1 | uint8 | NextFileIndex | Next file number; `0xFF` means no valid next entry |

Bytes 0x015–0x13F: not parsed; purpose unknown.

---

### Section 3: Machine Settings (BmcMachineSettings — bytes 0x140–0x165)

All values are `uint8` unless noted. The parser jumps directly to 0x140 after reading the header.

| Offset | Field(s) | Decoding |
|--------|----------|----------|
| 0x140 | InitialP (all modes) | `value / 2.0` cmH2O → APAP_InitialP, CPAP_InitialP, S_InitialEPAP, AutoS_InitialEPAP |
| 0x141 | TreatP / MinAPAP / EPAP / MinEPAP | `value / 2.0` cmH2O → CPAP_TreatP, APAP_MinAPAP, S_EPAP, AutoS_MinEPAP |
| 0x142 | RampTimeMinutes | Raw uint8, minutes |
| 0x143 | *skipped* | — |
| 0x144 | CPAP_ManualP | `value / 2.0` cmH2O |
| 0x145 | Flags | Bit 7 (0x80) = S_BackupRR |
| 0x146 | HumidifierLevel | Raw uint8 |
| 0x147 | Flags | Bit 6 (0x40) = LeakAlert; bit 1 (0x02) = AutoOff; bit 0 (0x01) = AutoOn |
| 0x148 | Reslex + IPAP delta | Bits [1:0] = Reslex level; bits [7:2] = IPAP_delta*2 → S_IPAP = S_EPAP + (bits[7:2] / 2.0); same delta applied to AutoS_MinIPAP |
| 0x149 | Sensitivity settings | Bits [2:0] = ISENS-1 (stored 0–6 → value 1–7); bits [5:3] = ESENS-1 |
| 0x14A | *skipped* | — |
| 0x14B | *skipped* | — |
| 0x14C | MaxAPAP / MaxIPAP | `value / 2.0` cmH2O → APAP_MaxAPAP, AutoS_MaxIPAP |
| 0x14D | Mode + APAP sensitivity | Bits [7:4] = BmcMode enum (0=CPAP, 1=AutoCPAP, 2=S, 3=ST, 4=T, 5=Titration, 6=AutoS); bits [3:0] = APAP_Sensitivity |
| 0x14E | *skipped* | — |
| 0x14F | RiseTime | Bits [7:6] = RiseTime-1 (stored 0–3 → value 1–4) |
| 0x150 | *skipped* | — |
| 0x151 | ReslexPatient | Bit 7 (0x80) = ReslexPatient flag |
| 0x152 | S_TiMin | `value / 10.0` seconds |
| 0x153 | S_TiMax | `value / 10.0` seconds |
| 0x154–0x15F | *skipped* | 12 bytes, purpose unknown |
| 0x160 | MaskType | BmcMaskType enum: 0=FullFace, 1=Nasal, 2=NasalPillow, 3=Other |
| 0x161 | *skipped* | — |
| 0x162 | AirTubeType | BmcAirTubeType enum: 0=Unheated22mm, 1=Unheated15mm, 2=Heated22mm, 3=Heated15mm |
| 0x163 | *skipped* | — |
| 0x164 | HeatedTubeLevel | Raw uint8 |
| 0x165 | Smart flags | Bit 0 = CPAP_SmartC; bit 1 = APAP_SmartA; bit 2 = AutoS_SmartB |

Bytes 0x166–0x1FF: not parsed; purpose unknown. Total packet size is 512 bytes (0x200).

---

## Summary of Unknowns

| Range (within packet) | Size | Status |
|-----------------------|------|--------|
| 0x007–0x00C | 6 B | Skipped after header/date |
| 0x015–0x13F | 299 B | Not read at all |
| 0x143, 0x14A, 0x14B, 0x14E, 0x150, 0x161, 0x163 | 7 B | Explicitly skipped |
| 0x154–0x15F | 12 B | Explicitly skipped |
| 0x166–0x1FF | 154 B | Not read |
