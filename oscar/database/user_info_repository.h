/* User Info Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the UserInfoRepository class which provides
 * database access for user personal information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef USER_INFO_REPOSITORY_H
#define USER_INFO_REPOSITORY_H

#include <QString>
#include <QDate>

// Forward declaration
class UserInfo;

/*!
 * \struct UserInfoData
 * \brief Data structure representing user personal information
 */
struct UserInfoData
{
    qint64 id = 0;
    qint64 profileId = 0;
    QString dob;           // ISO date string
    QString firstName;
    QString lastName;
    QString address;
    QString phone;
    QString email;
    QString country;
    double height = 0.0;
    int gender = 0;        // 0=NotSpecified, 1=Male, 2=Female
    QString timezone;
    QString passwordHash;  // SHA1 hash if password set
};

/*!
 * \class UserInfoRepository
 * \brief Repository for user_info table operations
 *
 * Provides CRUD operations for user personal information.
 * Handles conversion between UserInfo objects and database records.
 */
class UserInfoRepository
{
public:
    UserInfoRepository();
    ~UserInfoRepository();

    /*!
     * \brief Create a new user_info record
     * \param data User info data to insert
     * \return Record ID if successful, -1 on error
     */
    qint64 create(const UserInfoData& data);

    /*!
     * \brief Update an existing user_info record
     * \param data User info data with id set
     * \return true if successful, false otherwise
     */
    bool update(const UserInfoData& data);

    /*!
     * \brief Find user_info by profile ID
     * \param profileId Profile ID to search for
     * \return UserInfoData structure (id=0 if not found)
     */
    UserInfoData findByProfile(qint64 profileId);

    /*!
     * \brief Save UserInfo object to database
     * \param profileId Profile ID to associate with
     * \param userInfo UserInfo object to save
     * \return true if successful, false otherwise
     *
     * Creates or updates user_info record based on whether it exists.
     */
    bool saveFromUserInfo(qint64 profileId, UserInfo* userInfo);

    /*!
     * \brief Load database data into UserInfo object
     * \param profileId Profile ID to load from
     * \param userInfo UserInfo object to populate
     * \return true if successful, false otherwise
     */
    bool loadIntoUserInfo(qint64 profileId, UserInfo* userInfo);

    /*!
     * \brief Delete user_info record by profile ID
     * \param profileId Profile ID
     * \return true if successful, false otherwise
     */
    bool remove(qint64 profileId);

private:
    /*!
     * \brief Convert UserInfoData to database-ready format
     * \param data Source data
     * \return UserInfoData with proper formatting
     */
    UserInfoData prepareForDatabase(const UserInfoData& data);
};

#endif // USER_INFO_REPOSITORY_H
