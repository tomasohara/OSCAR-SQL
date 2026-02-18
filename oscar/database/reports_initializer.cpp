/* Reports Initializer Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportsInitializer class which creates and
 * maintains the hierarchical CSV export report tree.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "reports_initializer.h"
#include "report_tree_repository.h"
#include "orf_file_io.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include "version.h"

/*
 * Initialize the report tree with root nodes and system reports
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates the System and User root nodes, then loads system reports
 * from system_reports.orf file. Called during database creation.
 */
bool ReportsInitializer::initializeReportTree(QSqlDatabase& db)
{
    Q_UNUSED(db);
    
    qDebug() << "ReportsInitializer: Initializing report tree...";
    
    // Drop old tables if they exist (migration from old schema)
    if (!dropOldReportTables(db)) {
        qWarning() << "ReportsInitializer: Failed to drop old report tables (may not exist)";
        // Continue anyway - tables might not exist
    }
    
    // Create root nodes
    if (!createRootNodes(db)) {
        qCritical() << "ReportsInitializer: Failed to create root nodes";
        return false;
    }
    
    // Load system reports from file
    if (!loadSystemReportsFromFile(db)) {
        qCritical() << "ReportsInitializer: Failed to load system reports from file";
        return false;
    }
    
    // Save current version
    saveReportVersion(db);
    
    qDebug() << "ReportsInitializer: Report tree initialized successfully";
    return true;
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
    ReportTreeRepository repo;
    
    // Check if root nodes exist
    QList<ReportTreeNode> roots = repo.findRoots();
    
    if (roots.isEmpty()) {
        // Empty table - first time initialization
        qDebug() << "ReportsInitializer: First time initialization of report tree";
        return initializeReportTree(db);
    }
    
    // Roots exist - check if OSCAR version changed
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
 * Create the two root nodes (System and User)
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Creates the permanent root nodes with IDs 1 and 2.
 */
bool ReportsInitializer::createRootNodes(QSqlDatabase& db)
{
    Q_UNUSED(db);
    
    ReportTreeRepository repo;
    
    // Check if roots already exist
    QList<ReportTreeNode> existingRoots = repo.findRoots();
    if (!existingRoots.isEmpty()) {
        qDebug() << "ReportsInitializer: Root nodes already exist";
        return true;
    }
    
    // Create System root node
    ReportTreeNode systemRoot;
    systemRoot.parentId = 0;  // NULL parent
    systemRoot.name = "System";
    systemRoot.nodeType = "root";
    systemRoot.source = "system";
    systemRoot.displayOrder = 0;
    
    qint64 systemRootId = repo.create(systemRoot);
    if (systemRootId == 0) {
        qCritical() << "ReportsInitializer: Failed to create System root node";
        return false;
    }
    qDebug() << "ReportsInitializer: Created System root node with ID" << systemRootId;
    
    // Create User root node
    ReportTreeNode userRoot;
    userRoot.parentId = 0;  // NULL parent
    userRoot.name = "User";
    userRoot.nodeType = "root";
    userRoot.source = "user";
    userRoot.displayOrder = 1;
    
    qint64 userRootId = repo.create(userRoot);
    if (userRootId == 0) {
        qCritical() << "ReportsInitializer: Failed to create User root node";
        return false;
    }
    qDebug() << "ReportsInitializer: Created User root node with ID" << userRootId;
    
    return true;
}

/*
 * Load system reports from the system_reports.orf file
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Loads reports from oscar/docs/system_reports.orf into the System branch.
 */
bool ReportsInitializer::loadSystemReportsFromFile(QSqlDatabase& db)
{
    Q_UNUSED(db);
    
    QString filePath = getSystemReportsFilePath();
    
    if (!QFile::exists(filePath)) {
        qCritical() << "ReportsInitializer: System reports file not found:" << filePath;
        return false;
    }
    
    qDebug() << "ReportsInitializer: Loading system reports from" << filePath;
    
    // Parse the .orf file
    QList<OrfReportEntry> entries;
    QString errorMessage;
    
    if (!OrfFileIO::readFile(filePath, entries, errorMessage)) {
        qCritical() << "ReportsInitializer: Failed to parse system reports file:" << errorMessage;
        return false;
    }
    
    qDebug() << "ReportsInitializer: Parsed" << entries.count() << "entries from system reports file";
    
    // Find the System root node ID
    ReportTreeRepository repo;
    qint64 systemRootId = repo.findRootId("System");
    
    if (systemRootId == 0) {
        qCritical() << "ReportsInitializer: System root node not found";
        return false;
    }
    
    // Import entries into database under System root
    int reportCount = OrfFileIO::importToDatabase(entries, systemRootId, "system", errorMessage);
    
    if (reportCount < 0) {
        qCritical() << "ReportsInitializer: Failed to import system reports:" << errorMessage;
        return false;
    }
    
    qDebug() << "ReportsInitializer: Successfully loaded" << reportCount << "system reports";
    return true;
}

/*
 * Reinitialize system reports (delete and reload)
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Deletes all system reports and reloads from file. Used when OSCAR version changes.
 */
bool ReportsInitializer::reinitializeSystemReports(QSqlDatabase& db)
{
    Q_UNUSED(db);
    
    qDebug() << "ReportsInitializer: Deleting system reports...";
    
    ReportTreeRepository repo;
    
    // Delete all system nodes (but not the System root itself)
    if (!repo.deleteSystemNodes()) {
        qCritical() << "ReportsInitializer: Failed to delete system nodes";
        return false;
    }
    
    qDebug() << "ReportsInitializer: System reports deleted, reloading from file...";
    
    // Reload from file
    return loadSystemReportsFromFile(db);
}

/*
 * Drop old reports and report_contents tables if they exist
 *
 * Parameters:
 *   db - Database connection to use
 *
 * Returns: true if successful, false otherwise
 *
 * Migration helper: removes old schema tables.
 */
bool ReportsInitializer::dropOldReportTables(QSqlDatabase& db)
{
    QSqlQuery query(db);
    
    // Drop report_contents first (foreign key constraint)
    if (!query.exec("DROP TABLE IF EXISTS report_contents")) {
        qWarning() << "ReportsInitializer: Failed to drop report_contents table:" 
                   << query.lastError().text();
        return false;
    }
    
    // Drop reports table
    if (!query.exec("DROP TABLE IF EXISTS reports")) {
        qWarning() << "ReportsInitializer: Failed to drop reports table:" 
                   << query.lastError().text();
        return false;
    }
    
    qDebug() << "ReportsInitializer: Old report tables dropped (if they existed)";
    return true;
}

/*
 * Get the path to the system_reports.orf file
 *
 * Returns: Full path to the system reports file
 *
 * Checks Qt resource system first (:/docs/system_reports.orf), then
 * falls back to file system paths for development builds.
 */
QString ReportsInitializer::getSystemReportsFilePath()
{
    // First, try Qt resource system (embedded in executable)
    QString resourcePath = ":/docs/system_reports.orf";
    if (QFile::exists(resourcePath)) {
        qDebug() << "ReportsInitializer::getSystemReportsFilePath: Using resource:" << resourcePath;
        return resourcePath;
    }
    
    // Get application directory for file system fallbacks
    QString appDir = QCoreApplication::applicationDirPath();
    
    // Try relative path: docs/system_reports.orf
    QString filePath = QDir(appDir).filePath("docs/system_reports.orf");
    if (QFile::exists(filePath)) {
        qDebug() << "ReportsInitializer::getSystemReportsFilePath: Using file:" << filePath;
        return filePath;
    }
    
    // Try one level up (for development builds): ../oscar/docs/system_reports.orf
    filePath = QDir(appDir).filePath("../oscar/docs/system_reports.orf");
    if (QFile::exists(filePath)) {
        qDebug() << "ReportsInitializer::getSystemReportsFilePath: Using file:" << QDir::cleanPath(filePath);
        return QDir::cleanPath(filePath);
    }
    
    // Try same directory as executable
    filePath = QDir(appDir).filePath("system_reports.orf");
    if (QFile::exists(filePath)) {
        qDebug() << "ReportsInitializer::getSystemReportsFilePath: Using file:" << filePath;
        return filePath;
    }
    
    // Return resource path as default (will fail later with proper error)
    qWarning() << "ReportsInitializer::getSystemReportsFilePath: File not found, returning resource path";
    return resourcePath;
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
