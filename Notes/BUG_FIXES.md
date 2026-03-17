# OSCAR Bug Fix Log

Notable bugs found and fixed during development/investigation.

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
