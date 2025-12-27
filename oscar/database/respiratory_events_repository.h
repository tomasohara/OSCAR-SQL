/* Respiratory Events Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file defines the RespiratoryEventsRepository class for database
 * access to respiratory events (apneas, hypopneas, RERAs, etc.).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RESPIRATORY_EVENTS_REPOSITORY_H
#define RESPIRATORY_EVENTS_REPOSITORY_H

#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct RespiratoryEventData
 * \brief Data structure for respiratory event records
 */
struct RespiratoryEventData
{
    qint64 id = 0;                      // Database primary key
    qint64 sessionId = 0;               // Foreign key to sessions table
    int eventType = 0;                  // Event type (OSA, CSA, Hypopnea, RERA, etc.)
    qint64 startTime = 0;               // ms since epoch
    qint64 endTime = 0;                 // ms since epoch
    int duration = 0;                   // seconds
    double desaturation = 0.0;          // SpO2 drop percentage (optional)
    int severity = 0;                   // Event severity 0-3 (optional)
    QDateTime createdAt;
};

/*!
 * \class RespiratoryEventsRepository
 * \brief Repository for CRUD operations on respiratory_events table
 */
class RespiratoryEventsRepository
{
public:
    RespiratoryEventsRepository();
    ~RespiratoryEventsRepository();
    
    /*!
     * \brief Create a new respiratory event record
     * \param data Event data to insert
     * \return Database ID of created event, or -1 on error
     */
    qint64 create(const RespiratoryEventData& data);
    
    /*!
     * \brief Update an existing respiratory event
     * \param data Event data with valid id
     * \return true if successful, false otherwise
     */
    bool update(const RespiratoryEventData& data);
    
    /*!
     * \brief Find all events for a session
     * \param sessionId Session database ID
     * \return List of event data
     */
    QList<RespiratoryEventData> findBySession(qint64 sessionId);
    
    /*!
     * \brief Find events by type
     * \param sessionId Session database ID
     * \param eventType Event type to filter
     * \return List of event data
     */
    QList<RespiratoryEventData> findByType(qint64 sessionId, int eventType);
    
    /*!
     * \brief Find events in time range
     * \param sessionId Session database ID
     * \param startTime Start of range (ms since epoch)
     * \param endTime End of range (ms since epoch)
     * \return List of event data
     */
    QList<RespiratoryEventData> findByTimeRange(qint64 sessionId, qint64 startTime, qint64 endTime);
    
    /*!
     * \brief Count events by type
     * \param sessionId Session database ID
     * \param eventType Event type to count
     * \return Number of events
     */
    int countByType(qint64 sessionId, int eventType);
    
    /*!
     * \brief Save multiple events at once (batch insert)
     * \param events List of events to save
     * \return true if successful, false otherwise
     */
    bool saveBatch(const QList<RespiratoryEventData>& events);
    
    /*!
     * \brief Delete a specific event
     * \param id Database primary key
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Delete all events for a session
     * \param sessionId Session database ID
     * \return true if successful, false otherwise
     */
    bool removeBySession(qint64 sessionId);
    
    /*!
     * \brief Count all events for a session
     * \param sessionId Session database ID
     * \return Number of events
     */
    int countBySession(qint64 sessionId);
};

#endif // RESPIRATORY_EVENTS_REPOSITORY_H
