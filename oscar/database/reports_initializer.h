/* Reports Initializer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportsInitializer class which handles
 * initialization and management of CSV export reports tables.
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
 * \brief Handles initialization and management of CSV export reports
 *
 * This class manages the reports and report_contents tables, including
 * table creation, population with default reports, and version tracking.
 * Separated from DatabaseSchema to keep reports logic modular.
 */
class ReportsInitializer
{
public:
    /*!
     * \brief Create the reports tables
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Creates reports and report_contents tables with indexes.
     */
    static bool createReportsTables(QSqlDatabase& db);

    /*!
     * \brief Initialize default CSV export reports
     * \param db Database connection to use
     * \return true if successful, false otherwise
     *
     * Populates reports and report_contents tables with default reports.
     * Delegates to checkAndUpdateReportVersion() for version tracking.
     */
    static bool initializeDefaultReports(QSqlDatabase& db);

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
    // Table creation methods
    static bool createReportsTable(QSqlDatabase& db);
    static bool createReportContentsTable(QSqlDatabase& db);
    static bool createReportIndexes(QSqlDatabase& db);
    
    // Report initialization methods
    static bool reinitializeSystemReports(QSqlDatabase& db);
    static bool initializeSystemReports(QSqlDatabase& db);
    static bool updateDefaultReportQueries(QSqlDatabase& db);
    
    // Version tracking methods
    static QString getSavedReportVersion();
    static void saveReportVersion(QSqlDatabase& db);
};

#endif // REPORTS_INITIALIZER_H
