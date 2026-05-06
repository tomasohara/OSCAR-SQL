# BMC Legacy SD Card File Formats

*Derived from `bmcDataParsing.cpp`, `bmcDataParsing.h`, and `bmc_loader.cpp`.*
*This covers the legacy BMC format only — see `BMC_G3X_IDX_File_Layout.md` for G3X.*

---

## SD Card Directory Layout

A valid BMC SD card directory contains at minimum three files sharing the same
base name (determined by finding the single `*.USR` file):

| File | Extension | Description |
|------|-----------|-------------|
| Data file | `.USR` | Machine info, session summaries, respiratory events |
| Index file | `.IDX` | Per-day waveform pointers and machine settings |
| Waveform files | `.000` `.001` … `.999` | Raw waveform data (circular buffer) |

Detection: `BmcData::DirectoryHasBmcData()` requires exactly one `.USR` file
plus matching `.idx` and `.000` files.

---

## 1. USR File

The `.USR` file is the primary data file. It contains machine identification,
an in-progress session area, a session count, and an array of historic session
records.

### 1.1 Machine Info

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x002D | 32 | char[] | Serial Number | Null-terminated ASCII, trimmed |
| 0x2296 | 32 | char[] | Model Number | Null-terminated ASCII, trimmed |

### 1.2 Session Count

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x102338 | 2 | uint16 LE | Session Count | Number of historic sessions |

### 1.3 In-Progress Session (offset 0x0431)

The in-progress session represents a session that may still be recording.
It uses a different format from historic sessions.

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x0431 | 2 | uint16 LE | Encoded Date | BMC encoded date (see Date Encoding) |

Starting at offset **0x0441**, the in-progress session contains a sequence of
variable-length event messages:

#### In-Progress Event Message Format

| Field | Size | Type | Notes |
|-------|------|------|-------|
| Message Type | 1 | uint8 | See table below; `0xFF` = end of list |
| Data Length | 1 | uint8 | Only present if msgType != `0x02`; if msgType == `0x02`, length is implicitly 3 |
| Data | *datalen* | uint8[] | Event-specific payload |

#### In-Progress Respiratory Event Types (3-byte payload)

| Type | Event | Byte 0 | Byte 1 | Byte 2 |
|------|-------|--------|--------|--------|
| 0x07 | CSA (Central Sleep Apnea) | Hour offset | Minute offset | Duration (seconds) |
| 0x08 | OSA (Obstructive Sleep Apnea) | Hour offset | Minute offset | Duration (seconds) |
| 0x09 | HYP (Hypopnea) | Hour offset | Minute offset | Duration (seconds) |

Event start time = session start + (Byte0 hours) + (Byte1 minutes).
Event end time = start time + Byte2 seconds.

### 1.4 Historic Sessions (offset 0x102340)

Historic sessions are stored sequentially starting at offset **0x102340** and
continuing to the end of the file. Each session is variable-length.

#### Session Navigation

Each session begins with a 5-byte navigation header:

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x00 | 1 | uint8 | Session Marker | Must be `0xE1` |
| 0x01 | 4 | uint32 LE | Next Session Offset | Absolute file offset of next session; `0x00000000`, `0xFFFFFFFF`, or any value <= current position treated as EOF |

#### Session Header

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| 0x00 | 1 | uint8 | Marker | `0xE1` |
| 0x01 | 6 | — | *navigation / unknown* | Next-session pointer + padding |
| 0x07 | 2 | uint16 LE | Encoded Date | BMC encoded date (see Date Encoding) |
| 0x09 | 6 | — | *unknown* | Not parsed |
| 0x0F | 2 | uint16 LE | Duration | Session duration in minutes |
| 0x11 | 52 | — | *unknown* | Bytes 0x11–0x44 not parsed |

#### Message Block at Offset 0x45

Starting at offset **0x45** within the session, a sequence of 5-byte messages
is read until an `0xFF` marker type is encountered:

| Field | Size | Type | Notes |
|-------|------|------|-------|
| Message Type | 1 | uint8 | `0xFF` = end of block |
| Data | 4 | uint32 LE | Stored as `MessageItem32` |

These are stored in `MessagesOffset45` (purpose not fully decoded).

#### Data Message Arrays (after offset-0x45 block)

Immediately following the 0x45 message block, the remainder of the session
consists of typed data arrays. Each array has a 5-byte header:

| Field | Size | Type | Notes |
|-------|------|------|-------|
| Array Type | 1 | uint8 | Determines element size |
| Count | 2 | uint16 LE | Number of elements |
| *padding* | 2 | uint16 LE | Discarded |

Element sizes depend on the array type:

| Array Type | Element Size | Storage | Notes |
|------------|-------------|---------|-------|
| 0x82 | 4 bytes | uint32 LE | Stored as `MessageItem32` |
| 0x86 | 4 bytes | uint32 LE | Stored as `MessageItem32` |
| 0x83 | 3 bytes | uint8 × 3 | **OSA event**: hour, minute, duration |
| 0x84 | 3 bytes | uint8 × 3 | **Hypopnea event**: hour, minute, duration |
| 0x87 | 3 bytes | uint8 × 3 | **CSA event**: hour, minute, duration |
| *other* | 2 bytes | uint16 LE | Stored as `MessageItem16` |

#### Historic Session Respiratory Events (3-byte items: types 0x83, 0x84, 0x87)

| Byte | Field | Notes |
|------|-------|-------|
| 0 | Hour offset | Hours from session start |
| 1 | Minute offset | Minutes from start of that hour |
| 2 | Duration | Seconds |

Event start time = session start + (Byte0 hours) + (Byte1 minutes).
Event end time = start time + Byte2 seconds.

---

## 2. IDX File

See `BMC_IDX_File_Layout.md` for the full layout. Summary:

- Pre-data area: 0x0000–0x07FF (2048 bytes, not parsed)
- Packet array starts at **0x0800**, with **512-byte** packets to EOF
- Each packet is parsed as both a `BmcIdxEntry` (waveform pointers) and a
  `BmcMachineSettings` (therapy settings)
- Packet header: `0xAAAA` magic, entry index, year/month/day
- Waveform pointers at offsets 0x00D–0x013
- Machine settings at offsets 0x140–0x165

---

## 3. Waveform Files (.000, .001, …)

Waveform data is stored in numbered files (`.000`, `.001`, etc.) that form a
circular buffer. Each file contains fixed-size **256-byte (0x100)** packets.

The IDX file's `StartOffsetPacket` field is a packet index; the byte offset
within the waveform file is `packetIndex × 0x100`.

### 3.1 Waveform Packet Layout (256 bytes)

All multi-byte fields are **little-endian**. The packet is mapped directly via
`BmcWaveformPacketStruct` (a packed struct).

| Offset | Size | Type | Field | Scaling | Notes |
|--------|------|------|-------|---------|-------|
| 0x00 | 2 | uint16 LE | Magic | — | Packet header, always 0x'AAAA' |
| 0x02 | 1 | uint8 | SessionCounter | — | Increments ~once per therapy session (one night ≈ one count). Wraps at ~128. Differs between machines, reflecting prior usage history. Stored in low byte of a 16-bit slot |
| 0x03 | 1 | uint8 | *unknown* | — | Always 0 on all tested machines. May be the high byte of a 16-bit counter that hasn't exceeded 255, or a separate unused field |
| 0x04 | 2 | int16 LE | EPAP | ÷ 2.0 → cmH2O | Confirmed: 0x04 is always the smaller value (EPAP); 0x06 is always the larger (IPAP) |
| 0x06 | 2 | int16 LE | IPAP | ÷ 2.0 → cmH2O | Confirmed: always larger than 0x04 across all tested machines |
| 0x08 | 50 | int16 LE × 25 | PressureWave[25] | ÷ 10.0 → cmH2O | **Mask pressure waveform. Confirmed**: raw ÷ 10 = cmH2O (e.g. raw 85 → 8.5 cmH2O at EPAP=8.5). Loaded into CPAP_MaskPressure channel. No separate pressure-wave chart |
| 0x3A | 50 | int16 LE × 25 | FlowAbnormality[25] | unknown | 25 values, baseline ~366-390 raw. Spikes at the onset and recovery of apnea-like events but is low and flat during the event itself. Consistent with a **flow rate-of-change or variability metric**: during apnea, flow is near-zero and stable (low variability → low value); at the transition in/out, flow changes rapidly (high rate of change → spike). Likely a machine-internal signal used for breath detection and respiratory event flagging. NOT vent leak, NOT tidal flow |
| 0x6C | 50 | int16 LE × 25 | Flow[25] | ÷ 10.0 → L/min | 25 **total machine output flow** samples. Always positive (includes vent leak + patient tidal component). CPAP 4.0: range ~400–686 raw (40–68 L/min). BiPAP 8.5/11.5: range ~273–1034 raw (27–103 L/min). No separate tidal flow channel has been identified in the 256-byte packet |
| 0x9E | 22 | int16 LE × 11 | *flow variants* | — | Offsets 0x9E–0xB0 (11 words). All are visually similar to the Flow channel (0x6C) — appear to be flow rate with slightly different amplification or clipping. No additional clinical information beyond Flow[25]. Logged but not exported |
| 0xB2 | 2 | int16 LE | *unknown* | — | Purpose unclear, waveform see notes from range plot investigation |
| 0xB4 | 2 | int16 LE | *unknown* | — | Purpose unclear, interesting bar with spikes up and down, under study |
| 0xB6 | 2 | int16 LE | *unknown* | — | Purpose unclear, waveform, under study |
| 0xB8 | 2 | int16 LE | *unknown* | — | Purpose unclear, smooth curve, under study |
| 0xBA | 2 | int16 LE | *unknown* | — | Fixed value of 350 |
| 0xBC | 2 | int16 LE | *unknown* | — | Purpose unclear, smooth curve, under study |
| 0xBE | 2 | int16 LE | *unknown (mechanical)* | — | Values (~3300) are mechanical/machine-state. Previously suspected as pulse rate (÷50 ≈ 66–67 BPM) but dismissed: too constant, no physiological variation. See 0xCE for true pulse |
| 0xC0 | 2 | int16 LE | *unknown (model-specific)* | — | **Model-dependent**: BMC Luna — varies (418–482), closely related to 0xB8 but with more detail. BMC G3 — constant 400. Model-specific, not a universal clinical field |
| 0xC2 | 2 | int16 LE | *unknown (model-specific)* | — | **Model-dependent**: BMC Luna — varies (mean ~200, range −31 to 226), looks like a CPAP parameter but no clear correlation with Leak, IPAP, or EPAP found. BMC G3 — constant 1472 all night. Likely populated differently per firmware/model; not a universal clinical field |
| 0xC4 | 2 | int16 LE | Leak | ÷ 10.0 → L/min | Leak rate |
| 0xC6 | 2 | int16 LE | TidalVolume | × 1.0 → mL (?) | Tidal volume. **Uncertain**: TV × RR / 1000 does not consistently match MV field; true scaling unclear |
| 0xC8 | 2 | int16 LE | *unknown (model-specific)* | — | **Model-dependent**: BMC Luna — small range 0–26, mechanical character (similar to 0xBE). BMC G3 — always 0. Not a universal clinical field |
| 0xCA | 2 | int16 LE | MinuteVentilation | ÷ 10.0 → L/min | Minute ventilation |
| 0xCC | 2 | int16 LE | SpO2Pct | × 1.0 → % | SpO2 from **optional oximeter accessory**. 0 when accessory absent or not in use. Expected to be present or absent together with 0xCE (PulseRate) |
| 0xCE | 2 | int16 LE | PulseRate | × 1.0 → bpm | Pulse rate from **optional oximeter accessory**. 0 when accessory absent or not in use. All 3 test machines lack oximeters — confirmed 0. Expected to be present or absent together with 0xCC (SpO2) |
| 0xD0 | 2 | int16 LE | RespiratoryRate | × 1.0 → breaths/min | Respiratory rate |
| 0xD2 | 2 | int16 LE | IERatio | lookup table | I:E ratio (see below) |
| 0xD4 | 2 | int16 LE | *Large leak?* | — | Appears to be an inverted large leak plot, not used by OSCAR, which calculates large leaks on its own. |
| 0xD6 | 2 | int16 LE × 1 | *unknown* | — | **Model-dependent**: G2: Values mostly 1 or 2 with a few instances of 0 and 3. First data point apparently always 0. G3: always zero. |
| 0xD8 | 2 | int16 LE × 1 | *unknown* | — | always 0. |
| 0xDA | 2 | int16 LE × 1 | *unknown* | — | always 0. |
| 0xDC | 2 | int16 LE × 1 | *unknown* | — | always 0. |
| 0xDE | 2 | int16 LE × 1 | *unknown* | — | always 0. |
| 0xE0 | 30 | int16 LE × 15 | *unused* | — | Offsets 0xE0–0xF2: always 0 on all tested machines |
| 0xF4 | 2 | int16 LE | *alarm countdown* | — | Decrements by 5 at each hour boundary during the session, then increments by 5 once the preset alarm time is reached. Internal machine operation only — no clinical value |
| 0xF6 | 2 | int16 LE | *alarm time?* | — | Constant 115 throughout session. Likely encodes the user-set alarm time. Internal machine operation only — no clinical value |
| 0xF8 | 2 | uint16 LE | Year | raw (e.g. 2024) | Timestamp year |
| 0xFA | 1 | uint8 | Month | 1–12 | |
| 0xFB | 1 | uint8 | Day | 1–31 | |
| 0xFC | 1 | uint8 | Hour | 0–23 | |
| 0xFD | 1 | uint8 | Minute | 0–59 | |
| 0xFE | 1 | uint8 | Second | 0–59 | |
| 0xFF | 1 | uint8 | DayOfWeek | 0=Sunday … 6=Saturday | Day of week matching the packet timestamp. Confirmed across 69 consecutive days |

**Total: 256 bytes (0x100)**

### 3.2 Waveform Samples

Each packet contains **25 samples** for three waveform channels, representing
**1 second** of data at **25 Hz**:

| Channel | Array Offset | Samples | Raw Unit | Display Gain |
|---------|-------------|---------|----------|-------------|
| PressureWave | 0x08 | 25 | int16 | × 0.1 cmH2O (**confirmed**) |
| FlowAbnormality | 0x3A | 25 | int16 | × 1.0 |
| Flow | 0x6C | 25 | int16 | × 0.1 L/min |

### 3.3 I:E Ratio Decoding

The raw `IERatio` value (0–100) is transformed using a lookup table based on
the formula: `InspirationPct = (100 × raw) / (raw + 10)`.

The displayed value is: `100 - IERatioLookup[raw]` (if raw ≤ 100; else 0).

### 3.4 IPAP/EPAP Swap

The legacy BMC packet stores IPAP and EPAP in reversed positions relative to
OSCAR's channel expectations. During import, the loader swaps both the
scaled and raw IPAP/EPAP values.

### 3.5 Circular Buffer and Session Matching

Waveform files are written as a circular buffer — older data is overwritten
when the SD card fills. The loader validates sessions by:

1. Building "waveform crumbs" — reading timestamps from every 0x1000-packet
   boundary (16 crumbs per file)
2. Matching IDX entries against waveform packet timestamps; entries where the
   date differs by ≥ 3 days are considered overwritten and skipped
3. Sessions split on forward timestamp gaps ≥ 5 seconds; backward jumps up
   to 2 hours (DST fall-back) are tolerated

---

## 4. Date Encoding

### 4.1 BMC Encoded Date (USR file)

Used in `.USR` file session headers. A 16-bit packed date:

| Bits | Field | Extraction |
|------|-------|------------|
| 15–9 | Year offset | `(value >> 9) + 2000` |
| 8–5 | Month | `(value >> 5) & 0x0F` |
| 4–0 | Day | `value & 0x1F` |

The decoded date always uses time 12:00:00 (noon — the start of an "OSCAR day").

### 4.2 IDX File Date

Stored as three separate uint8 fields: `year` (+ 2000), `month`, `day`.

### 4.3 Waveform Packet Timestamp

Stored as individual fields at offsets 0xF8–0xFE: uint16 year (absolute),
uint8 month, day, hour, minute, second.

---

## 5. Byte Order

All multi-byte integers are **little-endian** (`QDataStream::LittleEndian`).

---

## 6. Summary of Known Unknowns

### USR File

| Range | Size | Status |
|-------|------|--------|
| 0x0000–0x002C | 45 B | Before serial number |
| 0x004D–0x0430 | ~995 B | Between serial and in-progress session |
| 0x02B6–0x102337 | ~1 MB | Between model number and session count |
| In-progress: 0x0433–0x0440 | 14 B | Between date and event stream |
| Historic: 0x09–0x0E | 6 B | Between date and duration |
| Historic: 0x11–0x44 | 52 B | Before message block |

### Waveform Packet

| Range | Size | Status |
|-------|------|--------|
| 0x02 | 1 B | Session counter — increments ~once per night, wraps at ~128 |
| 0x03 | 1 B | Always 0 — unused or high byte of counter that hasn't exceeded 255 |
| 0x9E–0xB0 | 22 B | 11 words — all visually similar to Flow channel, slightly different scaling/clipping. No additional clinical value |
| 0xB2–0xBD | 12 B | 6 words — under investigation |
| 0xBE | 2 B | Mechanical/machine-state value — **confirmed not pulse rate** |
| 0xC0 | 2 B | Unknown |
| 0xC2 | 2 B | Model-specific: varies on BMC Luna (mean ~200), constant 1472 on BMC G3. No correlation found with Leak/IPAP/EPAP |
| 0xC8 | 2 B | Model-specific: small range 0–26 on BMC Luna (mechanical), always 0 on BMC G3 |
| 0xD4 | 2 B | Near-constant (~10097–10100) — possible ADC baseline or atmospheric pressure calibration |
| 0xD6 | 2 B | Values 0–3; purpose unclear |
| 0xD8–0xF2 | 28 B | Always 0 on all tested machines |
| 0xF4 | 2 B | Internal alarm countdown — decrements 5/hour, no clinical value |
| 0xF6 | 2 B | Likely encodes preset alarm time — no clinical value |
