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
    qDebug() << "DatabaseSchema: Upgrading schema from version" << fromVersion << "to" << CURRENT_SCHEMA_VERSION;

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
        "    events_file TEXT,"
        "    summary_file TEXT,"
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
        "    channel_id INTEGER NOT NULL,"
        "    value REAL NOT NULL,"
        "    data_type TEXT,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
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
        "    event_type INTEGER NOT NULL,"
        "    start_time INTEGER NOT NULL,"
        "    end_time INTEGER NOT NULL,"
        "    duration INTEGER NOT NULL,"
        "    desaturation REAL,"
        "    severity INTEGER,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE"
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
        "    ahi REAL DEFAULT 0,"
        "    rdi REAL DEFAULT 0,"
        "    obstructive_count INTEGER DEFAULT 0,"
        "    central_count INTEGER DEFAULT 0,"
        "    hypopnea_count INTEGER DEFAULT 0,"
        "    rera_count INTEGER DEFAULT 0,"
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
        "    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE"
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
        "    central_count INTEGER DEFAULT 0,"
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
