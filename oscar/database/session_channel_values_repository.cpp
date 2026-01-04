/* Session Channel Values Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the SessionChannelValuesRepository class for database
 * access to session channel value/time summary data.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_channel_values_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SessionChannelValuesRepository::SessionChannelValuesRepository()
{
}

SessionChannelValuesRepository::~SessionChannelValuesRepository()
{
}

qint64 SessionChannelValuesRepository::create(const SessionChannelValueData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_channel_values "
        "(session_channel_id, value, count, time_ms) "
        "VALUES (?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionChannelId);
    query.addBindValue(data.value);
    query.addBindValue(data.count);
    query.addBindValue(data.timeMs);

    if (!query.exec()) {
        qWarning() << "SessionChannelValuesRepository::create() failed:" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

QList<SessionChannelValueData> SessionChannelValuesRepository::findBySessionChannel(qint64 sessionChannelId)
{
    QList<SessionChannelValueData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::findBySessionChannel() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_channel_id, value, count, time_ms, created_at "
        "FROM session_channel_values WHERE session_channel_id = ? "
        "ORDER BY value"
    );
    query.addBindValue(sessionChannelId);

    if (!query.exec()) {
        qWarning() << "SessionChannelValuesRepository::findBySessionChannel() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        SessionChannelValueData data;
        data.id = query.value(0).toLongLong();
        data.sessionChannelId = query.value(1).toLongLong();
        data.value = query.value(2).toInt();
        data.count = query.value(3).toInt();
        data.timeMs = query.value(4).toUInt();
        data.createdAt = query.value(5).toDateTime();
        result.append(data);
    }

    return result;
}

bool SessionChannelValuesRepository::saveBatch(qint64 sessionChannelId, const QList<SessionChannelValueData>& values)
{
    DatabaseManager& dbMgr = DatabaseManager::instance();
    QSqlDatabase db = dbMgr.database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::saveBatch() - Database not open";
        return false;
    }

    // Only start transaction if not already in one
    bool needTransaction = !dbMgr.inTransaction();
    if (needTransaction && !dbMgr.transaction()) {
        qWarning() << "SessionChannelValuesRepository::saveBatch() - Failed to start transaction";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT OR REPLACE INTO session_channel_values "
        "(session_channel_id, value, count, time_ms) "
        "VALUES (?, ?, ?, ?)"
    );

    for (const SessionChannelValueData& data : values) {
        query.addBindValue(sessionChannelId);
        query.addBindValue(data.value);
        query.addBindValue(data.count);
        query.addBindValue(data.timeMs);

        if (!query.exec()) {
            qWarning() << "SessionChannelValuesRepository::saveBatch() failed:" << query.lastError().text();
            if (needTransaction) {
                dbMgr.rollback();
            }
            return false;
        }
    }

    // Only commit if we started the transaction
    if (needTransaction && !dbMgr.commit()) {
        qWarning() << "SessionChannelValuesRepository::saveBatch() - Failed to commit transaction";
        return false;
    }

    return true;
}

bool SessionChannelValuesRepository::saveChannelSummaries(qint64 sessionChannelId, 
                                                          const QHash<EventStoreType, EventStoreType>& valueSummary,
                                                          const QHash<EventStoreType, quint32>& timeSummary)
{
    DatabaseManager& dbMgr = DatabaseManager::instance();
    QSqlDatabase db = dbMgr.database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::saveChannelSummaries() - Database not open";
        return false;
    }

    // Only start transaction if not already in one
    bool needTransaction = !dbMgr.inTransaction();
    if (needTransaction && !dbMgr.transaction()) {
        qWarning() << "SessionChannelValuesRepository::saveChannelSummaries() - Failed to start transaction";
        return false;
    }

    // First, delete existing values for this channel
    QSqlQuery deleteQuery(db);
    deleteQuery.prepare("DELETE FROM session_channel_values WHERE session_channel_id = ?");
    deleteQuery.addBindValue(sessionChannelId);
    
    if (!deleteQuery.exec()) {
        qWarning() << "SessionChannelValuesRepository::saveChannelSummaries() - Delete failed:" << deleteQuery.lastError().text();
        if (needTransaction) {
            dbMgr.rollback();
        }
        return false;
    }

    // Now insert new values
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_channel_values "
        "(session_channel_id, value, count, time_ms) "
        "VALUES (?, ?, ?, ?)"
    );

    // Iterate through valueSummary and get corresponding time from timeSummary
    for (auto it = valueSummary.begin(); it != valueSummary.end(); ++it) {
        EventStoreType value = it.key();
        EventStoreType count = it.value();
        quint32 timeMs = timeSummary.value(value, 0);  // Get time, default to 0 if not found

        query.addBindValue(sessionChannelId);
        query.addBindValue(value);
        query.addBindValue(count);
        query.addBindValue(timeMs);

        if (!query.exec()) {
            qWarning() << "SessionChannelValuesRepository::saveChannelSummaries() - Insert failed:" << query.lastError().text();
            if (needTransaction) {
                dbMgr.rollback();
            }
            return false;
        }
    }

    // Only commit if we started the transaction
    if (needTransaction && !dbMgr.commit()) {
        qWarning() << "SessionChannelValuesRepository::saveChannelSummaries() - Failed to commit transaction";
        return false;
    }

    return true;
}

bool SessionChannelValuesRepository::loadChannelSummaries(qint64 sessionChannelId,
                                                          QHash<EventStoreType, EventStoreType>& valueSummary,
                                                          QHash<EventStoreType, quint32>& timeSummary)
{
    valueSummary.clear();
    timeSummary.clear();
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::loadChannelSummaries() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT value, count, time_ms "
        "FROM session_channel_values WHERE session_channel_id = ?"
    );
    query.addBindValue(sessionChannelId);

    if (!query.exec()) {
        qWarning() << "SessionChannelValuesRepository::loadChannelSummaries() failed:" << query.lastError().text();
        return false;
    }

    int rowCount = 0;
    while (query.next()) {
        EventStoreType value = query.value(0).toInt();
        EventStoreType count = query.value(1).toInt();
        quint32 timeMs = query.value(2).toUInt();

        valueSummary[value] = count;
        timeSummary[value] = timeMs;
        rowCount++;
    }

    // Return true if we found at least one row
    return rowCount > 0;
}

bool SessionChannelValuesRepository::removeBySessionChannel(qint64 sessionChannelId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::removeBySessionChannel() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_channel_values WHERE session_channel_id = ?");
    query.addBindValue(sessionChannelId);

    if (!query.exec()) {
        qWarning() << "SessionChannelValuesRepository::removeBySessionChannel() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int SessionChannelValuesRepository::countBySessionChannel(qint64 sessionChannelId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelValuesRepository::countBySessionChannel() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM session_channel_values WHERE session_channel_id = ?");
    query.addBindValue(sessionChannelId);

    if (!query.exec()) {
        qWarning() << "SessionChannelValuesRepository::countBySessionChannel() failed:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}
