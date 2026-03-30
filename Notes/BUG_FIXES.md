# OSCAR Bug Fix Log

Notable bugs found and fixed during development/investigation.

---

## 2026-03-30 - G3X: Periodic breathing duration wrong (uint16 vs uint32)

**Files:** `bmcG3xDataParsing.cpp`

**Symptom:** PB episode durations were ~28s and ~23s instead of the correct ~159s and ~154s as reported by PAP-Link.

**Root cause:** EVT type 0x09 (PB marker) encodes duration as a uint32 at offset 0x1C (low 16 bits) combined with offset 0x1E (high 16 bits).  The code was reading only the low 16 bits (`value2`) and dividing by 1000, giving 28034/1000≈28s and 23390/1000≈23s.  The correct uint32 LE values are 159106 ms and 154462 ms (≈2:39 and ≈2:34 matching PAP-Link exactly), confirmed via raw byte dump: bytes [0x82,0x6D,0x02,0x00] = 0x00026D82 = 159106 and [0x5E,0x5B,0x02,0x00] = 0x00025B5E = 154462.

**Fix:** In `kG3xEvtTypePBMarker` case, compute `durationMs = static_cast<quint32>(value2) | (static_cast<quint32>(ReadUInt16LEPtr(rec, 0x1E)) << 16)` before dividing by 1000.

---

## 2026-03-27 - G3X: Flow limitation bars misaligned and wrong width

**Files:** `bmcDataParsing.h`, `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`

**Symptom:** FL events displayed as a near-invisible line when zoomed in (200 ms wide), with no visual correlation to individual breaths in the flow waveform.

**Root cause (two bugs):**
1. **Wrong width:** Bar was bookended at ±100 ms, giving a 200 ms display width.  The EVT value2 field (which for respiratory events carries duration in milliseconds) was read but not stored or used for FL events.  Confirmed via Python analysis of Kavolodin EVT data (5145 FL events): value2 range 0.36–3.98 s, mean 1.88 s — the device-reported inspiration duration.
2. **Wrong direction:** The bar was initially plotted forward from `ts` (correct), then changed to backward (`ts − dur` to `ts`) based on a misread of the flow waveform.  With backward plotting the bar covered expiration instead of inspiration.  Confirmed visually: `ts` is the trough at end-of-expiration / start-of-inspiration; the bar must run forward to cover the inspiratory peak where FL occurs.

**Fix:**
- Added `DurationMs` field to `BmcFlowLimitEvent`; EVT parser stores `value2` there.
- `bmc_loader.cpp` plots each FL event from `ts` to `ts + DurationMs` (forward), with a zero bookend 100 ms before `ts` to isolate from the previous event.
- Validated: FL bars now align with the inspiratory peak; severity 1/2/3 visually correlates with degree of inspiratory flattening in the flow waveform.

---

## 2026-03-25 - G3X: EVT scan misses all records when EventStartOffset is not 32-byte aligned

**Files:** `bmcG3xDataParsing.cpp`

**Symptom:** For at least one known device (Lijunjun G3 B20A, SC.75), all respiratory events (apneas, hypopneas, flow limits) are absent from the imported day because the EVT parse loop finds zero valid records.

**Root cause:** The IDX `EventStartOffset` is not guaranteed to fall on a 32-byte record boundary. When it falls mid-record (e.g., 16 bytes in), the parse loop starting exactly at `EventStartOffset` and stepping in 32-byte increments hits the second half of every record — none of which start with the required AE AA magic bytes. All records are silently skipped.

**Fix:** Round `EventStartOffset` down to the nearest 32-byte boundary before seeking. The existing AE AA magic check discards the partial record fragment at the start.

---

## 2026-03-25 - G3X: Replace 0x44-based PB with AASM computed PB from 0x0C breath markers

**Files:** `bmcG3xDataParsing.cpp`, `bmcg3x_loader.h`

**Symptom:** Periodic breathing was never shown in OSCAR for any G3X device. Firmware SC.72 (JCCPAP/Luna G3X) emits no EVT 0x44 records at all. On SC.74+/SC.75 the 0x44 records had not been validated and PB output was explicitly suppressed.

**Root cause:** PB detection depended entirely on firmware-emitted 0x44 event records, which are absent on SC.72. No universal PB signal existed.

**Fix:** Replaced 0x44-based detection with an AASM algorithm applied to device-classified CSA (central apnea) events from `rawRespEvents`. Each event carries a device-measured duration; startTime is derived as endTime − duration. The algorithm groups consecutive central apneas (≥3 s each) separated by ≤20 s of normal breathing into PB episodes (≥3 qualifying apneas per episode). Works on all firmware. `ExportPeriodicBreathing()` in `BmcG3xLoader` changed from `false` to `true`.

---

## 2026-03-25 - G3X: EVT 0x42 IPAP/EPAP fields reversed; bilevel devices showed wrong IPAP

**Files:** `bmcG3xDataParsing.cpp`

**Symptom:** For bilevel G3X devices (e.g. G3 B20A with 1 cmH2O pressure support), OSCAR displayed the same value for both `CPAP_IPAP` and `CPAP_EPAP`, equal to EPAP. IPAP was never shown.

**Root cause:** The EVT 0x42 record carries two pressure fields: `value2` (offset 0x1C) = EPAP, `unk1e` (offset 0x1E) = IPAP. The parser was treating `value2` as both IPAP and EPAP (CPAP assumption). Confirmed by comparing the Lijunjun G3 B20A (1 cmH2O PS): every 0x42 record has `unk1e` = `value2` + 100 (= +1.00 cmH2O).

**Fix:** Updated both the main EVT parse loop and the fallback scan in `ReadDateSession()` to read EPAP from `value2` and IPAP from `unk1e`. IPAP falls back to EPAP if `unk1e` is outside the plausible pressure range (handles pure CPAP devices where `unk1e` might be zero or absent).

---

## 2026-03-26 - G3X: Periodic breathing (EVT 0x44) suppressed; unknown-firmware warning added

**Files:** `bmc_loader.h`, `bmc_loader.cpp`, `bmcg3x_loader.h`, `bmcg3x_loader.cpp`

**Periodic breathing:** EVT type 0x44 ("PB") was being emitted to the CPAP_PB channel for all G3X sessions. The flag has not been validated against PAP-Link for any G3X firmware, and on SC.74+ firmware it produces clearly nonsensical output. Added `ExportPeriodicBreathing()` virtual method to `BmcLoader` (base returns `true` for legacy BMC). `BmcG3xLoader` overrides to `false`, which suppresses creation of the CPAP_PB event list entirely. The raw 0x44 records are still collected in the parser for future analysis.

**Unknown-firmware warning:** BMC G3X firmware version is read from the companion `.log` file (first 6 KB scanned for a null-terminated ASCII string starting with `"G3-2."`; offset varies by device — ~0x0420 in small G3 A20 logs, ~0x1420 in larger G3 B20A ring-buffer logs). Falls back to IDX offset `0x0345` (internal SC build string, e.g. `"G3-2.SC.72.01"`) if the `.log` is unavailable. The version is stored in `MachineInfo.properties["firmware"]` (added in `PeekInfo()`). In `Open()`, if the firmware string is non-empty and does not start with a known user-facing prefix (`"G3-2.11."` or `"G3-2.12."`, or contain `"SC.72"` / `"SC.74"` as IDX-fallback identifiers), a `QMessageBox::information` dialog is shown asking the user to send their SD card .zip to the OSCAR team. Import then continues normally.

---

## 2026-03-26 - G3X: Leak graph wrong/empty on firmware SC.74+ (Kavolodin / Patient 2); model string and firmware version also fixed

**Files:** `bmcG3xDataParsing.cpp`, `bmcDataParsing.h`

**Symptom:** On BMC G3 A20 machines with firmware SC.74+ (part-code prefix `880`, e.g. Kavolodin and Patient 2), the OSCAR leak graph showed a near-zero sparse line with only a handful of data points — clearly wrong. On the same machines, the machine model string displayed the internal part/config code (`"880A40383"`) instead of the product name (`"G3 A20"`). Firmware version was not recorded at all.

**Root cause (three bugs):**
1. **Leak field**: `kG3xOffsetLeak = 0x52A` is populated only in firmware SC.72 (prefix `110`). In firmware SC.74+ (prefix `880`) this field is always zero (0.33% non-zero, clearly noise). The correct leak field for SC.74+ is `0x568` (total mask leak; continuously populated at ~15–17 L/min baseline, scale 0.16 L/min per raw unit).
2. **Model string**: IDX offset `0x048` holds the internal part/config code (`"110A40113"` or `"880A40383"`), not the product name. The product name (`"G3 A20"`) is at IDX offset `0x100`.
3. **Firmware version**: Not read at all. The user-facing version string (e.g. `"G3-2.11.02.33"` or `"G3-2.12.54.13"`) is in the companion `.log` file; the internal SC build string (e.g. `"G3-2.SC.72.01"`) is at IDX offset `0x0345` as a fallback.

**Fix:**
- Added `FirmwareVersion` field to `BmcMachineInfo` in `bmcDataParsing.h`.
- Updated `ParseMachineInfo()` to read model from IDX `0x100` (with fallback to `0x048` if absent) and firmware version from the `.log` file (scan first 6 KB for `"G3-2."` prefix; offset varies by device). Falls back to IDX `0x0345` if `.log` is unavailable.
- Added `kG3xOffsetAlternateLeak = 0x568` constant.
- Added Phase 3.5 adaptive leak-field probe in `ReadDateSession()`: samples first 200 waveform packets; if fewer than 10% have non-zero `0x52A`, selects `0x568` for the entire day. This is robust to future firmware versions without needing hard-coded part-code lookup.
- Updated waveform loop to use `leakFieldOffset` (determined by Phase 3.5) instead of always using `kG3xOffsetLeak`.
- Scale factor `0.16 L/min per raw unit` applies to both fields.

---

## 2026-03-25 - G3X: Respiratory event duration corrected to value2 (milliseconds)

**Files:** `bmcG3xDataParsing.cpp`
**Root cause:** All respiratory event types (0x01–0x09, 0x0A) were using `value1` as duration in seconds. `value1` is actually a wrapping counter (0–999) with no duration information; `value2` encodes duration in milliseconds. Confirmed 2026-03-25 by extracting value2/1000 for a full OSCAR day and comparing against OSCAR waveform graphs: OSA durations of 27.7 s, 15.6 s; hypopnea 10.9 s; UA 11.5 s; RERA 11.3 s — all clinically plausible. `0x0B` value2/1000 = 0.4–1.1 s (sub-second, not event duration).
**Fix:** `G3xRawRespEvent.Value1` renamed to `Value2Millis`; raw event collection changed to store `value2`; duration = `round(value2 / 1000.0)` seconds, clamped to [10, 180]. RERA fixed-duration override removed.

---

## 2026-03-25 - G3X: Unclassified hypopnea (0x01) added to CPAP_Hypopnea channel

**Files:** `bmcG3xDataParsing.cpp`
**Feature:** EVT message type `0x01` confirmed as unclassified hypopnea (2026-03-25, Patient 2 B33BF114508). Added `kG3xEvtTypeUH = 0x01` constant; added to EVT loop and Phase 2 mapping → `BmcRespiratoryEventType::HYP`. Duration uses value2/1000 s clamped to 10–180 s. 58 records observed across Patient 2 nights; absent from JCCPAP.

---

## 2026-03-26 - G3X: PB detection expanded to include CH (central hypopnea) events

**Files:** `bmcG3xDataParsing.cpp`
**Symptom:** JCCPAP-2 (SC.72 firmware) periodic breathing episode missed. SC.72 emits one CSA plus generic/central hypopneas during a PB event; CSA-only clustering cannot form a ≥3-apnea cluster from one CSA.
**Root cause:** Phase 2b PB clustering filtered only on `kG3xEvtTypeCSA` (0x04). On CPAP devices CH (0x08, central hypopnea) also indicates absent/reduced central drive and is part of the same periodic-breathing cluster.
**Fix:** Expanded Phase 2b filter to include `kG3xEvtTypeCH` (0x08) alongside CSA. CH events continue to be reported as hypopneas in OSCAR (unchanged); only the PB episode detection input changes.

---

## 2026-03-24 - G3X: RERA (Respiratory Effort Related Arousal) added to CPAP_RERA channel

**Files:** `bmcDataParsing.h`, `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`
**Feature:** EVT message type `0x0A` confirmed as RERA by PAP-Link correlation on five independent events across four JCCPAP nights (2026-02-12, 2026-02-21, 2026-02-22 ×2, 2026-02-24). All five PAP-Link RERA timestamps matched a `0x0A` record within ±1 minute; two RERAs on 2026-02-22 corresponded exactly to the two `0x0A` records on that night.
**Implementation:** `RERA` added to `BmcRespiratoryEventType` enum. `kG3xEvtTypeRERA = 0x0A` constant added. Phase 2 maps `0x0A` → `BmcRespiratoryEventType::RERA`. Written to `CPAP_RERA` (EVL_Event) in `bmc_loader`. Duration initially set to a fixed 10 s; updated 2026-03-25 to use value2/1000 s (observed ~11.3 s, consistent with PAP-Link's 10-second display).

---

## 2026-03-23 - G3X: Flow Limitation (Mild/Moderate/Severe) added to CPAP_FLG channel

**Files:** `bmcDataParsing.h`, `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`
**Feature:** EVT message types 0x0E (Mild), 0x0F (Moderate), 0x10 (Severe) confirmed as flow limitation events by PAP-Link alignment on two independent sessions (G3X-2 2026-02-20: 376/22/6 vs PAP-Link many/21/5; JCCPAP 2025-12-20: 181/11/5). These types are absent from devices with no FL.
**Implementation:** `BmcFlowLimitEvent` struct (Timestamp + Grade 1/2/3) collected in EVT loop, assigned to sessions, written to `CPAP_FLG` (EVL_Event, gain=1.0) with ±100 ms zero bookends to create isolated bars. `setPhysMin/Max(0.0/3.0)` anchors the y-axis so mild (grade 1) bars are visible. Statistics (min=0, median=0, 95th=0, max=3) are correct — FL is sparse (~2% of session time).

---

## 2026-03-23 - G3X: CPAP_Pressure artificially quantised to 0.5 cmH2O steps

**Files:** `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`, `bmc_loader.h`, `bmcg3x_loader.h`
**Symptom:** The G3X Pressure graph had only 0.5 cmH2O resolution despite the machine recording pressure at 0.01 cmH2O precision.
**Root cause:** `Raw.IPAP`/`Raw.EPAP` were written using `PressureHundredthsToRawHalfCm()`, which rounds to the nearest half-cmH2O to match the legacy BMC packet format. The `bmc_loader` then applied a fixed gain of 0.5. This was appropriate for the legacy machine (which genuinely only records half-cmH2O steps) but threw away the G3X's finer precision.
**Fix:** Added virtual `PressureChannelGain()` to `BmcLoader` (default 0.5 for legacy). G3X overrides to 0.01. G3X now stores raw hundredths of cmH2O directly in `Raw.IPAP`/`Raw.EPAP`, bypassing `PressureHundredthsToRawHalfCm()`. `bmc_loader` uses `PressureChannelGain()` for all three pressure event lists.

---

## 2026-03-22 - BMC Legacy: IPAP/EPAP fields mis-labelled in packet struct

**Files:** `bmcDataParsing.h`, `bmcDataParsing.cpp`
**Symptom:** `BmcWaveformPacketStruct` had `IPAP` at offset 0x04 and `EPAP` at 0x06, which was backwards. A runtime `std::swap` was papering over the error.
**Root cause:** The documentation was incorrect — 0x04 is always the smaller value (EPAP) and 0x06 is always the larger (IPAP), confirmed across all packets on all three test machines.
**Fix:** Swapped the field names in the struct declaration in `bmcDataParsing.h` (EPAP at 0x04, IPAP at 0x06). Removed the `std::swap` calls and their comment from `bmcDataParsing.cpp`.

---

## 2026-03-20 - BMC Legacy: MaskPressure waveform channel showed garbage data

**Files:** `bmc_loader.h`, `bmc_loader.cpp`
**Symptom:** CPAP_MaskPressure waveform channel showed unrecognizable data. Separate BMC_PressureWave chart was also displayed.
**Root cause:** `Raw.MaskPressure` (kBmcExtendedWaveformSamples = 50 elements) is never initialized in the legacy BMC packet constructor — it contains garbage. The actual mask pressure waveform is in `Raw.PressureWave` (25 samples at packet offset 0x08, ÷10 = cmH2O). The separate BMC_PressureWave chart is redundant; mask pressure belongs in the standard CPAP_MaskPressure channel.
**Fix:** Changed `wMaskPressure->AddWaveform` in `bmc_loader.cpp` to use `bmcWaveform.Raw.PressureWave`. Changed `ExportPressureWaveform()` in `bmc_loader.h` base class to return `false` (no separate chart).

---

## 2026-03-20 - BMC Legacy: PressureWaveform gain wrong by factor of 10

**Files:** `bmc_loader.h`
**Symptom:** The pressure waveform displayed values 10× too high (e.g., 85 cmH2O instead of 8.5 cmH2O).
**Root cause:** `PressureWaveformGain()` returned `1.0`. Raw waveform values (int16) store pressure in 0.1 cmH2O units; gain must be `0.1` to display cmH2O. Confirmed by cross-referencing raw PressureWave values against known EPAP/IPAP settings across three SD card images (BMC Luna, BMC Luna G3, BMC G3).
**Fix:** Changed `PressureWaveformGain()` return value in `bmc_loader.h` from `1.0` to `0.1`.

---

## 2026-03-20 - BMC Legacy: Pulse rate / SpO2 from optional oximeter accessory

**Files:** `bmcDataParsing.cpp`
**Note:** SpO2 (0xCC) and pulse rate (0xCE) are zero on all currently tested SD images because those machines did not have the oximeter accessory installed. The fields are correctly mapped; data will appear when an oximeter-equipped machine's card is loaded. Both channels are expected to be present or absent together. Offset 0xBE was briefly tried as an alternate pulse source but is mechanical, not physiological.

---

## 2026-03-20 - BMC Legacy: Ti/Te computation produced values ~10× too small

**Files:** `bmc_loader.cpp`
**Symptom:** CPAP_Ti showed 95% of values below 0.21 seconds — physiologically implausible for normal breathing (~0.8–1.2 s expected inspiratory time at 12–16 BPM).
**Root cause:** `IERatioMapped` is stored as a percentage (0–100) by the `BmcWaveformPacket` constructor, but the Ti/Te block divided by 1000 (treating it as permille). For example, `IERatioMapped = 23` at RR = 12 produced Ti = (60/12) × (23/1000) = 0.115 s instead of the correct 1.15 s.
**Fix:** Changed divisors in `bmc_loader.cpp` lines 519–520 from `1000.0` to `100.0`, and `(1000 - IERatioMapped)` to `(100 - IERatioMapped)`.

---

## 2026-03-20 - BMC G3X: CPAP_Ti / CPAP_Te / I:E channels based on unconfirmed offsets

**Files:** `bmcG3xDataParsing.cpp`, `bmc_loader.cpp`, `bmc_loader.h`, `bmcg3x_loader.h`
**Symptom:** G3X exported CPAP_Ti, CPAP_Te, CPAP_IE, BMC_IE_Ratio channels derived from waveform packet offsets 0x074 and 0x07E, which were assumed to be inspiration/expiration time in centiseconds.
**Root cause:** Analysis showed the sum of 0x074 + 0x07E is near-constant (~565 cs) regardless of respiratory rate, and neither field correlates with RR, tidal volume, minute ventilation, or pressure. They are not patient Ti/Te. The original label was incorrect.
**Fix:** Added `ExportTimingChannels()` virtual method to `BmcLoader` (default true). Overridden to false in `BmcG3xLoader`. Guarded creation and population of `wInspTime`, `wExpTime`, `wIEValue`, `wIERatio` event lists in `bmc_loader.cpp`. Suppressed IERatioMapped computation from 0x074/0x07E in `bmcG3xDataParsing.cpp`. Also consolidated duplicate Ti/Te write block into one guarded block.

---

## 2026-03-18 - Daily calendar navigation bar doesn't highlight on window focus

**Files:** `oscar/daily.ui`
**Symptom:** In OSCAR 1.7.1 (Qt 5), the calendar's month/year navigation bar changed from black-on-gray to white-on-blue when OSCAR had window focus. In 2.0 (Qt 6), it stays black-on-gray regardless of focus.
**Root cause:** Qt 5 applied the system highlight color to the navigation bar automatically. Qt 6 no longer does this, and the explicit stylesheet in `daily.ui` overrides any default behavior. Qt stylesheets don't support a `:focus` pseudo-state on parent widgets, so restoring this would require handling `QEvent::WindowActivate`/`WindowDeactivate` in code to swap the stylesheet.
**Status:** Noted; no fix applied.

---

## 2026-03-18 - Preferences dialog more vertically spread out than Qt5/Windows 10

**Files:** Preferences `.ui` file
**Symptom:** Preferences dialog is noticeably taller on Qt 6 / Windows 11 compared to Qt 5 / Windows 10.
**Root cause:** Not a bug. Qt 6 uses larger default layout margins, spacing, and widget padding. Windows 11's Segoe UI font renders with slightly different metrics, and native controls are taller for touch-friendly sizing. The combined effect makes the same `.ui` layout taller.
**Status:** Noted; no fix applied. Could be tightened via layout margins/spacing or stylesheet, but that risks affecting other platforms.

---

## 2026-03-18 - Crash on application exit

**Files:** `oscar/main.cpp`, `oscar/mainwindow.cpp`
**Symptom:** OSCAR crashes during shutdown in `QGuiApplication::~QGuiApplication` every time the application exits.
**Root cause:** Two `QApplication` instances existed simultaneously. A `QApplication app` was created at the top of `main()` (inside a Qt 6.5+ guard) to force light-mode color scheme, and then a second `QApplication mainapp` was created later for the actual event loop. Qt requires exactly one QApplication per process. When `mainapp` was destroyed on exit, it cleaned up global Qt state; then `app`'s destructor tried to clean the same state again, crashing in `QGuiApplication::~QGuiApplication`.
**Fix:** Removed the first `QApplication app` and moved the `setColorScheme(Qt::ColorScheme::Light)` call to `mainapp` immediately after its construction. Also restructured shutdown: (1) moved `Profiles::Done()` and `DestroyGraphGlobals()` from `closeEvent` to `main.cpp` after explicit `delete mainwin` so globals outlive all widgets; (2) un-parent loaders from MainWindow in `closeEvent` to avoid dual-ownership double-free; (3) removed `QCoreApplication::quit()` from `MainWindow::~MainWindow()`.

---

## 2026-03-18 - Crash when importing/exporting journal with no profile open

**Files:** `oscar/mainwindow.cpp`
**Symptom:** OSCAR crashes when user selects Journals/Import Journal (or Export) with no profile/database open.
**Root cause:** `profilePath()` (line 2528) dereferences `p_profile` without a null check. When no profile is loaded, `p_profile` is null, causing a crash in `QHash::contains`.
**Fix:** Added null guard for `p_profile` in `profilePath()`. Added early-return with warning message in both `on_actionImport_Journal_triggered()` and `on_actionExport_Journal_triggered()` when `p_profile` is null.

---

## 2026-03-18 - BMC G3X: mask-on hours now separate from hours-used

**Files:** `oscar/SleepLib/loader_plugins/bmcg3x_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.cpp`, `oscar/SleepLib/session.cpp`

**Symptom:** `session_summaries.hours_used` and `session_summaries.mask_on_hours` were identical; mask-on hours did not reflect the fact that the patient removed the mask before the machine was turned off.

**Root cause (two separate bugs):**
1. `Session::StoreSummaryToDatabase()` computed `hoursUsed = hours()`, but `hours()` returns MaskOn-slice time when slices are populated, making `hoursUsed == maskOnHours`.
2. The mask-off detector in `BmcG3xLoader::findStableEndMs` used a trailing high-leak scan; after mask removal the BMC G3X ramps pressure to minimum so leak returns to zero — the last packet has `Raw.Leak = 0` and the detector never triggered.

**Fix:**
- `session.cpp`: Changed `hoursUsed` to use `(s_last - s_first) / 3600000.0` (raw session span) instead of `hours()`.
- `bmcg3x_loader.h`: Replaced leak-based mask-off detection with flow-based detection. Scans backward through `Raw.Flow[50]` samples to find the last packet with any sample exceeding the noise floor (±25 raw). If the trailing no-flow period is ≥ 300 seconds, that packet's timestamp is the mask-off point. Validated on reference night: mask-off detected at 08:37:43, giving maskOnHrs = 8.16 vs totalHrs = 8.60 (≈ 27-minute difference).

---

## 2026-03-17 - Dark mode illegibility fixed in three windows

**Files:** `oscar/profileselector.cpp`, `oscar/daily.ui`, `oscar/oximeterimport.cpp`

**Symptom:** In system dark mode on Qt 6.2/6.4, three windows were illegible: the profile selector had a dark background, the daily page calendar widget had a dark background, and the oximeter import wizard showed white-on-white text.

**Root cause:** Qt 6.5+ provides `QApplication::styleHints()->setColorScheme()` to force light mode per-widget, but this API is unavailable on Qt 6.2/6.4. Without explicit palette overrides, these widgets inherited the system dark palette.

**Fix:**
- `profileselector.cpp`: Apply an explicit light `QPalette` (white base/window, black text) to `this` in the constructor.
- `daily.ui`: Extend the `QCalendarWidget` stylesheet with rules targeting `QAbstractItemView` (white background, black text) and `qt_calendar_navigationbar` (light grey background, black text).
- `oximeterimport.cpp`: Apply `WindowText`, `Text`, and `ButtonText` = black to `this->palette()` in the constructor so text is legible over the existing light gradient background.

---

## 2026-03-17 - Qt dialog/button translations added via `oscar_qt_*.ts` supplemental catalogs

**Files:** `oscar/translation.cpp`, `Translations/qt/oscar_qt_*.ts`, `Tools/generate_qt_dialog_translations.py`

**Symptom:** Standard Qt dialog strings such as `Open`, `Save`, `Cancel`, `Close`, and `QFileDialog` labels remained untranslated even when OSCAR itself was running in a translated language.

**Root cause:** OSCAR already had runtime support for loading supplemental `oscar_qt_*.qm` files, but the corresponding `.ts` source files did not exist. The runtime loader also truncated the language code to two letters, which prevented region-specific catalogs such as `zh_CN`, `pt_BR`, `es_MX`, and `en_UK` from being loaded by name.

**Fix:** Added trimmed Qt supplemental translation catalogs under `Translations/qt/` for every current OSCAR language, generated primarily from the local Qt 6.10.2 translation sources and supplemented with OSCAR-local strings/manual fallbacks where Qt did not ship a matching source catalog. Updated `initTranslations()` to try the full OSCAR language code first (for example `oscar_qt_pt_BR.qm`) and then fall back to the base language code if needed.

**Note:** Languages where Qt does not provide a matching source catalog (for example Afrikaans, Filipino, Greek, Norwegian Bokmal, Romanian, Thai) use best-effort supplemental translations and should be reviewed by native speakers when possible.

---

## 2026-03-17 — QMessageBox custom buttons not translated (Yes/Cancel in CPAP Data Located dialog)

**Files:** `oscar/mainwindow.cpp`, `Translations/*.ts` (27 language files)

**Symptom:** The "Yes" and "Cancel" buttons in the "CPAP Data Located" QMessageBox were not translated when OSCAR was set to a non-English language on an English OS.

**Root cause:** `tr("Yes")` and `tr("Cancel")` called in `mainwindow.cpp` look up translations in the `MainWindow` context only. These strings existed in other contexts in the `.ts` files (e.g. `QObject`, `RestoreDialog`) but were absent from the `MainWindow` context. Qt does not fall back across contexts.

**Fix:** Added "Yes" and "Cancel" to the `MainWindow` context in all 27 `.ts` files that had existing translations for those strings. Three files were not updated due to no existing translation: Czech, English (UK), Thai.

**Note:** QFileDialog button labels ("Open"/"Save"/"Cancel") remain untranslated — those are Qt-internal strings requiring `oscar_qt_xx.qm` files, which do not yet exist.

---

## 2026-03-17 — "Last Imported" column in profile selector not updating after SD card import

**Files:** `oscar/mainwindow.cpp`, `oscar/SleepLib/importcontext.cpp`

**Symptom:** The "Last Imported" column in the profile selector showed a stale date and was never updated after importing new data from an SD card.

**Root Cause:** `MachineInfo::lastimported` is set to `QDateTime::currentDateTime()` only when a `MachineInfo` is first constructed (new machine). For existing machines it is loaded from the database and never updated on subsequent imports. `Machine::SaveToDatabase()` returns early for already-registered machines without touching `lastImported`. Additionally, `ResmedLoader` calls `mach->AddSession()` directly (not via `new_sessions` or `ImportContext`), so there was no hook in the loader itself to catch the timestamp.

**Fix:** In `MainWindow::importCPAP()`, after `Open()` returns `c > 0` (sessions were imported) and inside the open DB transaction, iterate all machines in the profile matching the loader's name, set `m->info.lastimported = QDateTime::currentDateTime()`, and persist via `MachineRepository::update()`. This is the one place in the import flow guaranteed to run for all loaders regardless of how they add sessions. Also added the same fix in `ImportContext::Commit()` for the context-based path used by PRS1.

---

## 2026-03-15 — QFileDialog: DontUseNativeDialog required for button translation (design note)

**Files:** All call sites using `QFileDialog` throughout OSCAR.

**Background:** `QFileDialog::DontUseNativeDialog` was added to every `QFileDialog` call so that button labels (Open, Save, Cancel, etc.) are translated to the user's selected OSCAR language.

**Investigation:** We considered whether there was an alternative that would allow native OS dialogs to be used while still translating the buttons. There is not. Native dialogs are rendered entirely by the OS platform layer (e.g. COMDLG32 on Windows) and always use the OS locale. Qt has no mechanism to inject translated text into native dialogs on any supported platform. `QFileDialog::setLabelText()` is documented to have no effect on native dialogs on most platforms.

**Conclusion:** `DontUseNativeDialog` is the only correct cross-platform solution when the app language may differ from the OS language. The trade-off is that Qt-rendered dialogs have a slightly different appearance and may be marginally slower than native dialogs on some platforms.

---

## 2026-03-16 — BMC G3X: startup artifacts on Flow Rate, Pressure, and Pressure Trend graphs

**Files:** `oscar/SleepLib/loader_plugins/bmcg3x_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.h`, `oscar/SleepLib/loader_plugins/bmc_loader.cpp`

**Symptom:** At the left edge of the session view, Flow Rate showed a spike from y=0 to the first sample, Pressure showed a ramp from the APAP minimum (~4.0 cmH2O) up to the therapeutic value, and Pressure Trend showed the same ramp.

**Root cause:** The BMC G3X machine starts recording waveform packets immediately when powered on, before the patient puts on the mask. During this idle period, `PressureTrend` (0x76C) is held at the minimum APAP pressure (400 hundredths = 4.0 cmH2O). When the patient dons the mask, the APAP algorithm ramps pressure upward over several seconds to reach the therapeutic target. The session's `s_first` was set to the very first waveform packet, so all idle and ramp data was visible from the left graph edge.

**Fix:** Added virtual `findStableStartMs(BmcSession*)` to `BmcLoader` (base returns `StartTimestamp` unchanged). `BmcG3xLoader` overrides it with a two-phase scan: (1) skip the initial idle packets where `PressureTrend` is at the startup minimum, (2) from the first packet where pressure begins rising, advance until pressure stops rising (ramp peak). The resulting timestamp is passed to `session->really_set_first()` instead of `StartTimestamp`. The graph renderers naturally crop all earlier data: the waveform renderer skips samples before `s_first`, and the event renderer primes from the last pre-`s_first` event so step-function channels also start cleanly.

---

## 2026-03-15 — Viatom/Wellue import: SpO2 and Pulse Rate plots show "Plots Disabled" / empty after restart

**Files:** `oscar/SleepLib/loader_plugins/viatom_loader.h`

**Symptom:** After importing Viatom/Wellue oximeter data via Data → Import Viatom Data, the session appeared in the session list but SpO2 and Pulse Rate graphs showed "Plots Disabled". After restarting OSCAR, the same plots were simply empty. Debug log showed: `Session::LoadFromDatabase(): Session XXXXXXXXXX not found in database`.

**Root cause:** `Machine::SaveToDatabase()` rejects machines where both `info.serial` and `info.model` are empty (`"Cannot save machine without serial or model"`). In `ViatomLoader::newInfo()`, `model` was `QString()` (empty). `serial` is only set if the file's enclosing folder name is ≥9 characters long with the last 4 numeric (a Viatom device serial number pattern). When a user imports files from a non-serial-named folder (e.g. Downloads), both remain empty. The machine is never saved to the database, so `m_database_id` stays 0. `Session::Store()` skips all DB writes when the machine has no DB ID. After `TrashEvents()` clears in-memory data, `OpenEvents()` fails (machine not in DB), yielding 0 plot points → "Plots Disabled". On restart, `LoadSessionsFromDatabase()` finds no machine/sessions and nothing is shown.

**Fix:** Changed `newInfo()` in `viatom_loader.h` to provide a default `model` of `QObject::tr("Viatom Oximeter")`. This ensures the machine is always saved to the database even when no serial can be determined from the folder name, so session data is correctly stored and loaded.

---

## 2026-03-15 — MD300W1, Dreem, Somnopose, ZEO loaders: same empty model/serial bug as Viatom

**Files:** `oscar/SleepLib/loader_plugins/md300w1_loader.h`, `dreem_loader.h`, `somnopose_loader.h`, `zeo_loader.h`

**Symptom:** Same as the Viatom bug above — sessions would not persist to database after import.

**Root cause:** Same root cause. `newInfo()` returned empty `model` and empty `serial` in all four loaders. MD300W1 goes through the oximeter wizard; Dreem, Somnopose, and ZEO go through `importNonCPAP`. None populate serial from file data, so `Machine::SaveToDatabase()` always failed for them.

**Fix:** Added default model names: MD300W1 → `"MD300W1 Oximeter"`, Dreem → `"Dreem Headband"`, Somnopose → `"Somnopose"`, ZEO → `"Zeo Sleep Manager"`.

---

## 2026-03-12 — Statistics page month names not translated in Qt6

**Files:** `oscar/statistics.cpp`

**Symptom:** In Monthly report mode, column headers showing month names (e.g., "January", "February") were always displayed in English regardless of the active language. Worked correctly under Qt5.

**Root cause:** `QDate::toString(format)` behaviour changed between Qt5 and Qt6. In Qt5 it used the application's default locale for month/day names. In Qt6 it always uses the C locale (English). This is a documented Qt6 breaking change.

**Fix:** Changed `s.toString("MMMM<br>yyyy")` to `QLocale().toString(s, "MMMM<br>yyyy")` on line 1460, which explicitly uses the application locale and works correctly in both Qt5 and Qt6.

---

## 2026-03-12 — QDateEdit ignoreOlderSessionsDate displays incorrectly under translation

**Files:** `oscar/preferencesdialog.ui`, `Translations/Francais.fr.ts`

**Symptom:** The "Do not import sessions older than" date field in Preferences displayed strange/uneditable content when a non-English language (specifically French) was active.

**Root cause:** The `displayFormat` property (`dd MMMM yyyy`) of the `QDateEdit` widget was a plain `<string>` in the `.ui` file, so Qt's `uic` wrapped it in `tr()`. The French translation mapped it to `jj MMMM aaaa` (using French abbreviations jour/an), which are not valid Qt date format codes. Qt treated `jj` and `aaaa` as literal text, causing the widget to display literal "jj" and "aaaa" instead of numeric day and year.

**Fix:** Added `notr="true"` to the `displayFormat` string in `preferencesdialog.ui` so the format is never translated. Qt's `MMMM` token already renders month names in the application locale's language, so no translation of the format string is needed. Also corrected the French translation from `jj MMMM aaaa` to `dd MMMM yyyy`; this entry will become obsolete after the next `lupdate` run.

---

## 2026-03-12 — Purged data reappears after OSCAR restart

**File:** `oscar/SleepLib/session.cpp`, `Session::Destroy()`

**Symptom:** Purging a day (or all data) via Data → Advanced appeared to work, but after closing and restarting OSCAR the purged sessions were still present.

**Root cause:** `Session::Destroy()` only deleted the legacy `.000` summary and `.001` event files from disk, but never removed the session row from the SQLite database. Since summary file storage was moved to the database (`.000` writes are disabled), nothing was actually deleted — the files didn't exist and the DB rows remained intact. On restart OSCAR loaded sessions from the database, restoring all "purged" data.

**Fix:** Added `SessionRepository::remove(m_sessionrow_id)` call in `Session::Destroy()` before `unlinkSession()`. The DB schema uses `ON DELETE CASCADE` on all child tables (session_channels, session_slices, session_settings, event_lists, respiratory_events, etc.), so a single delete of the sessions row cleans up everything.

---

## 2026-03-12 — Rebuild-from-backup erases backup directory in BMC loaders

**Files:** `oscar/SleepLib/loader_plugins/bmc_loader.cpp`, `oscar/SleepLib/loader_plugins/bmcg3x_loader.cpp`, `Open()`

**Symptom:** "Rebuild from backup" for BMC and BMC G3x machines silently destroyed the backup data it was trying to import from, leaving the machine with no backup after the rebuild.

**Root cause:** Both `BmcLoader::Open()` and `BmcG3xLoader::Open()` unconditionally erase and recreate `mach->getBackupPath()` then copy `dirpath` into it. When rebuilding from backup, `dirpath` IS the backup directory, so the backup was wiped before any data was read.

**Fix:** Before the erase-and-copy block in both loaders, compare `QDir::cleanPath(dirpath)` to `QDir::cleanPath(backupPath)`. If they are equal, skip the backup creation entirely. `QDir::cleanPath()` normalises trailing slashes so the comparison is reliable.

---

## 2026-03-12 — Profile selector name column not translatable (locale name order)

**File:** `oscar/profileselector.cpp`, `oscar/profileselector.h`

**Symptom:** Names in the profile list and detail panel were always displayed in "Last, First" order regardless of locale. French users expect "First Last" order.

**Root cause:** Three of the four name-formatting call sites used bare `QString("%1, %2")` with no `tr()` wrapper, making them untranslatable. The fourth used `tr("Name: %1, %2")` but conflated the label and the name format in a single translation string, making it impossible for translators to change only the name order.

**Fix:** Added `ProfileSelector::formattedName(lastName, firstName)` helper that returns `tr("%1, %2").arg(lastName, firstName)`. All four call sites now use the helper. Translators can override `"%1, %2"` to `"%2 %1"` in their `.ts` file to get "First Last" order with no further code changes.

---

## 2026-03-12 — Profile selector name column sorts by display string, not by last name

**File:** `oscar/profileselector.cpp`, `oscar/profileselector.h`

**Symptom:** Clicking the name column header to sort would sort by the displayed string. In French ("First Last" order) this sorts by first name, not last name — inconsistent with English behaviour and user expectation.

**Root cause:** `MySortFilterProxyModel2::lessThan` compared `Qt::DisplayRole` data for the name column. When the display format is locale-dependent (e.g. "First Last" in French), the sort order follows the display format rather than a stable key.

**Fix:** Added `MySortFilterProxyModel2::NameSortRole` (`Qt::UserRole+3`) to carry a locale-independent `"lastname, firstname"` sort key. All three `setData` calls for column 5 now also store this key. `lessThan` detects the name column and compares by `NameSortRole` instead of `DisplayRole`. Added `ProfileSelector::nameSortKey(lastName, firstName)` static helper to produce the key consistently.

---

## 2026-03-12 — gOverviewGraph crash on mouse-over (Overview page)

**File:** `oscar/Graphs/gOverviewGraph.cpp`, `mouseMoveEvent()`

**Symptom:** OSCAR crashes when moving the mouse over the Feelings plot on the Overview page, if Feelings has only been set for one day.

**Root cause:** `d.value()` was called on a `QHash` iterator (`d = m_values.find(hl_day)`) before the guard `d != m_values.end()` was checked. When hovering over any day without a Feelings entry, `d` is the end iterator — dereferencing it is undefined behaviour and crashes.

**Stack trace frame:** `gOverviewGraph::mouseMoveEvent` at line 1079 (original), `QHash::iterator::value()`.

**Fix:** Moved `QMap<short, EventDataType> &valhash = d.value();` to inside the `if ((d != m_values.end()) && (day != nullptr))` block, so it is only evaluated when the iterator is valid. `valhash` is only used within that block, so no other changes were needed.

---

## 2026-03-16 — BMC G3X: oximetry channels continue recording after mask removal; CPAP waveforms do not

**Files:** No code change — investigation note only.

**Observation:** On the 2026-03-16 night, the Flow Rate waveform and most CPAP channels (Pressure, Mask Pressure, Tidal Volume, etc.) appear to stop at approximately 08:27, while the OSCAR session timeline and the Oximetry channels (SpO2, Pulse Rate) continue to 09:04.

**Root cause (data, not code):** The patient removed the mask at approximately 08:25–08:27. Binary evidence in the waveform file (B33BF114508.000):
- 08:25:34: Leak jumps from 0 to 15 raw units (mask seal breaking).
- 08:25:54: Flow spikes to 1159 raw units, leak reaches 1199 (mask coming off).
- 08:26:28 onward: Flow drops to 0–5 raw units (noise floor); leak stabilises at ~178–193 raw units (ambient air escaping from the running machine with no mask).
- 08:27–09:04: Flow amplitude never exceeds 18 raw units; all 60 waveform packets per minute are present and valid.

The SpO2 finger probe remained on the patient's finger, so OXI_SPO2 and OXI_Pulse continue recording valid data until 09:04. The CPAP machine also continued running (and recording) with no mask until 09:04, producing a constant-leak, near-zero-flow signature.

**Conclusion:** OSCAR is displaying the data correctly. The flow waveform is not missing — it is genuinely flat because the mask was off. The session end time of 09:04 reflects when the machine was switched off, not when therapy ended. No code change required.

---

## 2026-03-15 — "Find your CPAP data card" dialog Open/Cancel buttons not translated

**File:** `oscar/mainwindow.cpp`, `MainWindow::importCPAPData()` (around line 1285)

**Symptom:** When a non-English language was active, the Open and Cancel buttons in the "Find your CPAP data card" `QFileDialog` remained in English.

**Root cause:** `QFileDialog` defaults to the native OS file picker. Native dialogs render their own buttons outside Qt's widget and translation system, so `tr()` has no effect on them.

**Fix:** Added `w.setOption(QFileDialog::DontUseNativeDialog, true)` so Qt renders the dialog itself. Qt's own dialog widgets are fully subject to the translation system, and the buttons are translated correctly.
