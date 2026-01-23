/* Doctor Info Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the DoctorInfoRepository class for database
 * access to doctor/medical provider information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctor_info_repository.h"
#include "database_manager.h"
#include "../SleepLib/profiles.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

DoctorInfoRepository::DoctorInfoRepository()
{
}

DoctorInfoRepository::~DoctorInfoRepository()
{
}

qint64 DoctorInfoRepository::create(const DoctorInfoData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "DoctorInfoRepository::create(): Database not open";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO doctor_info "
        "(profile_id, name, phone, email, practice_name, address, patient_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(data.profileId);
    query.addBindValue(data.name);
    query.addBindValue(data.phone);
    query.addBindValue(data.email);
    query.addBindValue(data.practiceName);
    query.addBindValue(data.address);
    query.addBindValue(data.patientId);

    if (!query.exec()) {
        qWarning() << "DoctorInfoRepository::create() failed:" << query.lastError().text();
        return -1;
    }

    qint64 id = query.lastInsertId().toLongLong();
//    qDebug() << "DoctorInfoRepository: Created doctor_info record with id" << id;
    return id;
}

bool DoctorInfoRepository::update(const DoctorInfoData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "DoctorInfoRepository::update() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "UPDATE doctor_info SET "
        "name = ?, phone = ?, email = ?, practice_name = ?, address = ?, "
        "patient_id = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );

    query.addBindValue(data.name);
    query.addBindValue(data.phone);
    query.addBindValue(data.email);
    query.addBindValue(data.practiceName);
    query.addBindValue(data.address);
    query.addBindValue(data.patientId);
    query.addBindValue(data.id);

    if (!query.exec()) {
        qWarning() << "DoctorInfoRepository::update() failed:" << query.lastError().text();
        return false;
    }

    qDebug() << "DoctorInfoRepository: Updated doctor_info record id" << data.id;
    return true;
}

DoctorInfoData DoctorInfoRepository::findByProfile(qint64 profileId)
{
    DoctorInfoData data;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "DoctorInfoRepository::findByProfile() - Database not open";
        return data;
    }

    QSqlQuery query(db);
    query.prepare(
        "SELECT id, profile_id, name, phone, email, practice_name, address, patient_id "
        "FROM doctor_info WHERE profile_id = ?"
    );
    query.addBindValue(profileId);

    if (!query.exec()) {
        qWarning() << "DoctorInfoRepository::findByProfile() failed:" << query.lastError().text();
        return data;
    }

    if (query.next()) {
        data.id = query.value(0).toLongLong();
        data.profileId = query.value(1).toLongLong();
        data.name = query.value(2).toString();
        data.phone = query.value(3).toString();
        data.email = query.value(4).toString();
        data.practiceName = query.value(5).toString();
        data.address = query.value(6).toString();
        data.patientId = query.value(7).toString();
    }

    return data;
}

bool DoctorInfoRepository::saveFromDoctorInfo(qint64 profileId, DoctorInfo* doctorInfo)
{
    if (!doctorInfo) {
        qWarning() << "DoctorInfoRepository::saveFromDoctorInfo() - doctorInfo is null";
        return false;
    }

    // Convert DoctorInfo to DoctorInfoData
    DoctorInfoData data;
    data.profileId = profileId;
    data.name = doctorInfo->name();
    data.phone = doctorInfo->phone();
    data.email = doctorInfo->email();
    data.practiceName = doctorInfo->practiceName();
    data.address = doctorInfo->address();
    data.patientId = doctorInfo->patientID();

    // Check if record exists
    DoctorInfoData existing = findByProfile(profileId);
    
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

bool DoctorInfoRepository::loadIntoDoctorInfo(qint64 profileId, DoctorInfo* doctorInfo)
{
    if (!doctorInfo) {
        qWarning() << "DoctorInfoRepository::loadIntoDoctorInfo() - doctorInfo is null";
        return false;
    }

    DoctorInfoData data = findByProfile(profileId);
    
    if (data.id == 0) {
        qDebug() << "DoctorInfoRepository: No doctor_info found for profile" << profileId;
        return false;
    }

    // Load data into DoctorInfo object
    doctorInfo->setName(data.name);
    doctorInfo->setPhone(data.phone);
    doctorInfo->setEmail(data.email);
    doctorInfo->setPracticeName(data.practiceName);
    doctorInfo->setAddress(data.address);
    doctorInfo->setPatientID(data.patientId);

//    qDebug() << "DoctorInfoRepository: Loaded doctor_info for profile" << profileId;
    return true;
}

bool DoctorInfoRepository::remove(qint64 profileId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "DoctorInfoRepository::remove() - Database not open";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM doctor_info WHERE profile_id = ?");
    query.addBindValue(profileId);

    if (!query.exec()) {
        qWarning() << "DoctorInfoRepository::remove() failed:" << query.lastError().text();
        return false;
    }

    qDebug() << "DoctorInfoRepository: Removed doctor_info for profile" << profileId;
    return true;
}
