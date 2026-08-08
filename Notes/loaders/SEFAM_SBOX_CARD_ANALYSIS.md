# SEFAM S.Box AUTO — SD card format analysis

**Status:** container and waveforms decode with the existing loader; **these
cards carry no event log at all**, and therapy settings live somewhere the
loader does not look. The settings store has now been located and decoded — read
out of the device memory image, confirmed against a vendor report on one card
and against the delivered pressure on a second. Events remain unrecovered.

**Device:** SEFAM S.Box AUTO (APAP). `Created By=S.Box_AUTO`, firmware
`VER :A020400`. Same manufacturer and same firmware *platform* as the Rêve Auto,
but a different device class and a materially different card.

**Two cards examined**, from different devices with different therapy settings:

| | model code | sessions | span | vendor report |
|---|---|---|---|---|
| card A | `1200R` | 22 | 2026-06-23 → 2026-07-10, 53.9 h | yes, 6 days |
| card B | `1263R` | 3 | 2025-12-13 → 2026-02-07, 13.4 h | no |

Card A supplies the ground truth; card B is the independent check. Both model
codes report the same firmware version, and everything below holds on both
unless stated otherwise.

**Companion document:** `SEFAM_REVE_CARD_ANALYSIS.md` covers the Rêve Auto
`1279R` and is the reference for everything the two devices share (XOR-0xBF
header obfuscation, 10-second record framing, checksum/sequence trailer, `.INI`
semantics). This note records only what is **different or new** on the S.Box.

**Loader:** `sefam_loader.cpp` / `sefamDataParsing.cpp`. The S.Box is explicitly
*not* the validated model — `sefam_validated_model` is `"1279R"`, so opening an
S.Box card raises the "untested device" warning.

---

## Card layout

```
<SD card root>/
├── upload.dat                  optional; see below
└── 1200R/ or 1263R/            model code
    └── <serial>/               device serial (digits only)
        ├── <serial>.RAM      2,097,307 bytes — device memory image
        ├── <serial>.BKP      2,097,307 bytes — previous memory image
        └── DATA_0 … DATA_nnn/       one per session, unpadded
            ├── DATA_nnn.INI    1,290 bytes — cleartext channel manifest
            ├── DATA_nnn.FLW    flow                  25 Hz
            ├── DATA_nnn.PRE    pressure               5 Hz
            ├── DATA_nnn.LK     leak                   1 Hz
            ├── DATA_nnn.DET    breath-phase bitfield 25 Hz
            ├── DATA_nnn.NSD    near-constant bitfield 25 Hz
            ├── DATA_nnn.Y17    event bitfield        25 Hz   ← not in the INI
            └── .ABD .HRT .PLS .POS .SPO .STS .THO    header-only stubs
```

**There is no `DATA_nnn.LOG` anywhere on either card.** On card A this was
checked against the untouched archive the sample arrived in as well as the
extracted tree — 334 archive entries, no `.LOG`. It is not a collection
accident; it is a device trait.

Card A holds 22 session directories, 2026-06-23 22:20 → 2026-07-10 08:49, 53.9 h
of recording. Its `DATA_111` is a directory whose channel files are all
header-only stubs — the session was opened and nothing was written. **A loader
must tolerate a directory with an `.INI` and no data.**

Card B holds only three, spanning almost two months: a 3.5-minute session in
mid-December, then a 7.0 h night on 31 January and a 6.3 h night on 7 February.
Sessions are retained sparsely and consecutive directory numbers can be weeks
apart, so **the numbering says nothing about elapsed time** — read every `.INI`.

Directory names are **not zero-padded** (`DATA_0` … `DATA_2`, `DATA_90` …
`DATA_111`), so they must be sorted numerically. This is the same trap the Rêve
note records.

### `upload.dat`

Card B carries a 568-byte `upload.dat` at the **card root**, outside the model
directory. It is XOR-0xBF obfuscated like the file headers, and is almost
entirely zero: a short leader, then a patient-identification string field, then
padding, with a few bytes at the tail. On the card examined the identification
field held the device's "undefined" placeholder.

**Treat this file as personal data.** It is a staging file for the
manufacturer's upload path, and its one populated field is a patient
identifier. OSCAR has no reason to read it, and a backup that copies the card
verbatim will carry it along — worth knowing before anything on this card is
archived or shared.

---

## Header: the short 38-byte variant, and it matters

Channel files carry the **short** header, not the Rêve's 71-byte form:

```
#02/1200R<serial padded to 20 chars>/260705020303/
```

`#02/` is the format version (the Rêve writes `#03/`); the loader already treats
the tag as a version rather than a constant. What differs materially is that
**the 32-hex field is absent**, so there is no UTC epoch on this card — only the
12-character device-local timestamp. `FileHeader::utcEpoch` stays 0 and
`FileHeader::length` is 38 for every channel file, including the ones that carry
data. Header length is detected, never assumed, so waveforms decode correctly.

Confirm the geometry arithmetically rather than trusting the header type: a
`.FLW` of 66,324 bytes is `38 + 262 × 253`, and `66,324 − 71` is not a whole
number of records.

---

## The `.INI` — same schema mechanism, different rates

Cleartext, `[Chan0]`…`[Chan10]`, identical field names to the Rêve. The rates
are **not** the Rêve's:

| Channel | S.Box | Rêve | Declared in `.INI` |
|---|---|---|---|
| `FLW` | 25 Hz | 10 Hz | yes |
| `PRE` | 5 Hz | 5 Hz | yes |
| `LK` | 1 Hz | 1 Hz | yes |
| `DET` | 25 Hz | 10 Hz | yes |
| `NSD` | 25 Hz | 10 Hz | **no** |
| `Y17` | 25 Hz | 10 Hz | **no** |

`NSD` and `Y17` are present and populated but undeclared, so their geometry has
to be inferred from a channel that *is* declared. On this card all four of
`FLW`/`DET`/`NSD`/`Y17` have identical file sizes, which pins them to the same
25 Hz framing.

**Never hard-code a sample rate for this family.** The `.INI` is authoritative
for the declared channels, and the undeclared ones must be sized by comparison,
per card.

The `[Create Info]` block gives `Created By`, the full serial, the firmware
version and a creation timestamp; `[Start Record]` gives the session start as
separate Y/M/D/h/m/s fields plus a programmed and a real record duration.

---

## Why OSCAR shows waveforms but no events and no settings

Two independent blockers, both in the current loader:

1. **Events and settings come only from `.LOG`.** `sefam_loader.cpp` builds every
   event list from `data.log`, and the whole therapy-settings block is inside
   `if (lastKnown.valid)`, which is only ever set by `parseSettings()` on a log
   record of code 2 or code 13. No log ⇒ no mode, no pressures, no ramp, no
   humidifier, and no events.
2. **Event placement needs a UTC epoch the S.Box does not write.** The event loop
   is gated on `data.header.utcEpoch != 0`, because events are positioned
   relative to the session start using the epoch from the long header. On this
   card that is always 0, so even a card that *did* carry a `.LOG` would import
   no events without a second time base being wired in.

Fixing (1) without fixing (2) would achieve nothing on this device class.

---

## Vendor report for this card — the ground truth

The sample came with a *Sefam Analyze* 4.4.2 report covering six days of it. The
report is owner-password protected only, so it extracts with an ordinary PDF text
tool; no user password is needed.

It reports a full event set — AHI 8.3 (OA/OH/CA), 14.8 including central
hypopnoeas, snoring index 88.4/h, flow-limitation runs 11.1/h, mean apnoea 13 s
and mean hypopnoea 17 s — and a complete settings list. **So the analyzer obtains
both events and settings from this card**, or from data it has accumulated about
this device. That is what makes the gaps below worth chasing rather than
declaring impossible.

The report also carries device lifetime counters that appear nowhere in the
session files: total sessions, configuration date, global period in days. Those
are candidates for the same memory image the settings turned out to live in.

---

## Settings: a 12-slot history table inside `.RAM` / `.BKP`

**This is the main new finding, and it is confirmed, not inferred.**

Unlike the Rêve's, the S.Box memory image is **not encrypted**. It opens with a
155-byte plaintext header — a length prefix, the last session's `YYMMDDhhmmss`
timestamp, the firmware version string and two hex-text configuration blocks —
followed by a 2 MiB raw image containing UTF-16 path strings and binary
structures. (The Rêve's `.RAM`/`.BKP` are high-entropy throughout with zero
ASCII; see the Rêve note. **Do not assume one model's image tells you anything
about the other's.**)

The 155-byte header is nearly invariant. Between the `.RAM` and `.BKP` of the
same card it differs only in its timestamp, one three-byte field in each of the
two hex-text blocks, and a two-byte tail that behaves like a checksum. Between
the two *cards* the second hex block differs in exactly one byte. That block is
**not** the therapy settings: it holds identical values on two devices whose
minimum and maximum pressures differ (7.0/13.0 against 12.0/19.0), which rules
out the reading that first suggests itself. What it does hold is unknown.

### How it was found

The vendor report prints a **settings *history*** — six timestamped snapshots,
not one current set — and the minimum pressure changes across them:

| When | Min | Max | Ramp | Ramp pressure |
|---|---|---|---|---|
| 2026-06-23 22:21 | 8.0 | 15.0 | 45 min [I.Ramp] | 6.5 |
| 2026-07-04 17:38 | 6.0 | 15.0 | Disabled | 6.5 |
| 2026-07-04 17:40 | 8.0 | 15.0 | Disabled | 6.5 |
| 2026-07-04 23:46 | 8.5 | 15.0 | 15 min [T.Ramp] | 8.0 |
| 2026-07-06 15:55 | 10.0 | 17.0 | 15 min [T.Ramp] | 8.0 |
| 2026-07-08 01:34 | 12.0 | 19.0 | 15 min [T.Ramp] | 8.0 |

Four of the six coincide with a session start to within a minute; the two on
2026-07-04 at 17:38 and 17:40 have no session directory at all, so the device
records a settings change made outside therapy.

Six *varying* values in a known order is a search key. Scanning the 2 MiB image
for any column reproducing `80, 60, 80, 85, 100, 120` (tenths of a cmH₂O) at a
constant stride returns **exactly one location in the whole file**, at stride 24;
the maximum-pressure sequence sits two bytes ahead of it. A single hit in 2 MiB
is the evidence — this is not a value that happened to match.

### Layout

A table of **twelve 24-byte records, each twelve uint16 little-endian**,
beginning at offset **16338** — the same offset on both cards, from different
devices and different model codes, so it is fixed at least for firmware
`A020400`. Locating it by shape rather than by offset is still the safer
implementation.

| word | meaning | card A | card B |
|---|---|---|---|
| 0 | ramp start pressure, ×10 cmH₂O | 40–80 | 40 |
| 1 | repeats the ramp duration when word 3 is 8, otherwise 0 | 0 or 15 | 0 or 45 |
| 2 | ramp duration in minutes; 0 = ramp disabled | 0, 15, 45 | 45 |
| 3 | ramp mode: 8 = T.Ramp, 12 = I.Ramp | 8, 12 | 12, and 28 in the factory slot |
| 4 | maximum pressure, ×10 cmH₂O | 40–190 | 130, 200 |
| 5 | minimum pressure, ×10 cmH₂O | 40–120 | 40, 70 |
| 6, 7 | 60, 60 | constant | constant |
| 8 | 130 = the apnoea response pressure the report prints as 13.0 cmH₂O | constant | constant |
| 9 | **differs per device** — unassigned | 184 | 185 (1 in the factory slot) |
| 10 | **differs per device** — unassigned | 50 | 80 |
| 11 | varies within a card — unassigned | 164, and 0 in the last slot | 164, 40, 0 |

Words 0–8 are the confirmed part. Words 9–11 are where the still-unidentified
accessory settings must live, and word 9 or 10 differing between two devices is
the first positive evidence that any of them is a setting rather than padding.

Decoded, the twelve slots read:

```
slot  ramp P  ramp            max / min
 0     6.5    45 min I.Ramp   15.0 /  8.0
 1     6.5    disabled        15.0 /  6.0
 2     6.5    disabled        15.0 /  8.0
 3     8.0    15 min T.Ramp   15.0 /  8.5
 4     8.0    15 min T.Ramp   17.0 / 10.0
 5     8.0    15 min T.Ramp   19.0 / 12.0
 6     4.0    45 min I.Ramp    4.0 /  4.0
 7     4.0    45 min I.Ramp    7.0 /  4.0
 8     4.5    45 min I.Ramp    7.0 /  4.0
 9     6.5    45 min I.Ramp   16.0 /  9.0
10     8.0    15 min T.Ramp   17.0 / 10.0
11     8.0    15 min T.Ramp   19.0 / 12.0
```

Slots 0–5 reproduce the vendor's six printed snapshots **exactly and in
chronological order across five independent fields** — thirty of thirty field
values. Slots 6–11 are six older configurations, consistent with a device the
report describes as having years of history. The `.RAM` and `.BKP` images hold
byte-identical tables.

### Independent confirmation on card B

Card B has no vendor report, so the decode was checked against the pressure the
device actually delivered. Its table reads **minimum 7.0, maximum 13.0, ramp
45 min I.Ramp starting at 4.0** — and the mask pressure agrees:

| session | 5th pct | median | 99.9th pct | max |
|---|---|---|---|---|
| night 1 | 4.2 | 8.5 | **13.0** | 15.1 |
| night 2 | 4.0 | 6.7 | 9.6 | 11.4 |

The 99.9th percentile of night 1 lands exactly on the decoded 13.0 ceiling — the
device pressed against its limit and stopped there — and the 5th percentile of
both nights sits on the decoded 4.0 ramp start, which is what a 45-minute ramp
should produce. (`PRE` is a *mask* pressure carrying the breath ripple, so the
raw maximum overshoots the ceiling; see the Rêve note on `PRE`.) Nothing in
those numbers was used to derive the layout, so this is a genuine test of it and
not a restatement.

Card B's slot 0 is a factory record — 20.0/4.0, the full device range, with an
otherwise unseen mode value of 28 — followed by three identical therapy records.
Slots 4–8 are zeroed and slots 10–11 repeat slots 2–3.

### Traps

- **The record boundary is 8 bytes earlier than the obvious one.** Starting each
  record where the max/min column starts matches minimum and maximum pressure on
  all six entries but breaks the ramp fields on three of them. Anyone re-deriving
  this will land on the wrong alignment first; the ramp fields are the check that
  catches it.
- **Slot order is not plain chronological.** On card A slots 0–5 are the six most
  recent and 6–11 six older ones, so the table wraps; on card B the used slots
  are 0–3 with 10–11 repeating 2–3. That said, **"take the last non-zero slot"
  yields the correct current settings on both cards** — slot 11 on card A
  matches the vendor's "latest settings for the period", and slot 11 on card B
  matches the delivered pressure above. Use that rule, but do not assume the
  earlier slots are in order: neither card exercises the wrap unambiguously.
- **There are no timestamps.** The six change times the analyzer prints appear
  nowhere in the image. Searched: epoch seconds at four UTC offsets in both byte
  orders, minutes since several plausible bases, text and BCD date forms, and the
  very distinctive 120-second gap between two of the changes. Nothing matches. So
  the analyzer's change *dates* come from its own database, not from the card.

### What this means for a loader

Minimum and maximum pressure, ramp duration, ramp mode and ramp start pressure
are all recoverable for the S.Box. Because the change times are absent, settings
can only be applied as "current" — and on this card the settings changed three
times inside a six-day period, so attributing per-session settings would be
wrong today.

**Still unidentified:** humidifier level, Comfort Control Plus, patient circuit
diameter and heated-tube presence. Only card A has a report, so there is no
labelled value to match card B's differing words against — the same wall the
Rêve note describes under "Why the remaining comfort and accessory settings
cannot be decoded". What card B narrows is *where* to look: **words 9 and 10**,
which differ between the two devices while words 6, 7 and 8 do not. Word 6 or 7
holding 60 on both cards is at least consistent with the Rêve's mask-leak
candidate (0.6 lpm per count, 60 → 36 lpm, and card A's report prints 36 lpm),
but consistency is not confirmation.

---

## `Y17` on the S.Box — tested as an event source, rejected

With no `.LOG`, `Y17` is the only per-sample channel that could plausibly carry
events, so it was tested rather than assumed.

Over the six days the vendor report covers (35.6 h of recording), `Y17` is
97.5 % zero and takes 15 distinct values, all combinations of seven bits. Every
run of every bit is **exactly 0.80 s** — 20 samples at 25 Hz. It marks instants
at fixed width; it carries no event extent.

| bit | pulses | per hour | vendor index for the nearest event type |
|---|---|---|---|
| 0 | 683 | 19.2 | FL runs 11.1/h |
| 2 | 67 | 1.9 | obstructive AI 1.7/h |
| 3 | 2703 | 75.9 | snoring 88.4/h |
| 4 | 70 | 2.0 | — |
| 5 | 540 | 15.2 | — |
| 6 | 1 | 0.03 | — |
| 7 | 23 | 0.6 | central AI 0.8/h |

Bits 2 and 7 land close to the obstructive and central apnoea indices, and bit 3
is the most frequent bit against snoring as the most frequent event, but bits 0
and 5 have no counterpart, and no bit or pair of bits reproduces the hypopnoea
indices (obstructive 5.8/h, central 6.5/h).

The same test was run on the Rêve, where a `.LOG` provides ground truth. There,
bit 3 aligns with snore records and bit 0 with flow-limitation starts, but both
over-count by 1.5–2×, and apnoea and hypopnoea correspondence is weak. **`Y17`
is not a usable event source on either device.**

> The Rêve note's statement that `Y17` runs "do not line up" with `.LOG` records
> is too strong and should be revisited: the alignment test there used a ±2 s
> window, and a constant offset larger than that hid a real correspondence for
> two of the six event types. The conclusion — `Y17` cannot supply events —
> survives; the reasoning does not.

`DET` (127 distinct values on this card) and `NSD` (82) were also profiled; both
behave as the Rêve note describes and neither resembles an event marker.

---

## Open problems

1. **No event source has been found on this card.** Not in the session files, not
   in the memory image — the image contains no 49-byte log records and nothing
   with the shape of an event list. Yet the analyzer reports a full event set for
   these same days. Either the analyzer scores events itself from the flow signal
   (it does compute hypopnoea durations, which the Rêve's log does not carry), or
   there is a store in the memory image that has not been recognised.
2. **Humidifier, Comfort Control Plus, patient circuit and heated tube** are
   unassigned. Words 9 and 10 are the place to look; assigning them needs a card
   whose accessory settings are independently known.
3. **The settings table's slot ordering** is still not understood. The offset is
   now confirmed on two devices, and "last non-zero slot" gives the right
   current settings on both, but the ring's write order does not.
4. **The rest of the 2 MiB image is undescribed.** The header's two hex-text
   configuration blocks are byte-identical between the two snapshots apart from
   the timestamp and two three-byte fields, and the device lifetime counters the
   report prints must be somewhere.

---

## What would still help

- **An S.Box card with one accessory setting deliberately changed** — the method
  that pinned the Rêve's humidifier byte. Words 9 and 10 are the target, and a
  single controlled change would settle one of them outright.
- **An analyzer report for card B**, or for any second S.Box. Card B's therapy
  pressures are now corroborated, but its humidifier, Comfort Control Plus,
  circuit and heated-tube values are unknown, which is the only reason words 9
  and 10 remain unassigned.
- **A card whose analyzer report shows a settings change at a known session
  boundary**, to test whether change times can be inferred rather than read.
- **Confirmation of whether the analyzer scores events itself.** If it does, the
  absence of an event store on the card is the answer, not a gap, and the loader
  should say so plainly rather than importing waveforms silently.
