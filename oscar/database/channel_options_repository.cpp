/* Channel Options Repository Implementation
 *
 * Copyright (c) 2025-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channel_options_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ChannelOptionsRepository::ChannelOptionsRepository()
    : db(DatabaseManager::instance().database())
{
}

ChannelOptionsRepository::ChannelOptionsRepository(const QSqlDatabase& database)
    : db(database)
{
}

qint64 ChannelOptionsRepository::create(const ChannelOptionData& data)
{
    QSqlQuery query(db);
    
    query.prepare(R"(
        INSERT INTO channel_options (channel_id, option_key, option_value)
        VALUES (?, ?, ?)
    )");
    
    query.addBindValue(data.channelId);
    query.addBindValue(data.optionKey);
    query.addBindValue(data.optionValue);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::create failed:" << query.lastError().text();
        return -1;
    }
    
    return query.lastInsertId().toLongLong();
}

bool ChannelOptionsRepository::update(const ChannelOptionData& data)
{
    QSqlQuery query(db);
    
    query.prepare(R"(
        UPDATE channel_options SET
            channel_id = ?, option_key = ?, option_value = ?
        WHERE id = ?
    )");
    
    query.addBindValue(data.channelId);
    query.addBindValue(data.optionKey);
    query.addBindValue(data.optionValue);
    query.addBindValue(data.id);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::update failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool ChannelOptionsRepository::remove(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM channel_options WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::remove failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

ChannelOptionData ChannelOptionsRepository::findById(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channel_options WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::findById failed:" << query.lastError().text();
        return ChannelOptionData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return ChannelOptionData();
}

QList<ChannelOptionData> ChannelOptionsRepository::findByChannel(ChannelID channelId)
{
    QList<ChannelOptionData> options;
    
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channel_options WHERE channel_id = ? ORDER BY option_key");
    query.addBindValue(channelId);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::findByChannel failed:" << query.lastError().text();
        return options;
    }
    
    while (query.next()) {
        options.append(mapResultToData(query));
    }
    
    return options;
}

ChannelOptionData ChannelOptionsRepository::findByChannelAndKey(ChannelID channelId, int key)
{
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channel_options WHERE channel_id = ? AND option_key = ?");
    query.addBindValue(channelId);
    query.addBindValue(key);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::findByChannelAndKey failed:" << query.lastError().text();
        return ChannelOptionData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return ChannelOptionData();
}

QHash<int, QString> ChannelOptionsRepository::getOptionsHash(ChannelID channelId)
{
    QHash<int, QString> optionsHash;
    
    QList<ChannelOptionData> options = findByChannel(channelId);
    for (const ChannelOptionData& option : options) {
        optionsHash[option.optionKey] = option.optionValue;
    }
    
    return optionsHash;
}

bool ChannelOptionsRepository::saveBatch(ChannelID channelId, const QHash<int, QString>& options)
{
    if (options.isEmpty()) {
        return true;
    }
    
    // Use transaction for atomic batch operation
    db.transaction();
    
    // Use INSERT OR REPLACE to avoid duplicates
    QSqlQuery query(db);
    query.prepare(R"(
        INSERT OR REPLACE INTO channel_options (channel_id, option_key, option_value)
        VALUES (?, ?, ?)
    )");
    
    bool success = true;
    for (auto it = options.begin(); it != options.end(); ++it) {
        query.addBindValue(channelId);
        query.addBindValue(it.key());
        query.addBindValue(it.value());
        
        if (!query.exec()) {
            qWarning() << "ChannelOptionsRepository::saveBatch failed:" << query.lastError().text();
            success = false;
            break;
        }
    }
    
    if (success) {
        db.commit();
    } else {
        db.rollback();
        qWarning() << "ChannelOptionsRepository::saveBatch failed, rolled back transaction";
    }
    
    return success;
}

bool ChannelOptionsRepository::deleteByChannel(ChannelID channelId)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM channel_options WHERE channel_id = ?");
    query.addBindValue(channelId);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::deleteByChannel failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool ChannelOptionsRepository::hasOptions(ChannelID channelId)
{
    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM channel_options WHERE channel_id = ?");
    query.addBindValue(channelId);
    
    if (!query.exec()) {
        qWarning() << "ChannelOptionsRepository::hasOptions failed:" << query.lastError().text();
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

ChannelOptionData ChannelOptionsRepository::mapResultToData(const QSqlQuery& query)
{
    ChannelOptionData data;
    
    data.id = query.value("id").toLongLong();
    data.channelId = query.value("channel_id").toUInt();
    data.optionKey = query.value("option_key").toInt();
    data.optionValue = query.value("option_value").toString();
    
    return data;
}
