/* Report Tree Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportTreeRepository class for managing the
 * hierarchical report tree in the database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORT_TREE_REPOSITORY_H
#define REPORT_TREE_REPOSITORY_H

#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct ReportTreeNode
 * \brief Data structure for a node in the report tree
 *
 * Represents a single node (root, folder, or report) in the hierarchical
 * report tree stored in the report_tree table.
 */
struct ReportTreeNode {
    qint64 id = 0;              ///< Primary key
    qint64 parentId = 0;        ///< Parent node ID (0 = NULL for root nodes)
    QString name;               ///< Display name of the node
    QString nodeType;           ///< "root", "folder", or "report"
    QString source;             ///< "system" or "user"
    QString description;        ///< Optional description
    QString query;              ///< SQL query template (NULL for folders/roots)
    int displayOrder = 0;       ///< Sort order among siblings (0 = alphabetical)
    QDateTime createdAt;        ///< Creation timestamp
    QDateTime updatedAt;        ///< Last modification timestamp
    
    ReportTreeNode() {}
};

/*!
 * \class ReportTreeRepository
 * \brief Repository for managing the hierarchical report tree
 *
 * Provides CRUD operations for the report_tree table. The tree has two
 * permanent root nodes (System and User). System nodes are read-only;
 * user nodes are fully editable.
 */
class ReportTreeRepository
{
public:
    ReportTreeRepository();
    
    /*!
     * \brief Create a new node in the tree
     * \param node Node data to insert
     * \return New node ID, or 0 on error
     */
    qint64 create(const ReportTreeNode& node);
    
    /*!
     * \brief Update an existing node
     * \param node Node data with ID to update
     * \return true if successful, false otherwise
     */
    bool update(const ReportTreeNode& node);
    
    /*!
     * \brief Delete a node by ID (and all descendants via CASCADE)
     * \param id Node ID to delete
     * \return true if successful, false otherwise
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Find node by ID
     * \param id Node ID to find
     * \return ReportTreeNode with matching ID, or empty node if not found
     */
    ReportTreeNode findById(qint64 id);
    
    /*!
     * \brief Find all children of a parent node
     * \param parentId Parent node ID (0 for root-level nodes)
     * \return List of child nodes in database order
     */
    QList<ReportTreeNode> findChildren(qint64 parentId);
    
    /*!
     * \brief Find all children of a parent node, ordered for display
     * \param parentId Parent node ID (0 for root-level nodes)
     * \return List of child nodes ordered by display_order, then name
     */
    QList<ReportTreeNode> findChildrenOrdered(qint64 parentId);
    
    /*!
     * \brief Find root nodes (System and User)
     * \return List of root nodes (should be exactly 2)
     */
    QList<ReportTreeNode> findRoots();
    
    /*!
     * \brief Find all nodes with a specific source
     * \param source "system" or "user"
     * \return List of nodes with matching source
     */
    QList<ReportTreeNode> findBySource(const QString& source);
    
    /*!
     * \brief Check if a node with the given name exists under a parent
     * \param parentId Parent node ID
     * \param name Node name to check
     * \return true if name exists, false otherwise
     */
    bool exists(qint64 parentId, const QString& name);
    
    /*!
     * \brief Move a node to a new parent
     * \param nodeId Node ID to move
     * \param newParentId New parent node ID
     * \return true if successful, false otherwise
     */
    bool moveNode(qint64 nodeId, qint64 newParentId);
    
    /*!
     * \brief Update the display order of a node
     * \param nodeId Node ID to update
     * \param newOrder New display order value
     * \return true if successful, false otherwise
     */
    bool updateDisplayOrder(qint64 nodeId, int newOrder);
    
    /*!
     * \brief Count all descendants of a node (recursive)
     * \param nodeId Parent node ID
     * \return Number of descendant nodes
     */
    int countDescendants(qint64 nodeId);
    
    /*!
     * \brief Delete all system nodes (for reinitializing from .orf file)
     * \return true if successful, false otherwise
     */
    bool deleteSystemNodes();
    
    /*!
     * \brief Find a root node by name
     * \param name Root node name ("System" or "User")
     * \return Root node ID, or 0 if not found
     */
    qint64 findRootId(const QString& name);

private:
    QSqlDatabase getDatabase();
};

#endif // REPORT_TREE_REPOSITORY_H
