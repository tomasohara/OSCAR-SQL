/* EventList Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the EventListRepository class which handles
 * database operations for the event_lists table. This includes
 * creating, reading, updating, and deleting EventList metadata.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef EVENT_LIST_REPOSITORY_H
#define EVENT_LIST_REPOSITORY_H

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QString>
#include <QList>
#include "SleepLib/machine_common.h"

/*!
 * \struct EventListData
 * \brief Data structure representing an EventList's metadata
 *
 * This structure holds all metadata for an EventList including timing,
 * scaling, and dimensional information. Maps to the event_lists table.
 */
struct EventListData
{
    qint64 id;                      // Database primary key
    qint64 sessionId;               // Foreign key to sessions table
    qint64 profileId;               // Foreign key to profiles table (Schema v12 denormalization)
    ChannelID channelId;            // Channel identifier
    int eventlistIndex;             // Index for multiple EventLists per channel (0, 1, 2...)
    
    // EventList metadata
    int eventType;                  // 0=Event, 1=Waveform, 2=Gain, 3=Sum, 4=Span
    qint64 firstTime;               // Start time (ms since session start)
    qint64 lastTime;                // End time (ms since session start)
    qint32 count;                   // Number of data points
    EventDataType rate;             // Sample rate (ms per sample, 0 for events)
    
    // Data scaling
    EventDataType gain;
    EventDataType offset;
    EventDataType minValue;
    EventDataType maxValue;
    QString dimension;              // Units (e.g., "cmH2O", "L/min")
    
    // Second field support
    bool hasSecondField;
    EventDataType min2Value;
    EventDataType max2Value;
    
    // Data size tracking
    qint64 dataSize;                // Uncompressed data size in bytes
    qint64 compressedSize;          // Compressed size (if stored compressed)
    
    // Constructor with defaults
    EventListData()
        : id(0), sessionId(0), profileId(0), channelId(0), eventlistIndex(0)
        , eventType(0), firstTime(0), lastTime(0), count(0)
        , rate(0.0), gain(1.0), offset(0.0)
        , minValue(0.0), maxValue(0.0)
        , hasSecondField(false), min2Value(0.0), max2Value(0.0)
        , dataSize(0), compressedSize(0)
    {}
};

/*!
 * \class EventListRepository
 * \brief Repository for event_lists table operations
 *
 * This class provides CRUD operations for EventList metadata in the
 * event_lists database table. It handles storing and retrieving all
 * metadata associated with EventLists including timing, scaling,
 * and dimensional information.
 */
class EventListRepository
{
public:
    EventListRepository();
    ~EventListRepository();
    
    /*!
     * \brief Create a new EventList metadata record
     * \param data EventList metadata to store
     * \return Database ID of created record, or -1 on failure
     */
    qint64 create(const EventListData& data);
    
    /*!
     * \brief Find all EventLists for a session
     * \param sessionId Database session ID
     * \return List of EventList metadata records
     */
    QList<EventListData> findBySession(qint64 sessionId);
    
    /*!
     * \brief Find EventLists for a specific channel in a session
     * \param sessionId Database session ID
     * \param channelId Channel identifier
     * \return List of EventList metadata records (may be multiple)
     */
    QList<EventListData> findByChannel(qint64 sessionId, ChannelID channelId);
    
    /*!
     * \brief Find a specific EventList by session, channel, and index
     * \param sessionId Database session ID
     * \param channelId Channel identifier
     * \param eventlistIndex Index (0 for first, 1 for second, etc.)
     * \return EventList metadata, or empty struct if not found
     */
    EventListData findByIndex(qint64 sessionId, ChannelID channelId, int eventlistIndex);
    
    /*!
     * \brief Update an existing EventList metadata record
     * \param data EventList metadata with valid id field
     * \return true if successful, false otherwise
     */
    bool update(const EventListData& data);
    
    /*!
     * \brief Delete an EventList metadata record
     * \param id Database ID of record to delete
     * \return true if successful, false otherwise
     */
    bool deleteById(qint64 id);
    
    /*!
     * \brief Delete all EventLists for a session
     * \param sessionId Database session ID
     * \return true if successful, false otherwise
     */
    bool deleteBySession(qint64 sessionId);
    
    /*!
     * \brief Get count of EventLists for a session
     * \param sessionId Database session ID
     * \return Number of EventLists
     */
    int countBySession(qint64 sessionId);
    
    /*!
     * \brief Get total data size for a session
     * \param sessionId Database session ID
     * \param compressed If true, return compressed size; if false, uncompressed
     * \return Total size in bytes
     */
    qint64 getTotalSize(qint64 sessionId, bool compressed = true);

private:
    QSqlDatabase getDatabase();
    EventListData mapFromQuery(class QSqlQuery& query);
    
    // Prepared statement caching for performance during bulk import
    QSqlQuery m_createQuery;
    bool m_createPrepared = false;
    void prepareCreateStatement();
};

#endif // EVENT_LIST_REPOSITORY_H
