/* Profile Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ProfileRepository class which provides
 * database CRUD operations for user profiles.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PROFILE_REPOSITORY_H
#define PROFILE_REPOSITORY_H

#include <QString>
#include <QList>
#include <QSqlDatabase>

// Forward declarations to avoid circular dependencies
class Profile;

/*!
 * \struct ProfileData
 * \brief Simple data structure to hold profile information
 *
 * This struct represents a profile record in the database.
 * It's a plain data structure without the complexity of the
 * full Profile class.
 */
struct ProfileData
{
    qint64 id;              ///< Database primary key
    QString username;       ///< Unique username
    QString dataFolder;     ///< Path to profile data folder
    QString createdAt;      ///< Creation timestamp
    QString updatedAt;      ///< Last update timestamp

    ProfileData()
        : id(0)
    {}
};

/*!
 * \class ProfileRepository
 * \brief Repository for profile database operations
 *
 * This class provides CRUD (Create, Read, Update, Delete) operations
 * for profiles in the database. It abstracts all SQL operations and
 * provides a clean interface for profile data access.
 *
 * Usage:
 * \code
 * ProfileRepository repo;
 * ProfileData data;
 * data.username = "John";
 * data.dataFolder = "/path/to/data";
 * qint64 id = repo.create(data);
 * \endcode
 */
class ProfileRepository
{
public:
    ProfileRepository();
    ~ProfileRepository();

    /*!
     * \brief Create a new profile in the database
     * \param data Profile data to insert
     * \return Profile ID if successful, -1 on error
     *
     * Creates a new profile record. The id field in data is ignored
     * (auto-generated). Timestamps are set automatically.
     */
    qint64 create(const ProfileData& data);

    /*!
     * \brief Find a profile by database ID
     * \param id Database primary key
     * \return ProfileData if found, or null ProfileData (id=0) if not found
     */
    ProfileData findById(qint64 id);

    /*!
     * \brief Find a profile by username
     * \param username Username to search for
     * \return ProfileData if found, or null ProfileData (id=0) if not found
     */
    ProfileData findByUsername(const QString& username);

    /*!
     * \brief Get all profiles from database
     * \return List of all profile records
     */
    QList<ProfileData> findAll();

    /*!
     * \brief Update an existing profile
     * \param data Profile data with valid id field
     * \return true if successful, false otherwise
     *
     * Updates the profile identified by data.id. The updatedAt
     * timestamp is automatically set to current time.
     */
    bool update(const ProfileData& data);

    /*!
     * \brief Delete a profile from database
     * \param id Database primary key of profile to delete
     * \return true if successful, false otherwise
     *
     * Note: Foreign key constraints will cascade delete
     * all machines associated with this profile.
     */
    bool remove(qint64 id);

    /*!
     * \brief Check if a profile with given username exists
     * \param username Username to check
     * \return true if exists, false otherwise
     */
    bool exists(const QString& username);

    /*!
     * \brief Get count of profiles in database
     * \return Number of profile records
     */
    int count();

    /*!
     * \brief Resolve a portable profile path
     * \param portablePath Path that may contain %PROFDIR% variable
     * \param profilesBasePath Base path for profiles (e.g. GetAppData()+"/Profiles")
     * \return Resolved absolute path
     *
     * Replaces %PROFDIR% with the actual profiles base path.
     * Example: "%PROFDIR%/John" -> "C:/Users/.../OSCAR_Data/Profiles/John"
     */
    static QString resolvePath(const QString& portablePath, const QString& profilesBasePath);

private:
    /*!
     * \brief Get the database connection
     * \return Database instance from DatabaseManager
     */
    QSqlDatabase database();

    /*!
     * \brief Convert QSqlQuery result to ProfileData
     * \param query Query positioned at a result row
     * \return ProfileData populated from current row
     */
    ProfileData recordToData(class QSqlQuery& query);
};

#endif // PROFILE_REPOSITORY_H
