/* Graph Layouts Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Database access for the graph_layouts table. This unified table stores:
 *   - Per-profile "current" layout (profile_id NOT NULL, is_current = 1)
 *   - Shared named layout slots (profile_id IS NULL, is_current = 0)
 *
 * Both row types share the same QDataStream binary payload format (identical
 * to the old .shg file contents), allowing a single serialize/deserialize path.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GRAPH_LAYOUTS_REPOSITORY_H
#define GRAPH_LAYOUTS_REPOSITORY_H

#include <QString>
#include <QByteArray>
#include <QList>

/*!
 * \struct GraphLayoutData
 * \brief One row from the graph_layouts table.
 */
struct GraphLayoutData
{
    qint64     id            = 0;
    qint64     profileId     = 0;   //!< 0 means NULL (shared named layout)
    bool       profileIsNull = true;
    QString    viewName;             //!< 'daily' | 'overview' (lower-case)
    int        slotIndex     = 0;
    bool       isCurrent     = false;
    QString    description;
    int        formatVersion = 0;
    QByteArray data;
};

/*!
 * \class GraphLayoutsRepository
 * \brief CRUD for the graph_layouts table.
 */
class GraphLayoutsRepository
{
public:
    GraphLayoutsRepository();
    ~GraphLayoutsRepository();

    // --- Per-profile "current" layout ---

    //! \brief Save (upsert) the current layout for a profile/view.
    bool saveCurrentLayout(qint64 profileId, const QString& viewName,
                           int formatVersion, const QByteArray& data);

    //! \brief Load the current layout for a profile/view. Returns false if not found.
    bool loadCurrentLayout(qint64 profileId, const QString& viewName,
                           GraphLayoutData& out);

    //! \brief Delete the current layout row for a profile/view.
    bool deleteCurrentLayout(qint64 profileId, const QString& viewName);

    // --- Shared named layout slots ---

    //! \brief Save (upsert) a named slot. Preserves existing description on update.
    bool saveNamedLayout(const QString& viewName, int slotIndex,
                         const QString& description, int formatVersion,
                         const QByteArray& data);

    //! \brief Load a named slot. Returns false if not found.
    bool loadNamedLayout(const QString& viewName, int slotIndex, GraphLayoutData& out);

    //! \brief Delete a named slot.
    bool deleteNamedLayout(const QString& viewName, int slotIndex);

    //! \brief Update only the description of a named slot (for rename / add-description).
    bool updateNamedLayoutDescription(const QString& viewName, int slotIndex,
                                      const QString& description);

    //! \brief Return all named slots for a view, ordered by slot_index.
    QList<GraphLayoutData> loadAllNamedLayouts(const QString& viewName);
};

#endif // GRAPH_LAYOUTS_REPOSITORY_H
