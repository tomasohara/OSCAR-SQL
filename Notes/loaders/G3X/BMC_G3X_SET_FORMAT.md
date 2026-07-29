# BMC G3X `.set` File Format — Device Settings

**Date:** 2026-07-28
**Status:** Decoded and cross-validated. **AutoS is implemented** in
`BmcG3xData::ApplySetFileSettings()` (2026-07-29). CPAP / AutoCPAP / S remain decoded but
unimplemented — they have no ground-truth confirmation, so those modes keep the existing
inferred behaviour.
**Reference cards:**
- **E5 B25A Plus** reference card — firmware `E5-1.02.05.02`, `.set` is 2048 B
- **G3 A20** reference card — firmware `G3-2.12.54.13`, `.set` is 1536 B

---

## Why this matters

`BmcG3xData::ReadDateSession()` **never opens the `.set` file**. It does:

```cpp
std::memset(&dateSession.MacineSettings, 0, sizeof(BmcMachineSettings));
dateSession.MacineSettings.Mode = BmcMode::CPAP;
```

and then *infers* pressure settings from the IDX **observed daily pressure summary**
(`ItPressureMinHundredths` / `TsPressureMaxHundredths`), setting
`Mode = AutoCPAP` whenever `max > min`.

Consequences reported by a user comparing OSCAR against PAP-Link on the E5 card:

| Setting | OSCAR showed | Actual (PAP-Link) | Cause |
|---|---|---|---|
| Mode | AutoCPAP | AutoS | Hard-coded `CPAP`, then inferred from observed pressures |
| Pressure Max | 10.89 | Max IPAP 15.0 | 10.89 is an *observed* value, not a setting |
| Pressure Min | 7.5 | Min EPAP 7.5 | Coincidence — observed min equalled the setting |
| PS | n/a | 3.5 | Never parsed |
| Initial P | 7.5 | Initial EPAP 5.5 | Never parsed |
| Mask / Tube / Auto On / Auto Off / Ramp / SmartB | defaults | real values | Never parsed (`memset` 0) |

Note that a settings value of `10.89` is itself the tell: real BMC settings are always
whole multiples of 0.5 cmH₂O.

---

## File layout

The file is a sequence of **256-byte blocks**. Block 0 is a header; the rest are tagged
by their first two ASCII bytes.

| Tag | Meaning |
|---|---|
| `SH` | Settings header — timestamp of last change + change counter |
| `SS` | System settings (non-therapy: mask, tube, ramp, auto on/off, …) |
| `TS` | Therapy settings for **one** mode |

Observed block sequences:

```
E5 B25A Plus:  SH  SS  TS(6)  SS  TS(0) TS(1) TS(2) TS(6)
G3 A20:        SH  SS  TS(1)  SS  TS(0) TS(1)
```

So the file is: header, then **section A** = `SS` + `TS` for the active mode, then
**section B** = `SS` + a `TS` preset for every mode the device supports.

The two `SS` blocks are byte-identical in both cards.

### Which block is authoritative

Section A and section B disagree on the E5 card:

| AutoS field | Section A (0x200) | Section B (0x700) | PAP-Link |
|---|---:|---:|---:|
| Min EPAP | 9.00 | **7.50** | **7.5** |
| Max IPAP | 17.00 | **15.00** | **15.0** |
| Initial EPAP | 6.00 | **5.50** | **5.5** |
| PS | 4.00 | **3.50** | **3.5** |

**Section B is current.** Three independent confirmations:

1. PAP-Link's displayed values match section B exactly on all four fields.
2. The IDX *observed* daily pressure minimum for this card is 7.50 cmH₂O — impossible if
   Min EPAP were section A's 9.00.
3. On the G3 A20 card section A's `TS` and section B's `TS(1)` are **byte-identical**,
   so the rule is consistent there too.

Section A appears to be a stale snapshot (settings as of some earlier point). Do not use it.

**Rule: use the last `TS` block whose mode byte equals `SS[28]`.**

---

## `SH` block (offset 0x000)

| Offset | Type | Meaning | E5 B25A Plus | G3 A20 |
|---|---|---|---|---|
| 0x00 | char[2] | `"SH"` | | |
| 0x0C | 6 bytes | Timestamp of last settings change | 2026-07-08 22:24:06 | 2026-06-11 09:19:36 |
| 0x12 | u16 LE | Settings-change counter | 101 | 962 |

The timestamp uses the same encoding as `DecodeG3xTimestamp()` in
`bmcG3xDataParsing.cpp`: `year = 1900 + byte0`, then month, day, hour, minute, second.
The 8-byte record at 0x0C is repeated at 0x14, 0x20 and 0x28.

---

## `TS` block — therapy settings

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | char[2] | `"TS"` |
| 0x08 | u8 | **Mode** — matches the existing `BmcMode` enum |

The mode byte values line up exactly with `BmcMode` in `bmcDataParsing.h`
(`CPAP=0, AutoCPAP=1, S=2, ST=3, T=4, Titration=5, AutoS=6`).

All pressure fields are **u16 LE, hundredths of cmH₂O**. Field positions are
**mode-specific** — the block is effectively a union overlaid per mode.

### Mode 0 — CPAP
| Offset | Meaning | E5 B25A Plus | G3 A20 |
|---|---|---:|---:|
| 0x0A | Set pressure | 8.00 | 15.00 |
| 0x18 | Initial pressure | 4.00 | 15.00 |

### Mode 1 — AutoCPAP
| Offset | Meaning | E5 B25A Plus | G3 A20 |
|---|---|---:|---:|
| 0x0E | Min pressure | 6.00 | 6.00 |
| 0x10 | Max pressure | 15.00 | 15.00 |
| 0x18 | Initial pressure | 4.00 | 6.00 |

### Mode 2 — S
| Offset | Meaning | E5 B25A Plus |
|---|---|---:|
| 0x0A | EPAP | 4.00 |
| 0x12 | IPAP | 12.00 |
| 0x18 | Initial EPAP | 4.00 |

### Mode 6 — AutoS ✅ *confirmed against PAP-Link, 5/5 fields*
| Offset | Meaning | E5 B25A Plus | PAP-Link |
|---|---|---:|---:|
| 0x0E | Min EPAP | 7.50 | 7.5 ✓ |
| 0x16 | Max IPAP | 15.00 | 15.0 ✓ |
| 0x18 | Initial EPAP | 5.50 | 5.5 ✓ |
| 0x1A | **Rise time, milliseconds** (u16) | 300 | 300 ms ✓ |
| 0x2A | PS | 3.50 | 3.5 ✓ |

**Rise time (0x1A)** was initially mis-read as a pressure (300 → "3.00 cmH₂O"). It is
milliseconds. Two independent checks:

- 300 is the **only** uint16 equal to 300 anywhere in the 2048-byte file.
- Across this card's five `TS` blocks it reads 0 (CPAP), 0 (AutoCPAP), 200 (S),
  300 (current AutoS), 100 (stale AutoS) — zero exactly for the single-pressure modes,
  which have no IPAP/EPAP transition to ramp.

**Still unidentified in the AutoS block:** bytes 0x30, 0x31, 0x32 — see below.

### Confidence

- **Mode byte (0x08) and the AutoS field map:** high — the AutoS values match PAP-Link
  exactly on all four reported fields.
- **CPAP / AutoCPAP / S field maps:** medium — self-consistent and plausible across two
  devices, but **no ground-truth readout has been obtained** for those modes. The AutoCPAP
  map agrees on min/max across both cards, which is encouraging but not proof.

---

## `SS` block — system settings

Only one field is confidently identified:

| Offset | Meaning | E5 B25A Plus | G3 A20 |
|---|---|---|---|
| 0x1C | **Active therapy mode** | 6 (AutoS) ✓ | 1 (AutoCPAP) |

This matches PAP-Link's reported mode on the E5 card, and agrees with the mode byte of
the last `TS` block on both cards.

Raw `SS` bytes 0x08–0x2F:

```
E5 B25A Plus  0f 00 00 00 0f 00 00 00 0f 00 00 00 0f 00 00 00
              00 00 65 00 06 00 00 04 ff 00 01 ff 00 00 01 ff
              ff 01 01 00 00 08 01 01
G3 A20        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
              00 00 65 00 01 01 00 00 ff 00 00 ff 00 00 00 ff
              ff 01 00 00 00 0b 01 00
```

`0x1A` = `65 00` (101) on **both** devices — a constant of unknown meaning, distinct from
the `SH` change counter (which is 101 on the E5 card but 962 on the G3 A20 card).

**The remaining `SS` fields — mask type, air-tube type, auto on, auto off, ramp time,
Reslex, humidifier — are still NOT decodable.** Not for lack of ground truth any more, but
because several settings share the same encoded value, so they cannot be told apart.

### Candidate narrowing (2026-07-29)

A PAP-Link readout of the E5 card gives nine known values: Auto On **on**, Auto Off **on**,
Ramp **Auto**, Air tube **15 mm**, Mask **Nasal**, Pres Response **Soft**, I Sens
**Med High**, E Sens **Medium**, Rise Time **300 ms**.

Expected encodings, taken from the legacy IDX parser and the registered channel options in
`bmc_loader.cpp`: Auto On / Auto Off on = 1; air tube 15 mm = 1 (`0`=Normal 22mm,
`1`=Normal 15mm); mask Nasal = 1 (`0`=Full Face, `1`=Nasal); ramp Auto = `0xFF` (the value
`bmc_loader.cpp` tests for).

**Four of those settings encode to the same value, 1.** The whole `SS` block has only five
offsets that read 1 on the E5 and 0 on the G3 A20:

```
0x05  0x22  0x26  0x2A  0x2F
```

Five candidates for four settings, all indistinguishable — every one of them is a plausible
Auto On / Auto Off / air-tube / mask byte. No assignment can be made from this card alone.

Every byte differing between the two cards, for reference:

| SS offset | E5 | G3 A20 | note |
|---|---:|---:|---|
| 0x05 | 1 | 0 | candidate |
| 0x08, 0x0C, 0x10, 0x14 | 15 | 0 | four u32s all = 15; unexplained |
| 0x1C | 6 | 1 | **active mode** (confirmed) |
| 0x1D | 0 | 1 | |
| 0x1F | 4 | 0 | |
| 0x22, 0x26, 0x2A, 0x2F | 1 | 0 | candidates |
| 0x2D | 8 | 11 | |
| 0xFE | — | — | block trailer |

Ramp = Auto could not be located: no byte is `0xFF` on the E5 where it is not also `0xFF`
on the G3 A20, and the block's tail is `0xFF` filler, so the ramp field may not live in
`SS` at all.

### Pres Response / I Sens / E Sens — likely the `TS` triple at 0x30–0x32

Three consecutive `TS` bytes hold three small values, and all three changed together
between the stale and current AutoS snapshots:

| TS block | mode | 0x30 | 0x31 | 0x32 |
|---|---|---:|---:|---:|
| CPAP | 0 | 0 | 0 | 0 |
| AutoCPAP | 1 | 0 | 0 | 1 |
| S | 2 | 6 | 2 | 0 |
| AutoS (current) | 6 | 5 | 4 | 2 |
| AutoS (stale) | 6 | 7 | 5 | 3 |

Three bytes, three unaccounted-for settings (Pres Response, I Sens, E Sens), zero on CPAP
where trigger sensitivities do not apply. `0x31` = 4 is consistent with I Sens "Med High"
on a 1–5 scale, but `0x30` = 5 does not fit E Sens "Medium" on that same scale, so the
mapping is **not** established. Recorded as a lead, not a decode.

### What would settle it

Two captures of the **same** card with exactly **one** setting changed between them. Each
such pair pins one byte outright. The card already contains a natural before/after pair for
the `TS` block — the stale AutoS snapshot at 0x200 versus the live one at 0x700 — which is
how the 0x30–0x32 triple was spotted; but both `SS` copies are byte-identical, so `SS`
needs a fresh capture.

---

## Implementation notes

The display plumbing already exists — `bmc_loader.cpp` (~lines 273–331) already maps
every `BmcMode` including `AutoS` onto OSCAR channels:

```cpp
if (machineSettings.Mode == BmcMode::AutoS) {
    oscarSession->settings[CPAP_EPAPLo]        = machineSettings.AutoS_MinEPAP;
    oscarSession->settings[CPAP_IPAPHi]        = machineSettings.AutoS_MaxIPAP;
    oscarSession->settings[BMC_INITIAL_EPAP]   = machineSettings.AutoS_InitialEPAP;
    ...
}
```

and the mode channel already registers `addOption(6, "AutoS")`. The G3X path is the only
thing that never populates `BmcMachineSettings`. So the work is confined to
`BmcG3xDataParsing` — no new channels or UI required.

**Regression risk:** existing G3 A20 / B20A users currently get min/max from the observed
IDX pressure summary. Switching them to `.set` values changes what they see (to the
*correct* values, but it is a visible change). Consider whether to apply this to all G3X
devices or gate it initially.

---

## What was implemented (2026-07-29)

`BmcG3xData::ApplySetFileSettings()` in `bmcG3xDataParsing.cpp`, called from both exit
paths of `ReadDateSession()` (the EVT-only path and the waveform path) *after* the existing
pressure inference, so decoded settings supersede inferred ones.

**AutoS only.** The function reads `SS[0x1C]` for the active mode and takes the **last**
`TS` block carrying that mode. If the active mode is not AutoS it returns false and the
inferred settings stand — so CPAP and AutoCPAP devices are untouched. Confirmed by
simulation against both reference cards:

| Card | Active mode | Result |
|---|---|---|
| E5 B25A Plus | 6 = AutoS | block 0x700 → 7.5 / 15.0 / 5.5 / PS 3.5 — matches PAP-Link 4/4 |
| G3 A20 | 1 = AutoCPAP | falls through, inferred settings kept |

Fields populated: `Mode`, `AutoS_MinEPAP`, `AutoS_MaxIPAP`, `AutoS_InitialEPAP`,
`AutoS_PS` (new field added to `BmcMachineSettings`), and `AutoS_MinIPAP` **derived** as
`minEPAP + PS` — exact for a fixed-PS mode, and not read from any confirmed field.

A range check (`4 ≤ minEPAP ≤ 25`, `minEPAP ≤ maxIPAP ≤ 30`) guards against a corrupt or
misread block; on failure the function logs a warning and keeps the inferred settings.

**One related fix in `bmc_loader.cpp`:** the AutoS branch hard-coded
`settings[CPAP_PS] = 0` despite mapping the session to `MODE_BILEVEL_AUTO_FIXED_PS`, a
*fixed pressure-support* mode. It now reports `AutoS_PS`. Parsers that cannot recover PS
leave the field at 0, preserving previous behaviour — notably the legacy BMC parser, which
does not populate it.

### Consistency check

Max IPAP 15.0 with PS 3.5 implies a maximum EPAP of 11.5. The observed daily pressure
maximum on this card is 10.89 cmH₂O, inside the resulting `[7.5, 11.5]` EPAP band — an
independent check that the field map is right.
