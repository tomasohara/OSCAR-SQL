/* Respiratory Events Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the RespiratoryEventsRepository class for database
 * access to respiratory events (apneas, hypopneas, RERAs, etc.).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "respiratory_events_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

RespiratoryEventsRepository::RespiratoryEventsRepository()
{
}

RespiratoryEventsRepository::~RespiratoryEventsRepository()
{
}

qint64 RespiratoryEventsRepository::create(const RespiratoryEventData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO respiratory_events "
        "(session_id, event_type, start_time, end_time, duration, desaturation, severity) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.eventType);
    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.duration);
    query.addBindValue(data.desaturation);
    query.addBindValue(data.severity);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::create() failed:" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool RespiratoryEventsRepository::update(const RespiratoryEventData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE respiratory_events SET "
        "event_type = ?, start_time = ?, end_time = ?, duration = ?, "
        "desaturation = ?, severity = ? "
        "WHERE id = ?"
    );

    query.addBindValue(data.eventType);
    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.duration);
    query.addBindValue(data.desaturation);
    query.addBindValue(data.severity);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::update() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<RespiratoryEventData> RespiratoryEventsRepository::findBySession(qint64 sessionId)
{
    QList<RespiratoryEventData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findBySession() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, event_type, start_time, end_time, duration, "
        "desaturation, severity, created_at "
        "FROM respiratory_events WHERE session_id = ? ORDER BY start_time"
    );
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findBySession() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        RespiratoryEventData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.eventType = query.value(2).toInt();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toInt();
        data.desaturation = query.value(6).toDouble();
        data.severity = query.value(7).toInt();
        data.createdAt = query.value(8).toDateTime();
        result.append(data);
    }

    return result;
}

QList<RespiratoryEventData> RespiratoryEventsRepository::findByType(qint64 sessionId, int eventType)
{
    QList<RespiratoryEventData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findByType() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, event_type, start_time, end_time, duration, "
        "desaturation, severity, created_at "
        "FROM respiratory_events WHERE session_id = ? AND event_type = ? ORDER BY start_time"
    );
    query.addBindValue(sessionId);
    query.addBindValue(eventType);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findByType() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        RespiratoryEventData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.eventType = query.value(2).toInt();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toInt();
        data.desaturation = query.value(6).toDouble();
        data.severity = query.value(7).toInt();
        data.createdAt = query.value(8).toDateTime();
        result.append(data);
    }

    return result;
}

QList<RespiratoryEventData> RespiratoryEventsRepository::findByTimeRange(qint64 sessionId, qint64 startTime, qint64 endTime)
{
    QList<RespiratoryEventData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findByTimeRange() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, event_type, start_time, end_time, duration, "
        "desaturation, severity, created_at "
        "FROM respiratory_events WHERE session_id = ? AND start_time >= ? AND end_time <= ? "
        "ORDER BY start_time"
    );
    query.addBindValue(sessionId);
    query.addBindValue(startTime);
    query.addBindValue(endTime);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findByTimeRange() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        RespiratoryEventData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.eventType = query.value(2).toInt();
        data.startTime = query.value(3).toLongLong();
        data.endTime = query.value(4).toLongLong();
        data.duration = query.value(5).toInt();
        data.desaturation = query.value(6).toDouble();
        data.severity = query.value(7).toInt();
        data.createdAt = query.value(8).toDateTime();
        result.append(data);
    }

    return result;
}

int RespiratoryEventsRepository::countByType(qint64 sessionId, int eventType)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::countByType() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM respiratory_events WHERE session_id = ? AND event_type = ?");
    query.addBindValue(sessionId);
    query.addBindValue(eventType);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::countByType() failed:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}

bool RespiratoryEventsRepository::saveBatch(const QList<RespiratoryEventData>& events)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::saveBatch() - Database not open";
        return false;
    }

    if (!db.transaction()) {
        qWarning() << "RespiratoryEventsRepository::saveBatch() - Failed to start transaction";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO respiratory_events "
        "(session_id, event_type, start_time, end_time, duration, desaturation, severity) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"
    );

    for (const RespiratoryEventData& data : events) {
        query.addBindValue(data.sessionId);
        query.addBindValue(data.eventType);
        query.addBindValue(data.startTime);
        query.addBindValue(data.endTime);
        query.addBindValue(data.duration);
        query.addBindValue(data.desaturation);
        query.addBindValue(data.severity);

        if (!query.exec()) {
            qWarning() << "RespiratoryEventsRepository::saveBatch() failed:" << query.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qWarning() << "RespiratoryEventsRepository::saveBatch() - Failed to commit transaction";
        return false;
    }

    return true;
}

bool RespiratoryEventsRepository::remove(qint64 id)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM respiratory_events WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::remove() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool RespiratoryEventsRepository::removeBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::removeBySession() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM respiratory_events WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::removeBySession() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int RespiratoryEventsRepository::countBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::countBySession() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM respiratory_events WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::countBySession() failed:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}
