/* EventData Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the EventDataRepository class which handles
 * database operations for the event_data table. This includes storing
 * and retrieving compressed binary event/waveform data with integrity
 * verification.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef EVENT_DATA_REPOSITORY_H
#define EVENT_DATA_REPOSITORY_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QByteArray>
#include <QVector>
#include "SleepLib/machine_common.h"

// Forward declarations
class EventList;

/*!
 * \struct EventBinaryData
 * \brief Container for binary event/waveform data
 *
 * This structure holds the raw binary arrays for EventList data
 * including primary data, optional secondary data, and time deltas.
 */
struct EventBinaryData
{
    QByteArray primaryData;         // Primary data array (qint16[])
    QByteArray secondaryData;       // Secondary data array (qint16[], optional)
    QByteArray timeData;            // Time delta array (quint32[], for events only)
    
    int compressionMethod;          // 0=none, 1=qCompress
    quint16 checksum;               // CRC16 checksum of uncompressed data
    
    // Constructor
    EventBinaryData()
        : compressionMethod(0), checksum(0)
    {}
};

/*!
 * \class EventDataRepository
 * \brief Repository for event_data table operations
 *
 * This class provides storage and retrieval of binary event/waveform data
 * in the event_data table. It handles:
 * - Compression/decompression using qCompress/qUncompress
 * - Checksum calculation and verification
 * - Binary serialization of qint16 and quint32 arrays
 * - BLOB storage in SQLite database
 */
class EventDataRepository
{
public:
    EventDataRepository();
    ~EventDataRepository();
    
    /*!
     * \brief Reset cached prepared statements
     *
     * Call this if you need to force re-preparation of statements
     * (e.g., after database reconnection)
     */
    void resetPreparedStatements();
    
    /*!
     * \brief Store EventList binary data in database
     * \param eventlistId Database ID from event_lists table
     * \param eventList EventList object containing data to store
     * \return true if successful, false otherwise
     *
     * Serializes EventList data arrays to binary format, compresses if
     * beneficial (>10% savings), calculates checksum, and stores in database.
     */
    bool storeEventListData(qint64 eventlistId, EventList* eventList);
    
    /*!
     * \brief Load EventList binary data from database
     * \param eventlistId Database ID from event_lists table
     * \param eventList EventList object to populate with data
     * \return true if successful, false otherwise
     *
     * Retrieves binary data from database, decompresses if needed,
     * verifies checksum, and populates EventList data arrays.
     */
    bool loadEventListData(qint64 eventlistId, EventList* eventList);
    
    /*!
     * \brief Delete binary data for an EventList
     * \param eventlistId Database ID from event_lists table
     * \return true if successful, false otherwise
     */
    bool deleteByEventList(qint64 eventlistId);
    
    /*!
     * \brief Check if binary data exists for an EventList
     * \param eventlistId Database ID from event_lists table
     * \return true if data exists, false otherwise
     */
    bool exists(qint64 eventlistId);
    
    /*!
     * \brief Serialize EventList data to binary format
     * \param eventList EventList object to serialize
     * \return EventBinaryData structure with serialized arrays
     *
     * Static helper to convert EventList data arrays to binary format.
     * Can be used for testing or alternative storage mechanisms.
     */
    static EventBinaryData serializeEventList(EventList* eventList);
    
    /*!
     * \brief Deserialize binary data into EventList
     * \param binaryData Binary data to deserialize
     * \param eventList EventList object to populate
     * \return true if successful, false otherwise
     *
     * Static helper to populate EventList from binary data.
     * Can be used for testing or alternative storage mechanisms.
     */
    static bool deserializeEventList(const EventBinaryData& binaryData, EventList* eventList);
    
    /*!
     * \brief Calculate checksum for binary data
     * \param data Binary data to checksum
     * \return CRC16 checksum value
     *
     * Static helper to calculate checksums for data integrity verification.
     */
    static quint16 calculateChecksum(const QByteArray& data);
    
    /*!
     * \brief Compress binary data if beneficial
     * \param data Uncompressed binary data
     * \param compressedData Output: compressed data (if compression used)
     * \return true if compression used, false if uncompressed better
     *
     * Compresses data using qCompress. Only uses compression if it saves
     * more than 10% of space.
     */
    static bool compressIfBeneficial(const QByteArray& data, QByteArray& compressedData);

private:
    QSqlDatabase getDatabase();
    
    // Prepared statement caching for performance
    QSqlQuery m_insertQuery;
    QSqlQuery m_updateQuery;
    bool m_statementsPrepared;
    
    /*!
     * \brief Prepare cached SQL statements
     *
     * Prepares INSERT and UPDATE statements once for reuse.
     * Called automatically on first use.
     */
    void prepareStatements();
    
    /*!
     * \brief Serialize qint16 vector to binary
     * \param data Vector of qint16 values
     * \return Binary representation (little-endian)
     */
    static QByteArray serializeInt16Array(const QVector<EventStoreType>& data);
    
    /*!
     * \brief Serialize quint32 vector to binary
     * \param data Vector of quint32 values
     * \return Binary representation (little-endian)
     */
    static QByteArray serializeUInt32Array(const QVector<quint32>& data);
    
    /*!
     * \brief Deserialize binary to qint16 vector
     * \param data Binary data (little-endian)
     * \param count Number of elements expected
     * \return Vector of qint16 values
     */
    static QVector<EventStoreType> deserializeInt16Array(const QByteArray& data, int count);
    
    /*!
     * \brief Deserialize binary to quint32 vector
     * \param data Binary data (little-endian)
     * \param count Number of elements expected
     * \return Vector of quint32 values
     */
    static QVector<quint32> deserializeUInt32Array(const QByteArray& data, int count);
};

#endif // EVENT_DATA_REPOSITORY_H
