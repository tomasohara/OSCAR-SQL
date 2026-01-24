/* Database Schema Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the DatabaseSchema class which defines the SQL
 * schema for OSCAR's SQLite database, including tables, indexes, and
 * schema versioning.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DATABASE_SCHEMA_H
#define DATABASE_SCHEMA_H

#include <QSqlDatabase>
#include <QString>

/*!
 * \class DatabaseSchema
 * \brief Defines and creates the OSCAR database schema
 *
 * This class contains all SQL statements needed to create and maintain
 * the OSCAR database schema. It handles schema creation, versioning,
 * and future upgrades.
 *
 * Current schema version: 2
 * Tables: profiles, machines, user_info, doctor_info, profile_preferences, schema_version
 */
class DatabaseSchema
{
public:
    /*!
     * \brief Current schema version number
     *
     * Increment this when schema changes. Used to determine if
     * database upgrades are needed.
     */
    static const int CURRENT_SCHEMA_VERSION = 10;

    /*!
     * \brief Create the complete database schema
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * This method creates all tables, indexes, and inserts the
     * schema version. Safe to call on existing database - uses
     * IF NOT EXISTS for all CREATE statements.
     */
    static bool createSchema(QSqlDatabase& db);

    /*!
     * \brief Get the current schema version from database
     * \param db Database connection to use
     * \return Schema version number, or 0 if not found
     */
    static int getSchemaVersion(QSqlDatabase& db);

    /*!
     * \brief Upgrade database schema to current version
     * \param db Database connection to use
     * \param fromVersion Current version in database
     * \return true if successful, false otherwise
     *
     * Future use: handles schema migrations from older versions.
     */
    static bool upgradeSchema(QSqlDatabase& db, int fromVersion);

private:
    // Schema creation methods
    static bool createSchemaVersionTable(QSqlDatabase& db);
    static bool createProfilesTable(QSqlDatabase& db);
    static bool createMachinesTable(QSqlDatabase& db);
    static bool createUserInfoTable(QSqlDatabase& db);
    static bool createDoctorInfoTable(QSqlDatabase& db);
    static bool createProfilePreferencesTable(QSqlDatabase& db);
    
    // Session data tables (schema version 3)
    static bool createSessionsTable(QSqlDatabase& db);
    static bool createSessionSettingsTable(QSqlDatabase& db);
    static bool createSessionChannelsTable(QSqlDatabase& db);
    static bool createSessionChannelValuesTable(QSqlDatabase& db);  // Schema version 7
    static bool createRespiratoryEventsTable(QSqlDatabase& db);
    static bool createSessionSummariesTable(QSqlDatabase& db);
    static bool createSessionSlicesTable(QSqlDatabase& db);
    
    // Channel tables (schema version 5)
    static bool createChannelsTable(QSqlDatabase& db);
    static bool createChannelOptionsTable(QSqlDatabase& db);
    
    // Daily summaries table (schema version 6)
    static bool createDailySummariesTable(QSqlDatabase& db);
    
    // Event data tables (schema version 8)
    static bool createEventListsTable(QSqlDatabase& db);
    static bool createEventDataTable(QSqlDatabase& db);
    
    static bool createIndexes(QSqlDatabase& db);
    static bool setSchemaVersion(QSqlDatabase& db, int version);
};

#endif // DATABASE_SCHEMA_H
