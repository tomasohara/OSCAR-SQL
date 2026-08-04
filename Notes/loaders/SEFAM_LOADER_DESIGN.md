# SEFAM loader — design spec

**Date:** 2026-08-03
**Status:** approved design, not yet implemented.
**Format reference:** `Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md` — the container,
waveform scalings, event taxonomy and settings records are all decoded there and
validated against the manufacturer's *Sefam Analyze* report for the same card.
This document covers only how that format becomes an OSCAR loader.

---

## 1. Scope

Import **any Sefam card** matching `<digits><letter>/<digits>/DATA_nnn/`, not just
the one validated model.

Everything that can vary between models is read from the data rather than
hardcoded: sample rates and channel names come from the `.INI`, header length is
detected, and record size is derived from the declared rate. Only one model
(`1279R`, Rêve Auto) has been validated end to end against a vendor report, so
`Open()` emits the existing `deviceIsUntested(info)` signal for any other model
code. Unknown Sefam devices therefore import, but announce that the format has
not been confirmed for them.

Out of scope for this version: the `.RAM`/`.BKP` images (encrypted), the `DET`,
`NSD` and `Y17` channels, and oximetry/polygraph channels (never populated on a
CPAP-only device).

## 2. Files

| File | Purpose |
|---|---|
| `oscar/SleepLib/loader_plugins/sefamDataParsing.h/.cpp` | Pure decode. No OSCAR types. |
| `oscar/SleepLib/loader_plugins/sefam_loader.h/.cpp` | OSCAR integration. |

Both pairs added to `oscar.pro` (`SOURCES` and `HEADERS`). `main.cpp` gains the
`#include` and a `SefamLoader::Register();` call alongside the other loaders.

The split mirrors `bmc_loader.cpp` / `bmcDataParsing.cpp`. The reason is
verification: the format is already specified and validated on paper, so the
risk in this project is transcription error, not misunderstanding. A decode
layer that takes bytes and returns plain structs can be checked directly against
known-correct numbers without constructing a `Machine` or `Profile`.

## 3. Parser layer — `SefamParsing`

```cpp
struct FileHeader {
    QString   serial;        // trimmed of pad spaces
    QDateTime localStart;    // from YYMMDDhhmmss — device local time
    qint64    utcEpoch;      // 0 when absent (short header variant)
    int       length;        // 71 (channel files) or 38 (.LOG)
};

struct ChannelSpec { QString name; int freq, bits, min, max; };

struct LogRecord {
    qint64  utcSeconds;
    quint8  code;
    quint16 arg;
    QByteArray payload;      // bytes 11..48 — settings live here for codes 2/13
};

struct Settings {
    bool  valid = false;
    float minPressure = 0, maxPressure = 0, rampPressure = 0;
    int   rampMinutes = 0;
};

struct SessionData {
    QString dirName;
    FileHeader header;
    int recordCount = 0;                          // 10-second records
    QHash<QString, ChannelSpec>     channels;     // schema from the .INI
    QHash<QString, QVector<quint8>> samples;      // raw bytes per channel
    QVector<LogRecord>              log;
    Settings                        settings;
};

void descramble(QByteArray &);                    // XOR 0xBF in place — HEADER ONLY
bool parseHeader(const QByteArray &decoded, FileHeader &out);
bool parseIni(const QString &path, QHash<QString,ChannelSpec> &, QDateTime &start);
bool readChannel(const QString &path, const ChannelSpec &,
                 QVector<quint8> &out, int &records, QString &error);
bool readLog(const QString &path, QVector<LogRecord> &out);
bool parseSettings(const LogRecord &, Settings &);
```

`readChannel` carries the real logic: derive record size as
`freq × 10 × (bits/8) + 3`, require `(fileSize − headerLen) % recordSize == 0`,
then per record verify the 8-bit sum checksum and the big-endian sequence
number before concatenating sample bytes.

The `bits` field must be honoured in the record-size formula even though every
channel this version imports is 8-bit. The `.INI` declares `.PLS` as 16-bit, so a
parser that assumed 8 bits would compute the wrong record size and wrongly reject
the file as malformed. `readChannel` sizes 16-bit channels correctly; §6 simply
does not map any of them to an OSCAR channel.

**Header length is detected, never assumed.** Parse the fixed fields
(`#03/`, 20-char serial, `/`, 12-char timestamp, `/`), then test whether 32 hex
digits followed by `/` come next. Present → 71-byte channel header; absent →
38-byte `.LOG`-style header. Do **not** locate the end of the header by scanning
for the fourth `/` — a payload byte can decode to `/` and shift the parse (this
occurs in the sample card).

The parser returns error strings rather than logging, keeping it free of UI
dependencies.

## 4. Loader layer

```cpp
class SefamLoader : public CPAPLoader {
    bool Detect(const QString &path) override;
    int  Open(const QString &path) override;
    int  Version() override { return sefam_data_version; }   // = 1
    const QString &loaderName() override;                    // "SefamLoader"
    MachineInfo newInfo() override;                          // brand "Sefam"
    bool backupData(Machine *mach, const QString &path);
    static void Register();
};
```

No `initChannels()` override — every event maps to an existing OSCAR channel, so
there are no custom channel IDs to register (see §7).

`Detect()` walks for `<digits><letter>/<digits>/DATA_nnn/DATA_nnn.INI` and also
verifies the `#03/` tag in a channel header, so a coincidentally-shaped
directory tree from another vendor cannot produce a false positive.

**Machine identity.** Brand is `"Sefam"`. Model is taken from the `.INI`
`Created By` field with its trailing padding trimmed (`REVE_AUTO` → shown as
`Rêve Auto`), falling back to the model code when that field is absent or
unrecognised. Series is the model code (`1279R`); serial is the full
`<modelcode><digits>` string from the header, which matches the `.INI` `Serial
Number` field. The card carries no brand string of its own, so `"Sefam"` is a
naming choice: the same device is distributed in some markets under a
distributor's label (see the format notes), and users may report it under either
name.

## 5. Session assembly and time handling

One `DATA_nnn/` directory → one OSCAR `Session`. `SessionID` is the header's UTC
epoch: unique, stable across re-imports, and cheap to test against
`mach->SessionExists()`. When the header carries no UTC epoch (short-header
variant), `SessionID` falls back to `localStart.toSecsSinceEpoch()`. That is
stable for a given machine and timezone, which is sufficient for the dedup test;
it is not comparable across timezones, but neither is any other identifier such a
card offers.

Short mask-off restarts remain separate sessions. OSCAR's existing day grouping
merges them into an OSCAR day, as it does for other multi-session devices.

**Session start uses the local wall-clock fields**, so a 22:03 bedtime displays
as 22:03 regardless of the importing machine's timezone.

**Events are placed relatively**, which needs no timezone arithmetic:

```
eventTimeMs = sessionStartMs + (logRecord.utcSeconds − header.utcEpoch) × 1000
```

This requires the 32-hex field, which the short header variant lacks. A card
whose headers carry no UTC epoch imports **waveforms only**, with a warning, and
skips events. That is the correct conservative default: no non-`1279R` device
has had its event codes validated.

## 6. Waveforms

| `.INI` name | OSCAR channel | stored value | gain | rate |
|---|---|---|---|---|
| `FLW` − `LK` | `CPAP_FlowRate` | patient flow × 10 | 0.1 | 100 ms (10 Hz) |
| `PRE` | `CPAP_Pressure` | raw byte | 0.1 | 200 ms (5 Hz) |
| `LK` | `CPAP_LeakTotal` | raw byte | 0.6 | 1000 ms (1 Hz) |

Rates come from the `.INI`, not the table; the table records what the validated
model declares. Values are stored as `qint16` via the `qint16 *` overload of
`EventList::AddWaveform`.

`LK` is **total** leak including the intentional mask vent, hence
`CPAP_LeakTotal` rather than `CPAP_Leak`.

### Flow must be converted to patient flow

The card's `FLW` channel is **total** flow — it includes the intentional mask
vent, so it oscillates around roughly +22 L/min rather than zero. OSCAR cannot
use that directly: `FlowParser::calcPeaks()` detects breaths by crossings of a
hard-coded `zeroline = 0`. Measured over one 8.5 h session:

| signal | mean | zero crossings/min |
|---|---|---|
| total flow (`FLW`) | 26.70 | 7.0 |
| patient flow (`FLW − LK`) | **0.17** | **33.6** |

33.6 crossings/min is ~16.8 breaths/min, a normal respiratory rate. Imported as
total flow, breath detection fails outright and OSCAR produces **no** respiratory
rate, tidal volume, minute ventilation or Ti/Te — all of which it otherwise
derives for free.

So the loader subtracts the device's own leak estimate, sampled at `LK`'s rate
and held across each flow sample. This is principled rather than a fudge: total
flow = patient flow + leak, and the two channels were independently confirmed to
agree at the baseline (23.8 vs 22.8 L/min).

### Offsets must be baked into the stored value

`EventList` has an `offset` field, but OSCAR applies it **inconsistently**:

- `gLineChart.cpp` renders `(raw + offset) * gain` — offset applied *before* gain
- `EventList::data()` returns `raw * gain` — offset ignored
- `FlowParser::openFlow()` likewise applies gain alone

A channel needing an offset would therefore graph differently from how it is
summarised, and would break breath detection. The loader passes `offset = 0`
everywhere and pre-scales the stored value instead. This is why flow is stored
in tenths of a L/min with gain 0.1 rather than as raw bytes with gain 460/255 and
offset −180.

Channels not imported:

- `DET` — breath-phase bitfield. `Session::UpdateSummaries()` already calls
  `calcRespRate()`, which derives respiratory rate, tidal volume, minute
  ventilation, Ti and Te from the flow waveform. Importing `DET` would duplicate
  that and diverge from every other loader.
- `Y17` — undecoded.
- `NSD` — empty on the sample card.
- `ABD`, `HRT`, `PLS`, `POS`, `SPO`, `STS`, `THO` — header-only stubs. Skipped by
  the size test: file size equal to header size means the channel was not
  recorded.

### The 255 sentinel

`0xFF` marks invalid/no-data on all three waveforms. Imported naively it reads as
**153 L/min leak** and **25.5 cmH₂O**, which would corrupt both statistics. It is
not rare enough to ignore — 1,324 s of `LK` and 310 s of `PRE` across the sample
card, and one session is 412 of 420 seconds sentinel.

Each channel is therefore split into contiguous runs of valid samples, and **each
run becomes its own `EventList`** via a separate `AddEventList()` call, the same
way ResMed handles EDF gaps. One `EventList` holds one contiguous span, so this
is the only correct mechanism.

Splitting is at **sample** granularity, not record granularity: `PRE` sentinel
runs are all record-aligned but `LK` runs are not (28 of 36 in the sample), so
record-level splitting would leak sentinel values into the data.

## 7. Events

| Log code | OSCAR channel | Event value |
|---|---|---|
| 3 | `CPAP_Obstructive` | `arg / 10.0` seconds |
| 4 | `CPAP_ClearAirway` | `arg / 10.0` seconds |
| 5 | `CPAP_Hypopnea` | 0 |
| 6 | `CPAP_Hypopnea` | 0 |
| 7 | `CPAP_VSnore` | 0 |
| 8 | `CPAP_FlowLimit` | 0 |

**Codes 5 and 6 are obstructive and central hypopnea.** OSCAR has no central
hypopnea channel, and both existing loaders that face this — BMC G3X
(`bmcG3xDataParsing.cpp`) and Prisma (`prisma_loader.cpp:414`) — fold both into
`CPAP_Hypopnea`. Sefam follows that precedent for consistency. See
`Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md` for the taxonomy, which records the
distinction even though OSCAR discards it. Revisiting this across all three
loaders, probably by adding a general central-hypopnea channel, is a known
deferred decision and is deliberately **not** attempted here — a Sefam-only
channel would make the three loaders inconsistent with each other.

**Hypopneas record zero duration.** The log argument is zero in 97% of code 5/6
records. The vendor reports 15–16 s averages, so the duration exists somewhere —
most likely `Y17` — but it is not in the log, and storing a nominal value would
put a fabricated number in the database as though it had been measured.

### Event placement — direction established, exact alignment unverified

**The log timestamp is the event END.** Two independent lines of evidence agree:
reading it as a start produces impossible overlaps across 362 consecutive event
pairs (zero overlaps when read as an end), and apnea flags placed at the raw
timestamp render visibly late against the manufacturer's own waveform report,
by an amount that varies with each event's duration.

**But flags are placed at the event START.** OSCAR draws a `FLAG` channel as a
bare vertical line at the event timestamp — `gFlagsLine`'s FLAG branch ignores
the stored duration entirely, surfacing it only in the tooltip. ResMed, the
reference loader, passes the EDF+ annotation *onset*, so OSCAR's convention is
flag-at-start. (`bmc_loader` passes `EndTime` and is the outlier here.) The
loader therefore subtracts the duration before adding the event.

Hypopneas have no duration to subtract. Left at the raw timestamp they would sit
about 15 s late on this patient's most frequent event type, so their flags are
shifted by the manufacturer's published mean durations — 16 s for OH, 15 s for
CH. This is a **display-placement heuristic, not measured data**; real durations
vary, so individual events are approximate in both directions rather than
uniformly late. The stored duration remains zero. Both constants should be
removed once per-event extent is recovered (open problem 1).

> **Not verified to the sample.** The correction's *direction* is well founded —
> apnea flags at the raw timestamp were observed rendering late by an amount that
> varied with each event's duration, which is exactly the signature of a
> flag-at-end placement. Whether they now land precisely right is unconfirmed:
> the manufacturer's waveform PDFs lack the resolution to compare against, and
> they cannot be zoomed.
>
> This is a general limitation rather than a SEFAM quirk. Devices commonly record
> event timestamps at a coarser resolution than the flow waveform, so a flag can
> only ever be placed to within that quantisation regardless of how the loader
> interprets the field. Treat exact flag alignment as an open item across
> loaders, not something this one can settle.

Codes 9–13 and the administrative codes are parsed but not imported. The loader
computes no AHI; `calcAHIGraph()` derives it from the flag channels.

## 8. Settings

Taken from `.LOG` code 2 where present, falling back to code 13:

```
CPAP_Mode         = MODE_APAP
CPAP_PressureMin  = payload byte 11 × 0.1
CPAP_PressureMax  = payload byte 15 × 0.1
CPAP_RampTime     = payload byte 12
CPAP_RampPressure = payload byte 16 × 0.1
```

Only these four are written. The humidifier and comfort-level candidates
identified in the format notes are **not** written: they are positional guesses
that a single card cannot vary, and an unverified byte displayed as a therapy
setting is worse than showing nothing.

Mode is set to APAP because A-PAP is the only mode observed. A session with no
settings record carries no settings rather than inheriting a neighbour's.

## 9. Error handling

A malformed session is skipped; it is never fatal. Partial cards are normal on a
device that was unplugged mid-write, so a card with one bad directory must still
import the other thirty.

| Condition | Response |
|---|---|
| `.INI` missing or unparseable | Skip the directory |
| Header tag not `#03/` | Skip the directory |
| `(fileSize − headerLen) % recordSize != 0` | Skip that **channel**, keep the session |
| Checksum or sequence mismatch | Truncate the channel at the last good record, keep the session |
| Zero valid samples after sentinel splitting | Skip the session entirely |

The size-divisibility check is what catches a wrong assumed sample rate, so it
must not be silently tolerated. A torn final record from an interrupted write is
expected and is handled by truncation rather than rejection.

The log-only trailing directory present on the sample card (a session opened but
never recorded) is skipped by the `.INI` rule with no special-casing.

Failures go to `qWarning()` with directory and reason; one summary line at the
end if anything was skipped, not a dialog per failure. Unknown `.INI` channel
names are ignored silently — the schema belongs to the device, and a future model
declaring a channel we do not map is expected rather than erroneous.

Existing sessions are skipped via `mach->SessionExists(sessionID)` before any
file is read, making re-import idempotent and cheap.

`isAborted()` is checked at the top of the session loop. With ~30 sessions of
≤300 KB, worst-case cancellation latency is one session. Progress is
`setProgressMax(sessionCount)` / `setProgressValue(n)` with an `updateMessage`
per session.

## 10. Card backup

A `backupData(Machine*, const QString&)` method on the loader, called early in
`Open()`, following the Yuwell packaging. It is not a base-class virtual; each
loader defines and invokes its own.

```cpp
QDir ipath(path), bpath(mach->getBackupPath());
if (ipath == bpath) {                 // importing from the backup folder itself
    rebuild_from_backups = true;
    create_backups = false;
} else {
    rebuild_from_backups = false;
    create_backups = p_profile->session->backupCardData();
}
if (rebuild_from_backups || !create_backups) return true;
copyPath(ipath.absolutePath(), bpath.absolutePath(), true);
```

Compare `QDir` objects, not strings — separators differ on Windows, and getting
this wrong backs a folder up onto itself.

**The whole card is copied, including `.RAM`/`.BKP`.** ResMed backs up
selectively because its cards carry irrelevant bulk; a Sefam card is ~31 MB and
almost entirely relevant. The encrypted images are ~3.2 MB of that. They are not
read, but they are the only place the remaining unknowns (`Y17`, hypopnea
durations, log code 10) could ever be answered from, so a backup that discarded
them would destroy the one artefact a future investigation would need.

Because the copy preserves the `<model>/<serial>/DATA_nnn/` structure, `Detect()`
works unchanged on a backup folder and re-import from backup needs no special
path.

## 11. Verification

No unit tests — the project's QtTest harness is not used in this workflow.

**Golden numbers**, already confirmed against the vendor report, checkable by eye
on first import:

| Check | Expected |
|---|---|
| Sessions imported | 31 (trailing log-only directory skipped) |
| Total recorded time | 159.0 h |
| Event totals | OA 248, CA 191, Hypopnea 309, Snore 1408, FL 961 |
| Apnea mean duration | 16.5 s (OA), 12.8 s (CA) |
| Settings | min 4.0, max 20.0, ramp 45 min, ramp pressure 4.0 |
| AHI for the period | ≈ 4.7 |

**Three checks on the daily graph**, each targeting a specific design risk:

1. **Leak graph continuity** — gaps must be gaps. Spikes to 153 L/min mean
   sentinel splitting failed (§6).
2. **Event placement** against a vendor waveform PDF — resolves the
   start-versus-end question (§7).
3. **AHI ≈ 4.7** — catches event mapping or duration errors.

## 12. Known limitations

Carried forward deliberately, all documented in the format notes:

- Hypopneas have no duration.
- Central hypopnea is folded into `CPAP_Hypopnea`, discarding the device's
  obstructive/central split for hypopneas.
- Humidifier and comfort settings are not shown.
- Log code 10 (1.64/h) is unidentified and not imported.
- Event timestamps are assumed to be event ends pending visual confirmation.
- OSCAR's event counts will run slightly above the vendor's over long periods
  (~2% on the sample card). The vendor analyzer applies ramp and leak inclusion
  filtering that the raw log does not. This is a scoring-policy difference, not a
  defect, and must **not** be "fixed" by inventing filters to match.
