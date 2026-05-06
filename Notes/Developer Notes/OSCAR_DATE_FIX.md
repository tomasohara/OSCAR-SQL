# OSCAR Date Handling Fix

**Date:** 2026-02-28

## Background

An OSCAR date is not the same as a calendar date.  An OSCAR day begins at
**noon local time** on the named date and ends at noon local time the
following calendar day.  The standard SQL idiom for converting a
`start_time` (epoch milliseconds) to an OSCAR date is:

```sql
date(start_time/1000 - 43200, 'unixepoch', 'localtime')
```

The `-43200` shifts the epoch back by 12 hours so that the subsequent
`'localtime'` conversion and `date()` truncation land on the correct
OSCAR date.  Equivalently, an OSCAR date `D` spans:

```
[ D 12:00:00 local,  D+1 12:00:00 local )
```

## Files Changed

### 1. `oscar/backupdialog.cpp`

**Functions:** `getFirstDataDate()`, `getLastDataDate()`

Both SQL queries were computing a plain calendar date:

```sql
DATE(start_time/1000, 'unixepoch')        -- wrong
```

Fixed to return the correct OSCAR date:

```sql
DATE(start_time/1000 - 43200, 'unixepoch', 'localtime')   -- correct
```

Without this fix, the range presets ("Last Week", "Most Recent Day", etc.)
could anchor to the wrong day for any session that started before noon.

---

### 2. `oscar/database/backup/profile_backup.cpp`

**Function:** `buildSessionDateFilter()`

This function converts the user-supplied OSCAR-date `QDate` values into
epoch-millisecond boundaries for the sessions `WHERE` clause.  The old code
used **midnight UTC** as the boundary:

```cpp
// old — wrong
m_startDate.startOfDay(Qt::UTC).toMSecsSinceEpoch()          // lower
m_endDate.addDays(1).startOfDay(Qt::UTC).toMSecsSinceEpoch() // upper
```

Because an OSCAR day starts at noon local time (not midnight UTC), sessions
starting between midnight and noon local time on the first or last day of
the range could be silently excluded or included.

Fixed to use **noon local time**:

```cpp
// new — correct
QDateTime(m_startDate,          QTime(12,0,0), Qt::LocalTime).toMSecsSinceEpoch()  // lower
QDateTime(m_endDate.addDays(1), QTime(12,0,0), Qt::LocalTime).toMSecsSinceEpoch()  // upper
```

The `QDateTime(date, time, Qt::LocalTime)` constructor is available in both
Qt 5 and Qt 6, so the `#if QT_VERSION` guards were removed.

---

### 3. `oscar/docs/system_reports.orf`

**Reports:** Session Summaries/by Day, Session Summaries/by Week,
Session Summaries/by Month

Each report's `WHERE` clause already used the correct `-43200` OSCAR-date
idiom.  However, the `Period` column in `SELECT` and the `GROUP BY`
expression both omitted the offset:

```sql
-- old — wrong
date(s.start_time/1000, 'unixepoch', 'localtime') as Period
GROUP BY date(s.start_time/1000, 'unixepoch', 'localtime')
```

Sessions starting between midnight and noon local time were therefore
filtered by one OSCAR date but grouped and displayed under the next
calendar date.  Fixed by applying `-43200` consistently:

```sql
-- new — correct
date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') as Period
GROUP BY date(s.start_time/1000 - 43200, 'unixepoch', 'localtime')
```

The same fix was applied to the `strftime(...)` calls used as the `Period`
and `GROUP BY` in the by-Week and by-Month reports.

## Rule of Thumb

Whenever a SQL expression maps `sessions.start_time` to an OSCAR date,
always use:

```sql
date(start_time/1000 - 43200, 'unixepoch', 'localtime')
```

Whenever C++ code converts an OSCAR `QDate` to an epoch boundary, use
noon local time:

```cpp
QDateTime(oscarDate,          QTime(12,0,0), Qt::LocalTime)  // start of OSCAR day
QDateTime(oscarDate.addDays(1), QTime(12,0,0), Qt::LocalTime)  // end of OSCAR day (exclusive)
```
