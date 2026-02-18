/* Reports Initializer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportsInitializer class which handles
 * initialization and management of the hierarchical report tree.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORTS_INITIALIZER_H
#define REPORTS_INITIALIZER_H

#include <QSqlDatabase>
#include <QString>

/*!
 * \class ReportsInitializer
 * \brief Handles initialization and management of the report tree
 *
 * This class manages the report_tree table, including creation of root nodes,
 * loading system reports from the external system_reports.orf file, and
 * version tracking. Replaces the old reports/report_contents tables.
 */
class ReportsInitializer
{
public:
    /*!
     * \brief Initialize the report tree with root nodes and system reports
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Creates the System and User root nodes, then loads system reports
     * from the system_reports.orf file. Called during database creation.
     */
    static bool initializeReportTree(QSqlDatabase& db);

    /*!
     * \brief Check and update CSV report version
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Checks if OSCAR version changed and reinitializes system reports if needed.
     * This is called at every startup from DatabaseManager::initialize().
     */
    static bool checkAndUpdateReportVersion(QSqlDatabase& db);

private:
    /*!
     * \brief Create the two root nodes (System and User)
     * \param db Database connection to use
     * \return true if successful, false otherwise
     */
    static bool createRootNodes(QSqlDatabase& db);
    
    /*!
     * \brief Load system reports from the system_reports.orf file
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Loads reports from oscar/docs/system_reports.orf into the System branch.
     */
    static bool loadSystemReportsFromFile(QSqlDatabase& db);
    
    /*!
     * \brief Reinitialize system reports (delete and reload)
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Deletes all system reports and reloads from file. Used when OSCAR version changes.
     */
    static bool reinitializeSystemReports(QSqlDatabase& db);
    
    /*!
     * \brief Drop old reports and report_contents tables if they exist
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Migration helper: removes old schema tables.
     */
    static bool dropOldReportTables(QSqlDatabase& db);
    
    /*!
     * \brief Get the path to the system_reports.orf file
     * \return Full path to the system reports file
     */
    static QString getSystemReportsFilePath();
    
    // Version tracking methods
    static QString getSavedReportVersion();
    static void saveReportVersion(QSqlDatabase& db);
};

#endif // REPORTS_INITIALIZER_H
