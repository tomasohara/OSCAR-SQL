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
#include <QSettings>
#include "version.h"

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

    if (!createUserInfoTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create user_info table";
        return false;
    }

    if (!createDoctorInfoTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create doctor_info table";
        return false;
    }

    if (!createProfilePreferencesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create profile_preferences table";
        return false;
    }

    // Session data tables (schema version 3)
    if (!createSessionsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create sessions table";
        return false;
    }

    if (!createSessionSettingsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create session_settings table";
        return false;
    }

    if (!createSessionChannelsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create session_channels table";
        return false;
    }

    if (!createSessionChannelValuesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create session_channel_values table";
        return false;
    }

    if (!createRespiratoryEventsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create respiratory_events table";
        return false;
    }

    if (!createSessionSummariesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create session_summaries table";
        return false;
    }

    if (!createSessionSlicesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create session_slices table";
        return false;
    }

    // Channel tables (schema version 5)
    if (!createChannelsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create channels table";
        return false;
    }

    if (!createChannelOptionsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create channel_options table";
        return false;
    }

    // Daily summaries table (schema version 6)
    if (!createDailySummariesTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create daily_summaries table";
        return false;
    }

    // Event data tables (schema version 8)
    if (!createEventListsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create event_lists table";
        return false;
    }

    if (!createEventDataTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create event_data table";
        return false;
    }

    // Reports tables (schema version 11)
    if (!createReportsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create reports table";
        return false;
    }

    if (!createReportContentsTable(db)) {
        qCritical() << "DatabaseSchema: Failed to create report_contents table";
        return false;
    }

    if (!createReportIndexes(db)) {
        qCritical() << "DatabaseSchema: Failed to create report indexes";
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
 * Returns: false - Schema v12+ does not support incremental migrations
 *
 * SCHEMA V12 POLICY: No incremental migrations. If schema version doesn't match,
 * user must start with fresh database and reimport data. This ensures data integrity
 * and simplifies maintenance. The database_manager will display an appropriate
 * error message to the user.
 */
bool DatabaseSchema::upgradeSchema(QSqlDatabase& db, int fromVersion)
{
    Q_UNUSED(db);
    
    qCritical() << "DatabaseSchema: Schema version mismatch detected";
    qCritical() << "DatabaseSchema: Database version:" << fromVersion;
    qCritical() << "DatabaseSchema: Required version:" << CURRENT_SCHEMA_VERSION;
    qCritical() << "DatabaseSchema: Incremental migration not supported in schema v12+";
    qCritical() << "DatabaseSchema: Please start with a fresh database and reimport your data";
    
    return false;
    
    // Legacy migration code removed in schema v12
    // Users upgrading from v11 or earlier must reimport data
    // This ensures data integrity and simplifies maintenance

    // Upgrade from version 2 to version 3: Add session tables
    if (fromVersion < 3) {
        qDebug() << "DatabaseSchema: Applying version 3 upgrade (session tables)";
        
        if (!createSessionsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create sessions table during upgrade";
            return false;
        }

        if (!createSessionSettingsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create session_settings table during upgrade";
            return false;
        }

        if (!createSessionChannelsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create session_channels table during upgrade";
            return false;
        }

        if (!createRespiratoryEventsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create respiratory_events table during upgrade";
            return false;
        }

        if (!createSessionSummariesTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create session_summaries table during upgrade";
            return false;
        }

        if (!createSessionSlicesTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create session_slices table during upgrade";
            return false;
        }
        
        // Recreate indexes to include new session indexes
        if (!createIndexes(db)) {
            qCritical() << "DatabaseSchema: Failed to create indexes during upgrade";
            return false;
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 3)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 3";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 3";
    }
    
    // Upgrade from version 3 to version 4: Add status field to profiles table
    if (fromVersion < 4) {
        qDebug() << "DatabaseSchema: Applying version 4 upgrade (profile status tracking)";
        
        QSqlQuery query(db);
        
        // Add status column (defaults to 'active' for existing profiles)
        if (!query.exec("ALTER TABLE profiles ADD COLUMN status TEXT DEFAULT 'active' CHECK(status IN ('active', 'missing', 'archived'))")) {
            qCritical() << "DatabaseSchema: Failed to add status column:" << query.lastError().text();
            return false;
        }
        
        // Add status_changed_at column
        if (!query.exec("ALTER TABLE profiles ADD COLUMN status_changed_at TEXT")) {
            qCritical() << "DatabaseSchema: Failed to add status_changed_at column:" << query.lastError().text();
            return false;
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 4)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 4";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 4";
    }
    
    // Upgrade from version 4 to version 5: Add channels tables
    if (fromVersion < 5) {
        qDebug() << "DatabaseSchema: Applying version 5 upgrade (channels tables)";
        
        if (!createChannelsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create channels table during upgrade";
            return false;
        }

        if (!createChannelOptionsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create channel_options table during upgrade";
            return false;
        }
        
        // Recreate indexes to include new channels indexes
        if (!createIndexes(db)) {
            qCritical() << "DatabaseSchema: Failed to create indexes during upgrade";
            return false;
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 5)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 5";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 5";
    }
    
    // Upgrade from version 5 to version 6: Add daily_summaries table
    if (fromVersion < 6) {
        qDebug() << "DatabaseSchema: Applying version 6 upgrade (daily summaries table)";
        
        if (!createDailySummariesTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create daily_summaries table during upgrade";
            return false;
        }
        
        // Recreate indexes to include new daily_summaries indexes
        if (!createIndexes(db)) {
            qCritical() << "DatabaseSchema: Failed to create indexes during upgrade";
            return false;
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 6)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 6";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 6";
    }
    
    // Upgrade from version 6 to version 7: Add session_channel_values table
    if (fromVersion < 7) {
        qDebug() << "DatabaseSchema: Applying version 7 upgrade (session_channel_values table - fixes bug)";
        
        if (!createSessionChannelValuesTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create session_channel_values table during upgrade";
            return false;
        }
        
        // Create index for session_channel_values
        QSqlQuery query(db);
        if (!query.exec("CREATE INDEX IF NOT EXISTS idx_session_channel_values_lookup ON session_channel_values(session_channel_id, value)")) {
            qWarning() << "DatabaseSchema: Failed to create session_channel_values index:" << query.lastError().text();
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 7)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 7";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 7";
        qDebug() << "DatabaseSchema: NOTE - Existing sessions will need to be re-saved to populate value/time summaries";
    }
    
    // Upgrade from version 7 to version 8: Add event_lists and event_data tables
    if (fromVersion < 8) {
        qDebug() << "DatabaseSchema: Applying version 8 upgrade (event data tables)";
        
        if (!createEventListsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create event_lists table during upgrade";
            return false;
        }

        if (!createEventDataTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create event_data table during upgrade";
            return false;
        }
        
        // Create indexes for event tables
        QSqlQuery query(db);
        if (!query.exec("CREATE INDEX IF NOT EXISTS idx_event_lists_session ON event_lists(session_id)")) {
            qWarning() << "DatabaseSchema: Failed to create event_lists session index:" << query.lastError().text();
        }
        if (!query.exec("CREATE INDEX IF NOT EXISTS idx_event_lists_channel ON event_lists(session_id, channel_id)")) {
            qWarning() << "DatabaseSchema: Failed to create event_lists channel index:" << query.lastError().text();
        }
        if (!query.exec("CREATE INDEX IF NOT EXISTS idx_event_lists_type ON event_lists(event_type)")) {
            qWarning() << "DatabaseSchema: Failed to create event_lists type index:" << query.lastError().text();
        }
        if (!query.exec("CREATE INDEX IF NOT EXISTS idx_event_lists_time ON event_lists(session_id, first_time, last_time)")) {
            qWarning() << "DatabaseSchema: Failed to create event_lists time index:" << query.lastError().text();
        }
        if (!query.exec("CREATE INDEX IF NOT EXISTS idx_event_data_eventlist ON event_data(eventlist_id)")) {
            qWarning() << "DatabaseSchema: Failed to create event_data index:" << query.lastError().text();
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 8)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 8";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 8";
        qDebug() << "DatabaseSchema: Event/waveform data will now be stored in database";
        qDebug() << "DatabaseSchema: Re-import CPAP data to populate event data tables";
    }
    
    // Upgrade from version 8 to version 9: Add json_value column to session_settings
    if (fromVersion < 9) {
        qDebug() << "DatabaseSchema: Applying version 9 upgrade (journal migration support)";
        
        QSqlQuery query(db);
        
        // Add json_value column for complex data types (bookmarks, etc.)
        if (!query.exec("ALTER TABLE session_settings ADD COLUMN json_value TEXT")) {
            qCritical() << "DatabaseSchema: Failed to add json_value column:" << query.lastError().text();
            return false;
        }
        
        // Update schema version
        if (!setSchemaVersion(db, 9)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 9";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 9";
        qDebug() << "DatabaseSchema: Journal data can now be migrated to database";
        qDebug() << "DatabaseSchema: Journal migration will occur automatically on profile load";
    }
    
    // Upgrade from version 9 to version 10: Rename central_count to unclassified_count and add clear_airway_count
    if (fromVersion < 10) {
        qDebug() << "DatabaseSchema: Applying version 10 upgrade (rename central_count to unclassified_count, add clear_airway_count)";
        
        QSqlQuery query(db);
        
        // For session_summaries table:
        // 1. Add clear_airway_count column
        if (!query.exec("ALTER TABLE session_summaries ADD COLUMN clear_airway_count INTEGER DEFAULT 0")) {
            qCritical() << "DatabaseSchema: Failed to add clear_airway_count to session_summaries:" << query.lastError().text();
            return false;
        }
        
        // 2. Add unclassified_count column
        if (!query.exec("ALTER TABLE session_summaries ADD COLUMN unclassified_count INTEGER DEFAULT 0")) {
            qCritical() << "DatabaseSchema: Failed to add unclassified_count to session_summaries:" << query.lastError().text();
            return false;
        }
        
        // 3. Copy data from central_count to unclassified_count
        if (!query.exec("UPDATE session_summaries SET unclassified_count = central_count")) {
            qCritical() << "DatabaseSchema: Failed to copy central_count to unclassified_count in session_summaries:" << query.lastError().text();
            return false;
        }
        
        // For daily_summaries table:
        // 1. Add unclassified_count column
        if (!query.exec("ALTER TABLE daily_summaries ADD COLUMN unclassified_count INTEGER DEFAULT 0")) {
            qCritical() << "DatabaseSchema: Failed to add unclassified_count to daily_summaries:" << query.lastError().text();
            return false;
        }
        
        // 2. Copy data from central_count to unclassified_count
        if (!query.exec("UPDATE daily_summaries SET unclassified_count = central_count")) {
            qCritical() << "DatabaseSchema: Failed to copy central_count to unclassified_count in daily_summaries:" << query.lastError().text();
            return false;
        }
        
        // Note: We keep the old central_count columns for backward compatibility during transition
        // They will be ignored by new code but won't break existing databases
        
        // Update schema version
        if (!setSchemaVersion(db, 10)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 10";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 10";
        qDebug() << "DatabaseSchema: Renamed central_count to unclassified_count (semantically correct)";
        qDebug() << "DatabaseSchema: Added clear_airway_count to session_summaries";
        qDebug() << "DatabaseSchema: Old central_count columns retained for compatibility";
    }
    
    // Upgrade from version 10 to version 11: Add CSV export reports tables
    if (fromVersion < 11) {
        qDebug() << "DatabaseSchema: Applying version 11 upgrade (CSV export reports tables)";
        
        if (!createReportsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create reports table during upgrade";
            return false;
        }
        
        if (!createReportContentsTable(db)) {
            qCritical() << "DatabaseSchema: Failed to create report_contents table during upgrade";
            return false;
        }
        
        if (!createReportIndexes(db)) {
            qCritical() << "DatabaseSchema: Failed to create report indexes during upgrade";
            return false;
        }
        
        // Note: Default reports will be initialized by checkAndUpdateReportVersion()
        // which is called at every startup from DatabaseManager::initialize()
        
        // Update schema version
        if (!setSchemaVersion(db, 11)) {
            qCritical() << "DatabaseSchema: Failed to update schema version to 11";
            return false;
        }
        
        qDebug() << "DatabaseSchema: Successfully upgraded to version 11";
        qDebug() << "DatabaseSchema: CSV export reports tables created";
        qDebug() << "DatabaseSchema: Reports will be initialized at startup";
    }
    
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
        "    status TEXT DEFAULT 'active' CHECK(status IN ('active', 'missing', 'archived')),"
        "    status_changed_at TEXT,"
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

    // Index for user_info lookups by profile
    indexes << "CREATE INDEX IF NOT EXISTS idx_user_info_profile ON user_info(profile_id)";

    // Index for doctor_info lookups by profile
    indexes << "CREATE INDEX IF NOT EXISTS idx_doctor_info_profile ON doctor_info(profile_id)";

    // Index for preference lookups by profile and category
    indexes << "CREATE INDEX IF NOT EXISTS idx_preferences_profile ON profile_preferences(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_preferences_category ON profile_preferences(profile_id, category)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_preferences_key ON profile_preferences(profile_id, category, key)";

    // Session indexes (schema version 3)
    indexes << "CREATE INDEX IF NOT EXISTS idx_sessions_machine ON sessions(machine_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_sessions_time ON sessions(start_time, end_time)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_sessions_enabled ON sessions(machine_id, enabled)";
    
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_settings_session ON session_settings(session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_settings_channel ON session_settings(session_id, channel_id)";
    
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_channels_session ON session_channels(session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_channels_channel ON session_channels(channel_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_channels_lookup ON session_channels(session_id, channel_id)";
    
    indexes << "CREATE INDEX IF NOT EXISTS idx_respiratory_events_session ON respiratory_events(session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_respiratory_events_type ON respiratory_events(session_id, event_type)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_respiratory_events_time ON respiratory_events(start_time, end_time)";
    
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_summaries_session ON session_summaries(session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_summaries_ahi ON session_summaries(ahi)";
    
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_slices_session ON session_slices(session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_slices_time ON session_slices(start_time, end_time)";

    // Channels indexes (schema version 5)
    indexes << "CREATE INDEX IF NOT EXISTS idx_channels_profile ON channels(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_channels_code ON channels(channel_code)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_channels_lookup ON channels(profile_id, channel_id)";
    
    indexes << "CREATE INDEX IF NOT EXISTS idx_channel_options_channel ON channel_options(channel_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_channel_options_lookup ON channel_options(channel_id, option_key)";

    // Daily summaries indexes (schema version 6)
    indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_profile_date ON daily_summaries(profile_id, date)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_profile_machine ON daily_summaries(profile_id, machine_id, date)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_ahi ON daily_summaries(ahi)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_compliance ON daily_summaries(profile_id, is_compliant)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_daily_summaries_date_range ON daily_summaries(profile_id, date DESC)";

    // Event data indexes (schema version 8)
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_lists_session ON event_lists(session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_lists_channel ON event_lists(session_id, channel_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_lists_type ON event_lists(event_type)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_lists_time ON event_lists(session_id, first_time, last_time)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_data_eventlist ON event_data(eventlist_id)";

    // Profile ID indexes (schema version 12 - denormalization for query performance)
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_summaries_profile ON session_summaries(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_summaries_profile_date ON session_summaries(profile_id, session_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_settings_profile ON session_settings(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_settings_profile_channel ON session_settings(profile_id, channel_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_channels_profile ON session_channels(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_session_channels_profile_channel ON session_channels(profile_id, channel_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_lists_profile ON event_lists(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_event_lists_profile_channel ON event_lists(profile_id, channel_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_respiratory_events_profile ON respiratory_events(profile_id)";
    indexes << "CREATE INDEX IF NOT EXISTS idx_respiratory_events_profile_type ON respiratory_events(profile_id, event_type)";

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
    
    // Delete all existing version records first
    if (!query.exec("DELETE FROM schema_version")) {
        qWarning() << "DatabaseSchema: Failed to clear old schema versions:" 
                   << query.lastError().text();
        // Continue anyway - might be empty table
    }
    
    // Insert the current version
    query.prepare("INSERT INTO schema_version (version) VALUES (?)");
    query.addBindValue(version);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to set schema version:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: Schema version set to" << version;
    return true;
}

/*
 * Create the user_info table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The user_info table stores detailed user personal information.
 */
bool DatabaseSchema::createUserInfoTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS user_info ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL UNIQUE,"
        "    dob TEXT,"
        "    first_name TEXT,"
        "    last_name TEXT,"
        "    address TEXT,"
        "    phone TEXT,"
        "    email TEXT,"
        "    country TEXT,"
        "    height REAL,"
        "    gender INTEGER,"
        "    timezone TEXT,"
        "    dst_enabled INTEGER,"
        "    password_hash TEXT,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create user_info table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: user_info table created";
    return true;
}

/*
 * Create the doctor_info table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The doctor_info table stores doctor/medical provider information.
 */
bool DatabaseSchema::createDoctorInfoTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS doctor_info ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL UNIQUE,"
        "    name TEXT,"
        "    phone TEXT,"
        "    email TEXT,"
        "    practice_name TEXT,"
        "    address TEXT,"
        "    patient_id TEXT,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create doctor_info table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: doctor_info table created";
    return true;
}

/*
 * Create the profile_preferences table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The profile_preferences table stores all profile settings as key-value pairs.
 * This includes CPAP settings, oximetry settings, session settings, etc.
 */
bool DatabaseSchema::createProfilePreferencesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS profile_preferences ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL,"
        "    category TEXT NOT NULL,"
        "    key TEXT NOT NULL,"
        "    value TEXT,"
        "    data_type TEXT,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    UNIQUE(profile_id, category, key)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create profile_preferences table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: profile_preferences table created";
    return true;
}

/*
 * Create the sessions table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The sessions table stores core session metadata and timing information.
 * Waveform data continues to be stored in separate files.
 */
bool DatabaseSchema::createSessionsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS sessions ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL,"
        "    machine_id INTEGER NOT NULL,"
        "    start_time INTEGER NOT NULL,"
        "    end_time INTEGER NOT NULL,"
        "    duration INTEGER NOT NULL,"
        "    enabled INTEGER DEFAULT 1,"
        "    summary_only INTEGER DEFAULT 0,"
        "    no_settings INTEGER DEFAULT 0,"
        "    events_loaded INTEGER DEFAULT 0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE CASCADE,"
        "    UNIQUE(machine_id, session_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create sessions table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: sessions table created";
    return true;
}

/*
 * Create the session_settings table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The session_settings table stores machine configuration for each session
 * (CPAP mode, pressures, comfort settings, etc.)
 */
bool DatabaseSchema::createSessionSettingsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS session_settings ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL,"
        "    profile_id INTEGER NOT NULL,"
        "    channel_id INTEGER NOT NULL,"
        "    value REAL NOT NULL,"
        "    data_type TEXT,"
        "    json_value TEXT,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    UNIQUE(session_id, channel_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create session_settings table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: session_settings table created";
    return true;
}

/*
 * Create the session_channels table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The session_channels table stores summary statistics for each channel
 * in a session (count, avg, min, max, percentiles, etc.)
 */
bool DatabaseSchema::createSessionChannelsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS session_channels ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL,"
        "    profile_id INTEGER NOT NULL,"
        "    channel_id INTEGER NOT NULL,"
        "    count INTEGER DEFAULT 0,"
        "    sum REAL DEFAULT 0,"
        "    avg REAL DEFAULT 0,"
        "    wavg REAL DEFAULT 0,"
        "    min REAL DEFAULT 0,"
        "    max REAL DEFAULT 0,"
        "    median REAL DEFAULT 0,"
        "    p90 REAL DEFAULT 0,"
        "    p95 REAL DEFAULT 0,"
        "    phys_min REAL DEFAULT 0,"
        "    phys_max REAL DEFAULT 0,"
        "    cph REAL DEFAULT 0,"
        "    sph REAL DEFAULT 0,"
        "    first_time INTEGER,"
        "    last_time INTEGER,"
        "    gain REAL DEFAULT 1.0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    UNIQUE(session_id, channel_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create session_channels table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: session_channels table created";
    return true;
}

/*
 * Create the session_channel_values table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The session_channel_values table stores detailed value/time summary data
 * for each channel. This data is used to calculate weighted averages and
 * other statistics that require knowledge of how long each distinct value
 * was held. This fixes a critical bug where m_valuesummary and m_timesummary
 * were not being persisted to the database.
 */
bool DatabaseSchema::createSessionChannelValuesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS session_channel_values ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_channel_id INTEGER NOT NULL,"
        "    value INTEGER NOT NULL,"
        "    count INTEGER DEFAULT 0,"
        "    time_ms INTEGER DEFAULT 0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_channel_id) REFERENCES session_channels(id) ON DELETE CASCADE,"
        "    UNIQUE(session_channel_id, value)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create session_channel_values table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: session_channel_values table created";
    return true;
}

/*
 * Create the respiratory_events table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The respiratory_events table stores individual respiratory events
 * (apneas, hypopneas, RERAs, etc.)
 */
bool DatabaseSchema::createRespiratoryEventsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS respiratory_events ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL,"
        "    profile_id INTEGER NOT NULL,"
        "    channel_id INTEGER,"
        "    event_type INTEGER NOT NULL,"
        "    start_time INTEGER NOT NULL,"
        "    end_time INTEGER NOT NULL,"
        "    duration INTEGER NOT NULL,"
        "    desaturation REAL,"
        "    severity INTEGER,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create respiratory_events table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: respiratory_events table created";
    return true;
}

/*
 * Create the session_summaries table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The session_summaries table stores cached high-level session summaries
 * (AHI, event counts, pressure/leak statistics, etc.)
 */
bool DatabaseSchema::createSessionSummariesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS session_summaries ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL UNIQUE,"
        "    profile_id INTEGER NOT NULL,"
        "    ahi REAL DEFAULT 0,"
        "    rdi REAL DEFAULT 0,"
        "    obstructive_count INTEGER DEFAULT 0,"
        "    unclassified_count INTEGER DEFAULT 0,"
        "    hypopnea_count INTEGER DEFAULT 0,"
        "    rera_count INTEGER DEFAULT 0,"
        "    clear_airway_count INTEGER DEFAULT 0,"
        "    pressure_avg REAL,"
        "    pressure_min REAL,"
        "    pressure_max REAL,"
        "    pressure_95th REAL,"
        "    leak_total_avg REAL,"
        "    leak_total_95th REAL,"
        "    leak_total_max REAL,"
        "    spo2_avg REAL,"
        "    spo2_min REAL,"
        "    pulse_avg REAL,"
        "    hours_used REAL DEFAULT 0,"
        "    mask_on_hours REAL DEFAULT 0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create session_summaries table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: session_summaries table created";
    return true;
}

/*
 * Create the session_slices table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The session_slices table stores mask-on/mask-off periods within sessions.
 */
bool DatabaseSchema::createSessionSlicesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS session_slices ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL,"
        "    start_time INTEGER NOT NULL,"
        "    end_time INTEGER NOT NULL,"
        "    status INTEGER NOT NULL,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create session_slices table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: session_slices table created";
    return true;
}

/*
 * Create the channels table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The channels table stores per-profile channel customizations
 * (enabled state, colors, labels, thresholds, etc.)
 */
bool DatabaseSchema::createChannelsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS channels ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL,"
        "    channel_id INTEGER NOT NULL,"
        "    channel_code TEXT NOT NULL,"
        "    type INTEGER,"
        "    enabled INTEGER NOT NULL DEFAULT 1,"
        "    default_color TEXT,"
        "    fullname TEXT,"
        "    label TEXT,"
        "    description TEXT,"
        "    lower_threshold REAL,"
        "    lower_threshold_color TEXT,"
        "    upper_threshold REAL,"
        "    upper_threshold_color TEXT,"
        "    show_in_overview INTEGER DEFAULT 0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    UNIQUE(profile_id, channel_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create channels table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: channels table created";
    return true;
}

/*
 * Create the channel_options table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The channel_options table stores lookup values for LOOKUP-type channels
 * (e.g., CPAP_Mode: 0="CPAP", 1="APAP", 2="Bi-Level")
 */
bool DatabaseSchema::createChannelOptionsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS channel_options ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    channel_id INTEGER NOT NULL,"
        "    option_key INTEGER NOT NULL,"
        "    option_value TEXT NOT NULL,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    UNIQUE(channel_id, option_key)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create channel_options table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: channel_options table created";
    return true;
}

/*
 * Create the daily_summaries table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The daily_summaries table stores cached daily aggregate statistics
 * for fast report generation without loading individual sessions.
 */
bool DatabaseSchema::createDailySummariesTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS daily_summaries ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    profile_id INTEGER NOT NULL,"
        "    date TEXT NOT NULL,"
        "    machine_id INTEGER,"
        "    "
        "    session_count INTEGER DEFAULT 0,"
        "    enabled_session_count INTEGER DEFAULT 0,"
        "    "
        "    total_hours REAL DEFAULT 0,"
        "    mask_on_hours REAL DEFAULT 0,"
        "    "
        "    ahi REAL DEFAULT 0,"
        "    rdi REAL DEFAULT 0,"
        "    obstructive_count INTEGER DEFAULT 0,"
        "    unclassified_count INTEGER DEFAULT 0,"
        "    hypopnea_count INTEGER DEFAULT 0,"
        "    rera_count INTEGER DEFAULT 0,"
        "    clear_airway_count INTEGER DEFAULT 0,"
        "    "
        "    pressure_avg REAL,"
        "    pressure_min REAL,"
        "    pressure_max REAL,"
        "    pressure_95th REAL,"
        "    "
        "    leak_total_avg REAL,"
        "    leak_total_95th REAL,"
        "    leak_total_max REAL,"
        "    leak_unintentional_avg REAL,"
        "    "
        "    spo2_avg REAL,"
        "    spo2_min REAL,"
        "    pulse_avg REAL,"
        "    pulse_min REAL,"
        "    pulse_max REAL,"
        "    "
        "    is_compliant INTEGER DEFAULT 0,"
        "    has_oximetry INTEGER DEFAULT 0,"
        "    "
        "    calculated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    sessions_hash TEXT,"
        "    "
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE SET NULL,"
        "    UNIQUE(profile_id, date, machine_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create daily_summaries table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: daily_summaries table created";
    return true;
}

/*
 * Create the event_lists table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The event_lists table stores metadata for each EventList (one row per EventList).
 * This includes timing, scaling, and dimensional information but not the actual data arrays.
 */
bool DatabaseSchema::createEventListsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS event_lists ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER NOT NULL,"
        "    profile_id INTEGER NOT NULL,"
        "    channel_id INTEGER NOT NULL,"
        "    eventlist_index INTEGER NOT NULL DEFAULT 0,"
        "    event_type INTEGER NOT NULL,"
        "    first_time INTEGER NOT NULL,"
        "    last_time INTEGER NOT NULL,"
        "    count INTEGER NOT NULL,"
        "    rate REAL NOT NULL DEFAULT 0,"
        "    gain REAL NOT NULL DEFAULT 1.0,"
        "    offset REAL NOT NULL DEFAULT 0.0,"
        "    min_value REAL NOT NULL DEFAULT 0.0,"
        "    max_value REAL NOT NULL DEFAULT 0.0,"
        "    dimension TEXT,"
        "    has_second_field INTEGER NOT NULL DEFAULT 0,"
        "    min2_value REAL,"
        "    max2_value REAL,"
        "    data_size INTEGER NOT NULL DEFAULT 0,"
        "    compressed_size INTEGER,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
        "    UNIQUE(session_id, channel_id, eventlist_index)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create event_lists table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: event_lists table created";
    return true;
}

/*
 * Create the event_data table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The event_data table stores actual binary data for each EventList (one row per EventList).
 * Data is stored as compressed BLOBs for efficiency, with checksums for integrity verification.
 */
bool DatabaseSchema::createEventDataTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS event_data ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    eventlist_id INTEGER NOT NULL,"
        "    data_blob BLOB,"
        "    data_compressed BLOB,"
        "    data2_blob BLOB,"
        "    data2_compressed BLOB,"
        "    time_blob BLOB,"
        "    time_compressed BLOB,"
        "    compression_method INTEGER NOT NULL DEFAULT 0,"
        "    checksum INTEGER,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (eventlist_id) REFERENCES event_lists(id) ON DELETE CASCADE,"
        "    UNIQUE(eventlist_id)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create event_data table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: event_data table created";
    return true;
}

/*
 * Create the reports table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The reports table stores CSV export report definitions.
 * These are global (not profile-specific) and support user customization.
 */
bool DatabaseSchema::createReportsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS reports ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    name TEXT UNIQUE NOT NULL,"
        "    description TEXT,"
        "    display_order INTEGER DEFAULT 0,"
        "    is_system INTEGER DEFAULT 0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create reports table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: reports table created";
    return true;
}

/*
 * Create the report_contents table
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * The report_contents table stores SQL query templates for each report variety.
 * Queries contain macros like #PROFILE_ID, #START_DATE, #END_DATE for substitution.
 */
bool DatabaseSchema::createReportContentsTable(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    QString sql = 
        "CREATE TABLE IF NOT EXISTS report_contents ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    report_id INTEGER NOT NULL,"
        "    variety TEXT NOT NULL,"
        "    description TEXT,"
        "    query TEXT NOT NULL,"
        "    display_order INTEGER DEFAULT 0,"
        "    is_system INTEGER DEFAULT 0,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (report_id) REFERENCES reports(id) ON DELETE CASCADE,"
        "    UNIQUE(report_id, variety)"
        ")";

    if (!query.exec(sql)) {
        qCritical() << "DatabaseSchema: Failed to create report_contents table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseSchema: report_contents table created";
    return true;
}

/*
 * Create indexes for reports tables
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates indexes to optimize report queries.
 */
bool DatabaseSchema::createReportIndexes(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_report_contents_report ON report_contents(report_id)")) {
        qWarning() << "DatabaseSchema: Failed to create report_contents report index:" << query.lastError().text();
        return false;
    }
    
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_reports_name ON reports(name)")) {
        qWarning() << "DatabaseSchema: Failed to create reports name index:" << query.lastError().text();
        return false;
    }
    
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_report_contents_variety ON report_contents(report_id, variety)")) {
        qWarning() << "DatabaseSchema: Failed to create report_contents variety index:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "DatabaseSchema: Report indexes created";
    return true;
}

/*
 * Initialize default CSV export reports
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Populates reports and report_contents tables with the three default reports.
 * This is called during schema creation and upgrades if tables are empty.
 * 
 * Note: Queries are placeholders for now - actual queries will be migrated from
 * exportcsv.cpp once integration is complete.
 */
bool DatabaseSchema::initializeDefaultReports(QSqlDatabase& db)
{
    // Delegate to version checking which handles both first-time init
    // and updates when OSCAR version changes
    return checkAndUpdateReportVersion(db);
}

/*
 * Check and update CSV report version
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * This is the main entry point for report version tracking.
 * On first run, initializes reports and saves version.
 * On subsequent runs, checks if OSCAR version changed and reinitializes system reports if needed.
 */
bool DatabaseSchema::checkAndUpdateReportVersion(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    // Check if reports table has any data
    if (!query.exec("SELECT COUNT(*) FROM reports")) {
        qWarning() << "DatabaseSchema: Failed to check reports table:" << query.lastError().text();
        return false;
    }
    
    if (query.next() && query.value(0).toInt() == 0) {
        // Empty table - first time initialization
        qDebug() << "DatabaseSchema: First time initialization of CSV reports";
        
        if (!initializeSystemReports(db)) {
            qCritical() << "DatabaseSchema: Failed to initialize system reports";
            return false;
        }
        
        // Save current OSCAR version
        saveReportVersion(db);
        return true;
    }
    
    // Reports exist - check if OSCAR version changed
    QString savedVersion = getSavedReportVersion();
    QString currentVersion = getVersion().displayString();
    
    if (savedVersion.isEmpty()) {
        // No saved version (upgrading from version before version tracking)
        qDebug() << "DatabaseSchema: No saved report version found, saving current version:" << currentVersion;
        saveReportVersion(db);
        return true;
    }
    
    if (savedVersion != currentVersion) {
        qDebug() << "DatabaseSchema: OSCAR version changed from" << savedVersion << "to" << currentVersion;
        qDebug() << "DatabaseSchema: Reinitializing system reports to update queries...";
        
        if (!reinitializeSystemReports(db)) {
            qCritical() << "DatabaseSchema: Failed to reinitialize system reports";
            return false;
        }
        
        // Save new version
        saveReportVersion(db);
        
        qDebug() << "DatabaseSchema: System reports successfully updated for OSCAR" << currentVersion;
        qDebug() << "DatabaseSchema: Custom user reports were preserved";
    } else {
        qDebug() << "DatabaseSchema: CSV reports version matches OSCAR version" << currentVersion << "- no update needed";
    }
    
    return true;
}

/*
 * Reinitialize system reports
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Deletes all system reports and recreates them with current queries.
 * Custom user reports (is_system=0) are preserved.
 */
bool DatabaseSchema::reinitializeSystemReports(QSqlDatabase& db)
{
    qDebug() << "DatabaseSchema: Deleting system reports...";
    
    QSqlQuery query(db);
    
    // Delete system report contents first (to satisfy foreign key constraints)
    if (!query.exec("DELETE FROM report_contents WHERE is_system = 1")) {
        qCritical() << "DatabaseSchema: Failed to delete system report contents:" << query.lastError().text();
        return false;
    }
    
    int contentsDeleted = query.numRowsAffected();
    qDebug() << "DatabaseSchema: Deleted" << contentsDeleted << "system report content records";
    
    // Delete system reports (CASCADE will handle any remaining contents)
    if (!query.exec("DELETE FROM reports WHERE is_system = 1")) {
        qCritical() << "DatabaseSchema: Failed to delete system reports:" << query.lastError().text();
        return false;
    }
    
    int reportsDeleted = query.numRowsAffected();
    qDebug() << "DatabaseSchema: Deleted" << reportsDeleted << "system report records";
    
    // Reinitialize with current queries
    qDebug() << "DatabaseSchema: Recreating system reports with current queries...";
    return initializeSystemReports(db);
}

/*
 * Initialize system reports (moved from initializeDefaultReports)
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates the default system reports with current queries.
 * This is the extracted report creation logic from the original initializeDefaultReports().
 */
bool DatabaseSchema::initializeSystemReports(QSqlDatabase& db)
{
    qDebug() << "DatabaseSchema: Initializing system reports...";
    
    QSqlQuery query(db);
    
    // Report 1: Daily Summaries
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Daily Summaries");
    query.addBindValue("Aggregated daily CPAP data");
    query.addBindValue(0);  // display_order = 0 for alphabetical sorting
    query.addBindValue(1);  // is_system = 1
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Daily Summaries report:" << query.lastError().text();
        return false;
    }
    qint64 dailySummariesId = query.lastInsertId().toLongLong();
    
    // Report 2: Session Summaries
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Session Summaries");
    query.addBindValue("Individual session or aggregated session summaries");
    query.addBindValue(0);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Summaries report:" << query.lastError().text();
        return false;
    }
    qint64 sessionStatsId = query.lastInsertId().toLongLong();

    ///////////////////////////////////////////////////////////////////
    // Report 3: Device Settings
    ///////////////////////////////////////////////////////////////////
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Device Settings");
    query.addBindValue("Machine configuration settings");
    query.addBindValue(0);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Device Settings report:" << query.lastError().text();
        return false;
    }
    qint64 deviceSettingsId = query.lastInsertId().toLongLong();
    
    ///////////////////////////////////////////////////////////////////
    // Report 4: Channels used
    ///////////////////////////////////////////////////////////////////
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Channels Used");
    query.addBindValue("Channels used by this user");
    query.addBindValue(0);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Channels Used report:" << query.lastError().text();
        return false;
    }
    qint64 channelsUsedId = query.lastInsertId().toLongLong();

    // Add report contents (varieties)
    // Note: These are the actual queries migrated from exportcsv.cpp with macros for substitution
    
    // Channels used - Days (no aggregation)
    QString channelsUsedQuery =

    "SELECT \n"
    "	profile_id,\n"
    "   p.username,\n"
    "   channel_id,\n"
    "   printf('%X', channel_id) AS channel_id_hex,\n"
    "--    channel_code,\n"
    "   label,\n"
    "   fullname,\n"
    "   description,\n"
    "   c.type,\n"
    "   CASE WHEN (c.type & 1) THEN 'DATA       ' ELSE '' END ||\n"
    "   CASE WHEN (c.type & 2) THEN 'SETTING    ' ELSE '' END ||\n"
    "	CASE WHEN (c.type & 4) THEN 'FLAG       ' ELSE '' END ||\n"
    "   CASE WHEN (c.type & 8) THEN 'MINOR_FLAG ' ELSE '' END ||\n"
    "   CASE WHEN (c.type & 16) THEN 'SPAN       ' ELSE '' END ||\n"
    "   CASE WHEN (c.type & 32) THEN 'WAVEFORM   ' ELSE '' END AS flags\n"
    "FROM channels c\n"
    "JOIN profiles p ON c.profile_id = p.id\n"
    "WHERE profile_id = #PROFILE_ID\n"
    "    AND EXISTS (\n"
    "        SELECT 1 FROM session_channels sc WHERE sc.channel_id = c.channel_id)\n"
    "ORDER BY c.channel_id;\n";

    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(channelsUsedId);
    query.addBindValue("Channels");
    query.addBindValue("Channels used in this profile");
    query.addBindValue(channelsUsedQuery);
    query.addBindValue(1);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Channels Used - Channels content:" << query.lastError().text();
        return false;
    }

    ///////////////////////////////////////////////////////////////////
    // Report 4.1: Session Statistics
    ///////////////////////////////////////////////////////////////////
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Session Statistics");
    query.addBindValue("CPAP statistics by session");
    query.addBindValue(0);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Statistics report:" << query.lastError().text();
        return false;
    }
    qint64 sessionStatisticsId = query.lastInsertId().toLongLong();

    // Add report contents (varieties)
    // Note: These are the actual queries migrated from exportcsv.cpp with macros for substitution

    // Session Statistics (no aggregation)
    QString sessionStatisticsQuery =

        "SELECT\n"
        "    p.id,\n"
        "    p.username,\n"
        "    sc.channel_id,\n"
        " -- subtract 43200 from time to get OSCAR day, which starts at noon\n"
        "    date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') as Date,\n"
        "    time(s.start_time/1000, 'unixepoch', 'localtime') as Start,\n"
        "    time(s.end_time/1000, 'unixepoch', 'localtime') as End,\n"
        "    s.duration/1000000 as seconds,\n"
        "    c.label,\n"
        "    sc.count,\n"
        "    ROUND(sc.sum,2) as Sum,\n"
        "    ROUND(sc.min,2) as Min,\n"
        "    ROUND(sc.avg,2) as Avg,\n"
        "    ROUND(sc.wavg,2) as Wavg,\n"
        "    ROUND(sc.median,2) as Med,\n"
        "    ROUND(sc.p90,2) as '%90',\n"
        "    ROUND(sc.p95,2) as '%95',\n"
        "    ROUND(sc.max,2) as Max\n"

        "FROM session_channels sc\n"
        "JOIN sessions s ON sc.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "JOIN profiles p ON m.profile_id = p.id\n"
        "LEFT JOIN channels c ON sc.channel_id = c.channel_id AND c.profile_id = p.id\n"
        "WHERE p.id = #PROFILE_ID\n"
        "    AND sc.channel_id IN (0x1101, 0x1102, 0x110e,0x1105, 0x1106,0x1113,0x1108,0x1104,0x110b,0x110a,0x1103,0x1201 )\n"
        "    AND Date >= #START_DATE\n"
        "    AND Date <= #END_DATE\n"
        "ORDER BY s.start_time";

    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatisticsId);
    query.addBindValue("Sessions");
    query.addBindValue("Statistics by session");
    query.addBindValue(sessionStatisticsQuery);
    query.addBindValue(1);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Statistics content:" << query.lastError().text();
        return false;
    }

    ///////////////////////////////////////////////////////////////////
    // Daily Summaries - Days (no aggregation)
    ///////////////////////////////////////////////////////////////////
    QString dailyDaysQuery =
        "SELECT\n"
        "  ds.date as Period,\n"
        "  ROUND(ds.ahi, 2) as AHI,\n"
        "  ROUND(ds.rdi, 2) as RDI,\n"
        "  ds.obstructive_count as OA,\n"
        "  ds.unclassified_count as UA,\n"
        "  ds.hypopnea_count as H,\n"
        "  ds.clear_airway_count as CA,\n"
        "  ds.rera_count as RERA,\n"
        "  ROUND(ds.pressure_avg, 2) as Pressure_Avg,\n"
        "  ROUND(ds.pressure_95th, 2) as Pressure_95th,\n"
        "  ROUND(ds.leak_total_avg, 2) as Leak_Avg,\n"
        "  ROUND(ds.leak_total_95th, 2) as Leak_95th,\n"
        "  ROUND(ds.mask_on_hours, 2) as Hours\n"
        "FROM daily_summaries ds\n"
        "WHERE ds.profile_id = #PROFILE_ID\n"
        "  AND ds.date >= #START_DATE\n"
        "  AND ds.date <= #END_DATE\n"
        "ORDER BY ds.date";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(dailySummariesId);
    query.addBindValue("Days");
    query.addBindValue("Daily data (no aggregation)");
    query.addBindValue(dailyDaysQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Daily Summaries - Days content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Daily Summaries - Weeks (weekly aggregation)
    ///////////////////////////////////////////////////////////////////
    QString dailyWeeksQuery =
        "SELECT\n"
        "  strftime('%Y-W%W', ds.date) as Period,\n"
        "  MIN(ds.date) as Week_Start,\n"
        "  MAX(ds.date) as Week_End,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as AHI,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count) + SUM(ds.rera_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as RDI,\n"
        "  SUM(ds.obstructive_count) as OA,\n"
        "  SUM(ds.unclassified_count) as UA,\n"
        "  SUM(ds.hypopnea_count) as H,\n"
        "  SUM(ds.clear_airway_count) as CA,\n"
        "  SUM(ds.rera_count) as RERA,\n"
        "  ROUND(SUM(ds.pressure_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Pressure_Avg,\n"
        "  ROUND(AVG(ds.pressure_95th), 2) as Pressure_95th,\n"
        "  ROUND(SUM(ds.leak_total_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Leak_Avg,\n"
        "  ROUND(AVG(ds.leak_total_95th), 2) as Leak_95th,\n"
        "  ROUND(SUM(ds.mask_on_hours), 2) as Hours\n"
        "FROM daily_summaries ds\n"
        "WHERE ds.profile_id = #PROFILE_ID\n"
        "  AND ds.date >= #START_DATE\n"
        "  AND ds.date <= #END_DATE\n"
        "GROUP BY strftime('%Y-W%W', ds.date)\n"
        "ORDER BY Period";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(dailySummariesId);
    query.addBindValue("Weeks");
    query.addBindValue("Weekly aggregation");
    query.addBindValue(dailyWeeksQuery);
    query.addBindValue(2);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Daily Summaries - Weeks content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Daily Summaries - Months (monthly aggregation)
    ///////////////////////////////////////////////////////////////////
    QString dailyMonthsQuery =
        "SELECT\n"
        "  strftime('%Y-%m', ds.date) as Period,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as AHI,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count) + SUM(ds.rera_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as RDI,\n"
        "  SUM(ds.obstructive_count) as OA,\n"
        "  SUM(ds.unclassified_count) as UA,\n"
        "  SUM(ds.hypopnea_count) as H,\n"
        "  SUM(ds.clear_airway_count) as CA,\n"
        "  SUM(ds.rera_count) as RERA,\n"
        "  ROUND(SUM(ds.pressure_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Pressure_Avg,\n"
        "  ROUND(AVG(ds.pressure_95th), 2) as Pressure_95th,\n"
        "  ROUND(SUM(ds.leak_total_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Leak_Avg,\n"
        "  ROUND(AVG(ds.leak_total_95th), 2) as Leak_95th,\n"
        "  ROUND(SUM(ds.mask_on_hours), 2) as Hours\n"
        "FROM daily_summaries ds\n"
        "WHERE ds.profile_id = #PROFILE_ID\n"
        "  AND ds.date >= #START_DATE\n"
        "  AND ds.date <= #END_DATE\n"
        "GROUP BY strftime('%Y-%m', ds.date)\n"
        "ORDER BY Period";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(dailySummariesId);
    query.addBindValue("Months");
    query.addBindValue("Monthly aggregation");
    query.addBindValue(dailyMonthsQuery);
    query.addBindValue(3);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Daily Summaries - Months content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Summaries - Sessions (no aggregation - one row per session)
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsSessionsQuery =
        "SELECT\n"
        "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date,\n"
        "  ROUND(ss.ahi, 2) as AHI,\n"
        "  ROUND(ss.mask_on_hours, 2) as Hours,\n"
        "  ss.obstructive_count as OA,\n"
        "  ss.unclassified_count as UA,\n"
        "  ss.hypopnea_count as H,\n"
        "  ss.clear_airway_count as CA,\n"
        "  m.model as Machine\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "ORDER BY s.start_time";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatsId);
    query.addBindValue("Sessions");
    query.addBindValue("Individual sessions (no aggregation)");
    query.addBindValue(sessionStatsSessionsQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Summaries - Sessions content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Summaries - Days (daily aggregation)
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsDaysQuery =
        "SELECT\n"
        "  date(s.start_time/1000, 'unixepoch', 'localtime') as Period,\n"
        "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI,\n"
        "  SUM(ss.obstructive_count) as OA,\n"
        "  SUM(ss.unclassified_count) as UA,\n"
        "  SUM(ss.hypopnea_count) as H,\n"
        "  SUM(ss.clear_airway_count) as CA,\n"
        "  ROUND(SUM(ss.mask_on_hours), 2) as Hours\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "GROUP BY date(s.start_time/1000, 'unixepoch', 'localtime')\n"
        "ORDER BY Period";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatsId);
    query.addBindValue("Days");
    query.addBindValue("Daily aggregation");
    query.addBindValue(sessionStatsDaysQuery);
    query.addBindValue(2);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Statistics - Days content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Summaries - Weeks (weekly aggregation)
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsWeeksQuery =
        "SELECT\n"
        "  strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period,\n"
        "  MIN(date(s.start_time/1000, 'unixepoch', 'localtime')) as Week_Start,\n"
        "  MAX(date(s.start_time/1000, 'unixepoch', 'localtime')) as Week_End,\n"
        "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI,\n"
        "  SUM(ss.obstructive_count) as OA,\n"
        "  SUM(ss.unclassified_count) as UA,\n"
        "  SUM(ss.hypopnea_count) as H,\n"
        "  SUM(ss.clear_airway_count) as CA,\n"
        "  ROUND(SUM(ss.mask_on_hours), 2) as Hours\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "GROUP BY strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime'))\n"
        "ORDER BY Period";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatsId);
    query.addBindValue("Weeks");
    query.addBindValue("Weekly aggregation");
    query.addBindValue(sessionStatsWeeksQuery);
    query.addBindValue(3);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Summaries - Weeks content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Summaries - Months (monthly aggregation)
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsMonthsQuery =
        "SELECT\n"
        "  strftime('%Y-%m', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period,\n"
        "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI,\n"
        "  SUM(ss.obstructive_count) as OA,\n"
        "  SUM(ss.unclassified_count) as UA,\n"
        "  SUM(ss.hypopnea_count) as H,\n"
        "  SUM(ss.clear_airway_count) as CA,\n"
        "  ROUND(SUM(ss.mask_on_hours), 2) as Hours\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "GROUP BY strftime('%Y-%m', date(s.start_time/1000, 'unixepoch', 'localtime'))\n"
        "ORDER BY Period";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatsId);
    query.addBindValue("Months");
    query.addBindValue("Monthly aggregation");
    query.addBindValue(sessionStatsMonthsQuery);
    query.addBindValue(4);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Summaries - Months content:" << query.lastError().text();
        return false;
    }

    ///////////////////////////////////////////////////////////////////
    // Device Settings - All Sessions
    ///////////////////////////////////////////////////////////////////
    QString deviceSettingsQuery =
        "SELECT\n"
        "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date,\n"
        "  m.model as Machine,\n"
        "  st.channel_id as Channel_ID,\n"
        "  COALESCE(c.fullname, c.label, c.channel_code, 'Channel_' || st.channel_id) as Setting,\n"
        "  st.value as Value,\n"
        "  st.data_type as Type\n"
        "FROM session_settings st\n"
        "JOIN sessions s ON st.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "LEFT JOIN channels c ON c.profile_id = m.profile_id AND c.channel_id = st.channel_id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "ORDER BY s.start_time, st.channel_id";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(deviceSettingsId);
    query.addBindValue("All Sessions");
    query.addBindValue("All device settings (no aggregation)");
    query.addBindValue(deviceSettingsQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Device Settings - All Sessions content:" << query.lastError().text();
        return false;
    }
    
    ///////////////////////////////////////////////////////////////////
    // Report 4: Respiratory Events
    ///////////////////////////////////////////////////////////////////
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Respiratory Events");
    query.addBindValue("Detailed respiratory event data");
    query.addBindValue(0);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Respiratory Events report:" << query.lastError().text();
        return false;
    }
    qint64 respiratoryEventsId = query.lastInsertId().toLongLong();
    
    // Respiratory Events - Details (individual events with profile name, date, type, times)
    // Note: OSCAR dates run from noon to noon, so we subtract 12 hours (43200 seconds) before extracting date
    QString respiratoryEventsQuery =
        "SELECT\n"
        "  p.username as Profile_Name,\n"
        "  date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') as Date,\n"
        "  c.fullname as Name,\n"
        "  c.label as Event_Type,\n"
        "  time(re.start_time/1000, 'unixepoch', 'localtime') as Start_Time,\n"
        "  time(re.end_time/1000, 'unixepoch', 'localtime') as End_Time,\n"
        "  re.duration as Duration_Seconds\n"
        "FROM respiratory_events re\n"
        "JOIN sessions s ON re.session_id = s.id\n"
        "JOIN profiles p ON re.profile_id = p.id\n"
        "JOIN channels c ON re.channel_id = c.channel_id AND c.profile_id = re.profile_id\n"
        "WHERE re.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "--use re.event_type = 1 to see only AHI-contributing events\n"
        "--AND re.event_type = 1\n"
        "ORDER BY re.start_time";

    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(respiratoryEventsId);
    query.addBindValue("Details");
    query.addBindValue("Individual respiratory events with timestamps");
    query.addBindValue(respiratoryEventsQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Respiratory Events - Details content:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "DatabaseSchema: Default reports initialized successfully";
    qDebug() << "DatabaseSchema: Created 4 reports with 9 content varieties total";
    
    return true;
}

/*
 * Update default report queries with full SQL
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * This function updates existing report content queries with the full SQL queries.
 * Used to migrate from placeholder queries to production queries.
 */
bool DatabaseSchema::updateDefaultReportQueries(QSqlDatabase& db)
{
    qDebug() << "DatabaseSchema: Updating default report queries...";
    
    QSqlQuery query(db);
    
    // Update queries by report name and variety
    // We use report name and variety to find the records since those are unique
    
    ///////////////////////////////////////////////////////////////////
    // Daily Summaries - Days
    ///////////////////////////////////////////////////////////////////
    QString dailyDaysQuery =
        "SELECT\n"
        "  ds.date as Period,\n"
        "  ROUND(ds.ahi, 2) as AHI,\n"
        "  ROUND(ds.rdi, 2) as RDI,\n"
        "  ds.obstructive_count as OA,\n"
        "  ds.unclassified_count as UA,\n"
        "  ds.hypopnea_count as H,\n"
        "  ds.clear_airway_count as CA,\n"
        "  ds.rera_count as RERA,\n"
        "  ROUND(ds.pressure_avg, 2) as Pressure_Avg,\n"
        "  ROUND(ds.pressure_95th, 2) as Pressure_95th,\n"
        "  ROUND(ds.leak_total_avg, 2) as Leak_Avg,\n"
        "  ROUND(ds.leak_total_95th, 2) as Leak_95th,\n"
        "  ROUND(ds.mask_on_hours, 2) as Hours\n"
        "FROM daily_summaries ds\n"
        "WHERE ds.profile_id = #PROFILE_ID\n"
        "  AND ds.date >= #START_DATE\n"
        "  AND ds.date <= #END_DATE\n"
        "ORDER BY ds.date";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Daily Summaries') AND variety = 'Days'");
    query.addBindValue(dailyDaysQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Daily Summaries - Days:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Daily Summaries - Weeks
    ///////////////////////////////////////////////////////////////////
    QString dailyWeeksQuery =
        "SELECT\n"
        "  strftime('%Y-W%W', ds.date) as Period,\n"
        "  MIN(ds.date) as Week_Start,\n"
        "  MAX(ds.date) as Week_End,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as AHI,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count) + SUM(ds.rera_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as RDI,\n"
        "  SUM(ds.obstructive_count) as OA,\n"
        "  SUM(ds.unclassified_count) as UA,\n"
        "  SUM(ds.hypopnea_count) as H,\n"
        "  SUM(ds.clear_airway_count) as CA,\n"
        "  SUM(ds.rera_count) as RERA,\n"
        "  ROUND(SUM(ds.pressure_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Pressure_Avg,\n"
        "  ROUND(AVG(ds.pressure_95th), 2) as Pressure_95th,\n"
        "  ROUND(SUM(ds.leak_total_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Leak_Avg,\n"
        "  ROUND(AVG(ds.leak_total_95th), 2) as Leak_95th,\n"
        "  ROUND(SUM(ds.mask_on_hours), 2) as Hours\n"
        "FROM daily_summaries ds\n"
        "WHERE ds.profile_id = #PROFILE_ID\n"
        "  AND ds.date >= #START_DATE\n"
        "  AND ds.date <= #END_DATE\n"
        "GROUP BY strftime('%Y-W%W', ds.date)\n"
        "ORDER BY Period";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Daily Summaries') AND variety = 'Weeks'");
    query.addBindValue(dailyWeeksQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Daily Summaries - Weeks:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Daily Summaries - Months
    ///////////////////////////////////////////////////////////////////
    QString dailyMonthsQuery =
        "SELECT\n"
        "  strftime('%Y-%m', ds.date) as Period,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as AHI,\n"
        "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count) + SUM(ds.rera_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as RDI,\n"
        "  SUM(ds.obstructive_count) as OA,\n"
        "  SUM(ds.unclassified_count) as UA,\n"
        "  SUM(ds.hypopnea_count) as H,\n"
        "  SUM(ds.clear_airway_count) as CA,\n"
        "  SUM(ds.rera_count) as RERA,\n"
        "  ROUND(SUM(ds.pressure_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Pressure_Avg,\n"
        "  ROUND(AVG(ds.pressure_95th), 2) as Pressure_95th,\n"
        "  ROUND(SUM(ds.leak_total_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Leak_Avg,\n"
        "  ROUND(AVG(ds.leak_total_95th), 2) as Leak_95th,\n"
        "  ROUND(SUM(ds.mask_on_hours), 2) as Hours\n"
        "FROM daily_summaries ds\n"
        "WHERE ds.profile_id = #PROFILE_ID\n"
        "  AND ds.date >= #START_DATE\n"
        "  AND ds.date <= #END_DATE\n"
        "GROUP BY strftime('%Y-%m', ds.date)\n"
        "ORDER BY Period";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Daily Summaries') AND variety = 'Months'");
    query.addBindValue(dailyMonthsQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Daily Summaries - Months:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Statistics - Sessions
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsSessionsQuery =
        "SELECT\n"
        "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date,\n"
        "  ROUND(ss.ahi, 2) as AHI,\n"
        "  ROUND(ss.mask_on_hours, 2) as Hours,\n"
        "  ss.obstructive_count as OA,\n"
        "  ss.unclassified_count as UA,\n"
        "  ss.hypopnea_count as H,\n"
        "  ss.clear_airway_count as CA,\n"
        "  m.model as Machine\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "ORDER BY s.start_time";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Session Statistics') AND variety = 'Sessions'");
    query.addBindValue(sessionStatsSessionsQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Session Statistics - Sessions:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Statistics - Days
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsDaysQuery =
        "SELECT\n"
        "  date(s.start_time/1000, 'unixepoch', 'localtime') as Period,\n"
        "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI,\n"
        "  SUM(ss.obstructive_count) as OA,\n"
        "  SUM(ss.unclassified_count) as UA,\n"
        "  SUM(ss.hypopnea_count) as H,\n"
        "  SUM(ss.clear_airway_count) as CA,\n"
        "  ROUND(SUM(ss.mask_on_hours), 2) as Hours\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "GROUP BY date(s.start_time/1000, 'unixepoch', 'localtime')\n"
        "ORDER BY Period";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Session Statistics') AND variety = 'Days'");
    query.addBindValue(sessionStatsDaysQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Session Statistics - Days:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Statistics - Weeks
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsWeeksQuery =
        "SELECT\n"
        "  strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period,\n"
        "  MIN(date(s.start_time/1000, 'unixepoch', 'localtime')) as Week_Start,\n"
        "  MAX(date(s.start_time/1000, 'unixepoch', 'localtime')) as Week_End,\n"
        "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI,\n"
        "  SUM(ss.obstructive_count) as OA,\n"
        "  SUM(ss.unclassified_count) as UA,\n"
        "  SUM(ss.hypopnea_count) as H,\n"
        "  SUM(ss.clear_airway_count) as CA,\n"
        "  ROUND(SUM(ss.mask_on_hours), 2) as Hours\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "GROUP BY strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime'))\n"
        "ORDER BY Period";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Session Statistics') AND variety = 'Weeks'");
    query.addBindValue(sessionStatsWeeksQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Session Statistics - Weeks:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Session Statistics - Months
    ///////////////////////////////////////////////////////////////////
    QString sessionStatsMonthsQuery =
        "SELECT\n"
        "  strftime('%Y-%m', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period,\n"
        "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI,\n"
        "  SUM(ss.obstructive_count) as OA,\n"
        "  SUM(ss.unclassified_count) as UA,\n"
        "  SUM(ss.hypopnea_count) as H,\n"
        "  SUM(ss.clear_airway_count) as CA,\n"
        "  ROUND(SUM(ss.mask_on_hours), 2) as Hours\n"
        "FROM session_summaries ss\n"
        "JOIN sessions s ON ss.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "GROUP BY strftime('%Y-%m', date(s.start_time/1000, 'unixepoch', 'localtime'))\n"
        "ORDER BY Period";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Session Statistics') AND variety = 'Months'");
    query.addBindValue(sessionStatsMonthsQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Session Statistics - Months:" << query.lastError().text();
    }
    
    ///////////////////////////////////////////////////////////////////
    // Device Settings - All Sessions
    ///////////////////////////////////////////////////////////////////
    QString deviceSettingsQuery =
        "SELECT\n"
        "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date,\n"
        "  m.model as Machine,\n"
        "  st.channel_id as Channel_ID,\n"
        "  COALESCE(c.fullname, c.label, c.channel_code, 'Channel_' || st.channel_id) as Setting,\n"
        "  st.value as Value,\n"
        "  st.data_type as Type\n"
        "FROM session_settings st\n"
        "JOIN sessions s ON st.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "LEFT JOIN channels c ON c.profile_id = m.profile_id AND c.channel_id = st.channel_id\n"
        "WHERE m.profile_id = #PROFILE_ID\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= #END_DATE\n"
        "ORDER BY s.start_time, st.channel_id";
    
    query.prepare("UPDATE report_contents SET query = ? WHERE report_id = (SELECT id FROM reports WHERE name = 'Device Settings') AND variety = 'All Sessions'");
    query.addBindValue(deviceSettingsQuery);
    if (!query.exec()) {
        qWarning() << "DatabaseSchema: Failed to update Device Settings - All Sessions:" << query.lastError().text();
    }
    
    qDebug() << "DatabaseSchema: Default report queries updated successfully";
    return true;

    ///////////////////////////////////////////////////////////////////
    // Report 4.1: Session Statistics
    ///////////////////////////////////////////////////////////////////
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Session Statistics");
    query.addBindValue("CPAP statistics by session");
    query.addBindValue(0);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Statistics report:" << query.lastError().text();
        return false;
    }
    qint64 sessionStatisticsId = query.lastInsertId().toLongLong();

    // Add report contents (varieties)
    // Note: These are the actual queries migrated from exportcsv.cpp with macros for substitution

    // Session Statistics (no aggregation)
    QString sessionStatisticsQuery =

        "SELECT\n"
        "    p.id,\n"
        "    p.username,\n"
        "    sc.channel_id,\n"
        " -- subtract 43200 from time to get OSCAR day, which starts at noon\n"
        "    date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') as Date,\n"
        "    time(s.start_time/1000, 'unixepoch', 'localtime') as Start,\n"
        "    time(s.end_time/1000, 'unixepoch', 'localtime') as End,\n"
        "    s.duration/1000000 as seconds,\n"
        "    c.label,\n"
        "    sc.count,\n"
        "    ROUND(sc.sum,2) as Sum,\n"
        "    ROUND(sc.min,2) as Min,\n"
        "    ROUND(sc.avg,2) as Avg,\n"
        "    ROUND(sc.wavg,2) as Wavg,\n"
        "    ROUND(sc.median,2) as Med,\n"
        "    ROUND(sc.p90,2) as '%90',\n"
        "    ROUND(sc.p95,2) as '%95',\n"
        "    ROUND(sc.max,2) as Max\n"

        "FROM session_channels sc\n"
        "JOIN sessions s ON sc.session_id = s.id\n"
        "JOIN machines m ON s.machine_id = m.id\n"
        "JOIN profiles p ON m.profile_id = p.id\n"
        "LEFT JOIN channels c ON sc.channel_id = c.channel_id AND c.profile_id = p.id\n"
        "WHERE p.id = #PROFILE_ID\n"
        "    AND sc.channel_id IN (0x1101, 0x1102, 0x110e,0x1105, 0x1106,0x1113,0x1108,0x1104,0x110b,0x110a,0x1103,0x1201 )\n"
        "    AND Date >= #START_DATE\n"
        "    AND Date <= #END_DATE\n"
        "ORDER BY s.start_time";

    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatisticsId);
    query.addBindValue("Sessions");
    query.addBindValue("Statistics by session");
    query.addBindValue(sessionStatisticsQuery);
    query.addBindValue(1);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "DatabaseSchema: Failed to create Session Statistics content:" << query.lastError().text();
        return false;
    }
}

/*
 * Get saved CSV report version
 *
 * Returns: Saved OSCAR version string, or empty string if not found
 *
 * Retrieves the OSCAR version that was active when reports were last initialized.
 */
QString DatabaseSchema::getSavedReportVersion()
{
    QSettings settings;
    QString version = settings.value("csv_reports_version", "").toString();
    
    if (!version.isEmpty()) {
        qDebug() << "DatabaseSchema: Saved CSV reports version:" << version;
    }
    
    return version;
}

/*
 * Save CSV report version
 *
 * Parameters:
 *   db - Database connection (unused but kept for consistency)
 *
 * Saves the current OSCAR version to settings for future version checking.
 */
void DatabaseSchema::saveReportVersion(QSqlDatabase& db)
{
    Q_UNUSED(db);
    
    QString currentVersion = getVersion().displayString();
    QSettings settings;
    settings.setValue("csv_reports_version", currentVersion);
    
    qDebug() << "DatabaseSchema: Saved CSV reports version:" << currentVersion;
}
