/* Machine Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the MachineRepository class which provides
 * database CRUD operations for CPAP machines and related devices.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MACHINE_REPOSITORY_H
#define MACHINE_REPOSITORY_H

#include <QString>
#include <QList>
#include <QDateTime>
#include <QDate>
#include <QSqlDatabase>

// Forward declarations
class Machine;

/*!
 * \struct MachineData
 * \brief Simple data structure to hold machine information
 *
 * This struct represents a machine record in the database.
 * It corresponds to CPAP machines, oximeters, and other devices.
 */
struct MachineData
{
    qint64 id;                  ///< Database primary key
    qint64 profileId;           ///< Foreign key to profiles table
    qint64 machineId;           ///< OSCAR machine ID (unique per profile)
    QString loaderName;         ///< Loader plugin name (e.g. "ResMed", "Intellipap")
    int machineType;            ///< Machine type enumeration value
    QString brand;              ///< Manufacturer (e.g. "ResMed")
    QString model;              ///< Model name (e.g. "AirSense 10")
    QString series;             ///< Series name
    QString serialNumber;       ///< Device serial number
    QString modelNumber;        ///< Model number
    QString lastImported;       ///< Last import timestamp (ISO format)
    QString purgeDate;          ///< Data purge date (ISO format)
    int dataVersion;            ///< Data format version
    QString properties;         ///< JSON-encoded machine properties
    QString createdAt;          ///< Creation timestamp
    
    MachineData()
        : id(0)
        , profileId(0)
        , machineId(0)
        , machineType(0)
        , dataVersion(0)
    {}
};

/*!
 * \class MachineRepository
 * \brief Repository for machine database operations
 *
 * This class provides CRUD (Create, Read, Update, Delete) operations
 * for machines in the database. It abstracts all SQL operations and
 * provides a clean interface for machine data access.
 *
 * Usage:
 * \code
 * MachineRepository repo;
 * MachineData data;
 * data.profileId = 1;
 * data.machineId = 12345;
 * data.loaderName = "ResMed";
 * data.brand = "ResMed";
 * qint64 id = repo.create(data);
 * \endcode
 */
class MachineRepository
{
public:
    MachineRepository();
    ~MachineRepository();

    /*!
     * \brief Create a new machine in the database
     * \param data Machine data to insert
     * \return Machine database ID if successful, -1 on error
     *
     * Creates a new machine record. The id field in data is ignored
     * (auto-generated). The createdAt timestamp is set automatically.
     */
    qint64 create(const MachineData& data);

    /*!
     * \brief Find a machine by database ID
     * \param id Database primary key
     * \return MachineData if found, or null MachineData (id=0) if not found
     */
    MachineData findById(qint64 id);

    /*!
     * \brief Find a machine by profile ID and machine ID
     * \param profileId Profile database ID
     * \param machineId OSCAR machine ID
     * \return MachineData if found, or null MachineData (id=0) if not found
     */
    MachineData findByProfileAndMachineId(qint64 profileId, qint64 machineId);

    /*!
     * \brief Find a machine by serial number and loader name
     * \param serialNumber Device serial number
     * \param loaderName Loader plugin name
     * \return MachineData if found, or null MachineData (id=0) if not found
     */
    MachineData findBySerialAndLoader(const QString& serialNumber, const QString& loaderName);

    /*!
     * \brief Get all machines for a specific profile
     * \param profileId Profile database ID
     * \return List of machine records for this profile
     */
    QList<MachineData> findByProfile(qint64 profileId);

    /*!
     * \brief Get all machines in database
     * \return List of all machine records
     */
    QList<MachineData> findAll();

    /*!
     * \brief Update an existing machine
     * \param data Machine data with valid id field
     * \return true if successful, false otherwise
     */
    bool update(const MachineData& data);

    /*!
     * \brief Delete a machine from database
     * \param id Database primary key of machine to delete
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);

    /*!
     * \brief Check if a machine exists
     * \param profileId Profile database ID
     * \param machineId OSCAR machine ID
     * \return true if exists, false otherwise
     */
    bool exists(qint64 profileId, qint64 machineId);

    /*!
     * \brief Get count of machines for a profile
     * \param profileId Profile database ID (0 = all profiles)
     * \return Number of machine records
     */
    int count(qint64 profileId = 0);

private:
    /*!
     * \brief Get the database connection
     * \return Database instance from DatabaseManager
     */
    QSqlDatabase database();

    /*!
     * \brief Convert QSqlQuery result to MachineData
     * \param query Query positioned at a result row
     * \return MachineData populated from current row
     */
    MachineData recordToData(class QSqlQuery& query);
};

#endif // MACHINE_REPOSITORY_H
