/* Doctor Info Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the DoctorInfoRepository class which provides
 * database access for doctor/medical provider information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTOR_INFO_REPOSITORY_H
#define DOCTOR_INFO_REPOSITORY_H

#include <QString>

// Forward declaration
class DoctorInfo;

/*!
 * \struct DoctorInfoData
 * \brief Data structure representing doctor/medical provider information
 */
struct DoctorInfoData
{
    qint64 id = 0;
    qint64 profileId = 0;
    QString name;
    QString phone;
    QString email;
    QString practiceName;
    QString address;
    QString patientId;
};

/*!
 * \class DoctorInfoRepository
 * \brief Repository for doctor_info table operations
 *
 * Provides CRUD operations for doctor/medical provider information.
 * Handles conversion between DoctorInfo objects and database records.
 */
class DoctorInfoRepository
{
public:
    DoctorInfoRepository();
    ~DoctorInfoRepository();

    /*!
     * \brief Create a new doctor_info record
     * \param data Doctor info data to insert
     * \return Record ID if successful, -1 on error
     */
    qint64 create(const DoctorInfoData& data);

    /*!
     * \brief Update an existing doctor_info record
     * \param data Doctor info data with id set
     * \return true if successful, false otherwise
     */
    bool update(const DoctorInfoData& data);

    /*!
     * \brief Find doctor_info by profile ID
     * \param profileId Profile ID to search for
     * \return DoctorInfoData structure (id=0 if not found)
     */
    DoctorInfoData findByProfile(qint64 profileId);

    /*!
     * \brief Save DoctorInfo object to database
     * \param profileId Profile ID to associate with
     * \param doctorInfo DoctorInfo object to save
     * \return true if successful, false otherwise
     *
     * Creates or updates doctor_info record based on whether it exists.
     */
    bool saveFromDoctorInfo(qint64 profileId, DoctorInfo* doctorInfo);

    /*!
     * \brief Load database data into DoctorInfo object
     * \param profileId Profile ID to load from
     * \param doctorInfo DoctorInfo object to populate
     * \return true if successful, false otherwise
     */
    bool loadIntoDoctorInfo(qint64 profileId, DoctorInfo* doctorInfo);

    /*!
     * \brief Delete doctor_info record by profile ID
     * \param profileId Profile ID
     * \return true if successful, false otherwise
     */
    bool remove(qint64 profileId);
};

#endif // DOCTOR_INFO_REPOSITORY_H
