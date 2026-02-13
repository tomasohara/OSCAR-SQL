/* Session Channels Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file defines the SessionChannelsRepository class for database
 * access to session channel statistics (summary stats per channel).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SESSION_CHANNELS_REPOSITORY_H
#define SESSION_CHANNELS_REPOSITORY_H

#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct SessionChannelData
 * \brief Data structure for session channel statistics
 */
struct SessionChannelData
{
    qint64 id = 0;                      // Database primary key
    qint64 sessionId = 0;               // Foreign key to sessions table
    qint64 profileId = 0;               // Foreign key to profiles table (Schema v12 denormalization)
    int channelId = 0;                  // Channel ID (e.g., CPAP_Pressure)
    
    // Summary statistics
    int count = 0;                      // Number of samples
    double sum = 0.0;                   // Sum of all values
    double avg = 0.0;                   // Average
    double wavg = 0.0;                  // Weighted average
    double min = 0.0;                   // Minimum value
    double max = 0.0;                   // Maximum value
    double median = 0.0;                // 50th percentile
    double p90 = 0.0;                   // 90th percentile
    double p95 = 0.0;                   // 95th percentile
    
    // Physical bounds for display
    double physMin = 0.0;
    double physMax = 0.0;
    
    // Rate calculations
    double cph = 0.0;                   // Count per hour (e.g., AHI)
    double sph = 0.0;                   // Sum per hour
    
    // Timing
    qint64 firstTime = 0;               // ms since epoch
    qint64 lastTime = 0;                // ms since epoch
    
    // Gain for waveform data
    double gain = 1.0;
    
    QDateTime createdAt;
};

/*!
 * \class SessionChannelsRepository
 * \brief Repository for CRUD operations on session_channels table
 */
class SessionChannelsRepository
{
public:
    SessionChannelsRepository();
    ~SessionChannelsRepository();
    
    /*!
     * \brief Create a new channel statistics record
     * \param data Channel data to insert
     * \return Database ID of created record, or -1 on error
     */
    qint64 create(const SessionChannelData& data);
    
    /*!
     * \brief Update an existing channel statistics
     * \param data Channel data with valid id
     * \return true if successful, false otherwise
     */
    bool update(const SessionChannelData& data);
    
    /*!
     * \brief Find all channels for a session
     * \param sessionId Session database ID
     * \return List of channel data
     */
    QList<SessionChannelData> findBySession(qint64 sessionId);
    
    /*!
     * \brief Find specific channel by channel ID
     * \param sessionId Session database ID
     * \param channelId Channel ID to find
     * \return SessionChannelData (id will be 0 if not found)
     */
    SessionChannelData findByChannel(qint64 sessionId, int channelId);
    
    /*!
     * \brief Save multiple channels at once (batch insert/update)
     * \param sessionId Session database ID
     * \param profileId Profile database ID (Schema v12 denormalization)
     * \param channels List of channels to save
     * \return true if successful, false otherwise
     */
    bool saveBatch(qint64 sessionId, qint64 profileId, const QList<SessionChannelData>& channels);
    
    /*!
     * \brief Delete a specific channel
     * \param id Database primary key
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Delete all channels for a session
     * \param sessionId Session database ID
     * \return true if successful, false otherwise
     */
    bool removeBySession(qint64 sessionId);
    
    /*!
     * \brief Count channels for a session
     * \param sessionId Session database ID
     * \return Number of channels
     */
    int countBySession(qint64 sessionId);
};

#endif // SESSION_CHANNELS_REPOSITORY_H
