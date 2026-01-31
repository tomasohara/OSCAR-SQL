/* Reports Initializer Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportsInitializer class which creates and
 * maintains the CSV export reports tables and data.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "reports_initializer.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSettings>
#include "version.h"

/*
 * Create the reports tables
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates reports and report_contents tables with indexes.
 */
bool ReportsInitializer::createReportsTables(QSqlDatabase& db)
{
    qDebug() << "ReportsInitializer: Creating reports tables...";

    if (!createReportsTable(db)) {
        qCritical() << "ReportsInitializer: Failed to create reports table";
        return false;
    }

    if (!createReportContentsTable(db)) {
        qCritical() << "ReportsInitializer: Failed to create report_contents table";
        return false;
    }

    if (!createReportIndexes(db)) {
        qCritical() << "ReportsInitializer: Failed to create report indexes";
        return false;
    }

    qDebug() << "ReportsInitializer: Reports tables created successfully";
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
bool ReportsInitializer::createReportsTable(QSqlDatabase& db)
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
        qCritical() << "ReportsInitializer: Failed to create reports table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "ReportsInitializer: reports table created";
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
bool ReportsInitializer::createReportContentsTable(QSqlDatabase& db)
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
        qCritical() << "ReportsInitializer: Failed to create report_contents table:" 
                    << query.lastError().text();
        return false;
    }

    qDebug() << "ReportsInitializer: report_contents table created";
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
bool ReportsInitializer::createReportIndexes(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_report_contents_report ON report_contents(report_id)")) {
        qWarning() << "ReportsInitializer: Failed to create report_contents report index:" << query.lastError().text();
        return false;
    }
    
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_reports_name ON reports(name)")) {
        qWarning() << "ReportsInitializer: Failed to create reports name index:" << query.lastError().text();
        return false;
    }
    
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_report_contents_variety ON report_contents(report_id, variety)")) {
        qWarning() << "ReportsInitializer: Failed to create report_contents variety index:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "ReportsInitializer: Report indexes created";
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
 */
bool ReportsInitializer::initializeDefaultReports(QSqlDatabase& db)
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
bool ReportsInitializer::checkAndUpdateReportVersion(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    // Check if reports table has any data
    if (!query.exec("SELECT COUNT(*) FROM reports")) {
        qWarning() << "ReportsInitializer: Failed to check reports table:" << query.lastError().text();
        return false;
    }
    
    if (query.next() && query.value(0).toInt() == 0) {
        // Empty table - first time initialization
        qDebug() << "ReportsInitializer: First time initialization of CSV reports";
        
        if (!initializeSystemReports(db)) {
            qCritical() << "ReportsInitializer: Failed to initialize system reports";
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
        qDebug() << "ReportsInitializer: No saved report version found, saving current version:" << currentVersion;
        saveReportVersion(db);
        return true;
    }
    
    if (savedVersion != currentVersion) {
        qDebug() << "ReportsInitializer: OSCAR version changed from" << savedVersion << "to" << currentVersion;
        qDebug() << "ReportsInitializer: Reinitializing system reports to update queries...";
        
        if (!reinitializeSystemReports(db)) {
            qCritical() << "ReportsInitializer: Failed to reinitialize system reports";
            return false;
        }
        
        // Save new version
        saveReportVersion(db);
        
        qDebug() << "ReportsInitializer: System reports successfully updated for OSCAR" << currentVersion;
        qDebug() << "ReportsInitializer: Custom user reports were preserved";
    } else {
        qDebug() << "ReportsInitializer: CSV reports version matches OSCAR version" << currentVersion << "- no update needed";
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
bool ReportsInitializer::reinitializeSystemReports(QSqlDatabase& db)
{
    qDebug() << "ReportsInitializer: Deleting system reports...";
    
    QSqlQuery query(db);
    
    // Delete system report contents first (to satisfy foreign key constraints)
    if (!query.exec("DELETE FROM report_contents WHERE is_system = 1")) {
        qCritical() << "ReportsInitializer: Failed to delete system report contents:" << query.lastError().text();
        return false;
    }
    
    int contentsDeleted = query.numRowsAffected();
    qDebug() << "ReportsInitializer: Deleted" << contentsDeleted << "system report content records";
    
    // Delete system reports (CASCADE will handle any remaining contents)
    if (!query.exec("DELETE FROM reports WHERE is_system = 1")) {
        qCritical() << "ReportsInitializer: Failed to delete system reports:" << query.lastError().text();
        return false;
    }
    
    int reportsDeleted = query.numRowsAffected();
    qDebug() << "ReportsInitializer: Deleted" << reportsDeleted << "system report records";
    
    // Reinitialize with current queries
    qDebug() << "ReportsInitializer: Recreating system reports with current queries...";
    return initializeSystemReports(db);
}

/*
 * Initialize system reports (implementation with all SQL queries)
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates the default system reports with current queries.
 * This contains all the SQL query templates for the CSV export reports.
 */
bool ReportsInitializer::initializeSystemReports(QSqlDatabase& db)
{
    qDebug() << "ReportsInitializer: Initializing system reports...";
    
    QSqlQuery query(db);
    
    // Report 1: Daily Summaries
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Daily Summaries");
    query.addBindValue("Aggregated daily CPAP data");
    query.addBindValue(0);  // display_order = 0 for alphabetical sorting
    query.addBindValue(1);  // is_system = 1
    
    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Daily Summaries report:" << query.lastError().text();
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
        qCritical() << "ReportsInitializer: Failed to create Session Summaries report:" << query.lastError().text();
        return false;
    }
    qint64 sessionStatsId = query.lastInsertId().toLongLong();

    // Report 3: Device Settings
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Device Settings");
    query.addBindValue("Machine configuration settings");
    query.addBindValue(0);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Device Settings report:" << query.lastError().text();
        return false;
    }
    qint64 deviceSettingsId = query.lastInsertId().toLongLong();
    
    // Report 4: Channels used
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Channels Used");
    query.addBindValue("Channels used by this user");
    query.addBindValue(0);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Channels Used report:" << query.lastError().text();
        return false;
    }
    qint64 channelsUsedId = query.lastInsertId().toLongLong();

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
        qCritical() << "ReportsInitializer: Failed to create Channels Used - Channels content:" << query.lastError().text();
        return false;
    }

    // Report 4.1: Session Statistics
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Session Statistics");
    query.addBindValue("CPAP statistics by session");
    query.addBindValue(0);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Session Statistics report:" << query.lastError().text();
        return false;
    }
    qint64 sessionStatisticsId = query.lastInsertId().toLongLong();

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
        "ORDER BY s.start_time AND c.channel_id";

    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatisticsId);
    query.addBindValue("Sessions");
    query.addBindValue("Statistics by session");
    query.addBindValue(sessionStatisticsQuery);
    query.addBindValue(1);
    query.addBindValue(1);

    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Session Statistics content:" << query.lastError().text();
        return false;
    }

    // Daily Summaries - Days (no aggregation)
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
        qCritical() << "ReportsInitializer: Failed to create Daily Summaries - Days content:" << query.lastError().text();
        return false;
    }
    
    // Daily Summaries - Weeks (weekly aggregation)
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
        qCritical() << "ReportsInitializer: Failed to create Daily Summaries - Weeks content:" << query.lastError().text();
        return false;
    }
    
    // Daily Summaries - Months (monthly aggregation)
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
        qCritical() << "ReportsInitializer: Failed to create Daily Summaries - Months content:" << query.lastError().text();
        return false;
    }
    
    // Session Summaries - Sessions (no aggregation - one row per session)
    QString sessionStatsSessionsQuery =
        "SELECT\n"
        "  date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') as Date,\n"
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
        "  AND Date >= #START_DATE\n"
        "  AND Date <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "  AND m.machine_TYPE < 4\n"
        "ORDER BY s.start_time";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionStatsId);
    query.addBindValue("Sessions");
    query.addBindValue("Individual sessions (no aggregation)");
    query.addBindValue(sessionStatsSessionsQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Session Summaries - Sessions content:" << query.lastError().text();
        return false;
    }
    
    // Session Summaries - Days (daily aggregation)
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
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "  AND m.machine_TYPE < 4\n"
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
        qCritical() << "ReportsInitializer: Failed to create Session Statistics - Days content:" << query.lastError().text();
        return false;
    }
    
    // Session Summaries - Weeks (weekly aggregation)
    QString sessionStatsWeeksQuery =
        "SELECT\n"
        "  strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period,\n"
        "  MIN(date(s.start_time/1000 - 43200, 'unixepoch', 'localtime')) as Week_Start,\n"
        "  MAX(date(s.start_time/1000 - 43200, 'unixepoch', 'localtime')) as Week_End,\n"
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
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "  AND m.machine_TYPE < 4\n"
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
        qCritical() << "ReportsInitializer: Failed to create Session Summaries - Weeks content:" << query.lastError().text();
        return false;
    }
    
    // Session Summaries - Months (monthly aggregation)
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
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') >= #START_DATE\n"
        "  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "  AND m.machine_TYPE < 4\n"
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
        qCritical() << "ReportsInitializer: Failed to create Session Summaries - Months content:" << query.lastError().text();
        return false;
    }

    // Device Settings - All Sessions
    QString deviceSettingsQuery =
        "SELECT\n"
        "  date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') as Date,\n"
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
        "  AND Date >= #START_DATE\n"
        "  AND Date <= #END_DATE\n"
        "ORDER BY s.start_time, st.channel_id";
    
    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(deviceSettingsId);
    query.addBindValue("All Sessions");
    query.addBindValue("All device settings (no aggregation)");
    query.addBindValue(deviceSettingsQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Device Settings - All Sessions content:" << query.lastError().text();
        return false;
    }
    
    // Report 4: Respiratory Events
    query.prepare("INSERT INTO reports (name, description, display_order, is_system) VALUES (?, ?, ?, ?)");
    query.addBindValue("Respiratory Events");
    query.addBindValue("Detailed respiratory event data");
    query.addBindValue(0);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Respiratory Events report:" << query.lastError().text();
        return false;
    }
    qint64 respiratoryEventsId = query.lastInsertId().toLongLong();
    
    // Respiratory Events - Details (individual events with profile name, date, type, times)
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
        "  AND Date >= #START_DATE\n"
        "  AND Date <= #END_DATE\n"
        "  AND s.enabled = 1\n"
        "--use re.event_type = 1 to see only AHI-contributing events\n"
        "  AND re.event_type = 1\n"
        "ORDER BY re.start_time";

    query.prepare("INSERT INTO report_contents (report_id, variety, description, query, display_order, is_system) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(respiratoryEventsId);
    query.addBindValue("Details");
    query.addBindValue("Individual respiratory events with timestamps");
    query.addBindValue(respiratoryEventsQuery);
    query.addBindValue(1);
    query.addBindValue(1);
    
    if (!query.exec()) {
        qCritical() << "ReportsInitializer: Failed to create Respiratory Events - Details content:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "ReportsInitializer: Default reports initialized successfully";
    qDebug() << "ReportsInitializer: Created 6 reports with multiple content varieties";
    
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
 * Currently not used but kept for potential future use.
 */
bool ReportsInitializer::updateDefaultReportQueries(QSqlDatabase& db)
{
    Q_UNUSED(db);
    qDebug() << "ReportsInitializer: updateDefaultReportQueries called but not implemented";
    qDebug() << "ReportsInitializer: Query updates are handled through reinitializeSystemReports";
    return true;
}

/*
 * Get saved CSV report version
 *
 * Returns: Saved OSCAR version string, or empty string if not found
 *
 * Retrieves the OSCAR version that was active when reports were last initialized.
 */
QString ReportsInitializer::getSavedReportVersion()
{
    QSettings settings;
    QString version = settings.value("csv_reports_version", "").toString();
    
    if (!version.isEmpty()) {
        qDebug() << "ReportsInitializer: Saved CSV reports version:" << version;
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
void ReportsInitializer::saveReportVersion(QSqlDatabase& db)
{
    Q_UNUSED(db);
    
    QString currentVersion = getVersion().displayString();
    QSettings settings;
    settings.setValue("csv_reports_version", currentVersion);
    
    qDebug() << "ReportsInitializer: Saved CSV reports version:" << currentVersion;
}
