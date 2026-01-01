/* Session Channels Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the SessionChannelsRepository class for database
 * access to session channel statistics (summary stats per channel).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_channels_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SessionChannelsRepository::SessionChannelsRepository()
{
}

SessionChannelsRepository::~SessionChannelsRepository()
{
}

qint64 SessionChannelsRepository::create(const SessionChannelData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_channels "
        "(session_id, channel_id, count, sum, avg, wavg, min, max, median, p90, p95, "
        " phys_min, phys_max, cph, sph, first_time, last_time, gain) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.channelId);
    query.addBindValue(data.count);
    query.addBindValue(data.sum);
    query.addBindValue(data.avg);
    query.addBindValue(data.wavg);
    query.addBindValue(data.min);
    query.addBindValue(data.max);
    query.addBindValue(data.median);
    query.addBindValue(data.p90);
    query.addBindValue(data.p95);
    query.addBindValue(data.physMin);
    query.addBindValue(data.physMax);
    query.addBindValue(data.cph);
    query.addBindValue(data.sph);
    query.addBindValue(data.firstTime);
    query.addBindValue(data.lastTime);
    query.addBindValue(data.gain);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::create() failed:" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool SessionChannelsRepository::update(const SessionChannelData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE session_channels SET "
        "count = ?, sum = ?, avg = ?, wavg = ?, min = ?, max = ?, "
        "median = ?, p90 = ?, p95 = ?, phys_min = ?, phys_max = ?, "
        "cph = ?, sph = ?, first_time = ?, last_time = ?, gain = ? "
        "WHERE id = ?"
    );

    query.addBindValue(data.count);
    query.addBindValue(data.sum);
    query.addBindValue(data.avg);
    query.addBindValue(data.wavg);
    query.addBindValue(data.min);
    query.addBindValue(data.max);
    query.addBindValue(data.median);
    query.addBindValue(data.p90);
    query.addBindValue(data.p95);
    query.addBindValue(data.physMin);
    query.addBindValue(data.physMax);
    query.addBindValue(data.cph);
    query.addBindValue(data.sph);
    query.addBindValue(data.firstTime);
    query.addBindValue(data.lastTime);
    query.addBindValue(data.gain);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::update() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<SessionChannelData> SessionChannelsRepository::findBySession(qint64 sessionId)
{
    QList<SessionChannelData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::findBySession() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, channel_id, count, sum, avg, wavg, min, max, "
        "median, p90, p95, phys_min, phys_max, cph, sph, first_time, last_time, gain, created_at "
        "FROM session_channels WHERE session_id = ?"
    );
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::findBySession() failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        SessionChannelData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.channelId = query.value(2).toInt();
        data.count = query.value(3).toInt();
        data.sum = query.value(4).toDouble();
        data.avg = query.value(5).toDouble();
        data.wavg = query.value(6).toDouble();
        data.min = query.value(7).toDouble();
        data.max = query.value(8).toDouble();
        data.median = query.value(9).toDouble();
        data.p90 = query.value(10).toDouble();
        data.p95 = query.value(11).toDouble();
        data.physMin = query.value(12).toDouble();
        data.physMax = query.value(13).toDouble();
        data.cph = query.value(14).toDouble();
        data.sph = query.value(15).toDouble();
        data.firstTime = query.value(16).toLongLong();
        data.lastTime = query.value(17).toLongLong();
        data.gain = query.value(18).toDouble();
        data.createdAt = query.value(19).toDateTime();
        result.append(data);
    }

    return result;
}

SessionChannelData SessionChannelsRepository::findByChannel(qint64 sessionId, int channelId)
{
    SessionChannelData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::findByChannel() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, channel_id, count, sum, avg, wavg, min, max, "
        "median, p90, p95, phys_min, phys_max, cph, sph, first_time, last_time, gain, created_at "
        "FROM session_channels WHERE session_id = ? AND channel_id = ?"
    );
    query.addBindValue(sessionId);
    query.addBindValue(channelId);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::findByChannel() failed:" << query.lastError().text();
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.channelId = query.value(2).toInt();
        data.count = query.value(3).toInt();
        data.sum = query.value(4).toDouble();
        data.avg = query.value(5).toDouble();
        data.wavg = query.value(6).toDouble();
        data.min = query.value(7).toDouble();
        data.max = query.value(8).toDouble();
        data.median = query.value(9).toDouble();
        data.p90 = query.value(10).toDouble();
        data.p95 = query.value(11).toDouble();
        data.physMin = query.value(12).toDouble();
        data.physMax = query.value(13).toDouble();
        data.cph = query.value(14).toDouble();
        data.sph = query.value(15).toDouble();
        data.firstTime = query.value(16).toLongLong();
        data.lastTime = query.value(17).toLongLong();
        data.gain = query.value(18).toDouble();
        data.createdAt = query.value(19).toDateTime();
    }

    return data;
}

bool SessionChannelsRepository::saveBatch(qint64 sessionId, const QList<SessionChannelData>& channels)
{
    DatabaseManager& dbMgr = DatabaseManager::instance();
    QSqlDatabase db = dbMgr.database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::saveBatch() - Database not open";
        return false;
    }

    // Only start transaction if not already in one
    bool needTransaction = !dbMgr.inTransaction();
    if (needTransaction && !dbMgr.transaction()) {
        qWarning() << "SessionChannelsRepository::saveBatch() - Failed to start transaction";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT OR REPLACE INTO session_channels "
        "(session_id, channel_id, count, sum, avg, wavg, min, max, median, p90, p95, "
        " phys_min, phys_max, cph, sph, first_time, last_time, gain) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    for (const SessionChannelData& data : channels) {
        query.addBindValue(sessionId);
        query.addBindValue(data.channelId);
        query.addBindValue(data.count);
        query.addBindValue(data.sum);
        query.addBindValue(data.avg);
        query.addBindValue(data.wavg);
        query.addBindValue(data.min);
        query.addBindValue(data.max);
        query.addBindValue(data.median);
        query.addBindValue(data.p90);
        query.addBindValue(data.p95);
        query.addBindValue(data.physMin);
        query.addBindValue(data.physMax);
        query.addBindValue(data.cph);
        query.addBindValue(data.sph);
        query.addBindValue(data.firstTime);
        query.addBindValue(data.lastTime);
        query.addBindValue(data.gain);

        if (!query.exec()) {
            qWarning() << "SessionChannelsRepository::saveBatch() failed:" << query.lastError().text();
            if (needTransaction) {
                dbMgr.rollback();
            }
            return false;
        }
    }

    // Only commit if we started the transaction
    if (needTransaction && !dbMgr.commit()) {
        qWarning() << "SessionChannelsRepository::saveBatch() - Failed to commit transaction";
        return false;
    }

    return true;
}

bool SessionChannelsRepository::remove(qint64 id)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_channels WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::remove() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SessionChannelsRepository::removeBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::removeBySession() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_channels WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::removeBySession() failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int SessionChannelsRepository::countBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionChannelsRepository::countBySession() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM session_channels WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionChannelsRepository::countBySession() failed:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}
