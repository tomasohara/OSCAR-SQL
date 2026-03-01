/* Channel Repository Implementation
 *
 * Copyright (c) 2025-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channel_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ChannelRepository::ChannelRepository()
    : db(DatabaseManager::instance().database())
{
}

ChannelRepository::ChannelRepository(const QSqlDatabase& database)
    : db(database)
{
}

qint64 ChannelRepository::create(const ChannelData& data)
{
    QSqlQuery query(db);
    
    query.prepare(R"(
        INSERT INTO channels (
            profile_id, channel_id, channel_code, type, enabled,
            default_color, fullname, label, description,
            lower_threshold, lower_threshold_color,
            upper_threshold, upper_threshold_color,
            show_in_overview
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    
    query.addBindValue(data.profileId);
    query.addBindValue(data.channelId);
    query.addBindValue(data.channelCode);
    query.addBindValue(data.type);
    query.addBindValue(data.enabled);
    query.addBindValue(colorToString(data.defaultColor));
    query.addBindValue(data.fullname);
    query.addBindValue(data.label);
    query.addBindValue(data.description);
    query.addBindValue(data.lowerThreshold);
    query.addBindValue(colorToString(data.lowerThresholdColor));
    query.addBindValue(data.upperThreshold);
    query.addBindValue(colorToString(data.upperThresholdColor));
    query.addBindValue(data.showInOverview);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::create failed:" << query.lastError().text();
        return -1;
    }
    
    return query.lastInsertId().toLongLong();
}

bool ChannelRepository::update(const ChannelData& data)
{
    QSqlQuery query(db);
    
    query.prepare(R"(
        UPDATE channels SET
            profile_id = ?, channel_id = ?, channel_code = ?, type = ?, enabled = ?,
            default_color = ?, fullname = ?, label = ?, description = ?,
            lower_threshold = ?, lower_threshold_color = ?,
            upper_threshold = ?, upper_threshold_color = ?,
            show_in_overview = ?,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
    )");
    
    query.addBindValue(data.profileId);
    query.addBindValue(data.channelId);
    query.addBindValue(data.channelCode);
    query.addBindValue(data.type);
    query.addBindValue(data.enabled);
    query.addBindValue(colorToString(data.defaultColor));
    query.addBindValue(data.fullname);
    query.addBindValue(data.label);
    query.addBindValue(data.description);
    query.addBindValue(data.lowerThreshold);
    query.addBindValue(colorToString(data.lowerThresholdColor));
    query.addBindValue(data.upperThreshold);
    query.addBindValue(colorToString(data.upperThresholdColor));
    query.addBindValue(data.showInOverview);
    query.addBindValue(data.id);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::update failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool ChannelRepository::remove(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM channels WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::remove failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

ChannelData ChannelRepository::findById(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channels WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::findById failed:" << query.lastError().text();
        return ChannelData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return ChannelData();
}

QList<ChannelData> ChannelRepository::findByProfile(qint64 profileId)
{
    QList<ChannelData> channels;
    
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channels WHERE profile_id = ? ORDER BY channel_id");
    query.addBindValue(profileId);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::findByProfile failed:" << query.lastError().text();
        return channels;
    }
    
    while (query.next()) {
        channels.append(mapResultToData(query));
    }
    
    return channels;
}

ChannelData ChannelRepository::findByProfileAndChannelId(qint64 profileId, ChannelID channelId)
{
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channels WHERE profile_id = ? AND channel_id = ?");
    query.addBindValue(profileId);
    query.addBindValue(channelId);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::findByProfileAndChannelId failed:" << query.lastError().text();
        return ChannelData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return ChannelData();
}

bool ChannelRepository::saveBatch(qint64 profileId, const QList<ChannelData>& channels)
{
    // Use transaction for atomic batch operation
    if (!db.transaction()) {
        qWarning() << "ChannelRepository::saveBatch failed to start transaction:" << db.lastError().text();
        return false;
    }
    
    bool success = true;
    for (const ChannelData& channel : channels) {
        // Check if exists
        ChannelData existing = findByProfileAndChannelId(profileId, channel.channelId);
        
        if (existing.id > 0) {
            // Update existing
            ChannelData updated = channel;
            updated.id = existing.id;
            updated.profileId = profileId;
            if (!update(updated)) {
                success = false;
                break;
            }
        } else {
            // Create new
            ChannelData newChannel = channel;
            newChannel.profileId = profileId;
            if (create(newChannel) < 0) {
                success = false;
                break;
            }
        }
    }
    
    if (success) {
        if (!db.commit()) {
            qWarning() << "ChannelRepository::saveBatch failed to commit transaction:" << db.lastError().text();
            db.rollback();
            success = false;
        }
    } else {
        db.rollback();
        qWarning() << "ChannelRepository::saveBatch failed, rolled back transaction";
    }
    
    return success;
}

bool ChannelRepository::deleteByProfile(qint64 profileId)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM channels WHERE profile_id = ?");
    query.addBindValue(profileId);
    
    if (!query.exec()) {
        qWarning() << "ChannelRepository::deleteByProfile failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QString ChannelRepository::colorToString(const QColor& color)
{
    if (!color.isValid()) {
        return QString();
    }
    return color.name(); // Returns #RRGGBB format
}

QColor ChannelRepository::stringToColor(const QString& colorStr)
{
    if (colorStr.isEmpty()) {
        return QColor();
    }
    return QColor(colorStr);
}

ChannelData ChannelRepository::mapResultToData(const QSqlQuery& query)
{
    ChannelData data;
    
    data.id = query.value("id").toLongLong();
    data.profileId = query.value("profile_id").toLongLong();
    data.channelId = query.value("channel_id").toUInt();
    data.channelCode = query.value("channel_code").toString();
    data.type = query.value("type").toInt();
    data.enabled = query.value("enabled").toBool();
    
    data.defaultColor = stringToColor(query.value("default_color").toString());
    data.fullname = query.value("fullname").toString();
    data.label = query.value("label").toString();
    data.description = query.value("description").toString();
    
    data.lowerThreshold = query.value("lower_threshold").toDouble();
    data.lowerThresholdColor = stringToColor(query.value("lower_threshold_color").toString());
    data.upperThreshold = query.value("upper_threshold").toDouble();
    data.upperThresholdColor = stringToColor(query.value("upper_threshold_color").toString());
    
    data.showInOverview = query.value("show_in_overview").toBool();
    
    return data;
}
