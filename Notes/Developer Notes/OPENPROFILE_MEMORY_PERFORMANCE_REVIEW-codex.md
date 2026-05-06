# OpenProfile Memory and Performance Review -codex

Date: 2026-04-17
Scope: `MainWindow::OpenProfile()` and functions used while opening/closing profiles and building Welcome/Daily/Overview/Statistics pages.

## Findings (ordered by severity)

1. High: early-return paths in `OpenProfile()` can leak objects and leave partially-open state.

- Evidence:
  - `p_profile` is assigned before UI page construction and before all guard checks.
  - `ProgressDialog *progress = new ProgressDialog(this);` is manually deleted only on the normal success path.
  - `OpenProfile()` can return `false` after allocations:
    - Daily guard: `oscar/mainwindow.cpp:617-621`
    - Overview guard: `oscar/mainwindow.cpp:638-642`
  - In the overview guard path, `welcome` and `daily` have already been created.
- Impact:
  - Memory leak risk for `progress`.
  - Partial initialization risk (stale `p_profile`, partially added tabs, lock held) after aborted open.
- Recommended solution:
  - Use RAII for `ProgressDialog` (stack object or smart pointer).
  - Move all "already-active object" guards before expensive work and allocations.
  - Funnel all failures through one cleanup block that reverts state or calls `CloseProfile()`.

2. High: statistics generation is triggered during Daily load, making profile open heavier than needed.

- Evidence:
  - `OpenProfile()` calls `daily->ReloadGraphs()` immediately.
  - `Daily::Load()` calls `mainwin->GenerateStatistics()` unconditionally (`oscar/daily.cpp:1977-1980`).
  - `GenerateStatistics()` rebuilds statistics HTML, records box, and favorites (`oscar/mainwindow.cpp:2641-2653`).
- Impact:
  - Profile-open path pays full statistics cost even when user starts on Welcome or Daily.
- Recommended solution:
  - Lazy-load statistics on first Statistics tab visit.
  - Add a dirty-flag and debounce updates after imports/settings changes.

3. High: RX cache refresh (`updateRXChanges`) is expensive and called every statistics generation.

- Evidence:
  - `GenerateRXChanges()` always calls `updateRXChanges()` (`oscar/statistics.cpp:1244`).
  - `updateRXChanges()` scans full `daylist` and also repeatedly scans `rxitems` inside the loop (`oscar/statistics.cpp:365-688`).
- Impact:
  - High CPU time for large profiles.
  - Can dominate open-time because statistics is triggered while opening.
- Recommended solution:
  - Incremental RX cache maintenance keyed by import/purge/settings-change events.
  - Rebuild only affected ranges instead of rescanning all historical days.

4. Medium: Overview preload work is done immediately during open, even if user does not view Overview.

- Evidence:
  - `OpenProfile()` always calls `overview->ReloadGraphs()` (`oscar/mainwindow.cpp:652-654`).
  - `Overview::on_rangeCombo_activated()` iterates through date range and calls `day->OpenSummary()` with progress (`oscar/overview.cpp:769-782`).
- Impact:
  - Startup latency for users who do not need Overview immediately.
- Recommended solution:
  - Delay `overview->ReloadGraphs()` until first Overview tab activation.
  - Optional: async preload with cancellation.

5. Medium: repeated redraw calls in Overview range-change loop cause extra rendering work.

- Evidence:
  - In `Overview::on_XBoundsChanged()`, when larger range is detected, loop does:
    - `sc->reCalculate();`
    - `GraphView->updateScale();`
    - `GraphView->redraw();`
    - `GraphView->timedRedraw(150);`
  - This is done inside the loop for each chart (`oscar/overview.cpp:607-615`).
- Impact:
  - Multiple full redraw/scale passes for one user action.
- Recommended solution:
  - Recalculate all needed charts first, then perform one `updateScale/redraw` at the end.

6. Medium: favorites rebuild scans journal history on each statistics generation.

- Evidence:
  - `GenerateStatistics()` always calls `updateFavourites()`.
  - `updateFavourites()` loops from last to first journal day and builds full HTML (`oscar/mainwindow.cpp:1427-1496`).
- Impact:
  - Unnecessary repeated work during profile open and other stats refreshes.
- Recommended solution:
  - Refresh favorites only on bookmark/filter changes.
  - Cache generated favorites HTML and invalidate on relevant journal mutations.

## Additional note

No other definite memory leak was confirmed in this open-profile path during this review.
The strongest concrete leak/consistency issue is the `OpenProfile()` early-return cleanup gap described in finding #1.

## Follow-up based on repeated small/large profile switching tests

You reported this test pattern:

- open small profile
- open large profile
- open small profile again
- repeat many times
- observe Task Manager memory slowly increasing and not returning to original baseline

This symptom can be caused by true leaks, but it can also be caused by retained caches and allocator high-water behavior.

### Most relevant retention sources found in current code

1. SQLite page/temp caching is configured for performance:
   - `PRAGMA cache_size = -64000` (about 64 MB target)
   - `PRAGMA temp_store = MEMORY`
   - Reference: `oscar/database/database_manager.cpp:456-464`

2. Graph/text rendering uses Qt pixmap caching in graph views:
   - Graph view maintains pixmap cache and also uses `QPixmapCache`
   - Reference: `oscar/Graphs/gGraphView.cpp`, e.g. cache clear in close path at `:751`

These can legitimately keep process memory elevated after work has completed.

### Distinguishing leak vs retention (recommended validation)

1. Monitor `Private Bytes` / Commit size in addition to Working Set.
2. Add temporary lifecycle counters and log them after each `CloseProfile()`:
   - `Day` instances
   - `Session` instances
   - `EventList` instances
3. Add an optional diagnostic cleanup pass after close:
   - call SQLite `PRAGMA shrink_memory`
   - clear pixmap/text caches
4. Re-run the same loop and compare:
   - If memory drops significantly after forced cache cleanup, behavior is mostly retention.
   - If object counters or commit continue rising despite cleanup, likely leak.

### Confirmed leak-risk still applies

The concrete leak/consistency risk previously found remains valid:

- `OpenProfile()` has early return paths after allocations/partial initialization.
- References: `oscar/mainwindow.cpp:554`, `:617-621`, `:638-642`.
