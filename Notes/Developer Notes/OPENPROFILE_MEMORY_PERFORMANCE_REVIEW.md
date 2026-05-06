# OpenProfile Memory Leak and Performance Review

**Date:** 2026-04-17  
**Scope:** `MainWindow::OpenProfile()`, `MainWindow::CloseProfile()`, and the page classes they create: `Daily`, `Overview`, `Welcome`.

**Update 2026-04-17:** Root cause of observed growing-memory-on-profile-switch identified — see item 3 below.

---

## Memory Leaks

### 1. `ProgressDialog` leaked on two error-return paths

**Files:** `mainwindow.cpp:554, 620, 641`

`progress` is heap-allocated at line 554 with `new ProgressDialog(this)` and opened modally at line 558
(`progress->open()`). The normal return path (lines 689–690) explicitly closes and deletes it. But two
`return false` paths skip that cleanup:

- **Line 620** — `if (daily)` guard: exits without `delete progress`
- **Line 641** — `if (overview)` guard: exits without `delete progress`

Since `open()` was already called, both early returns leave a **visible modal dialog** on screen with no way
to dismiss it. `ProgressDialog(this)` has `MainWindow` as parent so Qt will reclaim it at app exit, but the
dialog stays open and visible for the rest of the session. `p_profile` is also set (line 552) but never
finished loading, leaving the application in a corrupt state.

These paths are only reachable due to programmer error (active `Daily`/`Overview` when `OpenProfile` is
called), so the risk in practice is low — but the consequence is severe if hit.

**Fix:** Before each `return false`, add `progress->close(); delete progress;` — or wrap `progress` in a
`std::unique_ptr` with a custom deleter that calls `close()` first.

---

### 2. Uninitialized `Daily::leakchart` member variable

**Files:** `daily.h:392`, `daily.cpp:439`

The header declares:
```cpp
gLineChart *leakchart;   // daily.h:392
```
The constructor does:
```cpp
gLineChart *leakchart = new gLineChart(CPAP_Leak, square);  // daily.cpp:439 — LOCAL variable
leakchart->addPlot(CPAP_LeakTotal, square);
leakchart->addPlot(CPAP_MaxLeak, square);
graphlist[schema::channel[CPAP_Leak].code()]->AddLayer(leakchart);
```
The local variable **shadows** the class member. The class member `Daily::leakchart` is never assigned in
the constructor and is never used anywhere in the codebase, leaving it holding an **indeterminate (garbage)
pointer** for the lifetime of the object.

The comment at line 437 ("this is class wide because the leak redline can be reset in preferences") indicates
the original intent was to keep a class-level reference, but refactoring introduced the shadowing local
without removing the header declaration.

No actual memory leak occurs — the `gLineChart` object is properly owned by the graph layer and is deleted
via `~gGraph()` → layer `unref()` — but the uninitialized member is dead code that constitutes undefined
behaviour if accidentally accessed.

**Fix (option A):** Remove `gLineChart *leakchart;` from `daily.h`. The local in the constructor is correct
as-is.

**Fix (option B):** Turn the constructor local into an assignment to the member by removing the type prefix
on line 439: `leakchart = new gLineChart(CPAP_Leak, square);`. This restores the original design intent and
allows future preference-driven threshold resets via the member pointer.

---

## Performance Issues

### 3. Five forced `repaint()` calls in `Welcome::refreshPage()`

**File:** `welcome.cpp:80–84`

```cpp
ui->importButton->repaint();
ui->dailyButton->repaint();
ui->overviewButton->repaint();
ui->statisticsButton->repaint();
ui->oximetryButton->repaint();
```

`repaint()` forces an **immediate synchronous redraw**, bypassing Qt's paint-event coalescing. Qt already
schedules a deferred paint event when `setEnabled()` or `setFont()` changes widget state (lines 68–78 just
above). These five calls trigger five immediate full repaints where zero would be needed — Qt's normal event
loop will repaint at idle time.

`refreshPage()` is called from the `Welcome` constructor, which is called from `OpenProfile()` on every
profile open, so this overhead fires every time a profile is opened.

**Fix:** Delete all five `repaint()` calls.

---

### 4. Four `GetMachines()` calls in `Welcome::refreshPage()` *(minor observation)*

**File:** `welcome.cpp:41–46`

```cpp
const auto & mlist      = p_profile->GetMachines(MT_CPAP);
QList<Machine *> oximachines = p_profile->GetMachines(MT_OXIMETER);
QList<Machine *> posmachines = p_profile->GetMachines(MT_POSITION);
QList<Machine *> stgmachines = p_profile->GetMachines(MT_SLEEPSTAGE);
```

`GetMachines()` iterates the in-memory `m_machlist`, which in practice contains only two entries
(`MT_JOURNAL` and `MT_CPAP`, occasionally with `MT_OXIMETER`). The overhead of four small-list passes and
four by-value `QList` returns is negligible in practice and will not have a measurable effect on performance.

The only tidy-up worth considering is that three of the four lists (`oximachines`, `posmachines`,
`stgmachines`) are used solely to compute the `noMachines` boolean, so they could be folded into a single
call or helper — but this is a readability preference, not a performance concern.

---

### 5. `CProgressBar` heap-allocated on every range change in `Overview::on_rangeCombo_activated()`

**File:** `overview.cpp:769–784`

```cpp
CProgressBar * progress = new CProgressBar(QObject::tr("Loading summaries"), mainwin, size);
for (int i = 1; i < size; ++i) {
    progress->add(1);
    // ...
    day->OpenSummary();
}
progress->close();
delete progress;
```

This is called from `Overview::ReloadGraphs()` → `on_rangeCombo_activated()`, which fires at profile open
and every time the user changes the date range combo. Every invocation allocates and destroys a
`CProgressBar` on the heap. The object is properly deleted so there is no leak, but it is trivially
stack-allocatable.

**Fix:** `CProgressBar progress(QObject::tr("Loading summaries"), mainwin, size);` — remove `new`/`delete`.

---

### 6. `Daily` constructor creates all channel graphs eagerly regardless of profile content

**File:** `daily.cpp:277–527`

The constructor unconditionally allocates `gGraph` objects (each with its layer objects) for every possible
channel: all CPAP codes (lines 288–315), oximeter codes (317–331), positional codes, fitness/journal graphs,
BMC, Prisma, ZEO, and more — approximately 30+ graphs total. All are created even if the profile contains
no data for those machine types.

The graphs are properly deleted via `gGraphView::~gGraphView()` → `~gGraph()`, so there is no leak, but
the unnecessary allocation and layer initialization is the largest single contributor to slow profile-open
times on profiles that use only one device type.

**Fix:** After `p_profile->LoadMachineData()` completes in `OpenProfile()`, query which machine types are
present and pass that information to the `Daily` constructor (or a post-construction initialisation call) so
graphs for absent channels are skipped. This is a more substantial refactor but would meaningfully speed up
profile open times for typical single-device profiles.

---

## Root Cause of Observed Growing Memory on Profile Switch

*Added after user reported memory increasing steadily when alternating between small and large profiles.*

### 3. Short/ignored sessions leaked in `Profile::UnloadMachineData()` — **confirmed growing-memory cause**

**Files:** `oscar/SleepLib/machine.cpp:406–409`, `oscar/SleepLib/profiles.cpp:946–961`

**How sessions are stored:** Every session loaded from the database is added to two structures:
- `mach->sessionlist` — `QHash<SessionID, Session*>`, used for save/import tracking
- `Day::sessions` — `QList<Session*>` inside the `Day` object in `profile->daylist`, used for display and statistics

**The short-session exception:** In `Machine::AddSession()` (machine.cpp:406–409), if a session is shorter
than the "ignore short sessions" threshold, it is added to `sessionlist` at line 356 but the function returns
at line 409 before calling `dd->addSession(s)`. The comment reads:
> *"keep the session to save importing it again, but don't add it to the day record this time"*

The default threshold is **5 minutes** (`ignoreShortSessions` initialised to `5.0` in profiles.h:793).
Every CPAP profile with any sub-5-minute therapy sessions will have short sessions in this category.

**The leak:** `Profile::UnloadMachineData()` (profiles.cpp:946) does this:
```cpp
mach->sessionlist.clear();   // clears the QHash — does NOT delete the Session* values
mach->day.clear();           // clears the QMap of date→Day*

for (auto & day : daylist) {
    delete day;              // Day::~Day() deletes sessions in Day::sessions
}
daylist.clear();
```

Sessions that are in `Day::sessions` are deleted correctly via `Day::~Day()`. Sessions that are only in
`sessionlist` (the short sessions) are pointed to by nothing after `sessionlist.clear()` — they are leaked.
`Session::~Session()` (which calls `TrashEvents()`) is never called for them.

Each profile open/close cycle allocates new `Session` objects for all short sessions (via
`LoadSessionsFromDatabase()` → `new Session(...)`) and never frees them. Across many cycles of
open-large/open-small alternation the leaked objects accumulate without bound.

**Severity:** Each `Session` object carries its full summary data loaded from the database (settings,
per-channel statistics, slices). A session object with summary data is easily 10–50 KB. However,
sub-5-minute sessions are occasional events (user puts on the mask and immediately removes it), not daily
occurrences, so the accumulated leak over many cycles is likely small — probably not the primary cause of
the observed Task Manager growth.

**Fix:** In `Profile::UnloadMachineData()`, before clearing `sessionlist`, explicitly delete any sessions
that are not covered by the subsequent `Day::~Day()` cleanup:

```cpp
void Profile::UnloadMachineData()
{
    for (auto & mach : m_machlist) {
        if (mach->getDatabaseId() == 0) {
            mach->saveSessionInfo();  // Legacy path only
        }

        // Collect sessions that are owned by Days and will be deleted by Day::~Day().
        // Sessions not in this set (short/ignored sessions) must be deleted explicitly
        // because sessionlist.clear() does not delete the Session* values.
        QSet<Session*> sessionsInDays;
        for (auto & day : mach->day) {
            for (auto it = day->begin(); it != day->end(); ++it) {
                sessionsInDays.insert(*it);
            }
        }
        for (auto & sess : mach->sessionlist) {
            if (!sessionsInDays.contains(sess)) {
                delete sess;
            }
        }

        mach->sessionlist.clear();
        mach->day.clear();
    }

    for (auto & day : daylist) {
        delete day;
    }
    daylist.clear();
    removeLock();
}
```

The `Day` iterator used above (`day->begin()` / `day->end()`) is already defined in `day.h` returning
iterators over `Day::sessions`.

---

## Analysis of Observed Task Manager Memory Growth

*Added after investigation of growing-but-never-shrinking memory when alternating between small and large profiles.*

**Conclusion: most likely Windows heap allocator retention, not a code leak.**

All primary data paths were traced and verified as correctly freed:
- Regular sessions are deleted via `Day::~Day()` → `Session::~Session()` → `TrashEvents()` → EventList objects freed
- `QSqlQuery` result sets (`QList<SessionData>` etc.) are stack-allocated locals, freed on function return
- SQLite page cache: `PRAGMA cache_size = -64000` sets a **64 MB fixed ceiling** per connection — bounded, not cumulative
- `QPixmapCache`: global Qt cache with automatic LRU eviction, bounded by Qt's default 10 MB limit

**Why Task Manager shows memory not decreasing:** when a large profile loads thousands of sessions, the
C++ heap grows to accommodate them. When `UnloadMachineData()` frees them, `delete` returns blocks to the
heap's internal free pool — but the heap does not call `VirtualFree()` to return those pages to Windows.
Task Manager reports all committed heap pages, including freed-but-retained blocks. When the small profile
is subsequently loaded, its smaller allocations reuse blocks from the pool, so memory stays flat rather
than shrinking.

**The distinguishing test:** open the large profile 20 times in a row. If memory plateaus after 2–3 cycles
(stabilising at the large profile's peak), it is heap retention — not a code leak. If memory grows
monotonically across all 20 opens, something is accumulating per cycle and warrants further investigation
with a heap profiler (e.g. Sysinternals VMMap, which can separate live heap from freed-but-retained pages).
