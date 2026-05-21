# AeonMed AS100 Auto — SD Card Analysis

Initial assessment of the AeonMed AS100 Auto SD card layout, comparing it against
existing OSCAR loaders to identify the closest structural analogue and a starting
point for building a loader.

**Sample:** `C:/Users/Guy/Downloads/Ola Stensby - AeonMed As100 Auto_OlaStensby`
**Date analyzed:** 2026-05-19
**Source:** single user dump, AS100 Auto, sessions from April 2022.

Companion document: `Notes/loaders/LOADER_DETECTION_PATTERNS.md` (general detection patterns
in the existing loader set).

---

## Card layout

```
<root>/
├── YZR00152.bkp                    4,194,304 bytes (exactly 4 MB — device image/log)
└── 00152202204/                    directory; name appears to be "<serial-tail><yyyymm>"
    ├── 20220420.dat                10,168 bytes  (short session, ~Apr 20 2022)
    └── 20220422.dat                22,300,380 bytes (full session, ~Apr 22 2022)
```

## File internals

### `YZR00152.bkp` (root-level, 4 MB)

- Exactly 4 MiB — a clean power of two, consistent with a raw flash-memory image
  of the device.
- Records with the `0xC5C5` sync word at 128-byte boundaries — but only in the
  first ~320 KB. Last sync at offset `0x4FE80` (record 2557 of 32 768).
- The remainder is sparse zero/`0xFF` fill.
- Likely a device-internal store, not session-organized data. May be skippable
  for OSCAR's import path; the per-day `.dat` files appear self-contained.

### `<yyyymmdd>.dat` (per-day session file)

Header layout, observed in `20220422.dat`:

| Offset | Length | Content |
|---|---|---|
| `0x00` | 8 | Date/time: `e6 07 04 16 10 0c 30 04` → year (LE uint16) `0x07E6 = 2022`, mo `04`, day `0x16 = 22`, hr `0x10 = 16`, min `0x0c = 12`, sec `0x30 = 48`. Matches filename. |
| `0x10` – `0x4F` | 64 | Mixed pointer/header data (some values look like ARM flash addresses ending in `0x20…`). |
| `0x50` | 16 | ASCII string `AS2XYZR00152` — **model code (`AS2X`) + serial (`YZR00152`)**, NUL-padded. |
| `0x60` – `0x13F` | ~224 | Additional header/setup fields, mostly zero. |
| `0x140` – ~`0x1000` | varies | **128-byte fixed records**, each beginning `c5c5 01 28 00 3c …`, last byte always `0x88`. Same record format appears in `.bkp`. |
| `0x10000` onward | bulk | **16-bit little-endian waveform samples** (e.g. ~`0x002a` → `0x002e` rising) — flow/pressure data. |

So a `.dat` file is structurally:

> **device-identity header → settings/event records (c5c5 / 0x88 framed) → padding → raw 16-bit waveform**

## Comparison to existing OSCAR loaders

Walking the six detection strategies from `Notes/loaders/LOADER_DETECTION_PATTERNS.md`:

| Strategy | AeonMed match? |
|---|---|
| Fixed sentinel at root | No standard sentinel. The `.bkp` could play this role, but the filename varies (`YZR<serial>.bkp`) — would need a glob (`YZR*.bkp`). |
| Sentinel sub-folder | No `FPHCARE/`, `P-Series/`, `THERAPY/`, `SL/`, `DV6/`, `DATALOG/`. |
| Same-basename triplet | No (`.bkp` has no sibling `.idx`/`.000`). |
| **File-content magic** | **Best option.** ASCII `"AS2X"` at offset `0x50` of any `.dat`, or `"AS2XYZR"` pattern, or the `0xC5C5 … 0x88` 128-byte framing. |
| Directory-name prefix | Session directory is numeric (`00152202204`), no brand string. |
| Factory of sub-formats | AS100 Auto is one model today, but AeonMed has other devices (AS101, ASV-series) that may eventually need their own format slots. |

### Closest existing loader by aspect

| Aspect | Closest existing loader |
|---|---|
| **Top-level layout** — one device-id-named subdir containing flat data files | **Yuwell Format A** (`<root>/RunLog.bys` + `MODEL-SERIAL/flat-file-per-session`). AeonMed's layout (one device dir, flat per-day `.dat` files) matches Format A's shape more closely than Format D (which nests session subdirs inside MODEL-SERIAL). Yuwell Format A also has a 0-byte root marker (`RunLog.bys`) — AeonMed has a large `.bkp` at root playing the same sentinel role. |
| **Root-level device sentinel file** | **BMC Legacy** (`.USR` at root, 4-byte header magic, device ID in body). AeonMed's `.bkp` plays the same role: a root-level file whose presence (and first bytes) can anchor `Detect()` before opening any session file. Use it as the primary detection path. |
| **Per-day session file** | No direct match. ResMed uses date-named *folders* (`DATALOG/yyyymmdd/`) of EDFs; PRS1 uses sequential IDs; BMC uses indexed records inside bulk files. Date-as-filename (e.g. `20220422.dat`) is **unique** in the documented loader set. |
| **Directory naming** (`serialtail+yyyymm`) | No match. BMC uses the full serial as basename; Yuwell uses the full `MODEL-SERIAL` string; Resvent uses calendar folders. AeonMed's `00152202204/` (last-5 of serial + `yyyymm`) is unique — needs a custom parser. |
| **Fixed-size record framing with a sync word** | **BMC** (legacy and G3X). Same walking approach: fixed strides, validate each 128-byte record by its leading `c5c5` magic. However, the `c5c5 … 0x88` framing does not appear in any other documented format (BMC uses different magic; Yuwell uses timestamp blocks; Resvent uses CRC-wrapped text). **No code reuse is possible** for the `c5c5` region — the parser is written from scratch. |
| **Detection sentinel quality** | **BMC G3X-style content-magic** (`bmcg3x_loader.cpp:19`) as *fallback*: open the first `.dat`, read bytes `0x50`–`0x5F`, require ASCII model marker (`"AS2X"` or `"AS"+digit+"X"`). Use only if no `.bkp` is found at root. |
| **Identity location** (model + serial as ASCII inside data file) | **BMC G3X** (serial at IDX `0x30`, product name at `0x100`); **PRS1** (in `PROP.TXT`). AeonMed embeds both at offset `0x50` of the `.dat` header. |
| **Design family** | AeonMed sits in the same Chinese-OEM tradition as **BMC and Yuwell** — bespoke binary formats, model/serial as ASCII in a fixed header offset, sync-word-framed records, no EDF. **Resvent** (`THERAPY/CONFIG/RECORD` platform) is superficially Chinese-OEM but uses a completely different architecture (text+CRC hybrid, month/day folder hierarchy, self-describing channel descriptors) and is **not** a useful template for AeonMed. |

## Recommended starting point for an AeonMed loader

1. **Scaffold from Yuwell Format A** (`yuwell_loader.{h,cpp}` → `YuwellFormatA`
   as the structural model). Format A's shape — one root-level sentinel file plus
   a `MODEL-SERIAL/` dir of flat per-session files — is closer to AeonMed than
   Format D's nested triplet layout. Keep the factory pattern (`YuwellFactory`
   style) because AeonMed has multiple AS-series models likely to diverge.
2. **Detection — two-stage, `.bkp`-first**:
   - *Primary (fast):* glob for `*.bkp` at the card root (analogous to BMC
     Legacy's `*.USR` glob). If found, read the first 2 bytes and verify the
     `0xC5C5` sync word. This is fast and unambiguous — no other documented
     format has a `.bkp` at root with that magic.
   - *Fallback:* If no `.bkp`, scan one level down for any subdir containing a
     `*.dat`, open the first `.dat`, and require the ASCII model marker
     (`"AS2X"` at offset `0x50`, or more generally `"AS"`+digit+`"X"` pattern).
     This is the BMC G3X content-magic style.
3. **Parsing** — three-region approach (no code reuse from existing loaders for
   the `c5c5` region — the framing is AeonMed-proprietary):
   - Header block (offsets `0x00`–`0x4F`): timestamp + ARM flash pointers.
     Timestamp at `0x00` (8 bytes, LE uint16 year + binary month/day/hour/min/sec)
     and ASCII identity at `0x50` → builds `MachineInfo` directly.
   - Settings records (`c5c5`/`0x88`-framed, 128 bytes each, from ~`0x140`
     to ~`0x1000`): decode channel-by-channel once field offsets are confirmed
     from a device report comparison.
   - Waveform region (16-bit LE samples from ~`0x10000`): emit as flow/pressure
     channels at whatever sample rate the device uses.

## Open questions

- Whether `00152202204` is always `<serialtail><yyyymm>` or some other
  concatenation. One sample isn't enough to confirm.
- Whether `AS2X` is the model code for all AS100 Auto units, or whether other
  variants use different magic.
- The exact channel-to-byte mapping inside both the `c5c5` settings records and
  the 16-bit waveform region — needs side-by-side comparison with the device's
  own report.
- Whether `YZR00152.bkp` is essential for import or just a service/diagnostic
  dump. The session `.dat` files appear self-contained (they carry their own
  identity header).
- AeonMed family scope: AS101, ASV models — same format with minor changes, or
  separate enough to need their own factory slot?

## Bottom line

**Closest single existing loader: Yuwell Format A** (flat per-session files in
a MODEL-SERIAL dir, root-level sentinel file playing the same detection role as
AeonMed's `.bkp`) **+ BMC Legacy** (root-level device file as primary detection
anchor) **+ BMC G3X** (content-magic fallback detection style; multi-region
binary parsing approach).

Build the AeonMed loader as a **Yuwell-Format-A-shaped wrapper** with:
- `.bkp`-first detection (BMC Legacy-style root sentinel)
- `c5c5`-magic fallback inside `.dat` (BMC G3X-style content-magic)
- from-scratch `c5c5`/`0x88` record parser (no reuse — format is AeonMed-proprietary)

Confirmed excluded as templates:
- **Resvent** (`THERAPY/CONFIG/RECORD`): text+CRC hybrid, folder hierarchy,
  self-describing channels — architecturally unrelated.
- **Yuwell Format D**: nested session subdirs, triplet files — more complex
  than AeonMed's flat layout.

No existing AeonMed references in the OSCAR codebase (`grep` for
`AeonMed|AS100|AS2X|YZR` returns nothing) — this would be a new loader.
