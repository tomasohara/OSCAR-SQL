# G3X EVT-Only Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import BMC G3X sessions that have no local waveform data but do have EVT event records and/or IDX IT-block summary statistics — allowing OSCAR to show events and a 30-second-resolution pressure trace for devices that use BMC's PapLink cloud sync and only retain the current session's waveform locally.

**Architecture:** Add a `BmcPressureSnapshot` list to `BmcSession`; restructure `ParseIdxRecords` to accept waveform-absent IDX records; add an EVT-only session-building path in `ReadDateSession` that splits sessions on EVT 0x40/0x41 markers and collects 0x42 pressure snapshots; teach `setSessionWaveforms` to emit `CPAP_Pressure/IPAP/EPAP` event lists from those snapshots when no waveform packets exist.

**Tech Stack:** C++17, Qt6, existing BMC G3X loader infrastructure (`bmcg3x_loader`, `bmcG3xDataParsing`, `bmc_loader`)

**Test dataset:** `c:\oscar\Testfiles\Greenaway\` — 148 nights with IT data from 2025-12-23; EVT data from 2026-01-19; no historical waveform data.

---

## File Map

| File | Change |
|---|---|
| `oscar/SleepLib/loader_plugins/bmcDataParsing.h` | Add `BmcPressureSnapshot` class; add `PressureSnapshots` member to `BmcSession` |
| `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp` | (1) Restructure `ParseIdxRecords` to accept no-waveform records; (2) Collect EVT session markers in Phase 1 of `ReadDateSession`; (3) Add EVT-only session-building branch |
| `oscar/SleepLib/loader_plugins/bmc_loader.cpp` | (4) Relax skip condition in `BmcLoaderTask::run()`; (5) Add pressure-snapshot-only branch in `setSessionWaveforms()` |

---

## Task 1 — Add `BmcPressureSnapshot` to `bmcDataParsing.h`

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmcDataParsing.h`

- [ ] **Step 1: Add the `BmcPressureSnapshot` class**

  Open `bmcDataParsing.h`. After the closing brace of `BmcFlowLimitEvent` (around line 94), insert:

  ```cpp
  /// @brief A pressure snapshot from an EVT 0x42 record.
  /// Used for EVT-only sessions where no waveform packets are present.
  class BmcPressureSnapshot
  {
  public:
      QDateTime Timestamp;
      int EpapHundredths = 0; ///< EPAP in hundredths of cmH2O
      int IpapHundredths = 0; ///< IPAP in hundredths of cmH2O
  };
  ```

- [ ] **Step 2: Add `PressureSnapshots` to `BmcSession`**

  In `bmcDataParsing.h`, find the `BmcSession` class (around line 369). Add the new member after `FlowLimitEvents`:

  ```cpp
  class BmcSession{
  public:
      QDateTime StartTimestamp;
      QDateTime EndTimestamp;

      QList<BmcWaveformPacket> Waveforms;
      QList<BmcRespiratoryEvent> RespiratoryEvents;
      QList<BmcFlowLimitEvent> FlowLimitEvents;
      QList<BmcPressureSnapshot> PressureSnapshots; ///< From EVT 0x42; populated for EVT-only sessions (waveLen==0).
  };
  ```

- [ ] **Step 3: Verify it compiles**

  Build in QtCreator. Expected: no errors. `BmcPressureSnapshot` is a value type with default constructor — no other files need changes yet.

- [ ] **Step 4: Commit**

  ```
  git add oscar/SleepLib/loader_plugins/bmcDataParsing.h
  git commit -m "Add BmcPressureSnapshot struct and PressureSnapshots list to BmcSession"
  ```

---

## Task 2 — Restructure `ParseIdxRecords` to accept no-waveform records

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp` — `ParseIdxRecords` (lines ~1763–1924)

**Context:** Currently the loop rejects any IDX record where `waveLen == 0 || waveEnd <= waveStart`. The Greenaway card has 148 such records that contain valid IT-block statistics and EVT data. The fix restructures the timestamp-derivation logic so both waveform-capable and waveform-absent records share the same IT/TS block parsing and link-building code.

- [ ] **Step 1: Restructure the loop body**

  Find the section in `ParseIdxRecords` that begins at the `if (waveLen == 0 || waveEnd <= waveStart)` check (around line 1806) and ends just before the IT-block parse at the `const int itOffset = offset + 0x80;` line (around line 1856).

  Replace this entire section (from `if (waveLen == 0 ...` through `dayEntry.LogLength = logLen;`) with:

  ```cpp
      G3xDayEntry dayEntry;
      dayEntry.Date            = QDate(year, month, day);
      dayEntry.WaveStartOffset = waveStart;
      dayEntry.WaveEndOffset   = waveEnd;
      dayEntry.WaveLength      = waveLen;
      dayEntry.EventStartOffset = eventStart;
      dayEntry.EventEndOffset   = eventEnd;
      dayEntry.EventLength      = eventLen;
      dayEntry.LogStartOffset   = logStart;
      dayEntry.LogEndOffset     = logEnd;
      dayEntry.LogLength        = logLen;

      if (waveLen > 0 && waveEnd > waveStart) {
          // Normal path: derive session timestamps from waveform packets.
          const QDateTime startTs = ReadWaveformPacketTimestamp(waveStart);
          const quint32 endPacketOffset = (waveEnd >= kG3xWaveformPacketSize)
                                              ? (waveEnd - kG3xWaveformPacketSize)
                                              : waveStart;
          QDateTime endTs = ReadWaveformPacketTimestamp(endPacketOffset);

          if (!startTs.isValid()) {
              ++idxBadTs;
              continue;
          }
          if (!endTs.isValid() || endTs < startTs) {
              const quint32 packetCount = std::max<quint32>(1, waveLen / kG3xWaveformPacketSize);
              endTs = startTs.addSecs(static_cast<int>(packetCount));
          }
          dayEntry.StartTimestamp = startTs;
          dayEntry.EndTimestamp   = endTs.addSecs(1);
      } else {
          // No waveform data: accept the record if IT block or EVT stream has data.
          const int itOff = offset + 0x80;
          const bool hasItDuration =
              (itOff + 0x18 <= idxBytes.size() &&
               idxBytes.at(itOff)     == 'I'   &&
               idxBytes.at(itOff + 1) == 'T'   &&
               ReadUInt32LE(idxBytes, itOff + 0x14) > 0);
          const bool hasEvtData = (eventLen > 0 && eventEnd > eventStart);
          if (!hasItDuration && !hasEvtData) {
              ++idxNoWave;
              continue;
          }
          // Use noon of the IDX calendar date as synthetic start.
          dayEntry.StartTimestamp = QDateTime(dayEntry.Date, QTime(12, 0, 0), Qt::LocalTime);
          const quint32 itDuration = hasItDuration
                                         ? ReadUInt32LE(idxBytes, itOff + 0x14)
                                         : 0;
          dayEntry.EndTimestamp = dayEntry.StartTimestamp.addSecs(
              itDuration > 0 ? static_cast<int>(itDuration) : 3600);
      }
  ```

  The IT-block parse, TS-block parse, and link-building code that follows is unchanged — it now runs for both waveform-capable and waveform-absent records.

- [ ] **Step 2: Verify the counter variable `idxNoWave` is still updated correctly**

  Check that the `idxNoWave` increment inside the `else` branch (for records with neither IT data nor EVT data) is the only path that increments it. The existing `#ifdef BMCDEBUG` summary log uses `idxNoWave` — that output remains correct.

- [ ] **Step 3: Build and run against Greenaway**

  Build in QtCreator. Import `c:\oscar\Testfiles\Greenaway\`. Expected: OSCAR now finds 148+ sessions instead of 0. They will have no events and no pressure yet (those come in later tasks). The calendar should show dates highlighted from Dec 2025 onward.

- [ ] **Step 4: Commit**

  ```
  git add oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp
  git commit -m "ParseIdxRecords: accept waveform-absent IDX records that have IT stats or EVT data"
  ```

---

## Task 3 — Collect EVT session markers in Phase 1 of `ReadDateSession`

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp` — `ReadDateSession`, Phase 1 EVT loop (lines ~783–944)

**Context:** The EVT loop currently ignores `0x40` (session start) and `0x41` (session end) markers. For EVT-only sessions those markers define session boundaries. We collect them into two vectors declared before Phase 1.

- [ ] **Step 1: Declare session-marker vectors before Phase 1**

  Find the line `const QString evtFilePath = fileBasePath + ".evt";` (start of Phase 1, around line 803). Immediately before it, add:

  ```cpp
      // Collected for EVT-only session boundary detection (waveLen == 0).
      QVector<QDateTime> evtSessionStarts;
      QVector<QDateTime> evtSessionEnds;
  ```

- [ ] **Step 2: Split the combined session-marker `case` and collect timestamps**

  Find the `switch (messageType)` block inside the EVT loop. The current combined case is:

  ```cpp
              case kG3xEvtTypeSessionStart:
              case kG3xEvtTypeSessionEnd:
                  // Not used: the waveform-heuristic start (findStableStartMs) is more accurate
                  // because it excludes startup noise; 0x40/0x41 are only second-precise.
                  break;
  ```

  Replace it with:

  ```cpp
              case kG3xEvtTypeSessionStart:
                  // Collected for EVT-only session building when no waveform data exists.
                  evtSessionStarts.append(evtTime);
                  break;
              case kG3xEvtTypeSessionEnd:
                  evtSessionEnds.append(evtTime);
                  break;
  ```

- [ ] **Step 3: Build**

  Build in QtCreator. Expected: no errors. The waveform path is unaffected — it never reads `evtSessionStarts`/`evtSessionEnds`.

- [ ] **Step 4: Commit**

  ```
  git add oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp
  git commit -m "ReadDateSession: collect EVT 0x40/0x41 session markers for EVT-only path"
  ```

---

## Task 4 — Add EVT-only session-building branch to `ReadDateSession`

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp` — `ReadDateSession`, after Phase 2c (around line 1134)

**Context:** When `dayEntry->WaveLength == 0`, skip Phases 3–3.5 and the waveform loop entirely. Instead build `BmcSession` objects from 0x40/0x41 markers and populate their `PressureSnapshots` from the 0x42 records already gathered in `timedSampleUpdates`. For days with no EVT markers at all (IT-only), create one synthetic session spanning the IT-block duration with two pressure snapshots at the EPAP value so the session passes the downstream skip check.

- [ ] **Step 1: Add the EVT-only branch after Phase 2c**

  Find the comment `// ---- Phase 3: Seed initial pressure from IDX summary ----` (around line 1136). Immediately before it, insert the following block. This block returns early from `ReadDateSession` for waveform-absent days:

  ```cpp
      // ---- EVT-only path (no waveform data on card) ----
      if (dayEntry->WaveLength == 0) {
          // Build BmcPressureSnapshot list from the 0x42 updates already collected in Phase 1.
          // timedSampleUpdates is sorted chronologically (sorted after Phase 1).
          QVector<BmcPressureSnapshot> evtPressureSnapshots;
          for (const G3xTimedSampleUpdate& u : timedSampleUpdates) {
              const int epap = u.Values.hasEPAP ? u.Values.epapHundredths
                                                : (u.Values.hasIPAP ? u.Values.ipapHundredths : 0);
              const int ipap = u.Values.hasIPAP ? u.Values.ipapHundredths : epap;
              if (epap > 0 || ipap > 0) {
                  BmcPressureSnapshot snap;
                  snap.Timestamp       = QDateTime::fromSecsSinceEpoch(u.TimestampSec);
                  snap.EpapHundredths  = epap;
                  snap.IpapHundredths  = ipap;
                  evtPressureSnapshots.append(snap);
              }
          }

          if (!evtSessionStarts.isEmpty()) {
              // Build one BmcSession per 0x40 marker.  Match each start to the first
              // 0x41 end that follows it; fall back to dayEntry->EndTimestamp if none.
              for (int i = 0; i < evtSessionStarts.size(); ++i) {
                  BmcSession* s      = new BmcSession();
                  s->StartTimestamp  = evtSessionStarts.at(i);
                  s->EndTimestamp    = dayEntry->EndTimestamp; // fallback

                  for (const QDateTime& e : evtSessionEnds) {
                      if (e > s->StartTimestamp) {
                          s->EndTimestamp = e;
                          break;
                      }
                  }

                  for (const BmcPressureSnapshot& snap : evtPressureSnapshots) {
                      if (snap.Timestamp >= s->StartTimestamp &&
                          snap.Timestamp <= s->EndTimestamp) {
                          s->PressureSnapshots.append(snap);
                      }
                  }
                  dateSession.Sessions.append(s);
              }
          } else {
              // No EVT session markers: create one synthetic session from IT block duration.
              BmcSession* s     = new BmcSession();
              s->StartTimestamp = dayEntry->StartTimestamp;
              s->EndTimestamp   = dayEntry->EndTimestamp;

              // Add two synthetic pressure snapshots (start + end) at the EPAP setting
              // so the session passes the downstream skip check and OSCAR has a pressure anchor.
              if (IsReasonablePressureHundredths(dayEntry->ItPressureEPAPHundredths)) {
                  BmcPressureSnapshot snapStart;
                  snapStart.Timestamp      = s->StartTimestamp;
                  snapStart.EpapHundredths = dayEntry->ItPressureEPAPHundredths;
                  snapStart.IpapHundredths = dayEntry->ItPressureEPAPHundredths;
                  BmcPressureSnapshot snapEnd;
                  snapEnd.Timestamp      = s->EndTimestamp.addSecs(-1);
                  snapEnd.EpapHundredths = dayEntry->ItPressureEPAPHundredths;
                  snapEnd.IpapHundredths = dayEntry->ItPressureEPAPHundredths;
                  s->PressureSnapshots.append(snapStart);
                  s->PressureSnapshots.append(snapEnd);
              }
              dateSession.Sessions.append(s);
          }

          // Assign respiratory and FL events to the session whose window contains them.
          for (BmcSession* s : dateSession.Sessions) {
              for (const BmcRespiratoryEvent& evt : dateSession.RespiratoryEvents) {
                  if (evt.StartTime >= s->StartTimestamp && evt.StartTime <= s->EndTimestamp) {
                      s->RespiratoryEvents.append(evt);
                  }
              }
              for (const BmcFlowLimitEvent& evt : dateSession.FlowLimitEvents) {
                  if (evt.Timestamp >= s->StartTimestamp && evt.Timestamp <= s->EndTimestamp) {
                      s->FlowLimitEvents.append(evt);
                  }
              }
          }

          // Machine settings from IT block (mirrors Phase 5 logic in the normal path).
          const float epapCmH2O = IsReasonablePressureHundredths(dayEntry->ItPressureEPAPHundredths)
                                      ? dayEntry->ItPressureEPAPHundredths / 100.0f
                                      : 4.0f;
          dateSession.MacineSettings.CPAP_TreatP   = epapCmH2O;
          dateSession.MacineSettings.CPAP_InitialP  = epapCmH2O;
          dateSession.MacineSettings.CPAP_ManualP   = epapCmH2O;

          int minPressureHundredths = 0;
          if (IsReasonablePressureHundredths(dayEntry->TsPressureMinHundredths))
              minPressureHundredths = dayEntry->TsPressureMinHundredths;
          else if (IsReasonablePressureHundredths(dayEntry->ItPressureMinHundredths))
              minPressureHundredths = dayEntry->ItPressureMinHundredths;

          int maxPressureHundredths = 0;
          if (IsReasonablePressureHundredths(dayEntry->TsPressureMaxHundredths))
              maxPressureHundredths = dayEntry->TsPressureMaxHundredths;
          else if (IsReasonablePressureHundredths(dayEntry->ItPressureMaxHundredths))
              maxPressureHundredths = dayEntry->ItPressureMaxHundredths;

          if (minPressureHundredths > 0) {
              dateSession.MacineSettings.APAP_IntialP = minPressureHundredths / 100.0f;
              dateSession.MacineSettings.APAP_MinAPAP = minPressureHundredths / 100.0f;
          }
          if (maxPressureHundredths > 0)
              dateSession.MacineSettings.APAP_MaxAPAP = maxPressureHundredths / 100.0f;

          dateSession.MacineSettings.Mode =
              (minPressureHundredths > 0 && maxPressureHundredths > minPressureHundredths)
                  ? BmcMode::AutoCPAP
                  : BmcMode::CPAP;

          return dateSession;
      }
      // ---- End EVT-only path — waveform path continues below ----
  ```

- [ ] **Step 2: Build**

  Build in QtCreator. Expected: no errors or warnings.

- [ ] **Step 3: Commit**

  ```
  git add oscar/SleepLib/loader_plugins/bmcG3xDataParsing.cpp
  git commit -m "ReadDateSession: add EVT-only session building for cards without local waveform data"
  ```

---

## Task 5 — Relax skip condition in `BmcLoaderTask::run()`

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmc_loader.cpp` — `BmcLoaderTask::run()` (two occurrences)

**Context:** `run()` currently skips any `BmcSession` that has no `Waveforms` and no `RespiratoryEvents`. EVT-only sessions will have `Waveforms` empty but may have `PressureSnapshots` (and possibly `RespiratoryEvents`). Both occurrences of the skip check must be updated.

- [ ] **Step 1: Update the first skip check (planning pass)**

  Find the first occurrence (around line 67):

  ```cpp
          if (bmcSession->Waveforms.length() == 0 && bmcSession->RespiratoryEvents.length() == 0) {
              continue;
          }
  ```

  Replace with:

  ```cpp
          if (bmcSession->Waveforms.isEmpty() &&
              bmcSession->RespiratoryEvents.isEmpty() &&
              bmcSession->PressureSnapshots.isEmpty()) {
              continue;
          }
  ```

- [ ] **Step 2: Update the second skip check (import pass)**

  Find the second occurrence (around line 176):

  ```cpp
          if (bmcSession->Waveforms.length() == 0 && bmcSession->RespiratoryEvents.length() == 0)
              continue;
  ```

  Replace with:

  ```cpp
          if (bmcSession->Waveforms.isEmpty() &&
              bmcSession->RespiratoryEvents.isEmpty() &&
              bmcSession->PressureSnapshots.isEmpty())
              continue;
  ```

- [ ] **Step 3: Build**

  Build in QtCreator. Expected: no errors.

- [ ] **Step 4: Commit**

  ```
  git add oscar/SleepLib/loader_plugins/bmc_loader.cpp
  git commit -m "BmcLoaderTask: pass EVT-only sessions that have PressureSnapshots but no waveforms"
  ```

---

## Task 6 — Add pressure-snapshot branch in `setSessionWaveforms()`

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmc_loader.cpp` — `BmcLoader::setSessionWaveforms()` (around line 405)

**Context:** When `Waveforms` is empty but `PressureSnapshots` has data, write `CPAP_Pressure`, `CPAP_IPAP`, and `CPAP_EPAP` event lists from the 30-second snapshots and return early. No flow waveform is created — the chart shows a stepped pressure trace only, matching what PapLink displays.

The raw pressure values in `BmcPressureSnapshot` are in hundredths of cmH2O (e.g. 650 = 6.50 cmH2O). `PressureChannelGain()` returns 0.01 for the G3X loader, so `stored_value × 0.01 = cmH2O`. Store the hundredths value directly as `qint16`.

- [ ] **Step 1: Add early-return pressure-snapshot branch**

  Find the start of `setSessionWaveforms()` (around line 405). After the opening of the function body (the lines declaring `waveformSampleIntervalMs` etc.), insert the following block before any `AddEventList` calls:

  ```cpp
      // ---- EVT-only path: pressure snapshots only, no waveform ----
      if (bmcSession->Waveforms.isEmpty() && !bmcSession->PressureSnapshots.isEmpty()) {
          const double pressureGain = PressureChannelGain(); // 0.01 for G3X
          auto wPressure = oscarSession->AddEventList(CPAP_Pressure, EVL_Event, pressureGain, 0.0, 0.0, 0.0, 1000);
          auto wIPAP     = oscarSession->AddEventList(CPAP_IPAP,     EVL_Event, pressureGain, 0.0, 0.0, 0.0, 1000);
          auto wEPAP     = oscarSession->AddEventList(CPAP_EPAP,     EVL_Event, pressureGain, 0.0, 0.0, 0.0, 1000);

          for (const BmcPressureSnapshot& snap : bmcSession->PressureSnapshots) {
              const qint64 ts = snap.Timestamp.toMSecsSinceEpoch();
              if (snap.IpapHundredths > 0) {
                  wPressure->AddEvent(ts, static_cast<EventStoreType>(snap.IpapHundredths));
                  wIPAP->AddEvent(ts,     static_cast<EventStoreType>(snap.IpapHundredths));
              }
              if (snap.EpapHundredths > 0) {
                  wEPAP->AddEvent(ts, static_cast<EventStoreType>(snap.EpapHundredths));
              }
          }
          return; // No flow, no mask pressure, no other channels for EVT-only sessions.
      }
      // ---- Normal waveform path continues below ----
  ```

  Note: `EventStoreType` is the type used by `AddEvent`. Check what type the existing calls in `setSessionWaveforms` use for the second argument — it is `qint16` in some callers and the raw value in others. Use the same cast as the existing `wPressure->AddEvent(timestamp, bmcWaveform.Raw.IPAP)` line nearby to ensure consistency.

  If `EventStoreType` is not directly accessible, use `qint16`:

  ```cpp
              wPressure->AddEvent(ts, static_cast<qint16>(snap.IpapHundredths));
              wIPAP->AddEvent(ts,     static_cast<qint16>(snap.IpapHundredths));
              wEPAP->AddEvent(ts,     static_cast<qint16>(snap.EpapHundredths));
  ```

- [ ] **Step 2: Build**

  Build in QtCreator. Expected: no errors.

- [ ] **Step 3: Commit**

  ```
  git add oscar/SleepLib/loader_plugins/bmc_loader.cpp
  git commit -m "setSessionWaveforms: emit CPAP_Pressure/IPAP/EPAP from EVT snapshots when no waveform data"
  ```

---

## Task 7 — Verify with Greenaway test files

**Test dataset:** `c:\oscar\Testfiles\Greenaway\` (machine A3125512078)

- [ ] **Step 1: Import the Greenaway SD card image**

  In OSCAR, run Import (Shift+F2) and point it at `c:\oscar\Testfiles\Greenaway\`. Expected: import completes without errors.

- [ ] **Step 2: Verify session count in the calendar**

  The OSCAR calendar should show 148 days with data, starting 2025-12-23 and ending 2026-06-09 (with some gaps — e.g. there is no entry for 2025-12-24 through 2025-12-22 gap, and Jan 1, 21, 27 may also be missing based on the IDX). Confirm at least 100 days are highlighted in the calendar.

- [ ] **Step 3: Verify an EVT-capable night (2026-01-20)**

  Click on 2026-01-20 in the calendar. Expected:
  - Duration shown (~5h30m per IT block)
  - Pressure chart shows a stepped line (30-second steps) ranging roughly 6–10 cmH2O
  - Respiratory events (apneas, hypopneas) visible on the events timeline
  - No flow waveform chart

- [ ] **Step 4: Verify an IT-only night (2025-12-31)**

  Click on 2025-12-31. Expected:
  - Duration shown (~4h43m per IT block)
  - A flat pressure line at EPAP (9.00 cmH2O for this night per IT block)
  - No events (no EVT data for this date)
  - No flow waveform

- [ ] **Step 5: Verify the existing JCCPAP waveform card still imports correctly**

  Import `c:\oscar\Testfiles\JCCPAP\`. Verify that a night with full waveform data (e.g., 2026-01-19) shows the flow waveform, full pressure resolution, and events as before. This confirms the EVT-only branch does not affect the normal waveform path.

- [ ] **Step 6: Log the fix**

  Append an entry to `Notes/Developer Notes/BUG_FIXES.md`:

  ```
  ## 2026-06-10 — BMC G3X: EVT-only import for PapLink-synced cards
  Files: bmcDataParsing.h, bmcG3xDataParsing.cpp, bmc_loader.cpp
  Symptom: OSCAR showed "no data found" for G3X cards that use BMC's PapLink cloud sync;
           the SD card retains only the current session's waveform file (16 KB) but stores
           148+ nights of IT summary statistics and EVT events in the .idx/.evt files.
  Root cause: ParseIdxRecords rejected any IDX record with waveLen==0; all 148 historical
              records had waveLen=0 (waveform data is on BMC's server, not the card).
  Fix: Accept waveLen==0 records that have IT-block duration>0 or EVT data. Add EVT-only
       session-building path that splits on 0x40/0x41 markers, populates PressureSnapshots
       from 0x42 records (30s steps), and emits CPAP_Pressure/IPAP/EPAP without a flow
       waveform. IT-only days (no EVT) get a synthetic session using the IT-block duration.
  ```
