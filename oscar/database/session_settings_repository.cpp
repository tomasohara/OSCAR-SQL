/* Session Settings Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the SessionSettingsRepository class for database
 * access to session settings (machine configuration per session).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_settings_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SessionSettingsRepository::SessionSettingsRepository()
{
}

SessionSettingsRepository::~SessionSettingsRepository()
{
}

qint64 SessionSettingsRepository::create(const SessionSettingData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO session_settings "
        "(session_id, channel_id, value, data_type, json_value) "
        "VALUES (?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.sessionId);
    query.addBindValue(data.channelId);
    query.addBindValue(data.value);
    query.addBindValue(data.dataType);
    query.addBindValue(data.jsonValue.isNull() ? QVariant(QVariant::String) : data.jsonValue);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::create() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::create", query);
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool SessionSettingsRepository::update(const SessionSettingData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE session_settings SET "
        "value = ?, data_type = ?, json_value = ? "
        "WHERE id = ?"
    );

    query.addBindValue(data.value);
    query.addBindValue(data.dataType);
    query.addBindValue(data.jsonValue.isNull() ? QVariant(QVariant::String) : data.jsonValue);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::update() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::update", query);
        return false;
    }

    return true;
}

QList<SessionSettingData> SessionSettingsRepository::findBySession(qint64 sessionId)
{
    QList<SessionSettingData> result;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::findBySession() - Database not open";
        return result;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, channel_id, value, data_type, json_value, created_at "
        "FROM session_settings WHERE session_id = ?"
    );
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::findBySession() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::findBySession", query);
        return result;
    }

    while (query.next()) {
        SessionSettingData data;
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.channelId = query.value(2).toInt();
        data.value = query.value(3).toDouble();
        data.dataType = query.value(4).toString();
        data.jsonValue = query.value(5).toString();
        data.createdAt = query.value(6).toDateTime();
        result.append(data);
    }

    return result;
}

SessionSettingData SessionSettingsRepository::findBySetting(qint64 sessionId, int channelId)
{
    SessionSettingData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::findBySetting() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, channel_id, value, data_type, json_value, created_at "
        "FROM session_settings WHERE session_id = ? AND channel_id = ?"
    );
    query.addBindValue(sessionId);
    query.addBindValue(channelId);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::findBySetting() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::findBySetting", query);
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.sessionId = query.value(1).toLongLong();
        data.channelId = query.value(2).toInt();
        data.value = query.value(3).toDouble();
        data.dataType = query.value(4).toString();
        data.jsonValue = query.value(5).toString();
        data.createdAt = query.value(6).toDateTime();
    }

    return data;
}

bool SessionSettingsRepository::saveBatch(qint64 sessionId, const QList<SessionSettingData>& settings)
{
    DatabaseManager& dbMgr = DatabaseManager::instance();
    QSqlDatabase db = dbMgr.database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::saveBatch() - Database not open";
        return false;
    }

    // NOTE: Transaction management removed - caller (Session::StoreToDatabase) is responsible
    // This method is always called within Machine::Save()'s outer transaction
    
    QSqlQuery query(db);
    query.prepare(
        "INSERT OR REPLACE INTO session_settings "
        "(session_id, profile_id, channel_id, value, data_type, json_value) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );

    for (const SessionSettingData& data : settings) {
        query.addBindValue(sessionId);
        query.addBindValue(data.profileId);
        query.addBindValue(data.channelId);
        query.addBindValue(data.value);
        query.addBindValue(data.dataType);
        query.addBindValue(data.jsonValue.isNull() ? QVariant(QVariant::String) : data.jsonValue);

        if (!query.exec()) {
            qWarning() << "SessionSettingsRepository::saveBatch() failed:" << query.lastError().text();
            DatabaseManager::instance().checkQueryError("SessionSettingsRepository::saveBatch", query);
            return false;
        }
    }

    return true;
}

bool SessionSettingsRepository::remove(qint64 id)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_settings WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::remove() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::remove", query);
        return false;
    }

    return true;
}

bool SessionSettingsRepository::removeBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::removeBySession() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM session_settings WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::removeBySession() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::removeBySession", query);
        return false;
    }

    return true;
}

int SessionSettingsRepository::countBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "SessionSettingsRepository::countBySession() - Database not open";
        return 0;
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM session_settings WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "SessionSettingsRepository::countBySession() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("SessionSettingsRepository::countBySession", query);
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}
