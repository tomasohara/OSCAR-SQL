cont# Daily Summaries Implementation Guide

## Status: Schema v6 - In Progress

This document provides the complete implementation for the daily_summaries table feature.

---

## Implementation Checklist

- [x] Updated schema version to 6 in database_schema.h
- [x] Added createDailySummariesTable declaration
- [x] Added table creation call in createSchema()
- [ ] Implement upgradeSchema v5->v6
- [ ] Implement createDailySummariesTable()
- [ ] Add daily_summaries indexes to createIndexes()
- [ ] Create daily_summary_repository.h
- [ ] Create daily_summary_repository.cpp
- [ ] Add files to oscar.pro
- [ ] Test compilation

---

## Code Additions Required

### 1. Add to database_schema.cpp - upgradeSchema() function

Insert after the v4->v5 upgrade block:

```cpp
// Upgrade from version 5 to version 6: Add daily_summaries table
if (fromVersion < 6) {
    qDebug() << "DatabaseSchema: Applying version 6 upgrade (daily summaries table)";
    
    if (!createDailySummariesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create daily_summaries table during upgrade";
        return false;
    }
    
    // Recreate indexes to include new daily_summaries indexes
    if (!createIndexes(db)) {
        qCritical() << "DatabaseSchema: Failed to create indexes during upgrade";
        return false;
    }
    
    // Update schema version
    if (!setSchemaVersion(db, 6)) {
        qCritical() << "DatabaseSchema: Failed to update schema version to 6";
        return false;
    }
    
    qDebug() << "DatabaseSchema: Successfully upgraded to version 6";
}
```

### 2. Implement createDailySummariesTable() in database_schema.cpp

Add at end of file before closing:

```cpp
/*
 * Create the daily_summaries table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The daily_summaries table stores cached daily aggregate statistics
 * for fast report generation without loading individual sessions.
 */
bool DatabaseSchema::createDailySummariesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS daily_summaries ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL,"
        "    date TEXT NOT NULL,"
        "    machine_id INTEGER,"
        "    "
        "    session_count INTEGER DEFAULT 0,"
        "    enabled_session_count INTEGER DEFAULT 0,"
        "    "
        "    total_hours REAL DEFAULT 0,"
        "    mask_on_hours REAL DEFAULT 0,"
        "    "
        "    ahi REAL DEFAULT 0,"
        "    rdi REAL DEFAULT 0,"
        "    obstructive_count INTEGER DEFAULT 0,"
        "    central_count INTEGER DEFAULT 0,"
        "    hypopnea_count INTEGER DEFAULT 0,"
        "    rera_count INTEGER DEFAULT 0,"
        "    clear_airway_count INTEGER DEFAULT 0,"
        "    "
        "    pressure_avg REAL,"
        "    pressure_min REAL,"
        "    pressure_max REAL,"
        "    pressure_95th REAL,"
        "    "
        "    leak_total_avg REAL,"
        "    leak_total_95th REAL,"
        "    leak_total_max REAL,"
        "    leak_unintentional_avg REAL,"
        "    "
        "    spo2_avg REAL,"
        "    spo2_min REAL,"
        "    pulse_avg REAL,"
        "    pulse_min REAL,"
        "    pulse_max REAL,"
        "    "
        "    is_compliant INTEGER DEFAULT 0,"
        "    has_oximetry INTEGER DEFAULT 0,"
        "    "
        "    calculated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    sessions_hash TEXT,"
        "    "
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE SET NULL,"
        "    UNIQUE(profile_id, date, machine_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create daily_summaries table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: daily_summaries table created";
    return true;
}
```

### 3. Add indexes to createIndexes() function

Add these lines to the indexes list before the "Execute each index creation" comment:

```cpp
// Daily summaries indexes (schema version 6)
indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_profile_date ON daily_summaries(profile_id, date)";
indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_profile_machine ON daily_summaries(profile_id, machine_id, date)";
indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_ahi ON daily_summaries(ahi)";
indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_compliance ON daily_summaries(profile_id, is_compliant)";
indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_date_range ON daily_summaries(profile_id, date DESC)";
```

---

## Repository Files to Create

### daily_summary_repository.h

```cpp
/* Daily Summary Repository Header
 *
 * Copyright (c) 2025 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAILY_SUMMARY_REPOSITORY_H
#define DAILY_SUMMARY_REPOSITORY_H

#include <QSqlDatabase>
#include <QString>
#include <QDate>
#include <QList>

struct DailySummaryData {
    qint64 id = 0;
    qint64 profileId = 0;
    QString date;  // ISO format YYYY-MM-DD
    qint64 machineId = 0;  // 0 = combined all machines
    
    // Session counts
    int sessionCount = 0;
    int enabledSessionCount = 0;
    
    // Time metrics
    double totalHours = 0.0;
    double maskOnHours = 0.0;
    
    // Respiratory events
    double ahi = 0.0;
    double rdi = 0.0;
    int obstructiveCount = 0;
    int centralCount = 0;
    int hypopneaCount = 0;
    int reraCount = 0;
    int clearAirwayCount = 0;
    
    // Pressure statistics
    double pressureAvg = 0.0;
    double pressureMin = 0.0;
    double pressureMax = 0.0;
    double pressure95th = 0.0;
    
    // Leak statistics
    double leakTotalAvg = 0.0;
    double leakTotal95th = 0.0;
    double leakTotalMax = 0.0;
    double leakUnintentionalAvg = 0.0;
    
    // Oximetry (if available)
    double spo2Avg = 0.0;
    double spo2Min = 0.0;
    double pulseAvg = 0.0;
    double pulseMin = 0.0;
    double pulseMax = 0.0;
    
    // Flags
    bool isCompliant = false;
    bool hasOximetry = false;
    
    // Metadata
    QString calculatedAt;
    QString sessionsHash;
};

class DailySummaryRepository
{
public:
    DailySummaryRepository();
    explicit DailySummaryRepository(const QSqlDatabase& database);
    
    // CRUD operations
    qint64 create(const DailySummaryData& data);
    bool update(const DailySummaryData& data);
    bool remove(qint64 id);
    
    // Query methods
    DailySummaryData findById(qint64 id);
    DailySummaryData findByProfileAndDate(qint64 profileId, const QDate& date, qint64 machineId = 0);
    QList<DailySummaryData> findByProfile(qint64 profileId);
    QList<DailySummaryData> findRange(qint64 profileId, const QDate& startDate, const QDate& endDate, qint64 machineId = 0);
    
    // Calculation methods (to be implemented later)
    bool calculateAndStore(qint64 profileId, const QDate& date, qint64 machineId = 0);
    bool calculateRange(qint64 profileId, const QDate& startDate, const QDate& endDate);
    
    // Cache invalidation
    bool invalidateDate(qint64 profileId, const QDate& date);
    bool invalidateRange(qint64 profileId, const QDate& startDate, const QDate& endDate);
    
    // Utility methods
    bool exists(qint64 profileId, const QDate& date, qint64 machineId = 0);
    int countDays(qint64 profileId, const QDate& startDate, const QDate& endDate);
    
private:
    QSqlDatabase db;
    
    DailySummaryData mapResultToData(const QSqlQuery& query);
    QString generateSessionsHash(qint64 profileId, const QDate& date);
};

#endif // DAILY_SUMMARY_REPOSITORY_H
```

### daily_summary_repository.cpp

```cpp
/* Daily Summary Repository Implementation
 *
 * Copyright (c) 2025 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "daily_summary_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

DailySummaryRepository::DailySummaryRepository()
    : db(DatabaseManager::instance().database())
{
}

DailySummaryRepository::DailySummaryRepository(const QSqlDatabase& database)
    : db(database)
{
}

qint64 DailySummaryRepository::create(const DailySummaryData& data)
{
    QSqlQuery query(db);
    
    query.prepare(R"(
        INSERT INTO daily_summaries (
            profile_id, date, machine_id,
            session_count, enabled_session_count,
            total_hours, mask_on_hours,
            ahi, rdi, obstructive_count, central_count, hypopnea_count, rera_count, clear_airway_count,
            pressure_avg, pressure_min, pressure_max, pressure_95th,
            leak_total_avg, leak_total_95th, leak_total_max, leak_unintentional_avg,
            spo2_avg, spo2_min, pulse_avg, pulse_min, pulse_max,
            is_compliant, has_oximetry, sessions_hash
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    
    query.addBindValue(data.profileId);
    query.addBindValue(data.date);
    query.addBindValue(data.machineId > 0 ? data.machineId : QVariant());
    query.addBindValue(data.sessionCount);
    query.addBindValue(data.enabledSessionCount);
    query.addBindValue(data.totalHours);
    query.addBindValue(data.maskOnHours);
    query.addBindValue(data.ahi);
    query.addBindValue(data.rdi);
    query.addBindValue(data.obstructiveCount);
    query.addBindValue(data.centralCount);
    query.addBindValue(data.hypopneaCount);
    query.addBindValue(data.reraCount);
    query.addBindValue(data.clearAirwayCount);
    query.addBindValue(data.pressureAvg);
    query.addBindValue(data.pressureMin);
    query.addBindValue(data.pressureMax);
    query.addBindValue(data.pressure95th);
    query.addBindValue(data.leakTotalAvg);
    query.addBindValue(data.leakTotal95th);
    query.addBindValue(data.leakTotalMax);
    query.addBindValue(data.leakUnintentionalAvg);
    query.addBindValue(data.spo2Avg);
    query.addBindValue(data.spo2Min);
    query.addBindValue(data.pulseAvg);
    query.addBindValue(data.pulseMin);
    query.addBindValue(data.pulseMax);
    query.addBindValue(data.isCompliant);
    query.addBindValue(data.hasOximetry);
    query.addBindValue(data.sessionsHash);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::create failed:" << query.lastError().text();
        return -1;
    }
    
    return query.lastInsertId().toLongLong();
}

bool DailySummaryRepository::update(const DailySummaryData& data)
{
    QSqlQuery query(db);
    
    query.prepare(R"(
        UPDATE daily_summaries SET
            profile_id = ?, date = ?, machine_id = ?,
            session_count = ?, enabled_session_count = ?,
            total_hours = ?, mask_on_hours = ?,
            ahi = ?, rdi = ?, obstructive_count = ?, central_count = ?, 
            hypopnea_count = ?, rera_count = ?, clear_airway_count = ?,
            pressure_avg = ?, pressure_min = ?, pressure_max = ?, pressure_95th = ?,
            leak_total_avg = ?, leak_total_95th = ?, leak_total_max = ?, leak_unintentional_avg = ?,
            spo2_avg = ?, spo2_min = ?, pulse_avg = ?, pulse_min = ?, pulse_max = ?,
            is_compliant = ?, has_oximetry = ?, sessions_hash = ?,
            calculated_at = CURRENT_TIMESTAMP
        WHERE id = ?
    )");
    
    query.addBindValue(data.profileId);
    query.addBindValue(data.date);
    query.addBindValue(data.machineId > 0 ? data.machineId : QVariant());
    query.addBindValue(data.sessionCount);
    query.addBindValue(data.enabledSessionCount);
    query.addBindValue(data.totalHours);
    query.addBindValue(data.maskOnHours);
    query.addBindValue(data.ahi);
    query.addBindValue(data.rdi);
    query.addBindValue(data.obstructiveCount);
    query.addBindValue(data.centralCount);
    query.addBindValue(data.hypopnea_count);
    query.addBindValue(data.reraCount);
    query.addBindValue(data.clearAirwayCount);
    query.addBindValue(data.pressureAvg);
    query.addBindValue(data.pressureMin);
    query.addBindValue(data.pressureMax);
    query.addBindValue(data.pressure95th);
    query.addBindValue(data.leakTotalAvg);
    query.addBindValue(data.leakTotal95th);
    query.addBindValue(data.leakTotalMax);
    query.addBindValue(data.leakUnintentionalAvg);
    query.addBindValue(data.spo2Avg);
    query.addBindValue(data.spo2Min);
    query.addBindValue(data.pulseAvg);
    query.addBindValue(data.pulseMin);
    query.addBindValue(data.pulseMax);
    query.addBindValue(data.isCompliant);
    query.addBindValue(data.hasOximetry);
    query.addBindValue(data.sessionsHash);
    query.addBindValue(data.id);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::update failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool DailySummaryRepository::remove(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM daily_summaries WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::remove failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

DailySummaryData DailySummaryRepository::findById(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("SELECT * FROM daily_summaries WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findById failed:" << query.lastError().text();
        return DailySummaryData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return DailySummaryData();
}

DailySummaryData DailySummaryRepository::findByProfileAndDate(qint64 profileId, const QDate& date, qint64 machineId)
{
    QSqlQuery query(db);
    
    if (machineId > 0) {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id = ?");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
        query.addBindValue(machineId);
    } else {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id IS NULL");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
    }
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findByProfileAndDate failed:" << query.lastError().text();
        return DailySummaryData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return DailySummaryData();
}

QList<DailySummaryData> DailySummaryRepository::findByProfile(qint64 profileId)
{
    QList<DailySummaryData> summaries;
    
    QSqlQuery query(db);
    query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? ORDER BY date DESC");
    query.addBindValue(profileId);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findByProfile failed:" << query.lastError().text();
        return summaries;
    }
    
    while (query.next()) {
        summaries.append(mapResultToData(query));
    }
    
    return summaries;
}

QList<DailySummaryData> DailySummaryRepository::findRange(qint64 profileId, const QDate& startDate, const QDate& endDate, qint64 machineId)
{
    QList<DailySummaryData> summaries;
    
    QSqlQuery query(db);
    
    if (machineId > 0) {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ? AND machine_id = ? ORDER BY date");
        query.addBindValue(profileId);
        query.addBindValue(startDate.toString(Qt::ISODate));
        query.addBindValue(endDate.toString(Qt::ISODate));
        query.addBindValue(machineId);
    } else {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ? AND machine_id IS NULL ORDER BY date");
        query.addBindValue(profileId);
        query.addBindValue(startDate.toString(Qt::ISODate));
        query.addBindValue(endDate.toString(Qt::ISODate));
    }
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findRange failed:" << query.lastError().text();
        return summaries;
    }
    
    while (query.next()) {
        summaries.append(mapResultToData(query));
    }
    
    return summaries;
}

bool DailySummaryRepository::calculateAndStore(qint64 profileId, const QDate& date, qint64 machineId)
{
    // TODO: Implement calculation logic
    // This will aggregate session data for the given date
    qDebug() << "DailySummaryRepository::calculateAndStore - Not yet implemented";
    return false;
}

bool DailySummaryRepository::calculateRange(qint64 profileId, const QDate& startDate, const QDate& endDate)
{
    // TODO: Implement bulk calculation
    qDebug() << "DailySummaryRepository::calculateRange - Not yet implemented";
    return false;
}

bool DailySummaryRepository::invalidateDate(qint64 profileId, const QDate& date)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM daily_summaries WHERE profile_id = ? AND date = ?");
    query.addBindValue(profileId);
    query.addBindValue(date.toString(Qt::ISODate));
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::invalidateDate failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool DailySummaryRepository::invalidateRange(qint64 profileId, const QDate& startDate, const QDate& endDate)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ?");
    query.addBindValue(profileId);
    query.addBindValue(startDate.toString(Qt::ISODate));
    query.addBindValue(endDate.toString(Qt::ISODate));
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::invalidateRange failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool DailySummaryRepository::exists(qint64 profileId, const QDate& date, qint64 machineId)
{
    QSqlQuery query(db);
    
    if (machineId > 0) {
        query.prepare("SELECT COUNT(*) FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id = ?");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
        query.addBindValue(machineId);
    } else {
        query.prepare("SELECT COUNT(*) FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id IS NULL");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
    }
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::exists failed:" << query.lastError().text();
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

int DailySummaryRepository::countDays(qint64 profileId, const QDate& startDate, const QDate& endDate)
{
    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ?");
    query.addBindValue(profileId);
    query.addBindValue(startDate.toString(Qt::ISODate));
    query.addBindValue(endDate.toString(Qt::ISODate));
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::countDays failed:" << query.lastError().text();
        return 0;
    }
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

QString DailySummaryRepository::generateSessionsHash(qint64 profileId, const QDate& date)
{
    // TODO: Implement hash generation based on session IDs
    // This is used for cache invalidation
    return QString();
}

DailySummaryData DailySummaryRepository::mapResultToData(const QSqlQuery& query)
{
    DailySummaryData data;
    
    data.id = query.value("id").toLongLong();
    data.profileId = query.value("profile_id").toLongLong();
    data.date = query.value("date").toString();
    data.machineId = query.value("machine_id").toLongLong();
    
    data.sessionCount = query.value("session_count").toInt();
    data.enabledSessionCount = query.value("enabled_session_count").toInt();
    
    data.totalHours = query.value("total_hours").toDouble();
    data.maskOnHours = query.value("mask_on_hours").toDouble();
    
    data.ahi = query.value("ahi").toDouble();
    data.rdi = query.value("rdi").toDouble();
    data.obstructiveCount = query.value("obstructive_count").toInt();
    data.centralCount = query.value("central_count").toInt();
    data.hypopneaCount = query.value("hypopnea_count").toInt();
    data.reraCount = query.value("rera_count").toInt();
    data.clearAirwayCount = query.value("clear_airway_count").toInt();
    
    data.pressureAvg = query.value("pressure_avg").toDouble();
    data.pressureMin = query.value("pressure_min").toDouble();
    data.pressureMax = query.value("pressure_max").toDouble();
    data.pressure95th = query.value("pressure_95th").toDouble();
    
    data.leakTotalAvg = query.value("leak_total_avg").toDouble();
    data.leakTotal95th = query.value("leak_total_95th").toDouble();
    data.leakTotalMax = query.value("leak_total_max").toDouble();
    data.leakUnintentionalAvg = query.value("leak_unintentional_avg").toDouble();
    
    data.spo2Avg = query.value("spo2_avg").toDouble();
    data.spo2Min = query.value("spo2_min").toDouble();
    data.pulseAvg = query.value("pulse_avg").toDouble();
    data.pulseMin = query.value("pulse_min").toDouble();
    data.pulseMax = query.value("pulse_max").toDouble();
    
    data.isCompliant = query.value("is_compliant").toBool();
    data.hasOximetry = query.value("has_oximetry").toBool();
    
    data.calculatedAt = query.value("calculated_at").toString();
    data.sessionsHash = query.value("sessions_hash").toString();
    
    return data;
}
```

---

## Add to oscar.pro

In SOURCES section, add:
```qmake
database/daily_summary_repository.cpp \
```

In HEADERS section, add:
```qmake
database/daily_summary_repository.h \
```

---

## Testing

After implementation:

1. **Compile test:**
```bash
qmake oscar.pro
make
```

2. **Test table creation:**
- Delete existing oscar.db
- Run OSCAR
- Check debug log for "daily_summaries table created"

3. **Test schema upgrade:**
- Keep oscar.db with schema v5
- Run OSCAR
- Check debug log for "Successfully upgraded to version 6"

4. **Verify table structure:**
```sql
sqlite3 oscar.db
.schema daily_summaries
```

---

## Future Work (Phase 2)

After basic implementation works:

1. Implement `calculateAndStore()` - aggregate session data for a date
2. Implement `calculateRange()` - bulk calculation for date ranges
3. Hook into Session::Save() to invalidate/recalculate on data changes
4. Update Overview screen to use daily_summaries
5. Update Statistics reports to use daily_summaries
6. Add background recalculation for existing data

---

## Performance Target

- Overview (90 days): < 100ms (currently 2-3 seconds)
- Statistics (1 year): < 200ms (currently 5-10 seconds)
- Calendar population: 1 query vs N queries

---

**Status:** Schema v6 table structure ready, repository class ready, calculation logic pending.
