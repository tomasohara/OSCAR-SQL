/* Preferences Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the PreferencesRepository class which provides
 * database access for profile preferences (key-value storage).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PREFERENCES_REPOSITORY_H
#define PREFERENCES_REPOSITORY_H

#include <QString>
#include <QList>
#include <QVariant>

// Forward declarations
class CPAPSettings;
class OxiSettings;
class SessionSettings;
class AppearanceSettings;
class UserSettings;

/*!
 * \struct PreferenceData
 * \brief Data structure representing a single preference key-value pair
 */
struct PreferenceData
{
    qint64 id = 0;
    qint64 profileId = 0;
    QString category;   // 'cpap', 'oxi', 'session', 'appearance', 'general'
    QString key;
    QString value;
    QString dataType;   // 'string', 'int', 'float', 'bool', 'date', 'time'
};

/*!
 * \class PreferencesRepository
 * \brief Repository for profile_preferences table operations
 *
 * Provides CRUD operations for profile preferences stored as key-value pairs.
 * Handles conversion between preference objects and database records.
 */
class PreferencesRepository
{
public:
    PreferencesRepository();
    ~PreferencesRepository();

    // Low-level CRUD operations
    qint64 create(const PreferenceData& data);
    bool update(const PreferenceData& data);
    PreferenceData find(qint64 profileId, const QString& category, const QString& key);
    QList<PreferenceData> findByProfile(qint64 profileId);
    QList<PreferenceData> findByCategory(qint64 profileId, const QString& category);
    bool remove(qint64 profileId);
    bool removeCategory(qint64 profileId, const QString& category);

    // High-level operations for preference objects
    bool savePreference(qint64 profileId, const QString& category, 
                       const QString& key, const QVariant& value);
    
    bool saveAllPreferences(qint64 profileId,
                           CPAPSettings* cpap,
                           OxiSettings* oxi,
                           SessionSettings* session,
                           AppearanceSettings* appearance,
                           UserSettings* general);
    
    bool loadAllPreferences(qint64 profileId,
                           CPAPSettings* cpap,
                           OxiSettings* oxi,
                           SessionSettings* session,
                           AppearanceSettings* appearance,
                           UserSettings* general);

private:
    // Helper methods
    QString dataTypeFromVariant(const QVariant& value);
    QVariant variantFromString(const QString& value, const QString& dataType);
    
    bool saveCPAPSettings(qint64 profileId, CPAPSettings* cpap);
    bool saveOxiSettings(qint64 profileId, OxiSettings* oxi);
    bool saveSessionSettings(qint64 profileId, SessionSettings* session);
    bool saveAppearanceSettings(qint64 profileId, AppearanceSettings* appearance);
    bool saveUserSettings(qint64 profileId, UserSettings* general);
    
    bool loadCPAPSettings(qint64 profileId, CPAPSettings* cpap);
    bool loadOxiSettings(qint64 profileId, OxiSettings* oxi);
    bool loadSessionSettings(qint64 profileId, SessionSettings* session);
    bool loadAppearanceSettings(qint64 profileId, AppearanceSettings* appearance);
    bool loadUserSettings(qint64 profileId, UserSettings* general);
};

#endif // PREFERENCES_REPOSITORY_H
