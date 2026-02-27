# Use of sessions.info

## What the file contains

`sessions.info` stores only one thing: a list of (SessionID, enabled-bit) pairs — one bit per session indicating whether the session is enabled or disabled. This was designed so that toggling a session on/off does not require rewriting the whole summary cache.

Written to: `<machine data path>/Sessions.info`

## Current status: superseded — DB is now authoritative

The cleanup described in the "Cleanup path" section below has been completed (2026-02-26).
`sessions.info` is no longer read or written for DB-backed machines. The `sessions.enabled`
column in the database is the single source of truth for enabled/disabled state.

## Load sequence (database path) — updated

1. `Machine::LoadSessionsFromDatabase()` calls `sess->LoadFromDatabase()`, which sets
   `s_enabled = sessionData.enabled` from the DB (`session.cpp`)
2. `loadSessionInfo()` is **not called** after DB load — the `sessions.info` file is ignored
   for DB-backed machines

## Save side — updated

- `Session::setEnabled()` (`session.cpp`) now writes directly to the DB via
  `SessionRepository::updateEnabled()` whenever the value changes and the session is
  DB-backed (`m_sessionrow_id > 0`)
- `saveSessionInfo()` is only called from `Profile::UnloadMachineData()` for legacy machines
  that have `getDatabaseId() == 0`

## Legacy / file-based path

For machines not in the database (`m_database_id == 0`), the old behaviour is unchanged:
`loadSessionInfo()` is called during load and `saveSessionInfo()` is called on unload.
Existing `sessions.info` files on disk are left in place and simply ignored for DB-backed
machines — no migration is needed.

## Prior state (for historical reference)

Prior to 2026-02-26, `sessions.info` was the authoritative runtime source of truth:
- `Session::setEnabled()` only updated an in-memory flag
- `loadSessionInfo()` overwrote the DB-loaded value on every load
- `saveSessionInfo()` wrote the file on every unload
- The DB `enabled` column drifted after any session was toggled by the user
