/* Session Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the SessionRepository class for database
 * access to session data (core session metadata and timing).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

//#ifdef DBDEBUG

SessionRepository::SessionRepository()
{
}

SessionRepository::~SessionRepository()
{
}

qint64 SessionRepository::create(const SessionData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO sessions "
        "(session_id, machine_id, start_time, end_time, duration, "
        " enabled, summary_only, no_settings, events_loaded) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.machineId);
    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.duration);
    query.addBindValue(data.enabled);
    query.addBindValue(data.summaryOnly);
    query.addBindValue(data.noSettings);
    query.addBindValue(data.eventsLoaded);
    // Note: events_file and summary_file columns removed in schema v12

    if (!query.exec()) {
        qWarning() << "SessionRepository::create() failed:" << query.lastError().text();
        return -1;
    }
    if (data.summaryOnly && data.machineId != 0)
        qDebug() << "SessionRepository::create() summary only session" << data.sessionId;

    return query.lastInsertId().toLongLong();
}

bool SessionRepository::update(const SessionData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE sessions SET "
        "session_id = ?, machine_id = ?, start_time = ?, end_time = ?, duration = ?, "
        "enabled = ?, summary_only = ?, no_settings = ?, events_loaded = ?, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.machineId);
    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.duration);
    query.addBindValue(data.enabled);
    query.addBindValue(data.summaryOnly);
    query.addBindValue(data.noSettings);
    query.addBindValue(data.eventsLoaded);
    // Note: events_file and summary_file columns removed in schema v12
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "SessionRepository::update() failed:" << query.lastError().text();
        return false;
    }

#ifdef DBDEBUG
    if (data.summaryOnly)
        qDebug() << "SessionRepository::update() summary only session" << data.sessionId;
    else
        qDebug() << "SessionRepository::update() updated session" << data.sessionId;
#endif

    return true;
}

SessionData SessionRepository::findById(qint64 id)
{
    SessionData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::findById() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, machine_id, start_time, end_time, duration, "
        "enabled, summary_only, no_settings, events_loaded, "
        "created_at, updated_at "
        "FROM sessions WHERE id = ?"
    );
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "SessionRepository::findById() failed:" << query.lastError().text();
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.machineId = query.value(2).toLongLong();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toLongLong();
        data.enabled = query.value(6).toInt();
        data.summaryOnly = query.value(7).toInt();
        data.noSettings = query.value(8).toInt();
        data.eventsLoaded = query.value(9).toInt();
        data.createdAt = query.value(10).toDateTime();
        data.updatedAt = query.value(11).toDateTime();
        // Note: eventsFile and summaryFile no longer stored (schema v12)
    }

    return data;
}

SessionData SessionRepository::findByMachineAndSessionId(qint64 machineId, qint64 sessionId)
{
    SessionData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::findByMachineAndSessionId() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, machine_id, start_time, end_time, duration, "
        "enabled, summary_only, no_settings, events_loaded, "
        "created_at, updated_at "
        "FROM sessions WHERE machine_id = ? AND session_id = ?"
    );
    query.addBindValue(machineId);
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionRepository::findByMachineAndSessionId() failed:" << query.lastError().text();
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.machineId = query.value(2).toLongLong();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toLongLong();
        data.enabled = query.value(6).toInt();
        data.summaryOnly = query.value(7).toInt();
        data.noSettings = query.value(8).toInt();
        data.eventsLoaded = query.value(9).toInt();
        data.createdAt = query.value(10).toDateTime();
        data.updatedAt = query.value(11).toDateTime();
    }

    return data;
}

QList<SessionData> SessionRepository::findByMachine(qint64 machineId)
{
    QList<SessionData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::findByMachine() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, machine_id, start_time, end_time, duration, "
        "enabled, summary_only, no_settings, events_loaded, "
        "created_at, updated_at "
        "FROM sessions WHERE machine_id = ? ORDER BY start_time"
    );
    query.addBindValue(machineId);

    if (!query.exec()) {
        qWarning() << "SessionRepository::findByMachine() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        SessionData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.machineId = query.value(2).toLongLong();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toLongLong();
        data.enabled = query.value(6).toInt();
        data.summaryOnly = query.value(7).toInt();
        data.noSettings = query.value(8).toInt();
        data.eventsLoaded = query.value(9).toInt();
        data.createdAt = query.value(10).toDateTime();
        data.updatedAt = query.value(11).toDateTime();
        result.append(data);
    }

    return result;
}

QList<SessionData> SessionRepository::findEnabledByMachine(qint64 machineId)
{
    QList<SessionData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::findEnabledByMachine() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, machine_id, start_time, end_time, duration, "
        "enabled, summary_only, no_settings, events_loaded, "
        "created_at, updated_at "
        "FROM sessions WHERE machine_id = ? AND enabled = 1 ORDER BY start_time"
    );
    query.addBindValue(machineId);

    if (!query.exec()) {
        qWarning() << "SessionRepository::findEnabledByMachine() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        SessionData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.machineId = query.value(2).toLongLong();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toLongLong();
        data.enabled = query.value(6).toInt();
        data.summaryOnly = query.value(7).toInt();
        data.noSettings = query.value(8).toInt();
        data.eventsLoaded = query.value(9).toInt();
        data.createdAt = query.value(10).toDateTime();
        data.updatedAt = query.value(11).toDateTime();
        result.append(data);
    }

    return result;
}

QList<SessionData> SessionRepository::findByTimeRange(qint64 machineId, qint64 startTime, qint64 endTime)
{
    QList<SessionData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::findByTimeRange() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, machine_id, start_time, end_time, duration, "
        "enabled, summary_only, no_settings, events_loaded, "
        "created_at, updated_at "
        "FROM sessions WHERE machine_id = ? AND start_time >= ? AND end_time <= ? "
        "ORDER BY start_time"
    );
    query.addBindValue(machineId);
    query.addBindValue(startTime);
    query.addBindValue(endTime);

    if (!query.exec()) {
        qWarning() << "SessionRepository::findByTimeRange() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        SessionData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.machineId = query.value(2).toLongLong();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toLongLong();
        data.enabled = query.value(6).toInt();
        data.summaryOnly = query.value(7).toInt();
        data.noSettings = query.value(8).toInt();
        data.eventsLoaded = query.value(9).toInt();
        data.createdAt = query.value(10).toDateTime();
        data.updatedAt = query.value(11).toDateTime();
        result.append(data);
    }

    return result;
}

bool SessionRepository::remove(qint64 id)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM sessions WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "SessionRepository::remove() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SessionRepository::removeByMachine(qint64 machineId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::removeByMachine() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM sessions WHERE machine_id = ?");
    query.addBindValue(machineId);

    if (!query.exec()) {
        qWarning() << "SessionRepository::removeByMachine() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int SessionRepository::countByMachine(qint64 machineId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::countByMachine() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM sessions WHERE machine_id = ?");
    query.addBindValue(machineId);

    if (!query.exec()) {
        qWarning() << "SessionRepository::countByMachine() failed:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}

bool SessionRepository::exists(qint64 machineId, qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::exists() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM sessions WHERE machine_id = ? AND session_id = ?");
    query.addBindValue(machineId);
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionRepository::exists() failed:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        return query.value(0).toInt() > 0;
    }

    return false;
}

bool SessionRepository::beginTransaction()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::beginTransaction() - Database not open";
        return false;
    }

    if (!db.transaction()) {
        qWarning() << "SessionRepository::beginTransaction() failed:" << db.lastError().text();
        return false;
    }

    return true;
}

bool SessionRepository::commitTransaction()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::commitTransaction() - Database not open";
        return false;
    }

    if (!db.commit()) {
        qWarning() << "SessionRepository::commitTransaction() failed:" << db.lastError().text();
        return false;
    }

    return true;
}

bool SessionRepository::rollbackTransaction()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionRepository::rollbackTransaction() - Database not open";
        return false;
    }

    if (!db.rollback()) {
        qWarning() << "SessionRepository::rollbackTransaction() failed:" << db.lastError().text();
        return false;
    }

    return true;
}
