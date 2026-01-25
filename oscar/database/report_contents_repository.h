/* Report Contents Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportContentsRepository class for managing CSV export
 * report content variations (varieties/resolutions) in the database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORT_CONTENTS_REPOSITORY_H
#define REPORT_CONTENTS_REPOSITORY_H

#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QDateTime>

/*!
 * \struct ReportContentData
 * \brief Data structure for CSV export report content
 *
 * Represents a single report content variation (e.g., "Days", "Weeks", "Months").
 * Each content links to a report and contains the SQL query template with macros.
 */
struct ReportContentData {
    qint64 id;                  ///< Primary key
    qint64 reportId;            ///< Foreign key to reports table
    QString variety;            ///< Variety/resolution name (e.g., "Days", "Weeks") - translatable
    QString description;        ///< Optional description (translatable)
    QString query;              ///< SQL query template with macros (#PROFILE_ID, #START_DATE, #END_DATE)
    int displayOrder;           ///< Display order for varieties within a report
    bool isSystem;              ///< True if system content (cannot be deleted, can be edited)
    QDateTime createdAt;        ///< Creation timestamp
    QDateTime updatedAt;        ///< Last update timestamp
    
    ReportContentData() : id(0), reportId(0), displayOrder(0), isSystem(false) {}
};

/*!
 * \class ReportContentsRepository
 * \brief Repository for managing CSV export report contents
 *
 * Provides CRUD operations for the report_contents table. System contents
 * (is_system=1) cannot be deleted but can be edited.
 */
class ReportContentsRepository
{
public:
    ReportContentsRepository();
    
    /*!
     * \brief Create a new report content
     * \param content Report content data to insert
     * \return New content ID, or 0 on error
     */
    qint64 create(const ReportContentData& content);
    
    /*!
     * \brief Update an existing report content
     * \param content Report content data with ID to update
     * \return true if successful, false otherwise
     */
    bool update(const ReportContentData& content);
    
    /*!
     * \brief Delete a report content by ID
     * \param id Content ID to delete
     * \return true if successful, false otherwise
     * 
     * Will fail if content is a system content (is_system=1)
     */
    bool remove(qint64 id);
    
    /*!
     * \brief Find report content by ID
     * \param id Content ID to find
     * \return ReportContentData with matching ID, or empty ReportContentData if not found
     */
    ReportContentData findById(qint64 id);
    
    /*!
     * \brief Find all content for a report
     * \param reportId Report ID
     * \return List of all content for the report in database order
     */
    QList<ReportContentData> findByReportId(qint64 reportId);
    
    /*!
     * \brief Find all content for a report, ordered by display_order
     * \param reportId Report ID
     * \return List of content ordered by display_order, then variety name
     */
    QList<ReportContentData> findByReportIdOrdered(qint64 reportId);
    
    /*!
     * \brief Find content by report ID and variety name
     * \param reportId Report ID
     * \param variety Variety name
     * \return ReportContentData if found, empty ReportContentData otherwise
     */
    ReportContentData findByReportIdAndVariety(qint64 reportId, const QString& variety);
    
    /*!
     * \brief Check if variety exists for a report
     * \param reportId Report ID
     * \param variety Variety name to check
     * \return true if exists, false otherwise
     */
    bool exists(qint64 reportId, const QString& variety);
    
    /*!
     * \brief Check if content is a system content
     * \param id Content ID to check
     * \return true if system content, false otherwise
     */
    bool isSystemContent(qint64 id);
    
    /*!
     * \brief Get count of content varieties for a report
     * \param reportId Report ID
     * \return Number of content varieties
     */
    int getContentCount(qint64 reportId);

private:
    QSqlDatabase getDatabase();
};

#endif // REPORT_CONTENTS_REPOSITORY_H
