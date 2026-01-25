/* Report Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportRepository class for managing CSV export
 * report definitions in the database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORT_REPOSITORY_H
#define REPORT_REPOSITORY_H

#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct ReportData
 * \brief Data structure for CSV export report
 *
 * Represents a single report definition stored in the reports table.
 * Reports are displayed in alphabetical order by name (or by display_order if used).
 */
struct ReportData {
    qint64 id;                  ///< Primary key
    QString name;               ///< Report name (unique, translatable)
    QString description;        ///< Optional description (translatable)
    int displayOrder;           ///< Display order (0 = alphabetical, future use)
    bool isSystem;              ///< True if system report (cannot be deleted, can be edited)
    QDateTime createdAt;        ///< Creation timestamp
    QDateTime updatedAt;        ///< Last update timestamp
    
    ReportData() : id(0), displayOrder(0), isSystem(false) {}
};

/*!
 * \class ReportRepository
 * \brief Repository for managing CSV export reports
 *
 * Provides CRUD operations for the reports table. System reports
 * (is_system=1) cannot be deleted but can be edited.
 */
class ReportRepository
{
public:
    ReportRepository();
    
    /*!
     * \brief Create a new report
     * \param report Report data to insert
     * \return New report ID, or 0 on error
     */
    qint64 create(const ReportData& report);
    
    /*!
     * \brief Update an existing report
     * \param report Report data with ID to update
     * \return true if successful, false otherwise
     */
    bool update(const ReportData& report);
    
    /*!
     * \brief Delete a report by ID
     * \param id Report ID to delete
     * \return true if successful, false otherwise
     * 
     * Will fail if report is a system report (is_system=1)
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Find report by ID
     * \param id Report ID to find
     * \return ReportData with matching ID, or empty ReportData if not found
     */
    ReportData findById(qint64 id);
    
    /*!
     * \brief Find report by name
     * \param name Report name to find
     * \return ReportData with matching name, or empty ReportData if not found
     */
    ReportData findByName(const QString& name);
    
    /*!
     * \brief Delete a report by ID (alias for remove)
     * \param id Report ID to delete
     * \return true if successful, false otherwise
     */
    bool deleteById(qint64 id);
    
    /*!
     * \brief Find all reports
     * \return List of all reports in database order
     */
    QList<ReportData> findAll();
    
    /*!
     * \brief Find all reports ordered for display
     * \return List of reports ordered by display_order (if non-zero), then name alphabetically
     */
    QList<ReportData> findAllOrdered();
    
    /*!
     * \brief Check if report name exists
     * \param name Report name to check
     * \return true if name exists, false otherwise
     */
    bool exists(const QString& name);
    
    /*!
     * \brief Check if report is a system report
     * \param id Report ID to check
     * \return true if system report, false otherwise
     */
    bool isSystemReport(qint64 id);

private:
    QSqlDatabase getDatabase();
};

#endif // REPORT_REPOSITORY_H
