/* Report Tree Model Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportTreeModel class which provides a
 * QStandardItemModel-based tree view of the hierarchical report structure.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORT_TREE_MODEL_H
#define REPORT_TREE_MODEL_H

#include <QStandardItemModel>
#include <QIcon>
#include "report_tree_repository.h"

/*!
 * \class ReportTreeModel
 * \brief Qt Model/View model for the hierarchical report tree
 *
 * This model provides a tree view of System and User report branches
 * using QStandardItemModel. It loads data from the report_tree database
 * table and supports drag-and-drop within the User branch.
 *
 * Design: Uses QStandardItemModel as specified in Section 7.4.2 of the
 * design document, not QAbstractItemModel.
 */
class ReportTreeModel : public QStandardItemModel
{
    Q_OBJECT

public:
    explicit ReportTreeModel(QObject* parent = nullptr);
    ~ReportTreeModel() override;

    /*!
     * \brief Custom data roles for tree items
     */
    enum DataRole {
        NodeIdRole = Qt::UserRole + 1,   ///< qint64 database ID
        NodeTypeRole,                     ///< QString: "root", "folder", "report"
        SourceRole,                       ///< QString: "system", "user"
        DescriptionRole,                  ///< QString: report description
        QueryRole                         ///< QString: SQL query template
    };

    /*!
     * \brief Load entire tree from database
     *
     * Clears the model and reloads all nodes from the report_tree table.
     * Creates System and User root items at the top level.
     */
    void loadFromDatabase();

    /*!
     * \brief Refresh the tree (alias for loadFromDatabase)
     */
    void refresh() { loadFromDatabase(); }

    // CRUD operations that update both model and database
    QStandardItem* addFolder(QStandardItem* parent, const QString& name);
    QStandardItem* addReport(QStandardItem* parent, const QString& name,
                             const QString& description, const QString& query);
    bool removeNode(QStandardItem* item);
    bool renameNode(QStandardItem* item, const QString& newName);
    bool updateQuery(QStandardItem* item, const QString& query);
    bool updateDescription(QStandardItem* item, const QString& description);
    QStandardItem* duplicateNode(QStandardItem* sourceItem, QStandardItem* targetParent);

    // Tree navigation helpers
    QStandardItem* findItemById(qint64 nodeId, QStandardItem* parent = nullptr);
    qint64 getNodeId(QStandardItem* item) const;
    QString getNodeType(QStandardItem* item) const;
    QString getSource(QStandardItem* item) const;
    bool isSystemNode(QStandardItem* item) const;
    bool isUserNode(QStandardItem* item) const;
    bool isReportNode(QStandardItem* item) const;
    bool isFolderNode(QStandardItem* item) const;
    bool isRootNode(QStandardItem* item) const;

    // Drag and drop support
    Qt::DropActions supportedDropActions() const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;

private:
    /*!
     * \brief Recursively load children from database
     *
     * \param parentId Parent node database ID (0 for roots)
     * \param parentItem Parent QStandardItem
     */
    void loadChildren(qint64 parentId, QStandardItem* parentItem);

    /*!
     * \brief Create a QStandardItem for a tree node
     *
     * \param node Database node structure
     * \return Configured QStandardItem
     */
    QStandardItem* createNodeItem(const ReportTreeNode& node);

    /*!
     * \brief Get icon for a node based on type and source
     *
     * \param nodeType "root", "folder", or "report"
     * \param source "system" or "user"
     * \return Appropriate icon from QStyle::standardIcon
     */
    QIcon iconForNode(const QString& nodeType, const QString& source);

    /*!
     * \brief Recursively copy a node and all its descendants
     *
     * \param sourceItem Source item to copy
     * \param newParentId Database ID of new parent
     * \param newSource "system" or "user"
     * \return Database ID of new root node
     */
    qint64 copyNodeRecursive(QStandardItem* sourceItem, qint64 newParentId, const QString& newSource);

    ReportTreeRepository m_repository;
    QIcon m_folderIcon;
    QIcon m_reportIcon;
    QIcon m_systemIcon;
    QIcon m_userIcon;
};

#endif // REPORT_TREE_MODEL_H
