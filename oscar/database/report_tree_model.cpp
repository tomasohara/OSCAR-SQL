/* Report Tree Model Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportTreeModel class for the hierarchical
 * report tree using QStandardItemModel.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "report_tree_model.h"
#include <QCoreApplication>
#include <QDebug>
#include <QApplication>
#include <QStyle>
#include <QMimeData>
#include <QIODevice>
#include <QDataStream>

/*
 * Constructor
 */
ReportTreeModel::ReportTreeModel(QObject* parent)
    : QStandardItemModel(parent)
{
    // Load standard icons from Qt
    m_folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    m_reportIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    m_systemIcon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    m_userIcon = QApplication::style()->standardIcon(QStyle::SP_DialogYesButton);
    
    // Single column - description accessible via right-click "Show Description"
    setHorizontalHeaderLabels(QStringList() << tr("Reports"));
    
    // Load tree from database
    loadFromDatabase();
}

/*
 * Destructor
 */
ReportTreeModel::~ReportTreeModel()
{
    // QStandardItemModel handles cleanup of items
}

/*
 * Load entire tree from database
 */
void ReportTreeModel::loadFromDatabase()
{
    qDebug() << "ReportTreeModel: Loading tree from database...";
    
    // Clear existing items
    clear();
    setHorizontalHeaderLabels(QStringList() << tr("Reports"));
    
    // Load root nodes (System and User)
    loadChildren(0, invisibleRootItem());
    
    qDebug() << "ReportTreeModel: Tree loaded with" << invisibleRootItem()->rowCount() << "root nodes";
}

/*
 * Recursively load children from database
 */
void ReportTreeModel::loadChildren(qint64 parentId, QStandardItem* parentItem)
{
    QList<ReportTreeNode> nodes = m_repository.findChildrenOrdered(parentId);
    
    for (const ReportTreeNode& node : nodes) {
        QStandardItem* item = createNodeItem(node);
        
        // Single column - description stored in role data, not a column
        parentItem->appendRow(item);
        
        // Recursively load children
        loadChildren(node.id, item);
    }
}

/*
 * Create a QStandardItem for a tree node
 */
QStandardItem* ReportTreeModel::createNodeItem(const ReportTreeNode& node)
{
    // Translate names and descriptions for system nodes; show user nodes as-is
    QString displayName = node.name;
    QString displayDesc = node.description;
    if (node.source == "system") {
        displayName = QCoreApplication::translate("SystemReports",
                          node.name.toUtf8().constData());
        if (!node.description.isEmpty()) {
            displayDesc = QCoreApplication::translate("SystemReports",
                              node.description.toUtf8().constData());
        }
    }

    QStandardItem* item = new QStandardItem(displayName);

    // Set icon based on node type
    item->setIcon(iconForNode(node.nodeType, node.source));

    // Store node data in custom roles
    item->setData(QVariant::fromValue(node.id), NodeIdRole);
    item->setData(node.nodeType, NodeTypeRole);
    item->setData(node.source, SourceRole);
    item->setData(displayDesc, DescriptionRole);
    item->setData(node.query, QueryRole);
    
    // Set editable flag only for user nodes (not roots)
    bool isEditable = (node.source == "user" && node.nodeType != "root");
    item->setEditable(isEditable);
    
    // System nodes use default (not gray) - tree coloring was too hard to read
    
    return item;
}

/*
 * Get icon for node type
 */
QIcon ReportTreeModel::iconForNode(const QString& nodeType, const QString& source)
{
    if (nodeType == "root") {
        return (source == "system") ? m_systemIcon : m_userIcon;
    } else if (nodeType == "folder") {
        return m_folderIcon;
    } else if (nodeType == "report") {
        return m_reportIcon;
    }
    
    return QIcon();
}

/*
 * Add a new folder
 */
QStandardItem* ReportTreeModel::addFolder(QStandardItem* parent, const QString& name)
{
    if (!parent || !isUserNode(parent)) {
        qWarning() << "ReportTreeModel::addFolder: Cannot add folder to System branch";
        return nullptr;
    }
    
    qint64 parentId = getNodeId(parent);
    
    // Create in database
    ReportTreeNode newNode;
    newNode.parentId = parentId;
    newNode.name = name;
    newNode.nodeType = "folder";
    newNode.source = "user";
    newNode.displayOrder = parent->rowCount();
    
    qint64 newId = m_repository.create(newNode);
    if (newId == 0) {
        qWarning() << "ReportTreeModel::addFolder: Failed to create folder in database";
        return nullptr;
    }
    
    // Add to model (single column)
    newNode.id = newId;
    QStandardItem* item = createNodeItem(newNode);
    parent->appendRow(item);
    
    return item;
}

/*
 * Add a new report
 */
QStandardItem* ReportTreeModel::addReport(QStandardItem* parent, const QString& name,
                                          const QString& description, const QString& query)
{
    if (!parent || !isUserNode(parent)) {
        qWarning() << "ReportTreeModel::addReport: Cannot add report to System branch";
        return nullptr;
    }
    
    qint64 parentId = getNodeId(parent);
    
    // Create in database
    ReportTreeNode newNode;
    newNode.parentId = parentId;
    newNode.name = name;
    newNode.nodeType = "report";
    newNode.source = "user";
    newNode.description = description;
    newNode.query = query;
    newNode.displayOrder = parent->rowCount();
    
    qint64 newId = m_repository.create(newNode);
    if (newId == 0) {
        qWarning() << "ReportTreeModel::addReport: Failed to create report in database";
        return nullptr;
    }
    
    // Add to model (single column)
    newNode.id = newId;
    QStandardItem* item = createNodeItem(newNode);
    parent->appendRow(item);
    
    return item;
}

/*
 * Remove a node
 */
bool ReportTreeModel::removeNode(QStandardItem* item)
{
    if (!item || !isUserNode(item) || isRootNode(item)) {
        qWarning() << "ReportTreeModel::removeNode: Cannot delete System node or root";
        return false;
    }
    
    qint64 nodeId = getNodeId(item);
    
    // Delete from database (CASCADE will delete children)
    if (!m_repository.remove(nodeId)) {
        qWarning() << "ReportTreeModel::removeNode: Failed to delete from database";
        return false;
    }
    
    // Remove from model
    QStandardItem* parent = item->parent();
    if (!parent) {
        parent = invisibleRootItem();
    }
    parent->removeRow(item->row());
    
    return true;
}

/*
 * Rename a node
 */
bool ReportTreeModel::renameNode(QStandardItem* item, const QString& newName)
{
    if (!item || !isUserNode(item)) {
        return false;
    }
    
    qint64 nodeId = getNodeId(item);
    
    // Update in database
    ReportTreeNode dbNode = m_repository.findById(nodeId);
    if (dbNode.id == 0) {
        return false;
    }
    
    dbNode.name = newName;
    if (!m_repository.update(dbNode)) {
        return false;
    }
    
    // Update in model
    item->setText(newName);
    
    return true;
}

/*
 * Update report query
 */
bool ReportTreeModel::updateQuery(QStandardItem* item, const QString& query)
{
    if (!item || !isUserNode(item) || !isReportNode(item)) {
        return false;
    }
    
    qint64 nodeId = getNodeId(item);
    
    // Update in database
    ReportTreeNode dbNode = m_repository.findById(nodeId);
    if (dbNode.id == 0) {
        return false;
    }
    
    dbNode.query = query;
    if (!m_repository.update(dbNode)) {
        return false;
    }
    
    // Update in model
    item->setData(query, QueryRole);
    
    return true;
}

/*
 * Update report description
 */
bool ReportTreeModel::updateDescription(QStandardItem* item, const QString& description)
{
    if (!item || !isUserNode(item)) {
        return false;
    }
    
    qint64 nodeId = getNodeId(item);
    
    // Update in database
    ReportTreeNode dbNode = m_repository.findById(nodeId);
    if (dbNode.id == 0) {
        return false;
    }
    
    dbNode.description = description;
    if (!m_repository.update(dbNode)) {
        return false;
    }
    
    // Update in model (description stored in role data only - no separate column)
    item->setData(description, DescriptionRole);
    
    return true;
}

/*
 * Duplicate a node
 */
QStandardItem* ReportTreeModel::duplicateNode(QStandardItem* sourceItem, QStandardItem* targetParent)
{
    if (!sourceItem || !targetParent || !isUserNode(targetParent)) {
        return nullptr;
    }
    
    qint64 targetParentId = getNodeId(targetParent);
    QString newSource = getSource(targetParent);
    
    // Copy recursively in database
    qint64 newNodeId = copyNodeRecursive(sourceItem, targetParentId, newSource);
    if (newNodeId == 0) {
        return nullptr;
    }
    
    // Reload the target parent to show new items
    // (simpler than manually building the tree)
    targetParent->removeRows(0, targetParent->rowCount());
    loadChildren(targetParentId, targetParent);
    
    // Find and return the newly created item
    return findItemById(newNodeId, targetParent);
}

/*
 * Recursively copy a node and all descendants
 */
qint64 ReportTreeModel::copyNodeRecursive(QStandardItem* sourceItem, qint64 newParentId, const QString& newSource)
{
    // Get source node data
    ReportTreeNode sourceNode = m_repository.findById(getNodeId(sourceItem));
    if (sourceNode.id == 0) {
        return 0;
    }
    
    // Create copy in database
    ReportTreeNode newNode = sourceNode;
    newNode.id = 0;  // Will be assigned by database
    newNode.parentId = newParentId;
    newNode.source = newSource;
    
    qint64 newId = m_repository.create(newNode);
    if (newId == 0) {
        return 0;
    }
    
    // Recursively copy children
    for (int i = 0; i < sourceItem->rowCount(); ++i) {
        QStandardItem* childItem = sourceItem->child(i);
        if (childItem) {
            copyNodeRecursive(childItem, newId, newSource);
        }
    }
    
    return newId;
}

/*
 * Helper methods
 */

qint64 ReportTreeModel::getNodeId(QStandardItem* item) const
{
    if (!item) return 0;
    return item->data(NodeIdRole).toLongLong();
}

QString ReportTreeModel::getNodeType(QStandardItem* item) const
{
    if (!item) return QString();
    return item->data(NodeTypeRole).toString();
}

QString ReportTreeModel::getSource(QStandardItem* item) const
{
    if (!item) return QString();
    return item->data(SourceRole).toString();
}

bool ReportTreeModel::isSystemNode(QStandardItem* item) const
{
    return (getSource(item) == "system");
}

bool ReportTreeModel::isUserNode(QStandardItem* item) const
{
    return (getSource(item) == "user");
}

bool ReportTreeModel::isReportNode(QStandardItem* item) const
{
    return (getNodeType(item) == "report");
}

bool ReportTreeModel::isFolderNode(QStandardItem* item) const
{
    return (getNodeType(item) == "folder");
}

bool ReportTreeModel::isRootNode(QStandardItem* item) const
{
    return (getNodeType(item) == "root");
}

QStandardItem* ReportTreeModel::findItemById(qint64 nodeId, QStandardItem* parent)
{
    if (!parent) {
        parent = invisibleRootItem();
    }
    
    // Check this item
    if (getNodeId(parent) == nodeId) {
        return parent;
    }
    
    // Check children recursively
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i);
        if (child) {
            QStandardItem* found = findItemById(nodeId, child);
            if (found) {
                return found;
            }
        }
    }
    
    return nullptr;
}

/*
 * setData override — persists inline renames (Qt::EditRole) to the database
 * before letting the base class update the in-memory item text.
 * Without this, F2 / right-click Rename only changes the model in memory.
 */
bool ReportTreeModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role == Qt::EditRole && index.isValid()) {
        QStandardItem* item = itemFromIndex(index);
        if (item && isUserNode(item) && !isRootNode(item)) {
            QString newName = value.toString().trimmed();
            if (newName.isEmpty()) {
                return false;  // Reject empty names
            }
            qint64 nodeId = getNodeId(item);
            ReportTreeNode dbNode = m_repository.findById(nodeId);
            if (dbNode.id != 0) {
                dbNode.name = newName;
                if (!m_repository.update(dbNode)) {
                    qWarning() << "ReportTreeModel::setData: Failed to persist rename for node" << nodeId;
                    return false;
                }
            }
        }
    }
    return QStandardItemModel::setData(index, value, role);
}

/*
 * Drag and drop support
 */

Qt::DropActions ReportTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

Qt::ItemFlags ReportTreeModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QStandardItemModel::flags(index);
    
    if (!index.isValid()) {
        return defaultFlags;
    }
    
    QStandardItem* item = itemFromIndex(index);
    if (!item) {
        return defaultFlags;
    }
    
    // Only User branch items can be dragged
    if (isUserNode(item) && !isRootNode(item)) {
        defaultFlags |= Qt::ItemIsDragEnabled;
    }
    
    // Only User branch folders and root can accept drops
    if (isUserNode(item) && (isFolderNode(item) || isRootNode(item))) {
        defaultFlags |= Qt::ItemIsDropEnabled;
    }
    
    return defaultFlags;
}

QStringList ReportTreeModel::mimeTypes() const
{
    QStringList types;
    types << "application/x-oscar-reporttree";
    return types;
}

QMimeData* ReportTreeModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty()) {
        return nullptr;
    }
    
    QMimeData* mimeData = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    
    // Store node IDs
    for (const QModelIndex& index : indexes) {
        if (index.isValid() && index.column() == 0) {
            QStandardItem* item = itemFromIndex(index);
            if (item) {
                stream << getNodeId(item);
            }
        }
    }
    
    mimeData->setData("application/x-oscar-reporttree", encodedData);
    return mimeData;
}

bool ReportTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                   int row, int column, const QModelIndex& parent)
{
    Q_UNUSED(column);
    Q_UNUSED(row);

    if (action == Qt::IgnoreAction) {
        return true;
    }
    
    if (!data || !data->hasFormat("application/x-oscar-reporttree")) {
        return false;
    }
    
    QStandardItem* targetParent = parent.isValid() ? itemFromIndex(parent) : invisibleRootItem();
    if (!targetParent || !isUserNode(targetParent)) {
        qWarning() << "ReportTreeModel::dropMimeData: Cannot drop on System branch";
        return false;
    }
    
    // Decode dragged node IDs
    QByteArray encodedData = data->data("application/x-oscar-reporttree");
    QDataStream stream(&encodedData, QIODevice::ReadOnly);
    QList<qint64> nodeIds;
    
    while (!stream.atEnd()) {
        qint64 nodeId;
        stream >> nodeId;
        nodeIds << nodeId;
    }
    
    if (nodeIds.isEmpty()) {
        return false;
    }
    
    // Move each node to the new parent
    qint64 targetParentId = getNodeId(targetParent);
    
    for (qint64 nodeId : nodeIds) {
        if (!m_repository.moveNode(nodeId, targetParentId)) {
            qWarning() << "ReportTreeModel::dropMimeData: Failed to move node" << nodeId;
            return false;
        }
    }
    
    // Reload the tree to reflect changes
    loadFromDatabase();
    
    return true;
}
