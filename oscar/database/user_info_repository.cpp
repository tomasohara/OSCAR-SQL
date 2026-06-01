/* User Info Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the UserInfoRepository class for database
 * access to user personal information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "user_info_repository.h"
#include "database_manager.h"
#include "../SleepLib/profiles.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

UserInfoRepository::UserInfoRepository()
{
}

UserInfoRepository::~UserInfoRepository()
{
}

qint64 UserInfoRepository::create(const UserInfoData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "UserInfoRepository::create() - Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO user_info "
        "(profile_id, dob, first_name, last_name, address, phone, email, "
        " country, height, gender, timezone) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.profileId);
    query.addBindValue(data.dob);
    query.addBindValue(data.firstName);
    query.addBindValue(data.lastName);
    query.addBindValue(data.address);
    query.addBindValue(data.phone);
    query.addBindValue(data.email);
    query.addBindValue(data.country);
    query.addBindValue(data.height);
    query.addBindValue(data.gender);
    query.addBindValue(data.timezone);

    if (!query.exec()) {
        qWarning() << "UserInfoRepository::create() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("UserInfoRepository::create", query);
        return -1;
    }

    qint64 id = query.lastInsertId().toLongLong();
    qDebug() << "UserInfoRepository: Created user_info record with id" << id;
    return id;
}

bool UserInfoRepository::update(const UserInfoData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "UserInfoRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE user_info SET "
        "dob = ?, first_name = ?, last_name = ?, address = ?, phone = ?, "
        "email = ?, country = ?, height = ?, gender = ?, timezone = ?, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );

    query.addBindValue(data.dob);
    query.addBindValue(data.firstName);
    query.addBindValue(data.lastName);
    query.addBindValue(data.address);
    query.addBindValue(data.phone);
    query.addBindValue(data.email);
    query.addBindValue(data.country);
    query.addBindValue(data.height);
    query.addBindValue(data.gender);
    query.addBindValue(data.timezone);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "UserInfoRepository::update() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("UserInfoRepository::update", query);
        return false;
    }

    qDebug() << "UserInfoRepository: Updated user_info record id" << data.id;
    return true;
}

UserInfoData UserInfoRepository::findByProfile(qint64 profileId)
{
    UserInfoData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "UserInfoRepository::findByProfile() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, profile_id, dob, first_name, last_name, address, phone, "
        "email, country, height, gender, timezone "
        "FROM user_info WHERE profile_id = ?"
    );
    query.addBindValue(profileId);

    if (!query.exec()) {
        qWarning() << "UserInfoRepository::findByProfile() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("UserInfoRepository::findByProfile", query);
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.profileId = query.value(1).toLongLong();
        data.dob = query.value(2).toString();
        data.firstName = query.value(3).toString();
        data.lastName = query.value(4).toString();
        data.address = query.value(5).toString();
        data.phone = query.value(6).toString();
        data.email = query.value(7).toString();
        data.country = query.value(8).toString();
        data.height = query.value(9).toDouble();
        data.gender = query.value(10).toInt();
        data.timezone = query.value(11).toString();
    }

    return data;
}

bool UserInfoRepository::saveFromUserInfo(qint64 profileId, UserInfo* userInfo)
{
    if (!userInfo) {
        qWarning() << "UserInfoRepository::saveFromUserInfo() - userInfo is null";
        return false;
    }

    // Convert UserInfo to UserInfoData
    UserInfoData data;
    data.profileId = profileId;
    data.dob = userInfo->DOB().toString(Qt::ISODate);
    data.firstName = userInfo->firstName();
    data.lastName = userInfo->lastName();
    data.address = userInfo->address();
    data.phone = userInfo->phone();
    data.email = userInfo->email();
    data.country = userInfo->country();
    data.height = userInfo->height();
    data.gender = (int)userInfo->gender();
    data.timezone = userInfo->timeZone();

    // Check if record exists
    UserInfoData existing = findByProfile(profileId);
    
    if (existing.id > 0) {
        // Update existing record
        data.id = existing.id;
        return update(data);
    } else {
        // Create new record
        qint64 id = create(data);
        return id > 0;
    }
}

bool UserInfoRepository::loadIntoUserInfo(qint64 profileId, UserInfo* userInfo)
{
    if (!userInfo) {
        qWarning() << "UserInfoRepository::loadIntoUserInfo(): UserInfo is null";
        return false;
    }

    UserInfoData data = findByProfile(profileId);
    
    if (data.id == 0) {
        qDebug() << "UserInfoRepository::loadIntoUserInfo(): No user_info found for profile" << profileId;
        return false;
    }

    // Load data into UserInfo object
    if (!data.dob.isEmpty()) {
        userInfo->setDOB(QDate::fromString(data.dob, Qt::ISODate));
    }
    userInfo->setFirstName(data.firstName);
    userInfo->setLastName(data.lastName);
    userInfo->setAddress(data.address);
    userInfo->setPhone(data.phone);
    userInfo->setEmail(data.email);
    userInfo->setCountry(data.country);
    userInfo->setHeight(data.height);
    userInfo->setGender((Gender)data.gender);
    userInfo->setTimeZone(data.timezone);

//    qDebug() << "UserInfoRepository: Loaded user_info for profile" << profileId;
    return true;
}

bool UserInfoRepository::remove(qint64 profileId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "UserInfoRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM user_info WHERE profile_id = ?");
    query.addBindValue(profileId);

    if (!query.exec()) {
        qWarning() << "UserInfoRepository::remove() failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("UserInfoRepository::remove", query);
        return false;
    }

    qDebug() << "UserInfoRepository: Removed user_info for profile" << profileId;
    return true;
}

UserInfoData UserInfoRepository::prepareForDatabase(const UserInfoData& data)
{
    // Currently no special preparation needed
    // In the future, could add validation, sanitization, etc.
    return data;
}
