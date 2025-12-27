/* Session Slices Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file defines the SessionSlicesRepository class for database
 * access to session slices (mask on/off periods).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SESSION_SLICES_REPOSITORY_H
#define SESSION_SLICES_REPOSITORY_H

#include <QString>
#include <QList>

/*!
 * \struct SessionSliceData
 * \brief Data structure for session slice records
 */
struct SessionSliceData
{
    qint64 id = 0;                      // Database primary key
    qint64 sessionId = 0;               // Foreign key to sessions table
    qint64 startTime = 0;               // ms since epoch
    qint64 endTime = 0;                 // ms since epoch
    int status = 0;                     // 0=Unknown, 1=EquipmentOff, 2=MaskOn, 3=MaskOff
};

/*!
 * \class SessionSlicesRepository
 * \brief Repository for CRUD operations on session_slices table
 */
class SessionSlicesRepository
{
public:
    SessionSlicesRepository();
    ~SessionSlicesRepository();
    
    /*!
     * \brief Create a new session slice record
     * \param data Slice data to insert
     * \return Database ID of created slice, or -1 on error
     */
    qint64 create(const SessionSliceData& data);
    
    /*!
     * \brief Update an existing session slice
     * \param data Slice data with valid id
     * \return true if successful, false otherwise
     */
    bool update(const SessionSliceData& data);
    
    /*!
     * \brief Find all slices for a session
     * \param sessionId Session database ID
     * \return List of slice data
     */
    QList<SessionSliceData> findBySession(qint64 sessionId);
    
    /*!
     * \brief Save multiple slices at once (batch insert)
     * \param slices List of slices to save
     * \return true if successful, false otherwise
     */
    bool saveBatch(const QList<SessionSliceData>& slices);
    
    /*!
     * \brief Delete a specific slice
     * \param id Database primary key
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Delete all slices for a session
     * \param sessionId Session database ID
     * \return true if successful, false otherwise
     */
    bool removeBySession(qint64 sessionId);
    
    /*!
     * \brief Count slices for a session
     * \param sessionId Session database ID
     * \return Number of slices
     */
    int countBySession(qint64 sessionId);
};

#endif // SESSION_SLICES_REPOSITORY_H
