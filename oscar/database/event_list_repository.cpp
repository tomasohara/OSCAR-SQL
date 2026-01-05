/* EventList Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the EventListRepository class which handles
 * database operations for the event_lists table.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "event_list_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

/*!
 * \brief Constructor
 *
 * Initializes the EventListRepository.
 */
EventListRepository::EventListRepository()
{
}

/*!
 * \brief Destructor
 */
EventListRepository::~EventListRepository()
{
}

/*!
 * \brief Get the database connection
 *
 * Returns: QSqlDatabase instance
 */
QSqlDatabase EventListRepository::getDatabase()
{
    return DatabaseManager::instance().database();
}

/*!
 * \brief Create a new EventList metadata record
 *
 * Parameters:
 *   data - EventList metadata to store
 *
 * Returns: Database ID of created record, or -1 on failure
 */
qint64 EventListRepository::create(const EventListData& data)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "INSERT INTO event_lists ("
        "    session_id, channel_id, eventlist_index,"
        "    event_type, first_time, last_time, count, rate,"
        "    gain, offset, min_value, max_value, dimension,"
        "    has_second_field, min2_value, max2_value,"
        "    data_size, compressed_size"
        ") VALUES ("
        "    :session_id, :channel_id, :eventlist_index,"
        "    :event_type, :first_time, :last_time, :count, :rate,"
        "    :gain, :offset, :min_value, :max_value, :dimension,"
        "    :has_second_field, :min2_value, :max2_value,"
        "    :data_size, :compressed_size"
        ")"
    );
    
    query.bindValue(":session_id", data.sessionId);
    query.bindValue(":channel_id", data.channelId);
    query.bindValue(":eventlist_index", data.eventlistIndex);
    query.bindValue(":event_type", data.eventType);
    query.bindValue(":first_time", data.firstTime);
    query.bindValue(":last_time", data.lastTime);
    query.bindValue(":count", data.count);
    query.bindValue(":rate", data.rate);
    query.bindValue(":gain", data.gain);
    query.bindValue(":offset", data.offset);
    query.bindValue(":min_value", data.minValue);
    query.bindValue(":max_value", data.maxValue);
    query.bindValue(":dimension", data.dimension);
    query.bindValue(":has_second_field", data.hasSecondField ? 1 : 0);
    query.bindValue(":min2_value", data.hasSecondField ? QVariant(data.min2Value) : QVariant());
    query.bindValue(":max2_value", data.hasSecondField ? QVariant(data.max2Value) : QVariant());
    query.bindValue(":data_size", data.dataSize);
    query.bindValue(":compressed_size", data.compressedSize > 0 ? QVariant(data.compressedSize) : QVariant());
    
    if (!query.exec()) {
        qCritical() << "EventListRepository: Failed to create event_list:"
                    << query.lastError().text();
        return -1;
    }
    
    return query.lastInsertId().toLongLong();
}

/*!
 * \brief Find all EventLists for a session
 *
 * Parameters:
 *   sessionId - Database session ID
 *
 * Returns: List of EventList metadata records
 */
QList<EventListData> EventListRepository::findBySession(qint64 sessionId)
{
    QList<EventListData> results;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, session_id, channel_id, eventlist_index,"
        "    event_type, first_time, last_time, count, rate,"
        "    gain, offset, min_value, max_value, dimension,"
        "    has_second_field, min2_value, max2_value,"
        "    data_size, compressed_size"
        " FROM event_lists"
        " WHERE session_id = :session_id"
        " ORDER BY channel_id, eventlist_index"
    );
    
    query.bindValue(":session_id", sessionId);
    
    if (!query.exec()) {
        qWarning() << "EventListRepository: Failed to find by session:"
                   << query.lastError().text();
        return results;
    }
    
    while (query.next()) {
        results.append(mapFromQuery(query));
    }
    
    return results;
}

/*!
 * \brief Find EventLists for a specific channel in a session
 *
 * Parameters:
 *   sessionId - Database session ID
 *   channelId - Channel identifier
 *
 * Returns: List of EventList metadata records (may be multiple)
 */
QList<EventListData> EventListRepository::findByChannel(qint64 sessionId, ChannelID channelId)
{
    QList<EventListData> results;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, session_id, channel_id, eventlist_index,"
        "    event_type, first_time, last_time, count, rate,"
        "    gain, offset, min_value, max_value, dimension,"
        "    has_second_field, min2_value, max2_value,"
        "    data_size, compressed_size"
        " FROM event_lists"
        " WHERE session_id = :session_id AND channel_id = :channel_id"
        " ORDER BY eventlist_index"
    );
    
    query.bindValue(":session_id", sessionId);
    query.bindValue(":channel_id", channelId);
    
    if (!query.exec()) {
        qWarning() << "EventListRepository: Failed to find by channel:"
                   << query.lastError().text();
        return results;
    }
    
    while (query.next()) {
        results.append(mapFromQuery(query));
    }
    
    return results;
}

/*!
 * \brief Find a specific EventList by session, channel, and index
 *
 * Parameters:
 *   sessionId - Database session ID
 *   channelId - Channel identifier
 *   eventlistIndex - Index (0 for first, 1 for second, etc.)
 *
 * Returns: EventList metadata, or empty struct if not found
 */
EventListData EventListRepository::findByIndex(qint64 sessionId, ChannelID channelId, int eventlistIndex)
{
    EventListData result;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, session_id, channel_id, eventlist_index,"
        "    event_type, first_time, last_time, count, rate,"
        "    gain, offset, min_value, max_value, dimension,"
        "    has_second_field, min2_value, max2_value,"
        "    data_size, compressed_size"
        " FROM event_lists"
        " WHERE session_id = :session_id"
        "   AND channel_id = :channel_id"
        "   AND eventlist_index = :eventlist_index"
    );
    
    query.bindValue(":session_id", sessionId);
    query.bindValue(":channel_id", channelId);
    query.bindValue(":eventlist_index", eventlistIndex);
    
    if (!query.exec()) {
        qWarning() << "EventListRepository: Failed to find by index:"
                   << query.lastError().text();
        return result;
    }
    
    if (query.next()) {
        result = mapFromQuery(query);
    }
    
    return result;
}

/*!
 * \brief Update an existing EventList metadata record
 *
 * Parameters:
 *   data - EventList metadata with valid id field
 *
 * Returns: true if successful, false otherwise
 */
bool EventListRepository::update(const EventListData& data)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "UPDATE event_lists SET"
        "    session_id = :session_id,"
        "    channel_id = :channel_id,"
        "    eventlist_index = :eventlist_index,"
        "    event_type = :event_type,"
        "    first_time = :first_time,"
        "    last_time = :last_time,"
        "    count = :count,"
        "    rate = :rate,"
        "    gain = :gain,"
        "    offset = :offset,"
        "    min_value = :min_value,"
        "    max_value = :max_value,"
        "    dimension = :dimension,"
        "    has_second_field = :has_second_field,"
        "    min2_value = :min2_value,"
        "    max2_value = :max2_value,"
        "    data_size = :data_size,"
        "    compressed_size = :compressed_size"
        " WHERE id = :id"
    );
    
    query.bindValue(":id", data.id);
    query.bindValue(":session_id", data.sessionId);
    query.bindValue(":channel_id", data.channelId);
    query.bindValue(":eventlist_index", data.eventlistIndex);
    query.bindValue(":event_type", data.eventType);
    query.bindValue(":first_time", data.firstTime);
    query.bindValue(":last_time", data.lastTime);
    query.bindValue(":count", data.count);
    query.bindValue(":rate", data.rate);
    query.bindValue(":gain", data.gain);
    query.bindValue(":offset", data.offset);
    query.bindValue(":min_value", data.minValue);
    query.bindValue(":max_value", data.maxValue);
    query.bindValue(":dimension", data.dimension);
    query.bindValue(":has_second_field", data.hasSecondField ? 1 : 0);
    query.bindValue(":min2_value", data.hasSecondField ? QVariant(data.min2Value) : QVariant());
    query.bindValue(":max2_value", data.hasSecondField ? QVariant(data.max2Value) : QVariant());
    query.bindValue(":data_size", data.dataSize);
    query.bindValue(":compressed_size", data.compressedSize > 0 ? QVariant(data.compressedSize) : QVariant());
    
    if (!query.exec()) {
        qCritical() << "EventListRepository: Failed to update event_list:"
                    << query.lastError().text();
        return false;
    }
    
    return true;
}

/*!
 * \brief Delete an EventList metadata record
 *
 * Parameters:
 *   id - Database ID of record to delete
 *
 * Returns: true if successful, false otherwise
 */
bool EventListRepository::deleteById(qint64 id)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("DELETE FROM event_lists WHERE id = :id");
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qCritical() << "EventListRepository: Failed to delete event_list:"
                    << query.lastError().text();
        return false;
    }
    
    return true;
}

/*!
 * \brief Delete all EventLists for a session
 *
 * Parameters:
 *   sessionId - Database session ID
 *
 * Returns: true if successful, false otherwise
 */
bool EventListRepository::deleteBySession(qint64 sessionId)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("DELETE FROM event_lists WHERE session_id = :session_id");
    query.bindValue(":session_id", sessionId);
    
    if (!query.exec()) {
        qCritical() << "EventListRepository: Failed to delete event_lists for session:"
                    << query.lastError().text();
        return false;
    }
    
    return true;
}

/*!
 * \brief Get count of EventLists for a session
 *
 * Parameters:
 *   sessionId - Database session ID
 *
 * Returns: Number of EventLists
 */
int EventListRepository::countBySession(qint64 sessionId)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("SELECT COUNT(*) FROM event_lists WHERE session_id = :session_id");
    query.bindValue(":session_id", sessionId);
    
    if (!query.exec() || !query.next()) {
        qWarning() << "EventListRepository: Failed to count event_lists:"
                   << query.lastError().text();
        return 0;
    }
    
    return query.value(0).toInt();
}

/*!
 * \brief Get total data size for a session
 *
 * Parameters:
 *   sessionId - Database session ID
 *   compressed - If true, return compressed size; if false, uncompressed
 *
 * Returns: Total size in bytes
 */
qint64 EventListRepository::getTotalSize(qint64 sessionId, bool compressed)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    QString sizeColumn = compressed ? "compressed_size" : "data_size";
    QString sql = QString("SELECT SUM(%1) FROM event_lists WHERE session_id = :session_id").arg(sizeColumn);
    
    query.prepare(sql);
    query.bindValue(":session_id", sessionId);
    
    if (!query.exec() || !query.next()) {
        qWarning() << "EventListRepository: Failed to get total size:"
                   << query.lastError().text();
        return 0;
    }
    
    return query.value(0).toLongLong();
}

/*!
 * \brief Map database query result to EventListData structure
 *
 * Parameters:
 *   query - QSqlQuery positioned at a valid row
 *
 * Returns: EventListData structure populated from query
 *
 * Maps a database query result row to the EventListData structure.
 */
EventListData EventListRepository::mapFromQuery(QSqlQuery& query)
{
    EventListData data;
    
    data.id = query.value("id").toLongLong();
    data.sessionId = query.value("session_id").toLongLong();
    data.channelId = query.value("channel_id").toUInt();
    data.eventlistIndex = query.value("eventlist_index").toInt();
    
    data.eventType = query.value("event_type").toInt();
    data.firstTime = query.value("first_time").toLongLong();
    data.lastTime = query.value("last_time").toLongLong();
    data.count = query.value("count").toInt();
    data.rate = query.value("rate").toDouble();
    
    data.gain = query.value("gain").toDouble();
    data.offset = query.value("offset").toDouble();
    data.minValue = query.value("min_value").toDouble();
    data.maxValue = query.value("max_value").toDouble();
    data.dimension = query.value("dimension").toString();
    
    data.hasSecondField = query.value("has_second_field").toInt() != 0;
    data.min2Value = query.value("min2_value").toDouble();
    data.max2Value = query.value("max2_value").toDouble();
    
    data.dataSize = query.value("data_size").toLongLong();
    data.compressedSize = query.value("compressed_size").toLongLong();
    
    return data;
}
