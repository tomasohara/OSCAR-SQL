/* Session Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file defines the SessionRepository class for database access
 * to session data (core session metadata and timing).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SESSION_REPOSITORY_H
#define SESSION_REPOSITORY_H

#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct SessionData
 * \brief Data structure for session records
 */
struct SessionData
{
    qint64 id = 0;                      // Database primary key
    qint64 sessionId = 0;               // Session ID (computed from date)
    qint64 machineId = 0;               // Foreign key to machines table
    
    // Timing
    qint64 startTime = 0;               // ms since epoch
    qint64 endTime = 0;                 // ms since epoch
    qint64 duration = 0;                // milliseconds
    
    // Status flags
    int enabled = 1;                    // 0=disabled, 1=enabled, 2=other
    int summaryOnly = 0;                // Boolean
    int noSettings = 0;                 // Boolean
    int eventsLoaded = 0;               // Boolean
    
    // File references
    QString eventsFile;                 // Path to events file (relative)
    QString summaryFile;                // Path to summary file (relative)
    
    // Metadata
    QDateTime createdAt;
    QDateTime updatedAt;
};

/*!
 * \class SessionRepository
 * \brief Repository for CRUD operations on sessions table
 */
class SessionRepository
{
public:
    SessionRepository();
    ~SessionRepository();
    
    /*!
     * \brief Create a new session record
     * \param data Session data to insert
     * \return Database ID of created session, or -1 on error
     */
    qint64 create(const SessionData& data);
    
    /*!
     * \brief Update an existing session record
     * \param data Session data with valid id
     * \return true if successful, false otherwise
     */
    bool update(const SessionData& data);
    
    /*!
     * \brief Find session by database ID
     * \param id Database primary key
     * \return SessionData (id will be 0 if not found)
     */
    SessionData findById(qint64 id);
    
    /*!
     * \brief Find session by machine ID and session ID
     * \param machineId Machine database ID
     * \param sessionId Session ID (computed from date)
     * \return SessionData (id will be 0 if not found)
     */
    SessionData findByMachineAndSessionId(qint64 machineId, qint64 sessionId);
    
    /*!
     * \brief Get all sessions for a machine
     * \param machineId Machine database ID
     * \return List of session data
     */
    QList<SessionData> findByMachine(qint64 machineId);
    
    /*!
     * \brief Get enabled sessions for a machine
     * \param machineId Machine database ID
     * \return List of enabled session data
     */
    QList<SessionData> findEnabledByMachine(qint64 machineId);
    
    /*!
     * \brief Get sessions in time range
     * \param machineId Machine database ID
     * \param startTime Start of range (ms since epoch)
     * \param endTime End of range (ms since epoch)
     * \return List of session data
     */
    QList<SessionData> findByTimeRange(qint64 machineId, qint64 startTime, qint64 endTime);
    
    /*!
     * \brief Delete a session and all related data (CASCADE)
     * \param id Database primary key
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Delete all sessions for a machine
     * \param machineId Machine database ID
     * \return true if successful, false otherwise
     */
    bool removeByMachine(qint64 machineId);
    
    /*!
     * \brief Count sessions for a machine
     * \param machineId Machine database ID
     * \return Number of sessions
     */
    int countByMachine(qint64 machineId);
    
    /*!
     * \brief Check if session exists
     * \param machineId Machine database ID
     * \param sessionId Session ID
     * \return true if exists, false otherwise
     */
    bool exists(qint64 machineId, qint64 sessionId);
};

#endif // SESSION_REPOSITORY_H
