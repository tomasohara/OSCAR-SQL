# RXChanges.cache File Use

## What it is

`RXChanges.cache` is a binary QDataStream performance cache for the **Device Settings Changes**
section of the Statistics page (originally called "Prescription Changes" — hence "RX").

File: `Profiles/<profile name>/RXChanges.cache`

## What it stores

A `QMap<QDate, RXItem>` — one entry per contiguous period during which the machine was used with
the same device settings. Each `RXItem` holds:

- Date range (`start`, `end`), day count, which machine
- Mode, pressure, and pressure-relief strings (the key that defines a "period")
- Aggregated AHI, RDI, and hours for the whole period
- Per-channel event flag counts and sums (`s_count`, `s_sum`)
- The actual `Day*` pointers for every date in the range

## How it works

`Statistics::updateRXChanges()` is called on every Statistics page render. It:

1. Loads the existing cache from disk
2. Scans all days in the profile in date order
3. For each day, either slots it into an existing matching period (same mode/pressure/relief/machine),
   or creates a new `RXItem` entry — splitting an existing one if the settings changed mid-range
4. Saves the updated cache back to disk

The rendered output is the "Device Settings Changes" table in the Statistics HTML report, showing
each settings period with its AHI, hours used, and event counts.

## Cache invalidation

The file is deleted (forcing a full rebuild on next Statistics view) in three places:

- Session deletion — `mainwindow.cpp:1965`
- Machine data reimport/reload — `mainwindow.cpp:2195`
- Machine data purge — `machine.cpp:546`

## Status

Actively used. Pure performance cache — the Statistics page would still work without it but would
recompute aggregated stats across all days on every view. Not needed in backups.
