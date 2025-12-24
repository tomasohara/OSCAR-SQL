/* Database Schema Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the DatabaseSchema class which creates and
 * maintains the OSCAR SQLite database schema.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "database_schema.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

/*
 * Create the complete database schema
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates all tables, indexes, and sets the schema version.
 * Safe to call multiple times - uses IF NOT EXISTS.
 */
bool DatabaseSchema::createSchema(QSqlDatabase& db)
{
    qDebug() << "DatabaseSchema: Creating schema...";

    // Create tables in order (schema_version first, then profiles, then machines)
    if (!createSchemaVersionTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create schema_version table";
        return false;
    }

    if (!createProfilesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create profiles table";
        return false;
    }

    if (!createMachinesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create machines table";
        return false;
    }

    // Create indexes
    if (!createIndexes(db)) {
        qCritical() << "DatabaseSchema: Failed to create indexes";
        return false;
    }

    // Set schema version
    if (!setSchemaVersion(db, CURRENT_SCHEMA_VERSION)) {
        qCritical() << "DatabaseSchema: Failed to set schema version";
        return false;
    }

    qDebug() << "DatabaseSchema: Schema creation complete";
    return true;
}

/*
 * Get the current schema version from database
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: Schema version number, or 0 if not found/error
 */
int DatabaseSchema::getSchemaVersion(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    if (!query.exec("SELECT version FROM schema_version LIMIT 1")) {
        qWarning() << "DatabaseSchema: Failed to get schema version:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}

/*
 * Upgrade database schema to current version
 *
 * Parameters:
 *   db - Database connection to use
 *   fromVersion - Current version in database
 *
 * Returns: true if successful, false otherwise
 *
 * Future use: will handle schema migrations from older versions.
 * Currently a placeholder that returns true.
 */
bool DatabaseSchema::upgradeSchema(QSqlDatabase& db, int fromVersion)
{
    qDebug() << "DatabaseSchema: Upgrading schema from version" << fromVersion;

    // Future schema upgrades will be implemented here
    // For now, just return true as we're at version 1
    
    Q_UNUSED(db);
    return true;
}

/*
 * Create the schema_version table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * This table tracks the schema version for future upgrades.
 */
bool DatabaseSchema::createSchemaVersionTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "    version INTEGER PRIMARY KEY,"
        "    applied_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create schema_version table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: schema_version table created";
    return true;
}

/*
 * Create the profiles table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The profiles table stores basic user profile information.
 */
bool DatabaseSchema::createProfilesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS profiles ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    username TEXT UNIQUE NOT NULL,"
        "    data_folder TEXT NOT NULL,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create profiles table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: profiles table created";
    return true;
}

/*
 * Create the machines table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The machines table stores information about CPAP machines and
 * other devices associated with each profile.
 */
bool DatabaseSchema::createMachinesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS machines ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL,"
        "    machine_id INTEGER NOT NULL,"
        "    loader_name TEXT NOT NULL,"
        "    machine_type INTEGER NOT NULL,"
        "    brand TEXT,"
        "    model TEXT,"
        "    series TEXT,"
        "    serial_number TEXT,"
        "    model_number TEXT,"
        "    last_imported TEXT,"
        "    purge_date TEXT,"
        "    data_version INTEGER DEFAULT 0,"
        "    properties TEXT,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    UNIQUE(profile_id, machine_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create machines table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: machines table created";
    return true;
}

/*
 * Create database indexes
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates indexes to optimize common query patterns.
 */
bool DatabaseSchema::createIndexes(QSqlDatabase& db)
{
    QSqlQuery query(db);
    QStringList indexes;

    // Index for profile lookups by username
    indexes << "CREATE INDEX IF NOT EXISTS idx_profiles_username ON profiles(username)";

    // Index for machine lookups by profile
    indexes << "CREATE INDEX IF NOT EXISTS idx_machines_profile ON machines(profile_id)";

    // Index for machine lookups by serial number
    indexes << "CREATE INDEX IF NOT EXISTS idx_machines_serial ON machines(serial_number)";

    // Index for machine lookups by loader name
    indexes << "CREATE INDEX IF NOT EXISTS idx_machines_loader ON machines(loader_name)";

    // Execute each index creation
    for (const QString& sql : indexes) {
        if (!query.exec(sql)) {
            qWarning() << "DatabaseSchema: Failed to create index:" 
                      << query.lastError().text();
            // Continue with other indexes even if one fails
        }
    }

    qDebug() << "DatabaseSchema: Indexes created";
    return true;
}

/*
 * Set the schema version in the database
 *
 * Parameters:
 *   db - Database connection to use
 *   version - Version number to set
 *
 * Returns: true if successful, false otherwise
 *
 * Inserts or updates the schema version number.
 */
bool DatabaseSchema::setSchemaVersion(QSqlDatabase& db, int version)
{
    QSqlQuery query(db);
    
    // Use INSERT OR REPLACE to handle both new and existing version records
    query.prepare("INSERT OR REPLACE INTO schema_version (version) VALUES (?)");
    query.addBindValue(version);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to set schema version:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: Schema version set to" << version;
    return true;
}
