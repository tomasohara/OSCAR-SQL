/* Channel Repository Header
 *
 * Copyright (c) 2025-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CHANNEL_REPOSITORY_H
#define CHANNEL_REPOSITORY_H

#include <QSqlDatabase>
#include <QColor>
#include <QString>
#include <QList>
#include "SleepLib/machine_common.h"

// Forward declaration
namespace schema {
    class Channel;
}

/**
 * @brief Data structure for channel preferences (per profile)
 */
struct ChannelData {
    qint64 id = 0;
    qint64 profileId = 0;
    ChannelID channelId = 0;
    QString channelCode;
    bool enabled = true;
    
    // Display properties
    QColor defaultColor;
    QString fullname;
    QString label;
    QString description;
    
    // Threshold settings
    double lowerThreshold = 0.0;
    QColor lowerThresholdColor;
    double upperThreshold = 0.0;
    QColor upperThresholdColor;
    
    // UI preferences
    bool showInOverview = false;
};

/**
 * @brief Repository for managing channel preferences in database
 * 
 * Handles per-profile channel customizations (colors, names, thresholds, etc.)
 */
class ChannelRepository
{
public:
    ChannelRepository();
    explicit ChannelRepository(const QSqlDatabase& database);
    
    // CRUD operations
    qint64 create(const ChannelData& data);
    bool update(const ChannelData& data);
    bool remove(qint64 id);
    
    // Queries
    ChannelData findById(qint64 id);
    QList<ChannelData> findByProfile(qint64 profileId);
    ChannelData findByProfileAndChannelId(qint64 profileId, ChannelID channelId);
    
    // Batch operations
    bool saveBatch(qint64 profileId, const QList<ChannelData>& channels);
    bool deleteByProfile(qint64 profileId);
    
    // Color conversion helpers
    static QString colorToString(const QColor& color);
    static QColor stringToColor(const QString& colorStr);
    
private:
    QSqlDatabase db;
    
    ChannelData mapResultToData(const QSqlQuery& query);
};

#endif // CHANNEL_REPOSITORY_H
