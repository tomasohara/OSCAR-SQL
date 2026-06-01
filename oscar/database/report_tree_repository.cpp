/* Report Tree Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportTreeRepository class for managing the
 * hierarchical report tree in the database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "report_tree_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ReportTreeRepository::ReportTreeRepository()
{
}

QSqlDatabase ReportTreeRepository::getDatabase()
{
    // Use the DatabaseManager singleton to get the active named connection.
    // Do NOT use QSqlDatabase::database() as that returns the default
    // connection which is not open - the DatabaseManager uses a named connection.
    return DatabaseManager::instance().database();
}

qint64 ReportTreeRepository::create(const ReportTreeNode& node)
{
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "INSERT INTO report_tree (parent_id, name, node_type, source, description, query, display_order) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"
    );
    
    // Use NULL for parentId of 0 (root nodes have no parent)
    if (node.parentId == 0) {
        query.addBindValue(QVariant(QVariant::LongLong));
    } else {
        query.addBindValue(node.parentId);
    }
    
    query.addBindValue(node.name);
    query.addBindValue(node.nodeType);
    query.addBindValue(node.source);
    query.addBindValue(node.description.isEmpty() ? QVariant(QVariant::String) : node.description);
    query.addBindValue(node.query.isEmpty() ? QVariant(QVariant::String) : node.query);
    query.addBindValue(node.displayOrder);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::create: Failed to insert node:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::create", query);
        return 0;
    }
    
    qint64 id = query.lastInsertId().toLongLong();
    qDebug() << "ReportTreeRepository::create: Created node" << node.name 
             << "with ID" << id;
    return id;
}

bool ReportTreeRepository::update(const ReportTreeNode& node)
{
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "UPDATE report_tree SET "
        "name = ?, description = ?, query = ?, display_order = ?, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );
    
    query.addBindValue(node.name);
    query.addBindValue(node.description.isEmpty() ? QVariant(QVariant::String) : node.description);
    query.addBindValue(node.query.isEmpty() ? QVariant(QVariant::String) : node.query);
    query.addBindValue(node.displayOrder);
    query.addBindValue(node.id);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::update: Failed to update node ID"
                    << node.id << ":" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::update", query);
        return false;
    }
    
    qDebug() << "ReportTreeRepository::update: Updated node ID" << node.id;
    return true;
}

bool ReportTreeRepository::remove(qint64 id)
{
    QSqlQuery query(getDatabase());
    
    query.prepare("DELETE FROM report_tree WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::remove: Failed to delete node ID"
                    << id << ":" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::remove", query);
        return false;
    }
    
    int rowsAffected = query.numRowsAffected();
    qDebug() << "ReportTreeRepository::remove: Deleted node ID" << id 
             << "(" << rowsAffected << "rows affected)";
    return rowsAffected > 0;
}

ReportTreeNode ReportTreeRepository::findById(qint64 id)
{
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "SELECT id, parent_id, name, node_type, source, description, query, "
        "display_order, created_at, updated_at "
        "FROM report_tree WHERE id = ?"
    );
    query.addBindValue(id);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::findById: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::findById", query);
        return ReportTreeNode();
    }
    
    if (!query.next()) {
        qWarning() << "ReportTreeRepository::findById: Node ID" << id << "not found";
        return ReportTreeNode();
    }
    
    ReportTreeNode node;
    node.id = query.value(0).toLongLong();
    node.parentId = query.value(1).isNull() ? 0 : query.value(1).toLongLong();
    node.name = query.value(2).toString();
    node.nodeType = query.value(3).toString();
    node.source = query.value(4).toString();
    node.description = query.value(5).toString();
    node.query = query.value(6).toString();
    node.displayOrder = query.value(7).toInt();
    node.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
    node.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
    
    return node;
}

QList<ReportTreeNode> ReportTreeRepository::findChildren(qint64 parentId)
{
    QList<ReportTreeNode> children;
    QSqlQuery query(getDatabase());
    
    if (parentId == 0) {
        // Find nodes with NULL parent_id (root nodes)
        query.prepare(
            "SELECT id, parent_id, name, node_type, source, description, query, "
            "display_order, created_at, updated_at "
            "FROM report_tree WHERE parent_id IS NULL"
        );
    } else {
        query.prepare(
            "SELECT id, parent_id, name, node_type, source, description, query, "
            "display_order, created_at, updated_at "
            "FROM report_tree WHERE parent_id = ?"
        );
        query.addBindValue(parentId);
    }
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::findChildren: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::findChildren", query);
        return children;
    }
    
    while (query.next()) {
        ReportTreeNode node;
        node.id = query.value(0).toLongLong();
        node.parentId = query.value(1).isNull() ? 0 : query.value(1).toLongLong();
        node.name = query.value(2).toString();
        node.nodeType = query.value(3).toString();
        node.source = query.value(4).toString();
        node.description = query.value(5).toString();
        node.query = query.value(6).toString();
        node.displayOrder = query.value(7).toInt();
        node.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        node.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        children.append(node);
    }
    
    return children;
}

QList<ReportTreeNode> ReportTreeRepository::findChildrenOrdered(qint64 parentId)
{
    QList<ReportTreeNode> children;
    QSqlQuery query(getDatabase());
    
    if (parentId == 0) {
        // Find nodes with NULL parent_id (root nodes)
        query.prepare(
            "SELECT id, parent_id, name, node_type, source, description, query, "
            "display_order, created_at, updated_at "
            "FROM report_tree WHERE parent_id IS NULL "
            "ORDER BY display_order, name"
        );
    } else {
        query.prepare(
            "SELECT id, parent_id, name, node_type, source, description, query, "
            "display_order, created_at, updated_at "
            "FROM report_tree WHERE parent_id = ? "
            "ORDER BY display_order, name"
        );
        query.addBindValue(parentId);
    }
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::findChildrenOrdered: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::findChildrenOrdered", query);
        return children;
    }
    
    while (query.next()) {
        ReportTreeNode node;
        node.id = query.value(0).toLongLong();
        node.parentId = query.value(1).isNull() ? 0 : query.value(1).toLongLong();
        node.name = query.value(2).toString();
        node.nodeType = query.value(3).toString();
        node.source = query.value(4).toString();
        node.description = query.value(5).toString();
        node.query = query.value(6).toString();
        node.displayOrder = query.value(7).toInt();
        node.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        node.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        children.append(node);
    }
    
    return children;
}

QList<ReportTreeNode> ReportTreeRepository::findRoots()
{
    return findChildren(0);
}

QList<ReportTreeNode> ReportTreeRepository::findBySource(const QString& source)
{
    QList<ReportTreeNode> nodes;
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "SELECT id, parent_id, name, node_type, source, description, query, "
        "display_order, created_at, updated_at "
        "FROM report_tree WHERE source = ? ORDER BY id"
    );
    query.addBindValue(source);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::findBySource: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::findBySource", query);
        return nodes;
    }
    
    while (query.next()) {
        ReportTreeNode node;
        node.id = query.value(0).toLongLong();
        node.parentId = query.value(1).isNull() ? 0 : query.value(1).toLongLong();
        node.name = query.value(2).toString();
        node.nodeType = query.value(3).toString();
        node.source = query.value(4).toString();
        node.description = query.value(5).toString();
        node.query = query.value(6).toString();
        node.displayOrder = query.value(7).toInt();
        node.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        node.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        nodes.append(node);
    }
    
    return nodes;
}

bool ReportTreeRepository::exists(qint64 parentId, const QString& name)
{
    QSqlQuery query(getDatabase());
    
    if (parentId == 0) {
        query.prepare(
            "SELECT COUNT(*) FROM report_tree WHERE parent_id IS NULL AND name = ?"
        );
        query.addBindValue(name);
    } else {
        query.prepare(
            "SELECT COUNT(*) FROM report_tree WHERE parent_id = ? AND name = ?"
        );
        query.addBindValue(parentId);
        query.addBindValue(name);
    }
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::exists: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::exists", query);
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

bool ReportTreeRepository::moveNode(qint64 nodeId, qint64 newParentId)
{
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "UPDATE report_tree SET parent_id = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );
    
    if (newParentId == 0) {
        query.addBindValue(QVariant(QVariant::LongLong));
    } else {
        query.addBindValue(newParentId);
    }
    query.addBindValue(nodeId);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::moveNode: Failed to move node ID"
                    << nodeId << "to parent" << newParentId << ":"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::moveNode", query);
        return false;
    }
    
    qDebug() << "ReportTreeRepository::moveNode: Moved node ID" << nodeId 
             << "to parent ID" << newParentId;
    return true;
}

bool ReportTreeRepository::updateDisplayOrder(qint64 nodeId, int newOrder)
{
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "UPDATE report_tree SET display_order = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?"
    );
    query.addBindValue(newOrder);
    query.addBindValue(nodeId);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::updateDisplayOrder: Failed to update node ID"
                    << nodeId << ":" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::updateDisplayOrder", query);
        return false;
    }
    
    return true;
}

int ReportTreeRepository::countDescendants(qint64 nodeId)
{
    QSqlQuery query(getDatabase());
    
    // Use recursive CTE to count all descendants
    query.prepare(
        "WITH RECURSIVE descendants AS ("
        "  SELECT id FROM report_tree WHERE parent_id = ? "
        "  UNION ALL "
        "  SELECT rt.id FROM report_tree rt "
        "  INNER JOIN descendants d ON rt.parent_id = d.id"
        ") "
        "SELECT COUNT(*) FROM descendants"
    );
    query.addBindValue(nodeId);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::countDescendants: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::countDescendants", query);
        return 0;
    }
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

bool ReportTreeRepository::deleteSystemNodes()
{
    QSqlQuery query(getDatabase());
    
    // Delete all nodes with source='system' (CASCADE will handle children)
    // But keep the System root node itself
    query.prepare("DELETE FROM report_tree WHERE source = 'system' AND node_type != 'root'");
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::deleteSystemNodes: Failed to delete system nodes:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::deleteSystemNodes", query);
        return false;
    }
    
    int rowsDeleted = query.numRowsAffected();
    qDebug() << "ReportTreeRepository::deleteSystemNodes: Deleted" << rowsDeleted 
             << "system nodes";
    
    return true;
}

qint64 ReportTreeRepository::findRootId(const QString& name)
{
    QSqlQuery query(getDatabase());
    
    query.prepare(
        "SELECT id FROM report_tree WHERE parent_id IS NULL AND name = ? AND node_type = 'root'"
    );
    query.addBindValue(name);
    
    if (!query.exec()) {
        qCritical() << "ReportTreeRepository::findRootId: Query failed:"
                    << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ReportTreeRepository::findRootId", query);
        return 0;
    }
    
    if (query.next()) {
        return query.value(0).toLongLong();
    }
    
    qWarning() << "ReportTreeRepository::findRootId: Root node" << name << "not found";
    return 0;
}
