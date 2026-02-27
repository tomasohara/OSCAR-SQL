# Use of sessions.info

## What the file contains

`sessions.info` stores only one thing: a list of (SessionID, enabled-bit) pairs — one bit per session indicating whether the session is enabled or disabled. This was designed so that toggling a session on/off does not require rewriting the whole summary cache.

Written to: `<machine data path>/Sessions.info`

## Current status: actively used and authoritative

Despite the database `sessions` table having an `enabled` column (with its own index `idx_sessions_enabled`), `sessions.info` is the actual authoritative source for session enabled/disabled state.

## Load sequence (database path)

1. `Machine::LoadSessionsFromDatabase()` calls `sess->LoadFromDatabase()`, which sets `s_enabled = sessionData.enabled` from the DB (`session.cpp:3069`)
2. Immediately after, `loadSessionInfo()` is called (`machine.cpp:713`), reads `sessions.info`, and calls `sess->setEnabled(b)` for each session — **overwriting** the value just loaded from the DB

So `sessions.info` wins over the DB on every load.

## Save side

- `Session::setEnabled()` (`session.cpp:114`) only updates the in-memory flag — it never touches the database
- `saveSessionInfo()` is called from `Profile::UnloadMachineData()` (`profiles.cpp:949`) on every unload, keeping `sessions.info` current
- Nothing ever updates `sessions.enabled` in the DB after initial import

## Consequence: DB `enabled` column is stale

The DB's `enabled` column is correct at import time but drifts after any session is toggled by the user. `SessionRepository::findEnabledByMachine()` (which filters `WHERE enabled = 1`) is therefore unreliable for users who have toggled sessions, and is not used in the main load path — `Machine::LoadSessionsFromDatabase()` calls `findByMachine()` (no filter).

## Cleanup path (future work)

To eliminate `sessions.info` and make the DB the single source of truth:

1. Make `Session::setEnabled()` issue an `UPDATE sessions SET enabled=? WHERE id=?` to the DB
2. Remove the `loadSessionInfo()` call after DB load in `Machine::Load()`
3. Remove the `saveSessionInfo()` call from `Profile::UnloadMachineData()`
4. The `sessions.info` files on disk can then be ignored (no migration needed — the DB values will be correct once writes go there)
