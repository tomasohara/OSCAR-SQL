/* Session Settings Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file defines the SessionSettingsRepository class for database
 * access to session settings (machine configuration per session).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SESSION_SETTINGS_REPOSITORY_H
#define SESSION_SETTINGS_REPOSITORY_H

#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct SessionSettingData
 * \brief Data structure for session setting records
 */
struct SessionSettingData
{
    qint64 id = 0;                      // Database primary key
    qint64 sessionId = 0;               // Foreign key to sessions table
    int channelId = 0;                  // Channel ID (e.g., CPAP_Pressure)
    double value = 0.0;                 // Setting value
    QString dataType;                   // 'int', 'float', 'bool', 'string'
    QDateTime createdAt;
};

/*!
 * \class SessionSettingsRepository
 * \brief Repository for CRUD operations on session_settings table
 */
class SessionSettingsRepository
{
public:
    SessionSettingsRepository();
    ~SessionSettingsRepository();
    
    /*!
     * \brief Create a new session setting record
     * \param data Setting data to insert
     * \return Database ID of created setting, or -1 on error
     */
    qint64 create(const SessionSettingData& data);
    
    /*!
     * \brief Update an existing session setting
     * \param data Setting data with valid id
     * \return true if successful, false otherwise
     */
    bool update(const SessionSettingData& data);
    
    /*!
     * \brief Find all settings for a session
     * \param sessionId Session database ID
     * \return List of setting data
     */
    QList<SessionSettingData> findBySession(qint64 sessionId);
    
    /*!
     * \brief Find specific setting by channel ID
     * \param sessionId Session database ID
     * \param channelId Channel ID to find
     * \return SessionSettingData (id will be 0 if not found)
     */
    SessionSettingData findBySetting(qint64 sessionId, int channelId);
    
    /*!
     * \brief Save multiple settings at once (batch insert/update)
     * \param sessionId Session database ID
     * \param settings List of settings to save
     * \return true if successful, false otherwise
     */
    bool saveBatch(qint64 sessionId, const QList<SessionSettingData>& settings);
    
    /*!
     * \brief Delete a specific setting
     * \param id Database primary key
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Delete all settings for a session
     * \param sessionId Session database ID
     * \return true if successful, false otherwise
     */
    bool removeBySession(qint64 sessionId);
    
    /*!
     * \brief Count settings for a session
     * \param sessionId Session database ID
     * \return Number of settings
     */
    int countBySession(qint64 sessionId);
};

#endif // SESSION_SETTINGS_REPOSITORY_H
