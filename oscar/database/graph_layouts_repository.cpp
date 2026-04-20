/* Graph Layouts Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "graph_layouts_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

GraphLayoutsRepository::GraphLayoutsRepository() {}
GraphLayoutsRepository::~GraphLayoutsRepository() {}

// ---------------------------------------------------------------------------
// Per-profile "current" layout
// ---------------------------------------------------------------------------

bool GraphLayoutsRepository::saveCurrentLayout(qint64 profileId, const QString& viewName,
                                                int formatVersion, const QByteArray& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "GraphLayoutsRepository::saveCurrentLayout() - DB not open";
        return false;
    }

    QSqlQuery q(db);
    // Upsert: on conflict update data, format_version, and timestamp; preserve description.
    q.prepare(
        "INSERT INTO graph_layouts "
        "  (profile_id, view_name, slot_index, is_current, format_version, data, updated_at) "
        "VALUES (?, ?, 0, 1, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(profile_id, view_name) WHERE is_current = 1 DO UPDATE SET "
        "  format_version = excluded.format_version, "
        "  data           = excluded.data, "
        "  updated_at     = CURRENT_TIMESTAMP"
    );
    q.addBindValue(profileId);
    q.addBindValue(viewName.toLower());
    q.addBindValue(formatVersion);
    q.addBindValue(data);

    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::saveCurrentLayout() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool GraphLayoutsRepository::loadCurrentLayout(qint64 profileId, const QString& viewName,
                                                GraphLayoutData& out)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, profile_id, view_name, slot_index, is_current, description, format_version, data "
        "FROM graph_layouts WHERE profile_id = ? AND view_name = ? AND is_current = 1"
    );
    q.addBindValue(profileId);
    q.addBindValue(viewName.toLower());

    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::loadCurrentLayout() failed:" << q.lastError().text();
        return false;
    }

    if (!q.next()) return false;

    out.id            = q.value(0).toLongLong();
    out.profileId     = q.value(1).toLongLong();
    out.profileIsNull = false;
    out.viewName      = q.value(2).toString();
    out.slotIndex     = q.value(3).toInt();
    out.isCurrent     = q.value(4).toInt() != 0;
    out.description   = q.value(5).toString();
    out.formatVersion = q.value(6).toInt();
    out.data          = q.value(7).toByteArray();
    return true;
}

bool GraphLayoutsRepository::deleteCurrentLayout(qint64 profileId, const QString& viewName)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(
        "DELETE FROM graph_layouts WHERE profile_id = ? AND view_name = ? AND is_current = 1"
    );
    q.addBindValue(profileId);
    q.addBindValue(viewName.toLower());
    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::deleteCurrentLayout() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Shared named layout slots
// ---------------------------------------------------------------------------

bool GraphLayoutsRepository::saveNamedLayout(const QString& viewName, int slotIndex,
                                              const QString& description, int formatVersion,
                                              const QByteArray& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "GraphLayoutsRepository::saveNamedLayout() - DB not open";
        return false;
    }

    QSqlQuery q(db);
    // On INSERT set description; on UPDATE preserve existing description (don't overwrite).
    q.prepare(
        "INSERT INTO graph_layouts "
        "  (profile_id, view_name, slot_index, is_current, description, format_version, data, updated_at) "
        "VALUES (NULL, ?, ?, 0, ?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(view_name, slot_index) WHERE profile_id IS NULL DO UPDATE SET "
        "  format_version = excluded.format_version, "
        "  data           = excluded.data, "
        "  updated_at     = CURRENT_TIMESTAMP"
    );
    q.addBindValue(viewName.toLower());
    q.addBindValue(slotIndex);
    q.addBindValue(description);
    q.addBindValue(formatVersion);
    q.addBindValue(data);

    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::saveNamedLayout() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool GraphLayoutsRepository::loadNamedLayout(const QString& viewName, int slotIndex,
                                              GraphLayoutData& out)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, view_name, slot_index, is_current, description, format_version, data "
        "FROM graph_layouts WHERE profile_id IS NULL AND view_name = ? AND slot_index = ?"
    );
    q.addBindValue(viewName.toLower());
    q.addBindValue(slotIndex);

    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::loadNamedLayout() failed:" << q.lastError().text();
        return false;
    }
    if (!q.next()) return false;

    out.id            = q.value(0).toLongLong();
    out.profileId     = 0;
    out.profileIsNull = true;
    out.viewName      = q.value(1).toString();
    out.slotIndex     = q.value(2).toInt();
    out.isCurrent     = false;
    out.description   = q.value(4).toString();
    out.formatVersion = q.value(5).toInt();
    out.data          = q.value(6).toByteArray();
    return true;
}

bool GraphLayoutsRepository::deleteNamedLayout(const QString& viewName, int slotIndex)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(
        "DELETE FROM graph_layouts WHERE profile_id IS NULL AND view_name = ? AND slot_index = ?"
    );
    q.addBindValue(viewName.toLower());
    q.addBindValue(slotIndex);
    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::deleteNamedLayout() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool GraphLayoutsRepository::updateNamedLayoutDescription(const QString& viewName, int slotIndex,
                                                           const QString& description)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(
        "UPDATE graph_layouts SET description = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE profile_id IS NULL AND view_name = ? AND slot_index = ?"
    );
    q.addBindValue(description);
    q.addBindValue(viewName.toLower());
    q.addBindValue(slotIndex);
    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::updateNamedLayoutDescription() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QList<GraphLayoutData> GraphLayoutsRepository::loadAllNamedLayouts(const QString& viewName)
{
    QList<GraphLayoutData> result;
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, view_name, slot_index, description, format_version, data "
        "FROM graph_layouts WHERE profile_id IS NULL AND view_name = ? ORDER BY slot_index"
    );
    q.addBindValue(viewName.toLower());

    if (!q.exec()) {
        qWarning() << "GraphLayoutsRepository::loadAllNamedLayouts() failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        GraphLayoutData d;
        d.id            = q.value(0).toLongLong();
        d.profileId     = 0;
        d.profileIsNull = true;
        d.viewName      = q.value(1).toString();
        d.slotIndex     = q.value(2).toInt();
        d.isCurrent     = false;
        d.description   = q.value(3).toString();
        d.formatVersion = q.value(4).toInt();
        d.data          = q.value(5).toByteArray();
        result.append(d);
    }
    return result;
}
