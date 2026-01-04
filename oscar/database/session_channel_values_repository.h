/* Session Channel Values Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file defines the SessionChannelValuesRepository class for database
 * access to session channel value/time summary data.
 *
 * This stores the m_valuesummary and m_timesummary data structures from
 * the Session class, which contain the count and time spent at each
 * distinct value for each channel.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SESSION_CHANNEL_VALUES_REPOSITORY_H
#define SESSION_CHANNEL_VALUES_REPOSITORY_H

#include <QString>
#include <QList>
#include <QHash>
#include <QDateTime>
#include "SleepLib/schema.h"

/*!
 * \struct SessionChannelValueData
 * \brief Data structure for individual channel value/time data
 */
struct SessionChannelValueData
{
    qint64 id = 0;                      // Database primary key
    qint64 sessionChannelId = 0;        // Foreign key to session_channels table
    qint16 value = 0;                   // The EventStoreType value
    qint32 count = 0;                   // How many times this value occurred
    quint32 timeMs = 0;                 // Total time in milliseconds at this value
    QDateTime createdAt;
};

/*!
 * \class SessionChannelValuesRepository
 * \brief Repository for CRUD operations on session_channel_values table
 */
class SessionChannelValuesRepository
{
public:
    SessionChannelValuesRepository();
    ~SessionChannelValuesRepository();
    
    /*!
     * \brief Create a new channel value record
     * \param data Channel value data to insert
     * \return Database ID of created record, or -1 on error
     */
    qint64 create(const SessionChannelValueData& data);
    
    /*!
     * \brief Find all values for a session channel
     * \param sessionChannelId Session channel database ID
     * \return List of value data
     */
    QList<SessionChannelValueData> findBySessionChannel(qint64 sessionChannelId);
    
    /*!
     * \brief Save multiple channel values at once (batch insert)
     * \param sessionChannelId Session channel database ID
     * \param values List of values to save
     * \return true if successful, false otherwise
     */
    bool saveBatch(qint64 sessionChannelId, const QList<SessionChannelValueData>& values);
    
    /*!
     * \brief Save value summary and time summary hashes for a channel
     * \param sessionChannelId Session channel database ID
     * \param valueSummary Hash of value -> count
     * \param timeSummary Hash of value -> time in milliseconds
     * \return true if successful, false otherwise
     */
    bool saveChannelSummaries(qint64 sessionChannelId, 
                              const QHash<EventStoreType, EventStoreType>& valueSummary,
                              const QHash<EventStoreType, quint32>& timeSummary);
    
    /*!
     * \brief Load value summary and time summary hashes for a channel
     * \param sessionChannelId Session channel database ID
     * \param valueSummary Output: Hash of value -> count
     * \param timeSummary Output: Hash of value -> time in milliseconds
     * \return true if successful, false otherwise
     */
    bool loadChannelSummaries(qint64 sessionChannelId,
                              QHash<EventStoreType, EventStoreType>& valueSummary,
                              QHash<EventStoreType, quint32>& timeSummary);
    
    /*!
     * \brief Delete all values for a session channel
     * \param sessionChannelId Session channel database ID
     * \return true if successful, false otherwise
     */
    bool removeBySessionChannel(qint64 sessionChannelId);
    
    /*!
     * \brief Count values for a session channel
     * \param sessionChannelId Session channel database ID
     * \return Number of distinct values
     */
    int countBySessionChannel(qint64 sessionChannelId);
};

#endif // SESSION_CHANNEL_VALUES_REPOSITORY_H
