# Python Waveform Demonstrator — Spec

**Status:** Draft
**Date:** 2026-04-14
**Target audience:** Power users who want to write their own SQL-based tools
against an OSCAR database.

## 1. Purpose

OSCAR stores per-session waveform data (flow rate, pressure, etc.) as compressed
binary BLOBs in the SQLite database. The `Export CSV` feature exposes summary
statistics only — the raw samples are not reachable without decoding the blobs.

This demonstrator is a **small, self-contained Python script** that shows how
to:

1. Open the OSCAR SQLite database read-only.
2. Look up a profile by username.
3. Find the profile's most recent **OSCAR day** (noon-to-noon).
4. Enumerate every session on that day, across all machines.
5. Look up channel IDs through the `channels` table (no hard-coded constants).
6. Load, decompress, and decode the Flow Rate and Pressure waveform blobs.
7. Plot both waveforms on a shared time axis.

The script is **not** a replacement for OSCAR. It is a reference implementation
that a user can copy, read, and extend into their own analysis tools.

## 2. Scope

### In scope
- Single profile, single day, all sessions on that day.
- Exactly two channels: `FlowRate` and `Pressure` (the `channels.channel_code`
  values — note that the DB column stores these **without** the `CPAP_`
  prefix that appears on the C++ schema constants).
- Plot to screen with matplotlib. No file output.
- Handles both EventList types: Flow Rate is always a waveform
  (`EVL_Waveform`, uniform sampling); Pressure is usually stored as
  step-change events (`EVL_Event`, non-uniform sampling with ms-delta time
  array) by most loaders — only Intellipap and VRem use `EVL_Waveform` for
  pressure. The demo picks the type automatically from
  `event_lists.event_type` and uses a step plot for event-type data.

### Out of scope
- Respiratory events, snore, leaks, SpO₂, pulse — structurally identical but
  not demonstrated here.
- GUI, session picker, date picker.
- Writing to the database. Opened read-only via `file:...?mode=ro`.
- Schema migration or version negotiation. Script asserts schema v13.

## 3. Command-line interface

```
oscar_waveform_demo.py <db_path> <profile_name>
```

- `<db_path>` — absolute path to `oscar.db` (or whatever the DB file is
  called). The path differs per OS; not the script's job to discover it.
- `<profile_name>` — `profiles.username` value (case-sensitive, as stored).

Exit codes: `0` success, `1` usage error, `2` profile/data not found, `3`
schema mismatch, `4` blob decode error.

## 4. Algorithm (the teaching progression)

Each step is a separate function, with a short docstring explaining *what
part of the schema it exercises*. The script's `main()` calls them in order
and prints a one-line status for each, so a reader can match code to effect.

### Step 1 — Open the database read-only

```python
uri = f"file:{db_path}?mode=ro"
conn = sqlite3.connect(uri, uri=True)
```

Verify `schema_version` matches the supported version (13). Abort on mismatch
with a clear message.

### Step 2 — Resolve profile name → profile_id

```sql
SELECT id FROM profiles WHERE username = ? AND status = 'active'
```

Teaches: profiles are keyed by autoincrement `id`; `username` is the human
handle. All downstream joins use `id`.

### Step 3 — Find the latest OSCAR day

An **OSCAR day** runs noon-to-noon. A session belongs to OSCAR day *D* if its
`start_time` falls in `[D 12:00, D+1 12:00)` local time.

Equivalently: **subtract 12 hours from `start_time` and take the calendar
date**. That gives the OSCAR day the session belongs to.

`sessions.start_time` is **milliseconds since the Unix epoch**, not seconds
(this is an important gotcha — SQLite's `date()` expects seconds).

The `daily_summaries` table is already keyed by OSCAR date, so the simplest
query is:

```sql
SELECT MAX(date) FROM daily_summaries WHERE profile_id = ?
```

Fallback (computed directly from sessions, demonstrates the math):

```sql
SELECT MAX(date(
    (s.start_time / 1000) - 12 * 3600,  -- shift 12h back, convert to seconds
    'unixepoch', 'localtime'
)) AS oscar_day
FROM sessions s
JOIN machines m ON s.machine_id = m.id
WHERE m.profile_id = ?
```

The script computes it both ways and asserts they agree — that's the teaching
moment about what "OSCAR day" means.

### Step 4 — Enumerate sessions on that day

```sql
SELECT s.id, s.start_time, s.end_time, m.brand, m.model
FROM sessions s
JOIN machines m ON s.machine_id = m.id
WHERE m.profile_id = ?
  AND date((s.start_time / 1000) - 12 * 3600, 'unixepoch', 'localtime') = ?
  AND s.enabled = 1
  AND s.summary_only = 0
ORDER BY s.start_time
```

Summary-only sessions have no waveforms; skip them.

Print a one-line header: `Found N sessions on 2026-04-13, total X minutes.`

### Step 5 — Look up channel IDs through the `channels` table

The demo uses the *channel code* strings as input, not magic numbers:

```sql
SELECT channel_id FROM channels
WHERE profile_id = ? AND channel_code = ?
```

with `channel_code` in `('FlowRate', 'Pressure')`.

Teaches: channel codes are stable strings, channel IDs are integers used as
join keys into `event_lists`, `session_channels`, etc. The `channels` table
also carries display labels, colors, units — show the `label` and `fullname`
columns in the plot legend.

Gotcha: the C++ schema uses identifier names like `CPAP_FlowRate` and
`CPAP_Pressure`, but the *string code* stored in `channels.channel_code` is
the short form (`"FlowRate"`, `"Pressure"`) — `common_gui.h` defines these
as `STR_GRAPH_FlowRate = "FlowRate"`, `STR_GRAPH_Pressure = "Pressure"`.
Use the short form when querying.

### Step 6 — For each session × channel, load the EventList metadata

```sql
SELECT id, first_time, last_time, count, rate, gain, offset,
       data_size, compressed_size
FROM event_lists
WHERE session_id = ? AND channel_id = ?
ORDER BY eventlist_index
```

No `event_type` filter: the demo works with both types. A channel's type
is read from `event_lists.event_type` — uniform-waveform (0) or step-event
(1). A single channel in one session **may be split into multiple
EventLists** (one per contiguous run of samples — mask-off gaps create
boundaries). The demo must concatenate them in `eventlist_index` order.

**Pressure is usually events, not a waveform.** Most loaders (ResMed, PRS1,
Prisma, ResVent, SleepStyle, BMC, F&P Icon, Yuwell) store `CPAP_Pressure`
as `EVL_Event` — a sparse list of step changes with a time-delta array —
because the therapy pressure only changes a handful of times per session.
Intellipap and VRem are the exceptions that store it as `EVL_Waveform`.
Flow Rate, by contrast, is always a waveform.

Field meanings (see also §6):

| Field           | Meaning                                                  |
|-----------------|----------------------------------------------------------|
| `first_time`    | ms-since-epoch timestamp of the first sample             |
| `last_time`     | ms-since-epoch timestamp of the last sample              |
| `count`         | number of samples (primary array length)                 |
| `rate`          | **milliseconds per sample** (e.g. 40 for 25 Hz)          |
| `gain`, `offset`| physical_value = raw_int16 * gain + offset               |
| `data_size`     | uncompressed byte length of `data_blob`                  |
| `compressed_size` | set when the blob is compressed; NULL otherwise        |

### Step 7 — Load the BLOB from `event_data`

```sql
SELECT data_blob, data_compressed, compression_method, checksum
FROM event_data WHERE eventlist_id = ?
```

Exactly **one** of `data_blob` / `data_compressed` is populated; check both.

- `compression_method = 0` → use `data_blob` as-is.
- `compression_method = 1` → `data_compressed` is in **Qt `qCompress` format**:
  a 4-byte big-endian uint32 (expected uncompressed length) followed by a
  standard zlib stream. Decode with:

  ```python
  import struct, zlib
  expected_len = struct.unpack('>I', compressed[:4])[0]
  raw = zlib.decompress(compressed[4:])
  assert len(raw) == expected_len
  ```

Verify the CRC16 checksum (`qChecksum`, which is the ISO/IEC 3309 / X-25 CRC)
against `calculated`. Warn, don't abort, on mismatch — OSCAR itself warns and
continues.

### Step 8 — Decode the raw samples

The uncompressed byte array is `count` little-endian signed 16-bit integers
(`int16`). Apply gain/offset to get physical values:

```python
import numpy as np
raw = np.frombuffer(decompressed, dtype='<i2', count=count)
values = raw.astype(np.float32) * gain + offset
```

Build the time axis from `first_time` and `rate`:

```python
times_ms = first_time + np.arange(count, dtype=np.int64) * int(rate)
```

### Step 9 — Plot

Two stacked subplots sharing the x-axis (time):

- Top: Flow Rate (L/min), from channel code `FlowRate`
- Bottom: Pressure (cmH₂O), from channel code `Pressure`

Time axis formatted as `HH:MM` local time. Vertical dashed lines at each
session boundary (`session.start_time`). Legend shows machine model for each
session.

One window per run; `plt.show()` blocks until closed.

## 5. Dependencies

- Python ≥ 3.9 (stdlib `sqlite3`, `zlib`, `struct`, `datetime`, `argparse`)
- `numpy`
- `matplotlib`

Nothing else. No ORM, no config file.

## 6. Reference: waveform blob format

Pulled from `oscar/database/event_data_repository.cpp`. Authoritative; the
demo is tested against it.

### Primary data array (always present for waveforms)

- Element type: `int16` (OSCAR `EventStoreType = qint16`)
- Byte order: little-endian
- Length: `event_lists.count` elements = `count * 2` bytes uncompressed
- Storage: either `event_data.data_blob` (raw) or `event_data.data_compressed`
  (qCompress format)

### Secondary data array (optional, `event_lists.has_second_field = 1`)

- Same element type, byte order, and length as primary
- Used for e.g. min/max pairs in some loaders — not relevant for Flow Rate or
  Pressure in this demo

### Time delta array (present for event-type EventLists)

- Only present when `event_lists.event_type = 1` (Event).
- Stored in `event_data.time_blob` (raw) or `event_data.time_compressed`
  (qCompress format) — same storage pattern as the primary data.
- Element type: `uint32` little-endian. Length: `count` elements.
- Each element is a **millisecond offset from `first_time`**. Absolute
  timestamp of sample i is `first_time + time[i]`.
- Waveforms (`event_type = 0`) always have uniform spacing `rate` ms apart,
  so no time blob is written.

Note: `event_lists.event_type` matches the C++ `EventListType` enum directly
(`EVL_Waveform = 0`, `EVL_Event = 1`). The schema reference doc had these
reversed at one point — the enum values in `oscar/SleepLib/event.h` are the
source of truth.

### qCompress format recap

```
[4 bytes big-endian uint32: expected uncompressed length][zlib stream]
```

Do **not** feed the leading 4 bytes to `zlib.decompress`.

### Checksum

`quint16` CRC computed via Qt's `qChecksum` over the uncompressed primary
data. Algorithm: ISO/IEC 3309 (polynomial 0x8408, initial 0xFFFF, final XOR
0xFFFF) — same as X.25. Python reference implementation included in the
script as `_qchecksum(bytes) -> int`.

## 7. Possible extensions (documented, not implemented)

- Respiratory events: join `respiratory_events` on `session_id`, plot as
  coloured spans over the Flow Rate subplot.
- Leaks / tidal volume / minute vent — identical pattern with a different
  `channel_code`.
- Multi-day plotting: loop the query in step 3 over a date range.
- CSV dump: write `(timestamp_ms, flow_L_per_min, pressure_cmH2O)` rows
  instead of plotting.

## 8. Acceptance test

Run against a ResMed test profile with at least one night of data:

- Script prints profile ID, OSCAR day, session count.
- Checksum warnings: zero.
- Plot window opens, both traces visible, pressure ≈ flat, flow oscillates.
- Session boundary lines align with gaps in the flow trace.
- No Python warnings on `numpy.frombuffer` or type mismatches.

