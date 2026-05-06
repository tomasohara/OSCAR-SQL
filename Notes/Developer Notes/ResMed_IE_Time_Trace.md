# ResMed Inspiratory / Expiratory Time Trace — Loader to Daily Page

**Question investigated:** For ResMed machines, do `CPAP_Ti` and `CPAP_Te`
(inspiratory and expiratory time) flow consistently from the EDF source through
to the Daily page, and is there any point at which they could be swapped or the
I:E ratio inverted by OSCAR itself?

**Short answer:** No swap exists in the OSCAR code path. Ti and Te each have
their own EDF labels, their own channel IDs, and independent `ToTimeDelta`
calls — there is no shared buffer or ordinal logic that could exchange them.
The device's own `IERatio.2s` signal is *not* loaded into OSCAR (the
`ToTimeDelta` call for `CPAP_IE` is commented out), and OSCAR does not synthesize
an I:E channel from Ti and Te. So any inverted I:E ratio you see is what the
ResMed firmware actually reported.

---

## 1. EDF source labels

Declared in `oscar/SleepLib/loader_plugins/resmed_loader.cpp:4277-4281`:

| ChannelID | EDF label set |
| --- | --- |
| `CPAP_Ti` | `"Ti"`, `"B5ITime.2s"` |
| `CPAP_Te` | `"Te"`, `"B5ETime.2s"` |
| `CPAP_IE` | `"I:E"`, `"IERatio.2s"` |

`matchSignal` (line 4222) uses case-insensitive `startsWith`. None of the other
ResMed labels (`TidVol.*`, `TgMV`, `TgtMV.2s`, `TgtIPAP.*`, `TgtEPAP.*`, `Test*`)
start with `"Ti"` or `"Te"`, so no false positives. `TidVol.*` is also consumed
earlier by the `CPAP_TidalVolume` branch (line 3891) regardless.

## 2. Loader EDF dispatch (`resmed_loader.cpp:3920-3949`)

```cpp
} else if (matchSignal(CPAP_IE, es.label)) { //I:E ratio
    code = CPAP_IE;
    es.gain /= 100.0;
    es.physical_maximum /= 100.0;
    es.physical_minimum /= 100.0;
//  Fix ToTimeDelta to store inverse of edf data - also fix labels and tool tip
//            ToTimeDelta(sess,edf,es, code,samples,duration,0,0, square);
} else if (matchSignal(CPAP_Ti, es.label)) {
    code = CPAP_Ti;
    if ( found_Ti_code ) continue;
    found_Ti_code = true;
    ToTimeDelta(sess,edf,es, code,samples,duration,0,0, square);
} else if (matchSignal(CPAP_Te, es.label)) {
    code = CPAP_Te;
    if ( found_Te_code ) continue;
    found_Te_code = true;
    ToTimeDelta(sess,edf,es, code,samples,duration,0,0, square);
}
```

Key observations:

- **`CPAP_Ti` and `CPAP_Te` are loaded normally** via `ToTimeDelta`, into
  separate `EventList`s identified by their channel IDs
  (`CPAP_Ti = 0x110B`, `CPAP_Te = 0x110A` — `schema.cpp:265-269`).
- **`CPAP_IE` is NOT loaded** — the call is commented out. The developer
  comment "Fix ToTimeDelta to store inverse of edf data" indicates this was
  parked because the direction of the device's I:E value relative to OSCAR's
  expected display direction was unresolved.
- The `found_Ti_code` / `found_Te_code` guards exist because some firmwares
  (e.g. 36037, 36039, 36377, 37051) emit two signals with the same label
  (e.g. `R5Ti.2s` AND `Ti.2s`). OSCAR keeps the first. This is the only
  per-firmware behavior to be aware of — see §6 below.

## 3. Storage path (`ToTimeDelta`, `resmed_loader.cpp:4005`)

`ToTimeDelta` is the single entry point for ResMed event-list population. It
takes `code` as an argument and writes into `sess->AddEventList(code, ...)`.
There is **no point** in this function where Ti and Te interact, are renamed,
or share state. Each is a self-contained pass through the EDF sample array.

A 20-second startpos shaving is applied uniformly to `CPAP_MinuteVent`,
`CPAP_RespRate`, `CPAP_TidalVolume`, `CPAP_Ti`, `CPAP_Te`, `CPAP_IE`
(line 4040-4044). Identical for both Ti and Te — no asymmetry.

## 4. Synthesis check (`calcs.cpp:478-485`)

```cpp
bool calcTi = !session->eventlist.contains(CPAP_Ti);
bool calcTe = !session->eventlist.contains(CPAP_Te);
...
if (calcTi) { Ti = m_session->AddEventList(CPAP_Ti, EVL_Event); Ti->setGain(0.02F); }
if (calcTe) { Te = m_session->AddEventList(CPAP_Te, EVL_Event); Te->setGain(0.02F); }
```

For ResMed, both event lists are present from the loader, so the calculator is
skipped. **There is no code anywhere in the codebase that synthesizes `CPAP_IE`
from `CPAP_Ti` and `CPAP_Te`.** The only places `CPAP_IE` is populated:

- `bmc_loader.cpp:442`     (BMC legacy)
- `resvent_loader.cpp:512` (Resvent)
- `resmed_loader.cpp:3920` — *commented out* (ResMed)

So for a ResMed session, `CPAP_IE` has no data.

## 5. Daily page (`daily.cpp`)

```cpp
// line 505 — note the developer comment
if (auto *g = graphlist.value(schema::channel[CPAP_IE].code()))
    g->AddLayer(lc=new gLineChart(CPAP_IE, false));      // this should be inverse of supplied value
// line 506-507
if (auto *g = graphlist.value(schema::channel[CPAP_Te].code())) g->AddLayer(lc=new gLineChart(CPAP_Te, false));
if (auto *g = graphlist.value(schema::channel[CPAP_Ti].code())) g->AddLayer(lc=new gLineChart(CPAP_Ti, false));
```

- The "this should be inverse of supplied value" comment is the same
  unresolved concern noted in the loader. Because no data flows into
  `CPAP_IE` for ResMed, the I/E Value graph is empty for ResMed sessions
  — the comment is moot in practice.
- Ti and Te graphs are added independently with no transform.
- Statistics block (line 1519) lists `CPAP_IE, CPAP_Ti, CPAP_Te`. For ResMed,
  only Ti and Te rows appear (`channelHasData(CPAP_IE)` is false at line 1533).

## 6. Consistency conclusion

| Risk | Present in OSCAR ResMed path? |
| --- | --- |
| `CPAP_Ti` ↔ `CPAP_Te` swap during EDF dispatch | No — distinct label sets, distinct channel IDs |
| Shared buffer or ordinal logic that could exchange Ti/Te | No — separate `ToTimeDelta` calls per signal |
| `matchSignal` false-positive on `Ti*` / `Te*` siblings | No — no other ResMed label starts with those prefixes; `TidVol.*` consumed earlier |
| OSCAR inverting I:E ratio direction | N/A — OSCAR does not load or synthesize `CPAP_IE` for ResMed |
| Per-firmware Ti/Te dedupe picking wrong instance | **Possible** — `found_Ti_code`/`found_Te_code` keeps first occurrence; verified on 36037/36039/36377/37051 only |

The only realistic OSCAR-side risk is item 5: if a firmware variant emits two
`"Ti"` (or two `"Te"`) signals in the *opposite* order from the models the
author tested, the discarded copy may have been the patient-relevant one.
Check by reading the raw EDF sample arrays for both occurrences and comparing.

## 7. Why your data may still look "inverted"

The OSCAR wiki page
`oscar/wiki/OSCAR/Inspiration Expiration (I_E) Ratio.mediawiki` line 11 says:

> We often see an I:E ratio of less than 1 suggesting a longer inspiration
> time than expiration. This inverse I:E ratio can occur, but most of the
> time it is an error in the respiration timing by the CPAP machine. In
> most cases, the machine will count part of expiration as inhale, resulting
> in an incorrect I:E time or ratio.

So inverted ratios on triggered or flow-limited breaths are a documented
ResMed firmware behavior, not an OSCAR transformation. OSCAR is faithfully
reporting what the device wrote into the EDF.

## 8. Recommended diagnostic

If you want to confirm the OSCAR-stored Ti/Te values exactly match the EDF:

1. Pick one PLD file from a session of interest.
2. Read the EDF directly (Python `pyedflib` or similar) and extract the raw
   samples for the `Ti` / `B5ITime.2s` and `Te` / `B5ETime.2s` signals — and
   `IERatio.2s` for ground truth I:E from the device.
3. Compare the per-sample stream against the events stored under `CPAP_Ti` /
   `CPAP_Te` for that session in the OSCAR DB.
4. If the values match, the apparent inversion is a device report; if they
   don't, the firmware-dedupe path (§6 item 5) likely picked the wrong copy.

## 9. File / line reference

| Concern | Location |
| --- | --- |
| EDF label → channel map | `oscar/SleepLib/loader_plugins/resmed_loader.cpp:4277-4281` |
| `matchSignal` (startsWith, case-insensitive) | `resmed_loader.cpp:4222-4236` |
| EDF dispatch for IE/Ti/Te | `resmed_loader.cpp:3920-3949` |
| `ToTimeDelta` storage | `resmed_loader.cpp:4005-4173` |
| Channel registration | `oscar/SleepLib/schema.cpp:262-269` |
| Optional Ti/Te synthesis (skipped for ResMed) | `oscar/SleepLib/calcs.cpp:478-485` |
| Daily-page graph layers | `oscar/daily.cpp:505-507` |
| Daily-page statistics list | `oscar/daily.cpp:1519` |
| Wiki note on inverted I:E | `oscar/wiki/OSCAR/Inspiration Expiration (I_E) Ratio.mediawiki:11` |
