# SEFAM Rêve Auto — SD card format analysis

**Status:** format decoded and **validated against the vendor's own analyzer
report** for the same 31 sessions. Container, waveform scalings, event taxonomy
and therapy settings are all confirmed. Remaining gaps are small and listed at
the end.
**Device:** SEFAM Rêve Auto (APAP). `Created By=REVE_AUTO`, model code `1279R`,
firmware `VER :A010500`. SEFAM is a French sleep-medicine manufacturer.

**Branding:** sold in India as the **Sanrai Rêve Auto** (Sanrai Med is a
distributor). This is a **relabel, not a variant**: the underside of the case is
marked "Sefam", and "Sanrai" appears only on an applied sticker. So the hardware
is a SEFAM unit that was stickered at distribution, and there is no reason to
expect a separate Sanrai firmware line — though regional firmware builds on
identical hardware remain possible in principle, and only a second card would
settle that.

Treat SEFAM and Sanrai as one device family, the way `prisma_loader.cpp` covers
both Weinmann and Löwenstein.

Note the card itself carries **no brand string at all**: the only identity on it
is `REVE_AUTO`, the model code `1279R`, and the serial. Detection is unaffected
(the model code and `#03/` header are a solid fingerprint), but whatever brand
OSCAR displays is a naming choice rather than something read from the data, and
users will report this machine under either name.
**Loader:** `sefam_loader.cpp` / `sefamDataParsing.cpp`, added 2026-08-03 from
this analysis. Design: `Notes/loaders/SEFAM_LOADER_DESIGN.md`. **Awaiting testing
by a real user** — everything below was verified against this one card and the
manufacturer's report for it.

Related: `SD_CARD_FINGERPRINTS.md` has an earlier entry for the **SEFAM S.Box
AUTO** (model code `1263R`, firmware `VER :A020400`) — same firmware platform,
different device class. That entry's guess that the obfuscation key is `0x9F` is
**wrong**; see below. Its "~100 byte header" estimate is also wrong (it is 71).

---

## Card layout

```
<SD card root>/
└── 1279R/                      model code
    └── <serial>/               device serial (digits only; the full serial
        │                       is "1279R" + this, i.e. modelcode+serial)
        ├── <serial>.RAM        1,633,228 bytes — encrypted device RAM image
        ├── <serial>.BKP        1,633,228 bytes — previous RAM image
        └── DATA_000 … DATA_031/
            ├── DATA_nnn.INI    1,167 bytes — cleartext channel manifest
            ├── DATA_nnn.LOG    event log
            ├── DATA_nnn.FLW    flow                  10 Hz
            ├── DATA_nnn.PRE    pressure               5 Hz
            ├── DATA_nnn.LK     leak                   1 Hz
            ├── DATA_nnn.DET    breath-phase bitfield 10 Hz
            ├── DATA_nnn.NSD    (unused here)         10 Hz
            ├── DATA_nnn.Y17    event bitfield        10 Hz   ← not in the INI
            └── .ABD .HRT .PLS .POS .SPO .STS .THO    header-only stubs
```

One directory per session, numbered sequentially, **not** by date. The sample
holds 31 complete sessions (2026-07-10 → 2026-07-30, 159.0 h) plus a
`DATA_031/` containing only a `.LOG` — the next session's log, opened but with
no recording yet. **A loader must tolerate a partial final directory.**

Sessions are mask-on/mask-off segments, not nights: several nights have a short
1–3 minute session immediately followed by the real 7–8 h one. Merging into
OSCAR days is the loader's job.

The `.ABD/.HRT/.PLS/.POS/.SPO/.STS/.THO` stubs are the polygraph channels the
platform supports but this device does not populate. **File size == header size
means "channel not recorded"** — that is the presence test, since the INI
declares the full schema regardless.

---

## Header obfuscation: XOR 0xBF — header only

The fixed-length text header of every `.LOG` and channel data file is XORed with
the constant `0xBF`. Confirmed by known-plaintext: the header contains the device
serial, which also appears in cleartext in the `.INI`.

```
'0'(0x30)^0xBF = 0x8F     '9'(0x39)^0xBF = 0x86
' '(0x20)^0xBF = 0x9F     'R'(0x52)^0xBF = 0xED
```

> **The obfuscation stops at the end of the header.** The sample and log bodies
> that follow are stored in the clear. An earlier revision of this document said
> the whole file was XORed; that was wrong. Every measurement in this document
> was computed on raw bodies and is unaffected — only the description was wrong —
> but a loader that descrambles the body will corrupt every sample and fail
> every checksum.
>
> Demonstrated on one 8.5 h session's `PRE` channel:
>
> | Body treated as | checksum + sequence | mean pressure |
> |---|---|---|
> | plaintext (correct) | **3075 / 3075** | **5.25 cmH₂O** |
> | XORed with 0xBF | 0 / 3075 | 15.39 cmH₂O |
>
> The manufacturer's report gives 5.2 cmH₂O for that session. Note also that the
> record checksums are computed over the bytes **as stored**, so they only
> validate against an undescrambled body.

This is obfuscation, not encryption — a single constant across the whole card.
The `.INI` is **not** obfuscated (plain text). The `.RAM`/`.BKP` are **not**
XOR-0xBF either (see below).

## File header (after XOR)

Channel files — **71 bytes, fixed**:

```
#03/<serial padded to 20 chars>/<YYMMDDhhmmss>/<32 hex chars>/
 │           20 bytes                12 bytes        32 bytes
 └─ format version
```

> **The leading tag is a format version, not a constant.** The Rêve Auto writes
> `#03/`; the **S.Box AUTO writes `#02/`**. A loader that matches `#03/`
> literally will reject S.Box cards outright. Accept any `#NN/`.
>
> Both observed versions share the same field layout, and header length is
> detected independently of the version, so the number is useful for diagnostics
> but should not gate parsing.

- `<YYMMDDhhmmss>` is **local** time and matches the `.INI` `[Start Record]`
  exactly.
- The 32-hex field is two 8-byte values: the first is the session start as a
  **UTC** Unix epoch, the second is an unidentified signature/checksum.
  In this sample local = UTC+2, consistent throughout.

`.LOG` files — **38 bytes, fixed**: the same string without the 32-hex field.

> **Do not locate the end of the header by scanning for the 4th `/`.** In
> `DATA_017.LOG` a payload byte decodes to `/` and shifts a delimiter-scanning
> parser by two bytes. Use the fixed lengths (71 / 38).

## Record structure — verified on all 31 sessions

After the header, data files are a flat array of fixed records, each holding
**exactly 10 seconds** of samples:

```
[ rate x 10 sample bytes ][ checksum 1 byte ][ sequence 2 bytes, big-endian ]
```

- **checksum** = low 8 bits of the sum of the record's data bytes.
  Verified 3075/3075 records on every channel of the longest session.
- **sequence** = 1-based, strictly incremental.
- Record sizes: 10 Hz → 103 B, 5 Hz → 53 B, 1 Hz → 13 B.
  (`.PLS` is 75 Hz / 16-bit → 1503 B, unexercised in this sample.)

`file_size == header + N * record_size` holds exactly for all 31 sessions ×
6 populated channels — the layout is confirmed, not inferred.

Session duration = `N × 10` seconds. Sample rates come from the `.INI`, so the
reader can be fully data-driven.

## The `.INI` — schema only

Cleartext, parseable with `QSettings::IniFormat`. Contains `[Create Info]`
(firmware, serial, creation date), `[BLE Device]`/`[Oximeter]`/`[PolyLink]`
pairing addresses, `[Start Record]`, and `[Chan0]`–`[Chan11]` giving each
channel's `Name`, `Type`, `Unit`, `Min`, `Max`, `Freq`, `Bit`.

**Every `.INI` on the card is byte-identical apart from the timestamp fields.**
There are **no therapy settings** — no mode, pressure range, ramp, EPR or
humidifier. This is the format's biggest gap for OSCAR (see *Open problems*).

`Y17` is not declared in the `.INI` at all, but is present and populated.

---

## Channel scaling — confirmed

| Channel | Rate | Formula | Basis |
|---|---|---|---|
| `FLW` | 10 Hz | `-180 + raw × 460/255` L/min | INI `Min=-180 Max=280`; zero at raw ≈ 99.8 |
| `PRE` | 5 Hz | `raw × 0.1` cmH₂O | see below |
| `LK` | 1 Hz | `raw × 0.6` L/min | INI `Min=0 Max=153` → 153/255 = 0.6 |

`raw == 255` is the **invalid/no-data sentinel** on all three (whole 10-second
records of `0xFF` appear when the blower is off).

**Pressure is ×0.1, not ×0.2.** Across all 31 sessions the true `PRE` range
(excluding the sentinel) is 0–154, i.e. **0.0–15.4 cmH₂O**; ×0.2 would imply
30.8 cmH₂O, which no CPAP delivers. Percentiles: p5 = 4.0, p50 = 5.1,
p90 = 7.5, p99 = 11.8 cmH₂O — a patient sitting near the 4 cmH₂O APAP floor
most of the night.

**Two independent cross-checks confirm the flow and leak scales simultaneously:**

1. At the same instant, the `FLW` baseline and `LK` agree. `FLW` raw ≈ 113 →
   23.8 L/min; `LK` raw ≈ 38 → 22.8 L/min. So `FLW` is **total** flow
   (patient + leak), not patient flow.
2. Binning `LK` by concurrent `PRE` over a whole night gives a clean orifice
   law `LK_raw ≈ k·√P` with k essentially constant:

   | PRE (cmH₂O) | 4.0 | 4.5 | 5.0 | 5.5 | 6.0 | 6.5 | 7.0 | 7.5 |
   |---|---|---|---|---|---|---|---|---|
   | mean LK raw | 38.1 | 41.2 | 45.1 | 46.7 | 47.1 | 49.7 | 57.9 | 57.7 |
   | k = LK/√P | 19.1 | 19.4 | 20.2 | 19.9 | 19.2 | 19.5 | 21.9 | 21.1 |

   Converted, that is ≈23 L/min at 4 cmH₂O rising to ≈37 at 10 — textbook
   vented-mask flow. So `LK` is **total leak including intentional mask vent**,
   which OSCAR must handle the way it does for ResMed/PRS1 total-leak devices.

## `DET` — breath-phase bitfield (10 Hz)

Not a data channel; a per-sample flag byte. Bit occupancy over one night and
mean concurrent flow:

| Bit | Set | Mean flow SET | Mean flow CLEAR | Reading |
|---|---|---|---|---|
| 5 | 35.1 % | **42.6 L/min** | **18.3 L/min** | **inspiration** — confirmed |
| 6 | 12.8 % | 42.9 | 24.4 | sub-phase of inspiration (peak flow) |
| 1 | 89.1 % | 26.5 | 29.0 | likely "valid / patient connected" |
| 0 | 6.3 % | 30.0 | 26.6 | unknown |
| 4 | 0.8 % | 39.4 | 26.7 | unknown |
| 7 | 4.7 % | 32.2 | 26.5 | unknown |
| 2, 3 | <0.3 % | — | — | unknown |

Bit 5's 35 % occupancy is exactly a normal I:E ratio and the flow separation is
unambiguous. This gives a loader free breath-phase segmentation without
detecting it from the flow signal.

## `Y17` — event bitfield (10 Hz), meanings unknown

Undeclared 10 Hz flag byte, 99 % zero. Bits 0, 2, 3, 4, 5 are used. Runs are
mostly 0.7–0.8 s with occasional 10 s ones. Per-hour run rates are stable
across sessions (bit5 ≈ 19–30/h, bit0 ≈ 7–14/h), which is the profile of a
real respiratory-event channel — but **which** events is not established.

**`Y17` runs do not line up with `.LOG` records** (only ~4 % of runs fall within
±3 s of any log entry), so the two are independent views, not duplicates.

Since the `.LOG` is now confirmed as the scored-event source, `Y17` is *not*
needed for AHI. Its most likely role is per-sample event **extent** — which
would also supply the hypopnea durations the log omits (open problem 1). A
loader can ignore it for now.

`NSD` is 99.96 % zero in this sample — nothing to decode from it here.

## `.LOG` — 49-byte event records

```
offset 0  : uint32 LE  Unix epoch (UTC)
offset 4  : uint32     always 0   (upper half of a 64-bit time_t)
offset 8  : uint8      event code
offset 9  : uint16 BE  argument
offset 11 : 38 bytes   zero, except codes 2/13 which populate ~bytes 11-25
```

### Event codes — CONFIRMED against the vendor report

The manufacturer's *Sefam Analyze* software was run over this same card,
producing a per-session CSV and a period synthesis. Matching its event counts
against the log census settles the taxonomy:

| Code | OSCAR event | log n | report n | log /h | report /h |
|---|---|---|---|---|---|
| 3 | **Obstructive Apnea** | 248 | 242 | 1.56 | 1.5 |
| 4 | **Central Apnea** | 191 | 190 | 1.20 | 1.2 |
| 5 | **Obstructive Hypopnea** | 76 | 75 | 0.47 | 0.5 |
| 6 | **Central Hypopnea** | 233 | 232 | 1.46 | 1.5 |
| 7 | **Snore** | 1408 | 1399 | 8.86 | 8.8 |
| 8 | **Flow limitation** | 961 | — | 6.05 | 6.1 |
| 2 | **Settings change** | 5 | 3 | | |
| 9 | therapy stop (`PRE`≈0, `LK`=255 → blower off) | 42 | | 0.26 | |
| 10 | unidentified | 260 | | 1.64 | |
| 13 | settings/state snapshot | 13 | | | |
| 1, 11, 12, 21, 24, 27, 28 | ≤11 each, administrative | | | | |

Agreement is **per session**, not just in total — on most of the 31 sessions the
four apnea/hypopnea counts match exactly. The residual over-counts (≤6 across
21 days) are the analyzer applying inclusion rules the raw log does not; its own
synthesis header says it filters on ramp and leak state.

Every derived index reconciles arithmetically:

```
AHI (OA,OH,CA)     1.56 + 0.47 + 1.20               = 3.23   report 3.2
AHI (OA,OH,CA,CH)  1.56 + 0.47 + 1.20 + 1.46        = 4.69   report 4.7
AHI obstructive    1.56 + 0.47                      = 2.03   report 2.0
AHI central        1.20 + 1.46                      = 2.66   report 2.7
```

**Apnea duration = argument × 0.1 s.** Decoded mean 16.5 s for code 3 and 12.8 s
for code 4, against a reported average apnoea duration of **17 s (OA) | 13 s
(CA)**. The hard floor at exactly 100 is the ≥10 s scoring rule.

**Hypopnea duration is *not* in the argument** — codes 5 and 6 carry 0 in 97 % of
records, yet the report gives average hypopnoea durations of 16 s (OH) and 15 s
(CH). Where the analyzer gets them is unresolved; `Y17` is the obvious suspect.

> **One vendor inconsistency, not a decode error.** The report's per-session
> `FL Runs` column sums to 352 (2.22/h) while its own synthesis index for the
> same period says **6.1/h**. The decoded code-8 count gives **6.05/h**, matching
> the synthesis. Every other event type agrees across all three sources
> (CSV sum, synthesis index, decoded log), so the CSV column is the outlier —
> most likely it counts grouped *runs* while the index counts individual
> detections. Prefer the synthesis reading: **code 8 = flow limitation, 1:1**.

### Settings records — codes 2 and 13

Bytes 11–25 are zero on every code **except 2 and 13**, which carry the therapy
settings. Decoded against the report's printed settings (`Mode=A-PAP`,
`Min=4.0`, `Max=20.0`, `Ramp=45 min`, `Ramp pressure=4.0`, `CC+=Level 2`,
`Humidifier=Level 4`, `Heated tube=Present`):

**Code 2** payload `40 45 45 31 200 40 60 60 130 200 121 4 6 3 0`:

| Byte | Value | Meaning |
|---|---|---|
| 11 | 40 | **Min pressure 4.0** (×0.1 cmH₂O) |
| 12 | 45 | **Ramp 45 min** |
| 15 | 200 | **Max pressure 20.0** (×0.1 cmH₂O) |
| 16 | 40 | **Ramp pressure 4.0** (×0.1 cmH₂O) |
| 22 | 4 | candidate: humidifier level (report says Level 4) |
| 13, 14, 17–21, 23–25 | 45, 31, 60, 60, 130, 200, 121, 6, 3, 0 | unassigned |

Code 2 occurs 5 times. The report's `setting change` column is non-zero on
exactly the two sessions carrying multiple code-2 records (counts 1 and 2,
matching), and **byte 23 is the only byte that differs** between the pair on the
session reporting two changes — so byte 23 is the field that was edited.

**Code 13** payload `[0/1] 0 3 40 100 200 0 [5–10] [0/1] 51 55 50 57 55 52`
repeats min (40 → 4.0) and max (200 → 20.0) pressure and adds a mode-like `3`
at byte 13 and a varying 5–10 counter at byte 18. Bytes 20–25 are a constant
six-byte tail.

This **overturns the earlier conclusion that settings were unrecoverable** — they
are in the `.LOG`, not the encrypted `.RAM`. Enough is confirmed for a loader to
report mode, pressure range and ramp; the comfort/humidifier fields are
candidates pending a second card with different settings to vary them against.

## `.RAM` / `.BKP` — encrypted, not readable

Both 1,633,228 bytes. The first 74 bytes are structured cleartext (`4c 00 00 01`,
then a uint32 LE timestamp at offset 4, then a fixed block); everything after is
**8.000 bits/byte entropy in every one of 398 4 KB windows** — no plaintext, and
not XOR-0xBF.

The `.RAM` and `.BKP` in this sample were written ~6 h apart and differ in only
**1,803 of 1.6 M bytes (0.1 %)**, with all other offsets byte-identical. Content
changes therefore stay positionally local, which rules out CBC/CTR with a varying
IV and points to ECB or a fixed keystream. That makes a known-plaintext attack
conceivable *if* a card ever arrives with its device settings independently
known — but nothing is readable today.

---

## Validation summary

Everything below was checked against *Sefam Analyze* output for the same 31
sessions, not merely inferred from the card:

| Quantity | Result |
|---|---|
| Session→directory mapping | 31/31, `DATA_nnn` ↔ report session `n+1`, timestamps exact |
| Event taxonomy (OA/CA/OH/CH/snore/FL) | confirmed, per session and in total |
| Derived indices (AHI and components) | reconcile arithmetically to ±0.05 |
| Apnea durations | 16.5 / 12.8 s decoded vs 17 / 13 s reported |
| `PRE` × 0.1 cmH₂O | mean pressure matches report on **30 of 31 sessions within ±0.17 cmH₂O**, most within ±0.05 |
| Total recorded time | 159.0 h decoded vs "operating time 159 h 18" reported |
| Therapy settings | min/max pressure, ramp time and ramp pressure all recovered from log code 2 |
| Mask-disconnect seconds | `LK`==255 matches exactly where disconnects are long (412 s session: 412 decoded) |

The one session where mean pressure differs (−1.08 cmH₂O) is the 20-minute
session that was 23 % mask-disconnected — the analyzer excludes disconnected
time from the average, so the disagreement is expected and explains itself.

## Open problems

1. **Hypopnea durations** are not in the log argument (codes 5/6 carry 0), yet
   the analyzer reports 16 s / 15 s averages. Source unknown — `Y17` is the
   likely candidate.
2. **`Y17` bit meanings** still unknown. It is *not* the apnea source (the
   `.LOG` is), and its runs do not line up with log records, so it is an
   independent signal — possibly the per-sample event *extent* that would answer
   problem 1.
3. **Log code 10** (260 records, 1.64/h) is unidentified and matches no report
   column.
4. **Comfort/humidifier setting bytes** in code 2 are candidates only; byte 23 is
   known to be the field edited on a settings change, but its encoding is not
   established. Varying them needs a second card.
5. **Report "Average leaks"** is 0.01–0.06 — nowhere near the decoded total leak
   of 23–45 L/min, so the analyzer reports *unintentional* leak in some other
   unit. A loader must decide which it presents; the card gives total.
6. The 8-byte header signature is unidentified (not needed to read data).
7. `NSD` is empty in this sample; purpose unknown.
8. `.RAM`/`.BKP` remain encrypted — but this no longer matters, since the
   settings turned out to be in the `.LOG`.

## What would still help

- **A second card with different therapy settings** — the single most useful
  next artefact. It would pin down the comfort/humidifier bytes, confirm the
  mode encoding (only `A-PAP` has been seen), and show which log codes change
  in fixed-CPAP mode.
- A card whose report shows **non-zero "No breath"** events, to identify code 10.

## Loader viability

**Good.** The container was always the easy part; the vendor report has now also
settled the two things that actually blocked a useful loader — the event
taxonomy and the therapy settings. A loader can produce flow, pressure, leak,
breath phase, OA/CA/OH/CH with real durations for apneas, snore, flow
limitation, AHI, and the pressure/ramp settings, with every one of those
validated against the manufacturer's own analysis of the same nights.

Two caveats for implementation. Apnea durations come from the log argument but
**hypopnea durations do not** — either leave them as zero-duration flags or
resolve open problem 1 first. And the analyzer applies inclusion filtering
(ramp, leak) that the raw log does not, so OSCAR's counts will run a few events
higher across a 3-week period; that is a scoring-policy difference, not a bug,
and it should not be "fixed" by inventing filters to match.

Note the platform is shared with the S.Box AUTO (different model code, different
firmware, 25 Hz flow, and a shorter header in that sample). Header length and
sample rates must be taken from the data and the `.INI`, never hardcoded, if one
loader is to cover both.
