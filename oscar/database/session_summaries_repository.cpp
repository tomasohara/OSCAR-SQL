/* Session Summaries Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the SessionSummariesRepository class for database
 * access to session summaries (cached high-level metrics like AHI).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_summaries_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SessionSummariesRepository::SessionSummariesRepository()
{
}

SessionSummariesRepository::~SessionSummariesRepository()
{
}

qint64 SessionSummariesRepository::create(const SessionSummaryData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSummariesRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_summaries "
        "(session_id, ahi, rdi, obstructive_count, central_count, hypopnea_count, rera_count, "
        " pressure_avg, pressure_min, pressure_max, pressure_95th, "
        " leak_total_avg, leak_total_95th, leak_total_max, "
        " spo2_avg, spo2_min, pulse_avg, hours_used, mask_on_hours) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.ahi);
    query.addBindValue(data.rdi);
    query.addBindValue(data.obstructiveCount);
    query.addBindValue(data.centralCount);
    query.addBindValue(data.hypopneaCount);
    query.addBindValue(data.reraCount);
    query.addBindValue(data.pressureAvg);
    query.addBindValue(data.pressureMin);
    query.addBindValue(data.pressureMax);
    query.addBindValue(data.pressure95th);
    query.addBindValue(data.leakTotalAvg);
    query.addBindValue(data.leakTotal95th);
    query.addBindValue(data.leakTotalMax);
    query.addBindValue(data.spo2Avg);
    query.addBindValue(data.spo2Min);
    query.addBindValue(data.pulseAvg);
    query.addBindValue(data.hoursUsed);
    query.addBindValue(data.maskOnHours);

    if (!query.exec()) {
        qWarning() << "SessionSummariesRepository::create() failed:" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool SessionSummariesRepository::update(const SessionSummaryData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSummariesRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE session_summaries SET "
        "ahi = ?, rdi = ?, obstructive_count = ?, central_count = ?, "
        "hypopnea_count = ?, rera_count = ?, pressure_avg = ?, pressure_min = ?, "
        "pressure_max = ?, pressure_95th = ?, leak_total_avg = ?, leak_total_95th = ?, "
        "leak_total_max = ?, spo2_avg = ?, spo2_min = ?, pulse_avg = ?, "
        "hours_used = ?, mask_on_hours = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );

    query.addBindValue(data.ahi);
    query.addBindValue(data.rdi);
    query.addBindValue(data.obstructiveCount);
    query.addBindValue(data.centralCount);
    query.addBindValue(data.hypopneaCount);
    query.addBindValue(data.reraCount);
    query.addBindValue(data.pressureAvg);
    query.addBindValue(data.pressureMin);
    query.addBindValue(data.pressureMax);
    query.addBindValue(data.pressure95th);
    query.addBindValue(data.leakTotalAvg);
    query.addBindValue(data.leakTotal95th);
    query.addBindValue(data.leakTotalMax);
    query.addBindValue(data.spo2Avg);
    query.addBindValue(data.spo2Min);
    query.addBindValue(data.pulseAvg);
    query.addBindValue(data.hoursUsed);
    query.addBindValue(data.maskOnHours);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "SessionSummariesRepository::update() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

SessionSummaryData SessionSummariesRepository::findBySession(qint64 sessionId)
{
    SessionSummaryData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSummariesRepository::findBySession() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, ahi, rdi, obstructive_count, central_count, "
        "hypopnea_count, rera_count, pressure_avg, pressure_min, pressure_max, pressure_95th, "
        "leak_total_avg, leak_total_95th, leak_total_max, spo2_avg, spo2_min, pulse_avg, "
        "hours_used, mask_on_hours, created_at, updated_at "
        "FROM session_summaries WHERE session_id = ?"
    );
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSummariesRepository::findBySession() failed:" << query.lastError().text();
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.ahi = query.value(2).toDouble();
        data.rdi = query.value(3).toDouble();
        data.obstructiveCount = query.value(4).toInt();
        data.centralCount = query.value(5).toInt();
        data.hypopneaCount = query.value(6).toInt();
        data.reraCount = query.value(7).toInt();
        data.pressureAvg = query.value(8).toDouble();
        data.pressureMin = query.value(9).toDouble();
        data.pressureMax = query.value(10).toDouble();
        data.pressure95th = query.value(11).toDouble();
        data.leakTotalAvg = query.value(12).toDouble();
        data.leakTotal95th = query.value(13).toDouble();
        data.leakTotalMax = query.value(14).toDouble();
        data.spo2Avg = query.value(15).toDouble();
        data.spo2Min = query.value(16).toDouble();
        data.pulseAvg = query.value(17).toDouble();
        data.hoursUsed = query.value(18).toDouble();
        data.maskOnHours = query.value(19).toDouble();
        data.createdAt = query.value(20).toDateTime();
        data.updatedAt = query.value(21).toDateTime();
    }

    return data;
}

bool SessionSummariesRepository::remove(qint64 id)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSummariesRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_summaries WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "SessionSummariesRepository::remove() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SessionSummariesRepository::removeBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSummariesRepository::removeBySession() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_summaries WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSummariesRepository::removeBySession() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SessionSummariesRepository::exists(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSummariesRepository::exists() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM session_summaries WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSummariesRepository::exists() failed:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        return query.value(0).toInt() > 0;
    }

    return false;
}

bool SessionSummariesRepository::createOrUpdate(const SessionSummaryData& data)
{
    if (data.sessionId == 0) {
        qWarning() << "SessionSummariesRepository::createOrUpdate() - Invalid sessionId (0)";
        return false;
    }
    
    // Check if summary already exists
    SessionSummaryData existing = findBySession(data.sessionId);
    
    if (existing.id > 0) {
        // Update existing record
        SessionSummaryData updateData = data;
        updateData.id = existing.id;
        return update(updateData);
    } else {
        // Create new record
        qint64 newId = create(data);
        return newId > 0;
    }
}
