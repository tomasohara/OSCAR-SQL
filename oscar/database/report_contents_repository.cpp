/* Report Contents Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportContentsRepository class for managing CSV export
 * report content variations in the database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "report_contents_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ReportContentsRepository::ReportContentsRepository()
{
}

QSqlDatabase ReportContentsRepository::getDatabase()
{
    return DatabaseManager::instance().database();
}

qint64 ReportContentsRepository::create(const ReportContentData& content)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) "
        "VALUES (:report_id, :variety, :description, :query, :display_order, :is_system)"
    );
    
    query.bindValue(":report_id", content.reportId);
    query.bindValue(":variety", content.variety);
    query.bindValue(":description", content.description);
    query.bindValue(":query", content.query);
    query.bindValue(":display_order", content.displayOrder);
    query.bindValue(":is_system", content.isSystem ? 1 : 0);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::create() - Failed to create content:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::create", query);
        return 0;
    }
    
    return query.lastInsertId().toLongLong();
}

bool ReportContentsRepository::update(const ReportContentData& content)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "UPDATE report_contents "
        "SET report_id = :report_id, "
        "    variety = :variety, "
        "    description = :description, "
        "    query = :query, "
        "    display_order = :display_order, "
        "    is_system = :is_system, "
        "    updated_at = CURRENT_TIMESTAMP "
        "WHERE id = :id"
    );
    
    query.bindValue(":id", content.id);
    query.bindValue(":report_id", content.reportId);
    query.bindValue(":variety", content.variety);
    query.bindValue(":description", content.description);
    query.bindValue(":query", content.query);
    query.bindValue(":display_order", content.displayOrder);
    query.bindValue(":is_system", content.isSystem ? 1 : 0);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::update() - Failed to update content:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::update", query);
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool ReportContentsRepository::remove(qint64 id)
{
    // Check if it's a system content first
    if (isSystemContent(id)) {
        qWarning() << "ReportContentsRepository::remove() - Cannot delete system content, id:" << id;
        return false;
    }
    
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("DELETE FROM report_contents WHERE id = :id");
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::remove() - Failed to delete content:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::remove", query);
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

ReportContentData ReportContentsRepository::findById(qint64 id)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, report_id, variety, description, query, display_order, is_system, created_at, updated_at "
        "FROM report_contents "
        "WHERE id = :id"
    );
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::findById() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::findById", query);
        return ReportContentData();
    }
    
    if (query.next()) {
        ReportContentData content;
        content.id = query.value(0).toLongLong();
        content.reportId = query.value(1).toLongLong();
        content.variety = query.value(2).toString();
        content.description = query.value(3).toString();
        content.query = query.value(4).toString();
        content.displayOrder = query.value(5).toInt();
        content.isSystem = query.value(6).toInt() != 0;
        content.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
        content.updatedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        return content;
    }
    
    return ReportContentData();
}

QList<ReportContentData> ReportContentsRepository::findByReportId(qint64 reportId)
{
    QList<ReportContentData> contents;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, report_id, variety, description, query, display_order, is_system, created_at, updated_at "
        "FROM report_contents "
        "WHERE report_id = :report_id"
    );
    query.bindValue(":report_id", reportId);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::findByReportId() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::findByReportId", query);
        return contents;
    }
    
    while (query.next()) {
        ReportContentData content;
        content.id = query.value(0).toLongLong();
        content.reportId = query.value(1).toLongLong();
        content.variety = query.value(2).toString();
        content.description = query.value(3).toString();
        content.query = query.value(4).toString();
        content.displayOrder = query.value(5).toInt();
        content.isSystem = query.value(6).toInt() != 0;
        content.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
        content.updatedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        contents.append(content);
    }
    
    return contents;
}

QList<ReportContentData> ReportContentsRepository::findByReportIdOrdered(qint64 reportId)
{
    QList<ReportContentData> contents;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    // Order by display_order, then variety name
    query.prepare(
        "SELECT id, report_id, variety, description, query, display_order, is_system, created_at, updated_at "
        "FROM report_contents "
        "WHERE report_id = :report_id "
        "ORDER BY display_order, variety COLLATE NOCASE"
    );
    query.bindValue(":report_id", reportId);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::findByReportIdOrdered() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::findByReportIdOrdered", query);
        return contents;
    }
    
    while (query.next()) {
        ReportContentData content;
        content.id = query.value(0).toLongLong();
        content.reportId = query.value(1).toLongLong();
        content.variety = query.value(2).toString();
        content.description = query.value(3).toString();
        content.query = query.value(4).toString();
        content.displayOrder = query.value(5).toInt();
        content.isSystem = query.value(6).toInt() != 0;
        content.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
        content.updatedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        contents.append(content);
    }
    
    return contents;
}

ReportContentData ReportContentsRepository::findByReportIdAndVariety(qint64 reportId, const QString& variety)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, report_id, variety, description, query, display_order, is_system, created_at, updated_at "
        "FROM report_contents "
        "WHERE report_id = :report_id AND variety = :variety"
    );
    query.bindValue(":report_id", reportId);
    query.bindValue(":variety", variety);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::findByReportIdAndVariety() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::findByReportIdAndVariety", query);
        return ReportContentData();
    }
    
    if (query.next()) {
        ReportContentData content;
        content.id = query.value(0).toLongLong();
        content.reportId = query.value(1).toLongLong();
        content.variety = query.value(2).toString();
        content.description = query.value(3).toString();
        content.query = query.value(4).toString();
        content.displayOrder = query.value(5).toInt();
        content.isSystem = query.value(6).toInt() != 0;
        content.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
        content.updatedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        return content;
    }
    
    return ReportContentData();
}

bool ReportContentsRepository::exists(qint64 reportId, const QString& variety)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT COUNT(*) FROM report_contents "
        "WHERE report_id = :report_id AND variety = :variety"
    );
    query.bindValue(":report_id", reportId);
    query.bindValue(":variety", variety);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::exists() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::exists", query);
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

bool ReportContentsRepository::isSystemContent(qint64 id)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("SELECT is_system FROM report_contents WHERE id = :id");
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::isSystemContent() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::isSystemContent", query);
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() != 0;
    }
    
    return false;
}

int ReportContentsRepository::getContentCount(qint64 reportId)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("SELECT COUNT(*) FROM report_contents WHERE report_id = :report_id");
    query.bindValue(":report_id", reportId);
    
    if (!query.exec()) {
        qWarning() << "ReportContentsRepository::getContentCount() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportContentsRepository::getContentCount", query);
        return 0;
    }
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}
