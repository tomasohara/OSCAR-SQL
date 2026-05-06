# Loader External Call Analysis
*Generated 2026-04-23 — for use in guiding loader testing against OSCAR 2.0 SQL structure*

---

## Purpose

This document maps every external call each loader makes outside its own files.
The primary goal is to identify loaders that may be missing calls (or making
unexpected calls) that would cause them to fail or silently misbehave under the
new SQLite-backed import architecture.

---

## Import Architecture Overview

There are **three distinct session-persistence paths** in use across the loaders.
Understanding which path each loader uses is the key to the DB risk assessment.

### Path A — ImportContext (new/correct path)
```
loader: context()->AddSession(session)
  └─ stores session to disk (session->Store)
  └─ adds to ImportContext::m_sessions map
mainwindow: ctx->Commit()
  └─ calls mach->AddSession() for each session in m_sessions
  └─ updates lastImported in DB via MachineRepository
mainwindow: dbMgr.commit()  (entire import is one transaction)
```
Machine is saved to DB in `ProfileImportContext::CreateMachineFromInfo()`.

### Path B — finishAddingSessions (transitional path)
```
loader: this->addSession(session)      [MachineLoader method]
  └─ adds to MachineLoader::new_sessions map
loader: finishAddingSessions()
  └─ saves machine to DB if getDatabaseId() == 0
  └─ calls mach->AddSession() for each session in new_sessions
  └─ calls p_profile->calculateDailySummaries()
mainwindow: ctx->Commit()              [processes any m_sessions — empty here]
```
Note: `finishAddingSessions()` contains a comment: *"TODO: Remove once all loaders use ImportContext."*

### Path C — Direct call (legacy/bypass path)
```
loader: mach->AddSession(session)      [directly, no accumulation]
mainwindow: ctx->Commit()              [m_sessions empty — does nothing]
mainwindow stamps lastImported for all machines of this loader type regardless
```
Machine DB save must be done explicitly by the loader or relies on
`mach->AddSession()` to handle it internally.

---

## Loader Classification Table

| Loader | Session Path | SaveToDatabase | SessionExists | UpdateSummaries | Store(path) | Risk |
|--------|-------------|----------------|---------------|-----------------|-------------|------|
| **prs1** | A (ImportContext) | via ProfileImportContext | context() | in ImportContext::AddSession | in ImportContext::AddSession | Low |
| **prisma** | A (ImportContext) | via ProfileImportContext | — | — | — | Medium (no UpdateSummaries) |
| **icon** | B (addSession/finish) | via finishAddingSessions | ✓ | — | — | Low |
| **sleepstyle** | B (addSession/finish) | via finishAddingSessions | ✓ | ✓ | — | Low |
| **intellipap** | B (addSession) + direct | via finishAddingSessions | ✓ | ✓ | — | Medium (mixed paths) |
| **yuwell** | B (finish) + direct | via finishAddingSessions | ✓ | ✓ | — | Medium (mixed paths) |
| **resmed** | C direct (in task) + finish | explicit (getDatabaseId check) | — | ✓ | — | Low (explicit DB save) |
| **bmc** | C direct | explicit (getDatabaseId check) | ✓ | ✓ | ✓ | Low (explicit DB save; uses SessionRepository) |
| **bmcg3x** | C direct (via BmcLoaderTask) | explicit (getDatabaseId check) | — | — | — | Medium (inherits bmc task; no session-level calls in own files) |
| **dreem** | C direct | — | ✓ | — | — | **High** |
| **weinmann** | C direct | — | ✓ | ✓ | — | **High** |
| **somnopose** | C direct | — | ✓ | — | ✓ | **High** |
| **zeo** | C direct | — | ✓ | — | ✓ | **High** |
| **viatom** | C direct | — | ✓ | — | — | **High** |
| **resvent** | C direct | — | ✓ | ✓ | ✓ | **High** |
| **vrem** | C direct | — | ✓ | ✓ | ✓ | **High** |
| **edf** | None (pure parser) | — | — | — | — | N/A |
| **cms50** | Partial (CreateMachine only) | — | — | — | — | **Incomplete** |
| **cms50f37** | Partial (CreateMachine only) | — | — | — | — | **Incomplete** |
| **md300w1** | None (oximeter) | — | — | — | — | N/A |
| **mseries** | Disabled (`#ifdef REMSTAR_M_SUPPORT`) | — | — | — | — | N/A |

---

## Per-Loader Detail

### bmc
**Files:** `bmc_loader.cpp`, `bmcDataParsing.cpp`
**Import path:** Path C (direct). Explicitly manages DB layer itself.

External calls:
- `p_profile->lookupMachine()`, `p_profile->CreateMachine()`, `p_profile->GetDay()`
- `mach->getDatabaseId()`, `mach->SaveToDatabase()` ← explicit DB save
- `mach->getBackupPath()`, `mach->SessionExists()`, `mach->unlinkSession()`, `mach->AddSession()`, `mach->Save()`
- `session->really_set_first()`, `session->really_set_last()`, `session->SetChanged()`, `session->UpdateSummaries()`, `session->Store()`
- `session->AddEventList()` (16+ calls)
- `SessionRepository::findByMachine()`, `SessionRepository::remove()` ← direct repo access
- `new Channel()`, `channel.add(GRP_CPAP)` (multiple)

**Notable:** The only loader that uses `SessionRepository` directly for overlap detection.
Also the only loader that calls `mach->unlinkSession()` (session removal).

---

### bmcg3x
**Files:** `bmcg3x_loader.cpp`, `bmcG3xDataParsing.cpp`
**Import path:** Path C via inherited `BmcLoaderTask`. Explicitly manages DB.

External calls in `bmcg3x_loader.cpp` only:
- `p_profile->lookupMachine()`, `p_profile->CreateMachine()`
- `p_profile->session->ignoreOlderSessionsDate()`, `p_profile->session->ignoreOlderSessions()`
- `mach->setInfo()`, `mach->LastDay()`, `mach->purgeDate()`
- `mach->getDatabaseId()`, `mach->SaveToDatabase()` ← explicit DB save
- `mach->getBackupPath()`, `mach->Save()`

**Notable:** `bmcg3x_loader.cpp` contains no session-level calls at all. All session
handling is inherited from `BmcLoader` via `BmcLoaderTask`. The G3X-specific parsing
is in `bmcG3xDataParsing.cpp` which also has no external API calls — it is a pure parser.

---

### cms50
**Files:** `cms50_loader.cpp`
**Import path:** Incomplete — no session storage.

External calls:
- `p_profile->oxi->oximeterType()` (type check only)
- `p_profile->CreateMachine()` (machine created but sessions never stored)

**Notable:** Code comment at line 357: `// TODO: Store the data to the session`.
This loader creates a machine but never stores session data. It is functionally
incomplete regardless of database structure.

---

### cms50f37
**Files:** `cms50f37_loader.cpp`
**Import path:** Incomplete — no session storage found.

External calls:
- `p_profile->oxi->oximeterType()` (type check only; returns early if wrong type)

**Notable:** 1048 lines but no machine/session API calls found. Likely an oximeter
loader that does device communication rather than SD-card import.

---

### dreem
**Files:** `dreem_loader.cpp`
**Import path:** Path C (direct).

External calls:
- `p_profile->CreateMachine()`, `p_profile->StoreMachines()`, `mach->SaveSummaryCache()`
- `mach->AddSession()`, `mach->Save()`, `mach->SessionExists()`
- `session->SetChanged()`, `session->AddEventList()`, `session->really_set_first()`, `session->really_set_last()`

**Risk:** No `SaveToDatabase()` or `getDatabaseId()` check. Machine may not be in DB
before sessions are added. Also calls `p_profile->StoreMachines()` and
`mach->SaveSummaryCache()` which are unusual (only this loader does both).

---

### edf
**Files:** `edfparser.cpp`
**Import path:** None — pure parser library.

No machine/session/database API calls. Used as a parsing component by other loaders
(e.g. resmed, sleepstyle). Not a standalone loader.

---

### icon
**Files:** `icon_loader.cpp`
**Import path:** Path B (`addSession()` + `finishAddingSessions()`).

External calls:
- `p_profile->CreateMachine()`, `mach->getBackupPath()`, `mach->SessionExists()`, `mach->Save()`, `mach->setModel()`
- `session->really_set_first()`, `session->really_set_last()`, `session->SetChanged()`, `session->AddEventList()` (6+ calls)
- `finishAddingSessions()` (called at end of Open)

**Notable:** Uses `this->addSession()` (Path B), which triggers DB machine save check
inside `finishAddingSessions()`. Does not call `session->UpdateSummaries()`.

---

### intellipap
**Files:** `intellipap_loader.cpp`
**Import path:** Mixed — Path B (`addSession()` + `finishAddingSessions()`) for most
sessions; direct `mach->AddSession()` in the `addSessions()` helper function.

External calls:
- `p_profile->CreateMachine()`, `mach->getBackupPath()`, `mach->SessionExists()`, `mach->Save()`
- `session->really_set_first()`, `session->really_set_last()`, `session->SetChanged()`, `session->UpdateSummaries()`
- `session->AddEventList()` (20+ calls)
- `finishAddingSessions()` at end of import
- `mach->AddSession()` direct (in `addSessions()` helper)

**Risk:** Mixed path. Sessions via the direct `mach->AddSession()` in `addSessions()`
may bypass the DB machine-ID check that `finishAddingSessions()` performs.

---

### md300w1
**Files:** `md300w1_loader.cpp`
**Import path:** None found — oximeter device loader.

No `p_profile->CreateMachine()` or session API calls found. Likely uses a different
base class or communication path.

---

### mseries
**Files:** `mseries_loader.cpp`
**Import path:** Disabled (`#ifdef REMSTAR_M_SUPPORT` wraps entire file).

Not compiled in standard builds. Contents irrelevant until REMSTAR_M_SUPPORT is
defined.

---

### prisma
**Files:** `prisma_loader.cpp`
**Import path:** Path A (`context()->AddSession()`). Machine creation via
`p_profile->CreateMachine()` but NOT via `context->CreateMachineFromInfo()`.

External calls:
- `p_profile->CreateMachine()`
- `loader->context()->AddSession(session)` ← Path A
- `session->SetChanged()`, `session->really_set_first()`, `session->really_set_last()`
- `mach->AddSession()` (NOT via context — direct)
- `new Channel()`, `channel.add(GRP_*)` (multiple)

**Risk:** Creates machine via `p_profile->CreateMachine()` directly rather than
`context->CreateMachineFromInfo()`, which means the automatic DB save that
`ProfileImportContext::CreateMachineFromInfo()` performs may not happen. Machine
may have getDatabaseId() == 0 when sessions are added via `context()->AddSession()`.
Also note: `mach->AddSession()` appears directly in addition to `context()->AddSession()` —
may be adding some sessions through Path A and others through Path C.

---

### prs1
**Files:** `prs1_loader.cpp`, `prs1_parser.cpp`, `prs1_parser_asv.cpp`,
`prs1_parser_vent.cpp`, `prs1_parser_xpap.cpp`
**Import path:** Path A (`context()->AddSession()`). Machine via
`context->CreateMachineFromInfo()` (implicit through `CreateMachineFromProperties()`).

External calls:
- `p_profile->CreateMachine()` (indirectly via context)
- `context()->GetBackupPath()`, `context()->SessionExists()`, `context()->CreateSession()`, `context()->AddSession()` ← Path A
- `context()->IgnoreSessionsOlderThan()`, `context()->ShouldIgnoreOldSessions()`
- `context()->FlushUnexpectedMessages()`
- `session->set_first()`, `session->really_set_last()`
- `session->settings[...]` (many settings writes)
- `finishAddingSessions()` (called, but `new_sessions` is empty — runs `calculateDailySummaries()` only)

**Notable:** Most fully migrated to ImportContext. The `prs1_parser_*.cpp` files contain
only pure parsing logic — no external API calls.

---

### resmed
**Files:** `resmed_loader.cpp`, `resmed_EDFinfo.cpp`
**Import path:** Path C direct (via task code) + explicit DB save + `finishAddingSessions()`.

External calls:
- `p_profile->CreateMachine()`, `p_profile->forceResmedPrefs()` (ResMed-specific)
- `mach->getDatabaseId()`, `mach->SaveToDatabase()` ← explicit DB save
- `mach->getBackupPath()`, `mach->getDataPath()`, `mach->Save()`
- `mach->AddSession()` (direct in task, thread-safe via sessionMutex)
- `session->Store()`, `session->SetChanged()`, `session->UpdateSummaries()`, `session->AddEventList()`
- `finishAddingSessions()` (runs `calculateDailySummaries()`)
- `new Channel()`, `channel.add(GRP_*)` (multiple)

**Notable:** Has explicit `getDatabaseId() == 0` guard before `SaveToDatabase()`, same
pattern as bmc and bmcg3x. One commented-out `mach->AddSession()` at line 3039.

---

### resvent
**Files:** `resvent_loader.cpp`
**Import path:** Path C direct.

External calls:
- `p_profile->CreateMachine()`, `mach->getDataPath()`, `mach->SessionExists()`, `mach->AddSession()`, `mach->Save()`
- `session->Store(machine->getDataPath())` ← file-based store with explicit path
- `session->SetChanged()`, `session->UpdateSummaries()`, `session->really_set_first()`, `session->really_set_last()`
- `session->AddEventList()`
- `new Channel()`, `channel.add(GRP_*)` (multiple)

**Risk:** No `SaveToDatabase()` or `getDatabaseId()` check. Calls `session->Store(path)`
with explicit data path — depends on file-based session store working correctly.
Machine may not be in DB when sessions are stored.

---

### sleepstyle
**Files:** `sleepstyle_loader.cpp`, `sleepstyle_EDFinfo.cpp`
**Import path:** Path B (`addSession()` + `finishAddingSessions()`).

External calls:
- `p_profile->CreateMachine()`, `mach->getBackupPath()`, `mach->SessionExists()`, `mach->Save()`, `mach->setModel()`
- `session->SetChanged()`, `session->UpdateSummaries()`, `session->really_set_first()`, `session->really_set_last()`
- `session->AddEventList()`
- `finishAddingSessions()`
- `new Channel()`, `channel.add(GRP_*)` (multiple)

---

### somnopose
**Files:** `somnopose_loader.cpp`
**Import path:** Path C direct.

External calls:
- `p_profile->CreateMachine()`, `mach->SessionExists()`, `mach->AddSession()`, `mach->Save()`
- `session->Store()`, `session->SetChanged()`, `session->really_set_first()`, `session->really_set_last()`
- `session->AddEventList()`

**Risk:** No `SaveToDatabase()` or `getDatabaseId()` check. Calls `session->Store()`
directly.

---

### viatom
**Files:** `viatom_loader.cpp`
**Import path:** Path C direct.

External calls:
- `p_profile->CreateMachine()`, `mach->SessionExists()`, `mach->AddSession()`, `mach->Save()`
- `session->SetChanged()`, `session->AddEventList()`
- `context()->LogUnexpectedMessage()`, `m_machine->previouslySeenUnexpectedData()`
- `session->warnOnUnexpectedData()` (unique to this loader)

**Risk:** No `SaveToDatabase()` or `getDatabaseId()` check. Does not call
`session->UpdateSummaries()`, `really_set_first()`, or `really_set_last()`.

---

### vrem
**Files:** `vrem_loader.cpp`
**Import path:** Path C direct.

External calls:
- `p_profile->CreateMachine()`, `mach->getDataPath()`, `mach->SessionExists()`, `mach->AddSession()`
- `session->Store(machine->getDataPath())` ← file-based store with explicit path
- `session->SetChanged()`, `session->UpdateSummaries()`
- `session->AddEventList()`
- `new Channel()`, `channel.add(GRP_*)` (multiple)

**Risk:** No `mach->Save()`, no `SaveToDatabase()`, no `getDatabaseId()` check.
Calls `session->Store(path)` directly. Machine may not be in DB.

---

### weinmann
**Files:** `weinmann_loader.cpp`
**Import path:** Path C direct.

External calls:
- `p_profile->CreateMachine()`, `mach->SessionExists()`, `mach->AddSession()`, `mach->Save()`
- `session->SetChanged()`, `session->UpdateSummaries()`, `session->really_set_first()`, `session->really_set_last()`
- `session->AddEventList()`
- `new Channel()`, `channel.add(GRP_*)` (multiple)

**Risk:** No `SaveToDatabase()` or `getDatabaseId()` check.

---

### yuwell
**Files:** `yuwell_loader.cpp`
**Import path:** Mixed — direct `mach->AddSession()` for most sessions,
`finishAddingSessions()` at the end.

External calls:
- `p_profile->CreateMachine()`, `mach->getBackupPath()`, `mach->getDataPath()`, `mach->setModel()`
- `mach->SessionExists()`, `mach->AddSession()` (direct, multiple call sites), `mach->Save()`
- `session->Store()`, `session->SetChanged()`, `session->UpdateSummaries()`, `session->really_set_first()`, `session->really_set_last()`
- `session->AddEventList()`
- `finishAddingSessions()` (called at end, but `new_sessions` may be empty since sessions were added directly)

**Risk:** `finishAddingSessions()` is called after direct `mach->AddSession()` calls.
The `new_sessions` map may be empty when it runs, so `calculateDailySummaries()` still
runs but the machine DB-save check is skipped (condition: `!new_sessions.empty()`).

---

### zeo
**Files:** `zeo_loader.cpp`
**Import path:** Path C direct.

External calls:
- `p_profile->CreateMachine()`, `mach->SessionExists()`, `mach->AddSession()`, `mach->Save()`
- `session->Store()`, `session->SetChanged()`, `session->really_set_first()`, `session->really_set_last()`
- `session->AddEventList()`

**Risk:** No `SaveToDatabase()` or `getDatabaseId()` check. Calls `session->Store()`
directly.

---

## Calls INTO Loaders — What's Not Uniform

### Two Import Entry Points

**CPAP loaders** — called via `MainWindow::importCPAP()`:
- `loader->Open(const QString& path)` — single directory
- Wrapped in `dbMgr.transaction()` / `dbMgr.commit()`
- Preceded by `SetContext(ctx)`, followed by `ctx->Commit()`

**Non-CPAP loaders** (Zeo, Dreem, Somnopose, Viatom) — called via `MainWindow::importNonCPAP()`:
- `loader.Open(const QStringList& files)` — list of individual files
- **No DB transaction wrapper**
- `SetContext(ctx)` and `ctx->Commit()` still present

The base class `MachineLoader` declares both:
```cpp
virtual int Open(const QString & path) = 0;   // pure virtual — CPAP
virtual int Open(const QStringList & paths);  // non-CPAP override
```

**MSeriesLoader** — completely non-standard (inside `#ifdef REMSTAR_M_SUPPORT`):
- Called as `mseries.Open(filename, p_profile)` — two-argument form
- No `SetContext()`, no `ctx->Commit()`, no DB transaction
- Entirely bypasses modern import infrastructure

### `getNameFilter()` — Non-CPAP Only

`importNonCPAP()` calls `loader.getNameFilter()` to populate the file dialog.
CPAP loaders are never asked for this. Base class provides a default `""`.

### Loader-Name Special Cases Elsewhere

These affect per-loader runtime behaviour outside the loader itself:

| Location | Loader | Behaviour difference |
|----------|--------|---------------------|
| `mainwindow.cpp:2450` | PRS1 only | Destroys `CPAP_Leak` during recalc; others destroy `CPAP_LargeLeak` |
| `mainwindow.cpp:612` | ResMed only | `#ifdef LOCK_RESMED_SESSIONS` — forces specific profile settings |
| `profiles.cpp:916` | Intellipap only | Skips "enable automatic backups?" prompt |
| `daily.cpp:1585` | ResMed + PRS1 | Different re-flagging logic path |
| `calcs.cpp:1025` | PRS1 only | `calcrdi = (loaderName == "PRS1")` — RDI calculated differently |
| `MinutesAtPressure.cpp:290` | PRS1 only | `bucketsPerPressure = 2` vs `INTERVALS_PER_CCMH2O` for all others |
| `reports.cpp:286–297` | PRS1, ResMed, Intellipap, SleepStyle | Different report sections per loader |
| `welcome.cpp:56` | ResMed S9 only | SD card compatibility warning shown |
| `preferencesdialog.cpp:82,1301` | ResMed only | Extra preferences UI elements |

---

## Summary Risk Table

| Risk Level | Loaders | Primary Concern |
|------------|---------|-----------------|
| **Low** | prs1, icon, sleepstyle, bmc, bmcg3x, resmed | Proper DB path used, or explicit DB save |
| **Medium** | intellipap, yuwell, prisma | Mixed paths; some sessions may bypass DB checks |
| **High** | dreem, weinmann, somnopose, zeo, viatom, resvent, vrem | No DB save check; direct mach->AddSession or session->Store |
| **Incomplete** | cms50, cms50f37 | Never store session data |
| **N/A** | edf (parser only), md300w1 (oximeter), mseries (disabled) | Not relevant |

---

## Key Questions for Testing

When testing "High risk" loaders, specifically verify:

1. **Machine created in DB?** After import, does `machines.id` exist for the imported machine?
2. **Sessions stored in DB?** Do `sessions` rows exist with correct `machine_id`?
3. **Daily summaries calculated?** Is `p_profile->calculateDailySummaries()` triggered? (Only called inside `finishAddingSessions()` — loaders on Path C that skip this may have missing daily summaries.)
4. **lastImported stamped?** The mainwindow stamps `lastImported` for all machines of the loader type after import — this is the safety net for Path C loaders.
5. **session->Store(path) vs DB?** Loaders calling `session->Store(path)` are writing to files. If the DB layer is authoritative, does `mach->AddSession()` also persist to DB, or does it rely on the file existing?

---

## Unusual Calls (One Loader Only)

| Call | Loader | Notes |
|------|--------|-------|
| `mach->unlinkSession()` | bmc | Removes a session from machine |
| `SessionRepository::findByMachine()` | bmc | Direct repo access for overlap detection |
| `SessionRepository::remove()` | bmc | Direct repo session deletion |
| `p_profile->forceResmedPrefs()` | resmed | ResMed-specific settings override |
| `mach->SaveSummaryCache()` | dreem | Unusual; most loaders don't call this |
| `p_profile->StoreMachines()` | dreem | Unusual; usually implicit |
| `session->warnOnUnexpectedData()` | viatom | Device data validation |
| `p_profile->GetDay()` | bmc | Day-level lookup |
| `mach->LastDay()`, `mach->purgeDate()` | bmcg3x | Import window calculation |
| `context()->ShouldIgnoreOldSessions()` | prs1, bmcg3x | Session age filtering |
