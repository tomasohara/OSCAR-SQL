/* Respiratory Events Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file manages database operations for individual respiratory events
 * (Obstructive Apnea, Hypopnea, RERA, Clear Airway, etc.)
 */

#ifndef RESPIRATORY_EVENTS_REPOSITORY_H
#define RESPIRATORY_EVENTS_REPOSITORY_H

#include <QString>
#include <QList>
#include <QSqlQuery>

/**
 * @brief Data structure for respiratory event records
 * 
 * Maps to respiratory_events table in database
 */
struct RespiratoryEventData {
    qint64 id;              ///< Database primary key (0 for new records)
    qint64 sessionId;       ///< Foreign key to sessions table
    qint64 profileId;       ///< Foreign key to profiles table (Schema v12 denormalization)
    int channelId;          ///< Channel ID for this event (Schema v12 denormalization)
    int eventType;          ///< 0=OA, 1=UA, 2=H, 3=RERA, 4=CAA, 5=User
    qint64 startTime;       ///< Event start time (Unix timestamp in milliseconds)
    qint64 endTime;         ///< Event end time (Unix timestamp in milliseconds)
    int duration;           ///< Event duration in seconds
    double desaturation;    ///< Optional SpO2 desaturation (%)
    int severity;           ///< Optional severity rating
    
    RespiratoryEventData() 
        : id(0), sessionId(0), profileId(0), channelId(0), eventType(0), 
          startTime(0), endTime(0), duration(0),
          desaturation(0.0), severity(0) {}
};

/**
 * @brief Repository for managing respiratory_events table
 * 
 * Provides CRUD operations for individual respiratory events.
 * These events are extracted from EventLists and stored as individual
 * rows for efficient querying without decompressing BLOB data.
 */
class RespiratoryEventsRepository
{
public:
    RespiratoryEventsRepository();
    
    /**
     * @brief Insert a single respiratory event
     * @param data Event data to insert
     * @return Database ID of created record, or -1 on error
     */
    qint64 create(const RespiratoryEventData& data);
    
    /**
     * @brief Insert multiple respiratory events in a batch
     * @param events List of events to insert
     * @return true if all events inserted successfully
     * 
     * Uses transaction for performance and atomicity
     */
    bool createBatch(const QList<RespiratoryEventData>& events);
    
    /**
     * @brief Find all respiratory events for a session
     * @param sessionId Database ID of session
     * @return List of events ordered by start_time
     */
    QList<RespiratoryEventData> findBySession(qint64 sessionId);
    
    /**
     * @brief Find respiratory events by type for a session
     * @param sessionId Database ID of session
     * @param eventType Event type (0=OA, 1=UA, 2=H, 3=RERA, 4=CAA)
     * @return List of events of specified type
     */
    QList<RespiratoryEventData> findByType(qint64 sessionId, int eventType);
    
    /**
     * @brief Find events within a time range
     * @param sessionId Database ID of session
     * @param startTime Range start (Unix timestamp in ms)
     * @param endTime Range end (Unix timestamp in ms)
     * @return List of events overlapping the time range
     */
    QList<RespiratoryEventData> findInTimeRange(qint64 sessionId, qint64 startTime, qint64 endTime);
    
    /**
     * @brief Delete all respiratory events for a session
     * @param sessionId Database ID of session
     * @return true if deletion successful
     */
    bool deleteBySession(qint64 sessionId);
    
    /**
     * @brief Count events by type for a session
     * @param sessionId Database ID of session
     * @return Map of event_type to count
     */
    QMap<int, int> countByType(qint64 sessionId);
    
private:
    /**
     * @brief Helper to populate RespiratoryEventData from QSqlQuery
     * @param query Active query positioned at a result row
     * @return Populated RespiratoryEventData structure
     */
    RespiratoryEventData populateFromQuery(const QSqlQuery& query);
};

#endif // RESPIRATORY_EVENTS_REPOSITORY_H
