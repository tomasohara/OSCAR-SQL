/* Report Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportRepository class for managing CSV export
 * report definitions in the database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "report_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ReportRepository::ReportRepository()
{
}

QSqlDatabase ReportRepository::getDatabase()
{
    return DatabaseManager::instance().database();
}

qint64 ReportRepository::create(const ReportData& report)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "INSERT INTO reports (name, description, display_order, is_system) "
        "VALUES (:name, :description, :display_order, :is_system)"
    );
    
    query.bindValue(":name", report.name);
    query.bindValue(":description", report.description);
    query.bindValue(":display_order", report.displayOrder);
    query.bindValue(":is_system", report.isSystem ? 1 : 0);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::create() - Failed to create report:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::create", query);
        return 0;
    }
    
    return query.lastInsertId().toLongLong();
}

bool ReportRepository::update(const ReportData& report)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "UPDATE reports "
        "SET name = :name, "
        "    description = :description, "
        "    display_order = :display_order, "
        "    is_system = :is_system, "
        "    updated_at = CURRENT_TIMESTAMP "
        "WHERE id = :id"
    );
    
    query.bindValue(":id", report.id);
    query.bindValue(":name", report.name);
    query.bindValue(":description", report.description);
    query.bindValue(":display_order", report.displayOrder);
    query.bindValue(":is_system", report.isSystem ? 1 : 0);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::update() - Failed to update report:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::update", query);
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool ReportRepository::remove(qint64 id)
{
    // Check if it's a system report first
    if (isSystemReport(id)) {
        qWarning() << "ReportRepository::remove() - Cannot delete system report, id:" << id;
        return false;
    }
    
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("DELETE FROM reports WHERE id = :id");
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::remove() - Failed to delete report:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::remove", query);
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

ReportData ReportRepository::findById(qint64 id)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, name, description, display_order, is_system, created_at, updated_at "
        "FROM reports "
        "WHERE id = :id"
    );
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::findById() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::findById", query);
        return ReportData();
    }
    
    if (query.next()) {
        ReportData report;
        report.id = query.value(0).toLongLong();
        report.name = query.value(1).toString();
        report.description = query.value(2).toString();
        report.displayOrder = query.value(3).toInt();
        report.isSystem = query.value(4).toInt() != 0;
        report.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        report.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        return report;
    }
    
    return ReportData();
}

QList<ReportData> ReportRepository::findAll()
{
    QList<ReportData> reports;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    if (!query.exec(
        "SELECT id, name, description, display_order, is_system, created_at, updated_at "
        "FROM reports"
    )) {
        qWarning() << "ReportRepository::findAll() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::findAll", query);
        return reports;
    }
    
    while (query.next()) {
        ReportData report;
        report.id = query.value(0).toLongLong();
        report.name = query.value(1).toString();
        report.description = query.value(2).toString();
        report.displayOrder = query.value(3).toInt();
        report.isSystem = query.value(4).toInt() != 0;
        report.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        report.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        reports.append(report);
    }
    
    return reports;
}

QList<ReportData> ReportRepository::findAllOrdered()
{
    QList<ReportData> reports;
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    // Order by display_order first (if non-zero), then by name alphabetically
    if (!query.exec(
        "SELECT id, name, description, display_order, is_system, created_at, updated_at "
        "FROM reports "
        "ORDER BY CASE WHEN display_order = 0 THEN 999999 ELSE display_order END, "
        "         name COLLATE NOCASE"
    )) {
        qWarning() << "ReportRepository::findAllOrdered() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::findAllOrdered", query);
        return reports;
    }
    
    while (query.next()) {
        ReportData report;
        report.id = query.value(0).toLongLong();
        report.name = query.value(1).toString();
        report.description = query.value(2).toString();
        report.displayOrder = query.value(3).toInt();
        report.isSystem = query.value(4).toInt() != 0;
        report.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        report.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        reports.append(report);
    }
    
    return reports;
}

bool ReportRepository::exists(const QString& name)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("SELECT COUNT(*) FROM reports WHERE name = :name");
    query.bindValue(":name", name);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::exists() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::exists", query);
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

bool ReportRepository::isSystemReport(qint64 id)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare("SELECT is_system FROM reports WHERE id = :id");
    query.bindValue(":id", id);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::isSystemReport() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::isSystemReport", query);
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() != 0;
    }
    
    return false;
}

ReportData ReportRepository::findByName(const QString& name)
{
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    
    query.prepare(
        "SELECT id, name, description, display_order, is_system, created_at, updated_at "
        "FROM reports "
        "WHERE name = :name"
    );
    query.bindValue(":name", name);
    
    if (!query.exec()) {
        qWarning() << "ReportRepository::findByName() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportRepository::findByName", query);
        return ReportData();
    }
    
    if (query.next()) {
        ReportData report;
        report.id = query.value(0).toLongLong();
        report.name = query.value(1).toString();
        report.description = query.value(2).toString();
        report.displayOrder = query.value(3).toInt();
        report.isSystem = query.value(4).toInt() != 0;
        report.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        report.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        return report;
    }
    
    return ReportData();
}

bool ReportRepository::deleteById(qint64 id)
{
    // Alias for remove() method
    return remove(id);
}
