/* Channel Options Repository Header
 *
 * Copyright (c) 2025 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CHANNEL_OPTIONS_REPOSITORY_H
#define CHANNEL_OPTIONS_REPOSITORY_H

#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QHash>
#include "SleepLib/machine_common.h"

/**
 * @brief Data structure for channel lookup options
 * 
 * Stores key/value pairs for LOOKUP-type channels (e.g., CPAP_Mode: 0="CPAP", 1="APAP")
 */
struct ChannelOptionData {
    qint64 id = 0;
    ChannelID channelId = 0;
    int optionKey = 0;
    QString optionValue;
};

/**
 * @brief Repository for managing channel lookup options
 * 
 * Handles storage of option values for LOOKUP DataType channels.
 * These are global (not profile-specific) and populated from schema.cpp.
 */
class ChannelOptionsRepository
{
public:
    ChannelOptionsRepository();
    explicit ChannelOptionsRepository(const QSqlDatabase& database);
    
    // CRUD operations
    qint64 create(const ChannelOptionData& data);
    bool update(const ChannelOptionData& data);
    bool remove(qint64 id);
    
    // Queries
    ChannelOptionData findById(qint64 id);
    QList<ChannelOptionData> findByChannel(ChannelID channelId);
    ChannelOptionData findByChannelAndKey(ChannelID channelId, int key);
    QHash<int, QString> getOptionsHash(ChannelID channelId);
    
    // Batch operations
    bool saveBatch(ChannelID channelId, const QHash<int, QString>& options);
    bool deleteByChannel(ChannelID channelId);
    
    // Check if options exist for channel
    bool hasOptions(ChannelID channelId);
    
private:
    QSqlDatabase db;
    
    ChannelOptionData mapResultToData(const QSqlQuery& query);
};

#endif // CHANNEL_OPTIONS_REPOSITORY_H
