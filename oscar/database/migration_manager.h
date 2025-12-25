/* Migration Manager Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the MigrationManager class which handles
 * migration of existing XML-based profile and machine data to
 * the SQLite database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MIGRATION_MANAGER_H
#define MIGRATION_MANAGER_H

#include <QString>
#include <QObject>

// Forward declarations
class Profile;
class ProfileRepository;
class MachineRepository;

/*!
 * \class MigrationManager
 * \brief Manages migration from XML files to database
 *
 * This class handles the migration of profile and machine data
 * from the legacy XML file format to the new SQLite database.
 * It can detect if migration is needed and perform the migration
 * with progress reporting.
 *
 * Usage:
 * \code
 * MigrationManager manager;
 * if (manager.isMigrationNeeded(profilePath)) {
 *     bool success = manager.migrateProfile(profilePath);
 * }
 * \endcode
 */
class MigrationManager : public QObject
{
    Q_OBJECT

public:
    MigrationManager(QObject* parent = nullptr);
    ~MigrationManager();

    /*!
     * \brief Check if migration is needed for a profile
     * \param profilePath Path to profile directory
     * \return true if machines.xml exists but database has no machines
     *
     * This checks if the profile has legacy machines.xml but the
     * database has not yet been populated.
     */
    bool isMigrationNeeded(const QString& profilePath);

    /*!
     * \brief Migrate a profile's data from XML to database
     * \param profilePath Path to profile directory
     * \return true if successful, false otherwise
     *
     * This performs the complete migration:
     * 1. Creates profile record if needed
     * 2. Reads machines.xml
     * 3. Parses and imports each machine
     * 4. Emits progress signals during migration
     */
    bool migrateProfile(const QString& profilePath);

    /*!
     * \brief Migrate a single profile and its machines
     * \param profile Pointer to Profile object to migrate
     * \return true if successful, false otherwise
     *
     * Alternative migration method that works with an existing
     * Profile object instead of a path.
     */
    bool migrateProfile(Profile* profile);

    /*!
     * \brief Migrate all profiles in the OSCAR data directory
     * \param profilesPath Path to Profiles directory (e.g. GetAppData()+"/Profiles")
     * \return Number of profiles successfully migrated
     *
     * Scans the Profiles directory and migrates all profile folders found.
     * Each subdirectory is treated as a separate profile.
     */
    int migrateAllProfiles(const QString& profilesPath);

    /*!
     * \brief Get the last error message
     * \return Error message from last operation, or empty string
     */
    QString lastError() const { return m_lastError; }

signals:
    /*!
     * \brief Emitted when migration progress changes
     * \param current Current progress (0-100)
     * \param total Maximum progress value (usually 100)
     * \param message Status message
     */
    void progressChanged(int current, int total, const QString& message);

    /*!
     * \brief Emitted when migration completes
     * \param success true if successful, false if failed
     */
    void migrationComplete(bool success);

private:
    /*!
     * \brief Read and parse machines.xml file
     * \param machinesXmlPath Full path to machines.xml
     * \return true if successful, false otherwise
     *
     * This reads the XML file and populates machine records
     * into the database.
     */
    bool parseMachinesXml(const QString& machinesXmlPath, qint64 profileId);

    /*!
     * \brief Get or create profile database record
     * \param profilePath Path to profile directory
     * \param username Username for the profile
     * \return Profile database ID, or -1 on error
     */
    qint64 getOrCreateProfile(const QString& profilePath, const QString& username);

    /*!
     * \brief Migrate extended profile data (user info, doctor info, preferences)
     * \param profile Pointer to Profile object with loaded data
     * \param profileId Database ID of the profile
     * \return true if successful, false otherwise
     *
     * This migrates user info, doctor info, and all preferences from
     * the Profile object into the extended database tables.
     */
    bool migrateExtendedData(Profile* profile, qint64 profileId);

    QString m_lastError;        ///< Last error message
    ProfileRepository* m_profileRepo;   ///< Profile repository
    MachineRepository* m_machineRepo;   ///< Machine repository
};

#endif // MIGRATION_MANAGER_H
