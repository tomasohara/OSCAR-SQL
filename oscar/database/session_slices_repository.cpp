/* Session Slices Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the SessionSlicesRepository class for database
 * access to session slices (mask on/off periods).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_slices_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SessionSlicesRepository::SessionSlicesRepository()
{
}

SessionSlicesRepository::~SessionSlicesRepository()
{
}

qint64 SessionSlicesRepository::create(const SessionSliceData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_slices "
        "(session_id, start_time, end_time, status) "
        "VALUES (?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.status);

    if (!query.exec()) {
        qWarning() << "SessionSlicesRepository::create() failed:" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool SessionSlicesRepository::update(const SessionSliceData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE session_slices SET "
        "start_time = ?, end_time = ?, status = ? "
        "WHERE id = ?"
    );

    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.status);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "SessionSlicesRepository::update() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<SessionSliceData> SessionSlicesRepository::findBySession(qint64 sessionId)
{
    QList<SessionSliceData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::findBySession() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, start_time, end_time, status "
        "FROM session_slices WHERE session_id = ? ORDER BY start_time"
    );
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSlicesRepository::findBySession() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        SessionSliceData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.startTime = query.value(2).toLongLong();
        data.endTime = query.value(3).toLongLong();
        data.status = query.value(4).toInt();
        result.append(data);
    }

    return result;
}

bool SessionSlicesRepository::saveBatch(const QList<SessionSliceData>& slices)
{
    DatabaseManager& dbMgr = DatabaseManager::instance();
    QSqlDatabase db = dbMgr.database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::saveBatch() - Database not open";
        return false;
    }
/***
    // Only start transaction if not already in one
    bool needTransaction = !dbMgr.inTransaction();
    if (needTransaction && !dbMgr.transaction()) {
        qWarning() << "SessionSlicesRepository::saveBatch() - Failed to start transaction";
        return false;
    }
***/
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_slices "
        "(session_id, start_time, end_time, status) "
        "VALUES (?, ?, ?, ?)"
    );

    for (const SessionSliceData& data : slices) {
        query.addBindValue(data.sessionId);
        query.addBindValue(data.startTime);
        query.addBindValue(data.endTime);
        query.addBindValue(data.status);

        if (!query.exec()) {
            qWarning() << "SessionSlicesRepository::saveBatch() failed:" << query.lastError().text();
/***
            if (needTransaction) {
                dbMgr.rollback();
            }
***/
            return false;
        }
    }
/***
    // Only commit if we started the transaction
    if (needTransaction && !dbMgr.commit()) {
        qWarning() << "SessionSlicesRepository::saveBatch() - Failed to commit transaction";
        return false;
    }
***/

    return true;
}

bool SessionSlicesRepository::remove(qint64 id)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_slices WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "SessionSlicesRepository::remove() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SessionSlicesRepository::removeBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::removeBySession() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_slices WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSlicesRepository::removeBySession() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int SessionSlicesRepository::countBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSlicesRepository::countBySession() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM session_slices WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSlicesRepository::countBySession() failed:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}
