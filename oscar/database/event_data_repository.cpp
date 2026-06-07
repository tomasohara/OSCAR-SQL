/* EventData Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the EventDataRepository class which handles
 * database operations for compressed binary event/waveform data.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "event_data_repository.h"
#include "database_manager.h"
#include "SleepLib/event.h"
#include "SleepLib/performance_timer.h"
#include "SleepLib/profiles.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QDataStream>
#include <QIODevice>
#include <cstring>  // for memcpy in zero-copy serialization

/*!
 * \brief Constructor
 */
EventDataRepository::EventDataRepository()
    : m_statementsPrepared(false)
{
}

/*!
 * \brief Destructor
 */
EventDataRepository::~EventDataRepository()
{
}

/*!
 * \brief Reset cached prepared statements
 *
 * Forces re-preparation of SQL statements on next use.
 * Call this after database reconnection or schema changes.
 */
void EventDataRepository::resetPreparedStatements()
{
    m_statementsPrepared = false;
    m_insertQuery = QSqlQuery();
    m_updateQuery = QSqlQuery();
}

/*!
 * \brief Prepare cached SQL statements
 *
 * Prepares INSERT and UPDATE statements once for reuse across
 * multiple storeEventListData() calls. This eliminates the overhead
 * of re-parsing and re-compiling SQL for each EventList.
 *
 * Performance Impact: Saves small amount of database operation time during
 * import by preparing statements once instead of hundreds of times.
 */
void EventDataRepository::prepareStatements()
{
    if (m_statementsPrepared) {
        return;
    }
    
    QSqlDatabase db = getDatabase();
    
    // Prepare INSERT statement for new event data
    m_insertQuery = QSqlQuery(db);
    if (!m_insertQuery.prepare(
        "INSERT INTO event_data ("
        "    eventlist_id,"
        "    data_blob, data_compressed,"
        "    data2_blob, data2_compressed,"
        "    time_blob, time_compressed,"
        "    compression_method, checksum"
        ") VALUES ("
        "    :eventlist_id,"
        "    :data_blob, :data_compressed,"
        "    :data2_blob, :data2_compressed,"
        "    :time_blob, :time_compressed,"
        "    :compression_method, :checksum"
        ")")) {
        qCritical() << "EventDataRepository: Failed to prepare INSERT statement:"
                    << m_insertQuery.lastError().text();
        return;
    }
    
    // Prepare UPDATE statement for compressed_size
    m_updateQuery = QSqlQuery(db);
    if (!m_updateQuery.prepare(
        "UPDATE event_lists SET compressed_size = :size WHERE id = :id")) {
        qCritical() << "EventDataRepository: Failed to prepare UPDATE statement:"
                    << m_updateQuery.lastError().text();
        return;
    }
    
    m_statementsPrepared = true;
}

/*!
 * \brief Get the database connection
 *
 * Returns: QSqlDatabase instance
 */
QSqlDatabase EventDataRepository::getDatabase()
{
    return DatabaseManager::instance().database();
}

/*!
 * \brief Store EventList binary data in database
 *
 * Parameters:
 *   eventlistId - Database ID from event_lists table
 *   eventList - EventList object containing data to store
 *
 * Returns: true if successful, false otherwise
 *
 * Serializes EventList data to binary, compresses if beneficial,
 * calculates checksum, and stores in database.
 */
bool EventDataRepository::storeEventListData(qint64 eventlistId, EventList* eventList)
{
    PERF_TIMER_SCOPE("EventDataRepository::storeEventListData");
    
    if (!eventList || eventList->count() == 0) {
        qWarning() << "EventDataRepository: Cannot store empty EventList";
        return false;
    }
    
    // Serialize the EventList data to binary format
    PERF_TIMER_START("EventDataRepository::Serialize");
    EventBinaryData binaryData = serializeEventList(eventList);
    PERF_TIMER_STOP("EventDataRepository::Serialize");
    
    // Compress each data array if beneficial
    PERF_TIMER_START("EventDataRepository::Compress");
    QByteArray primaryCompressed;
    bool primaryUsesCompression = compressIfBeneficial(binaryData.primaryData, primaryCompressed);
    
    QByteArray secondaryCompressed;
    bool secondaryUsesCompression = false;
    if (!binaryData.secondaryData.isEmpty()) {
        secondaryUsesCompression = compressIfBeneficial(binaryData.secondaryData, secondaryCompressed);
    }
    
    QByteArray timeCompressed;
    bool timeUsesCompression = false;
    if (!binaryData.timeData.isEmpty()) {
        timeUsesCompression = compressIfBeneficial(binaryData.timeData, timeCompressed);
    }
    PERF_TIMER_STOP("EventDataRepository::Compress");
    
    // Determine overall compression method (1 if any array used compression)
    int compressionMethod = (primaryUsesCompression || secondaryUsesCompression || timeUsesCompression) ? 1 : 0;
    
    // Prepare cached statements on first use (eliminates re-parsing SQL for each EventList)
    prepareStatements();
    
    if (!m_statementsPrepared) {
        qCritical() << "EventDataRepository: Prepared statements not available";
        return false;
    }
    
    // Use the cached prepared INSERT statement (much faster than preparing each time)
    m_insertQuery.bindValue(":eventlist_id", eventlistId);
    
    // Store primary data (use compressed or uncompressed)
    if (primaryUsesCompression) {
        m_insertQuery.bindValue(":data_blob", QVariant());
        m_insertQuery.bindValue(":data_compressed", primaryCompressed);
    } else {
        m_insertQuery.bindValue(":data_blob", binaryData.primaryData);
        m_insertQuery.bindValue(":data_compressed", QVariant());
    }
    
    // Store secondary data if present
    if (!binaryData.secondaryData.isEmpty()) {
        if (secondaryUsesCompression) {
            m_insertQuery.bindValue(":data2_blob", QVariant());
            m_insertQuery.bindValue(":data2_compressed", secondaryCompressed);
        } else {
            m_insertQuery.bindValue(":data2_blob", binaryData.secondaryData);
            m_insertQuery.bindValue(":data2_compressed", QVariant());
        }
    } else {
        m_insertQuery.bindValue(":data2_blob", QVariant());
        m_insertQuery.bindValue(":data2_compressed", QVariant());
    }
    
    // Store time data if present
    if (!binaryData.timeData.isEmpty()) {
        if (timeUsesCompression) {
            m_insertQuery.bindValue(":time_blob", QVariant());
            m_insertQuery.bindValue(":time_compressed", timeCompressed);
        } else {
            m_insertQuery.bindValue(":time_blob", binaryData.timeData);
            m_insertQuery.bindValue(":time_compressed", QVariant());
        }
    } else {
        m_insertQuery.bindValue(":time_blob", QVariant());
        m_insertQuery.bindValue(":time_compressed", QVariant());
    }
    
    m_insertQuery.bindValue(":compression_method", compressionMethod);
    m_insertQuery.bindValue(":checksum", binaryData.checksum);
    
    PERF_TIMER_START("EventDataRepository::DBInsert");
    if (!m_insertQuery.exec()) {
        PERF_TIMER_STOP("EventDataRepository::DBInsert");
        qCritical() << "EventDataRepository: Failed to store event data:"
                    << m_insertQuery.lastError().text();
        DatabaseManager::instance().checkQueryError("EventDataRepository::storeEventListData", m_insertQuery);
        // Reset so subsequent calls don't inherit a broken prepared statement and fail
        // with "Parameter count mismatch" instead of the real error.
        resetPreparedStatements();
        return false;
    }
    PERF_TIMER_STOP("EventDataRepository::DBInsert");
    
    // Calculate total compressed size for reporting
    qint64 compressedSize = 0;
    compressedSize += primaryUsesCompression ? primaryCompressed.size() : binaryData.primaryData.size();
    if (!binaryData.secondaryData.isEmpty()) {
        compressedSize += secondaryUsesCompression ? secondaryCompressed.size() : binaryData.secondaryData.size();
    }
    if (!binaryData.timeData.isEmpty()) {
        compressedSize += timeUsesCompression ? timeCompressed.size() : binaryData.timeData.size();
    }
    
    // Update event_lists table with compressed size using cached prepared statement
    m_updateQuery.bindValue(":size", compressedSize);
    m_updateQuery.bindValue(":id", eventlistId);
    
    if (!m_updateQuery.exec()) {
        qWarning() << "EventDataRepository: Failed to update compressed_size:"
                   << m_updateQuery.lastError().text();
        // Don't fail the whole operation, just log the warning
    }
    
    return true;
}

/*!
 * \brief Load EventList binary data from database
 *
 * Parameters:
 *   eventlistId - Database ID from event_lists table
 *   eventList - EventList object to populate with data
 *
 * Returns: true if successful, false otherwise
 *
 * Retrieves binary data, decompresses if needed, verifies checksum,
 * and populates EventList data arrays.
 */
bool EventDataRepository::loadEventListData(qint64 eventlistId, EventList* eventList)
{
    if (!eventList) {
        qWarning() << "EventDataRepository: Cannot load into null EventList";
        return false;
    }
    
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT data_blob, data_compressed,"
        "    data2_blob, data2_compressed,"
        "    time_blob, time_compressed,"
        "    compression_method, checksum"
        " FROM event_data"
        " WHERE eventlist_id = :eventlist_id"
    );
    
    query.bindValue(":eventlist_id", eventlistId);
    
    if (!query.exec()) {
        qCritical() << "EventDataRepository: Failed to load event data:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("EventDataRepository::loadEventListData", query);
        return false;
    }
    
    if (!query.next()) {
        qWarning() << "EventDataRepository: No data found for eventlist_id" << eventlistId;
        return false;
    }
    
    EventBinaryData binaryData;
    binaryData.compressionMethod = query.value("compression_method").toInt();
    binaryData.checksum = query.value("checksum").toUInt();
    
    // Load primary data (try compressed first, then uncompressed)
    QByteArray primaryCompressed = query.value("data_compressed").toByteArray();
    if (!primaryCompressed.isEmpty()) {
        binaryData.primaryData = qUncompress(primaryCompressed);
        if (binaryData.primaryData.isEmpty()) {
            qCritical() << "EventDataRepository: Failed to decompress primary data";
            return false;
        }
    } else {
        binaryData.primaryData = query.value("data_blob").toByteArray();
    }
    
    // Load secondary data if present
    QByteArray secondaryCompressed = query.value("data2_compressed").toByteArray();
    if (!secondaryCompressed.isEmpty()) {
        binaryData.secondaryData = qUncompress(secondaryCompressed);
        if (binaryData.secondaryData.isEmpty()) {
            qCritical() << "EventDataRepository: Failed to decompress secondary data";
            return false;
        }
    } else {
        binaryData.secondaryData = query.value("data2_blob").toByteArray();
    }
    
    // Load time data if present
    QByteArray timeCompressed = query.value("time_compressed").toByteArray();
    if (!timeCompressed.isEmpty()) {
        binaryData.timeData = qUncompress(timeCompressed);
        if (binaryData.timeData.isEmpty()) {
            qCritical() << "EventDataRepository: Failed to decompress time data";
            return false;
        }
    } else {
        binaryData.timeData = query.value("time_blob").toByteArray();
    }
    
    // Verify checksum of primary data
    quint16 calculatedChecksum = calculateChecksum(binaryData.primaryData);
    if (calculatedChecksum != binaryData.checksum) {
        qWarning() << "EventDataRepository: Checksum mismatch!"
                   << "Expected:" << binaryData.checksum
                   << "Got:" << calculatedChecksum;
        // Continue anyway but log the warning
    }
    
    // Deserialize binary data into EventList
    return deserializeEventList(binaryData, eventList);
}

/*!
 * \brief Delete binary data for an EventList
 *
 * Parameters:
 *   eventlistId - Database ID from event_lists table
 *
 * Returns: true if successful, false otherwise
 */
bool EventDataRepository::deleteByEventList(qint64 eventlistId)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("DELETE FROM event_data WHERE eventlist_id = :eventlist_id");
    query.bindValue(":eventlist_id", eventlistId);
    
    if (!query.exec()) {
        qCritical() << "EventDataRepository: Failed to delete event_data:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("EventDataRepository::deleteByEventList", query);
        return false;
    }
    
    return true;
}

/*!
 * \brief Check if binary data exists for an EventList
 *
 * Parameters:
 *   eventlistId - Database ID from event_lists table
 *
 * Returns: true if data exists, false otherwise
 */
bool EventDataRepository::exists(qint64 eventlistId)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("SELECT COUNT(*) FROM event_data WHERE eventlist_id = :eventlist_id");
    query.bindValue(":eventlist_id", eventlistId);
    
    if (!query.exec() || !query.next()) {
        return false;
    }
    
    return query.value(0).toInt() > 0;
}

/*!
 * \brief Serialize EventList data to binary format
 *
 * Parameters:
 *   eventList - EventList object to serialize
 *
 * Returns: EventBinaryData structure with serialized arrays
 *
 * Converts EventList data arrays to binary format with little-endian encoding.
 */
EventBinaryData EventDataRepository::serializeEventList(EventList* eventList)
{
    EventBinaryData result;
    
    // Serialize primary data (qint16 array)
    result.primaryData = serializeInt16Array(eventList->getData());
    
    // Serialize secondary data if present
    if (eventList->hasSecondField()) {
        result.secondaryData = serializeInt16Array(eventList->getData2());
    }
    
    // Serialize time data for events (not waveforms)
    if (eventList->type() != EVL_Waveform) {
        result.timeData = serializeUInt32Array(eventList->getTime());
    }
    
    // Calculate checksum of primary data
    result.checksum = calculateChecksum(result.primaryData);
    
    return result;
}

/*!
 * \brief Deserialize binary data into EventList
 *
 * Parameters:
 *   binaryData - Binary data to deserialize
 *   eventList - EventList object to populate
 *
 * Returns: true if successful, false otherwise
 *
 * Populates EventList data arrays from binary format.
 */
bool EventDataRepository::deserializeEventList(const EventBinaryData& binaryData, EventList* eventList)
{
    if (binaryData.primaryData.isEmpty()) {
        qWarning() << "EventDataRepository: Cannot deserialize empty primary data";
        return false;
    }
    
    int count = eventList->count();
    
    // Deserialize primary data
    QVector<EventStoreType> primaryData = deserializeInt16Array(binaryData.primaryData, count);
    if (primaryData.size() != count) {
        qCritical() << "EventDataRepository: Primary data size mismatch. Expected:" << count
                    << "Got:" << primaryData.size();
        return false;
    }
    
    eventList->getData() = primaryData;
    
    // Deserialize secondary data if present
    if (!binaryData.secondaryData.isEmpty()) {
        QVector<EventStoreType> secondaryData = deserializeInt16Array(binaryData.secondaryData, count);
        if (secondaryData.size() != count) {
            qCritical() << "EventDataRepository: Secondary data size mismatch. Expected:" << count
                        << "Got:" << secondaryData.size();
            return false;
        }
        eventList->getData2() = secondaryData;
    }
    
    // Deserialize time data if present
    if (!binaryData.timeData.isEmpty()) {
        QVector<quint32> timeData = deserializeUInt32Array(binaryData.timeData, count);
        if (timeData.size() != count) {
            qCritical() << "EventDataRepository: Time data size mismatch. Expected:" << count
                        << "Got:" << timeData.size();
            return false;
        }
        eventList->getTime() = timeData;
    }
    
    return true;
}

/*!
 * \brief Calculate CRC16 checksum for binary data
 *
 * Parameters:
 *   data - Binary data to checksum
 *
 * Returns: CRC16 checksum value
 *
 * Uses Qt's qChecksum for data integrity verification.
 */
quint16 EventDataRepository::calculateChecksum(const QByteArray& data)
{
    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        return qChecksum(data.data(), data.size());
    #else
        return qChecksum(QByteArrayView(data));
    #endif
}

/*!
 * \brief Compress binary data if beneficial
 *
 * Parameters:
 *   data - Uncompressed binary data
 *   compressedData - Output: compressed data (if compression used)
 *
 * Returns: true if compression used, false if uncompressed better
 *
 * Compresses using qCompress. Only uses compression if it saves >10%.
 */
bool EventDataRepository::compressIfBeneficial(const QByteArray& data, QByteArray& compressedData)
{
    // Check user's compression preference first
    if (p_profile && !p_profile->session->compressSessionData()) {
        // User has disabled compression
        return false;
    }
    
    // Don't compress empty data
    if (data.isEmpty()) {
        return false;
    }

    // Don't bother if data size is "small"
    if (data.size() < 500)
        return false;
    
    // Compress with moderate level (6 is ~2x faster than 9 with only ~1-2% larger output)
    compressedData = qCompress(data, 6);
    
    // Only use compression if it saves more than 10%
    if (compressedData.size() < data.size() * 0.9) {
        return true;
    }
    
    compressedData.clear();
    return false;
}

/*!
 * \brief Serialize qint16 vector to binary
 *
 * Parameters:
 *   data - Vector of qint16 values
 *
 * Returns: Binary representation (little-endian)
 *
 * Converts qint16 array to binary format matching .001 file format.
 */
QByteArray EventDataRepository::serializeInt16Array(const QVector<EventStoreType>& data)
{
    if (data.isEmpty()) return QByteArray();
    
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    // Zero-copy: data is already in the correct byte order
    return QByteArray(reinterpret_cast<const char*>(data.constData()),
                      data.size() * sizeof(EventStoreType));
#else
    // Fallback for big-endian: use QDataStream for byte swapping
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    for (const EventStoreType& value : data) {
        stream << value;
    }
    
    return result;
#endif
}

/*!
 * \brief Serialize quint32 vector to binary
 *
 * Parameters:
 *   data - Vector of quint32 values
 *
 * Returns: Binary representation (little-endian)
 *
 * Converts quint32 array to binary format matching .001 file format.
 */
QByteArray EventDataRepository::serializeUInt32Array(const QVector<quint32>& data)
{
    if (data.isEmpty()) return QByteArray();
    
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    // Zero-copy: data is already in the correct byte order
    return QByteArray(reinterpret_cast<const char*>(data.constData()),
                      data.size() * sizeof(quint32));
#else
    // Fallback for big-endian: use QDataStream for byte swapping
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    for (const quint32& value : data) {
        stream << value;
    }
    
    return result;
#endif
}

/*!
 * \brief Deserialize binary to qint16 vector
 *
 * Parameters:
 *   data - Binary data (little-endian)
 *   count - Number of elements expected
 *
 * Returns: Vector of qint16 values
 *
 * Converts binary data to qint16 array.
 */
QVector<EventStoreType> EventDataRepository::deserializeInt16Array(const QByteArray& data, int count)
{
    QVector<EventStoreType> result;
    
    int expectedBytes = count * sizeof(EventStoreType);
    if (data.size() < expectedBytes) {
        qWarning() << "EventDataRepository::deserializeInt16Array: data too short, expected"
                    << expectedBytes << "got" << data.size();
        return result;
    }
    
    result.resize(count);
    
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    // Zero-copy: memcpy directly (data is already little-endian)
    memcpy(result.data(), data.constData(), expectedBytes);
#else
    // Fallback for big-endian: use QDataStream for byte swapping
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    for (int i = 0; i < count && !stream.atEnd(); ++i) {
        stream >> result[i];
    }
#endif
    
    return result;
}

/*!
 * \brief Deserialize binary to quint32 vector
 *
 * Parameters:
 *   data - Binary data (little-endian)
 *   count - Number of elements expected
 *
 * Returns: Vector of quint32 values
 *
 * Converts binary data to quint32 array.
 */
QVector<quint32> EventDataRepository::deserializeUInt32Array(const QByteArray& data, int count)
{
    QVector<quint32> result;
    
    int expectedBytes = count * sizeof(quint32);
    if (data.size() < expectedBytes) {
        qWarning() << "EventDataRepository::deserializeUInt32Array: data too short, expected"
                    << expectedBytes << "got" << data.size();
        return result;
    }
    
    result.resize(count);
    
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    // Zero-copy: memcpy directly (data is already little-endian)
    memcpy(result.data(), data.constData(), expectedBytes);
#else
    // Fallback for big-endian: use QDataStream for byte swapping
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    for (int i = 0; i < count && !stream.atEnd(); ++i) {
        stream >> result[i];
    }
#endif
    
    return result;
}
