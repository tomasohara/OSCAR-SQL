/* Report Variety Editor Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This dialog allows users to create new report varieties by copying
 * existing ones or creating from scratch. Users can edit the SQL query,
 * test it with sample data, and save to any report.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORTVARIETYEDITOR_H
#define REPORTVARIETYEDITOR_H

#include <QDialog>

namespace Ui {
class ReportVarietyEditor;
}

/*!
 * \class ReportVarietyEditor
 * \brief Dialog for creating/copying report varieties
 *
 * Provides functionality to:
 * - Copy existing variety queries
 * - Create new varieties from scratch
 * - Edit SQL queries with macro support
 * - Test queries before saving
 * - Select target report (new or existing)
 * - Validate variety name uniqueness
 */
class ReportVarietyEditor : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \brief Constructor for copying existing variety
     * \param sourceVarietyId ID of variety to copy (0 for new)
     * \param parent Parent widget
     */
    explicit ReportVarietyEditor(qint64 sourceVarietyId = 0, QWidget *parent = nullptr);
    
    /*!
     * \brief Destructor
     */
    ~ReportVarietyEditor();
    
    /*!
     * \brief Get the ID of the created variety
     * \return Created variety ID, or 0 if not created
     */
    qint64 createdVarietyId() const { return m_createdVarietyId; }

private slots:
    /*!
     * \brief Select target report
     */
    void on_selectReportButton_clicked();
    
    /*!
     * \brief Test query with sample data
     */
    void on_testQueryButton_clicked();
    
    /*!
     * \brief Save variety
     */
    void on_saveButton_clicked();
    
    /*!
     * \brief Cancel editing
     */
    void on_cancelButton_clicked();
    
    /*!
     * \brief Update UI when report name changes
     */
    void on_reportNameEdit_textChanged(const QString &text);
    
    /*!
     * \brief Update UI when variety name changes
     */
    void on_varietyNameEdit_textChanged(const QString &text);
    
    /*!
     * \brief Update UI when query text changes
     */
    void on_queryEdit_textChanged();

private:
    /*!
     * \brief Load source variety data
     * \param varietyId Variety ID to load
     */
    void loadSourceVariety(qint64 varietyId);
    
    /*!
     * \brief Validate variety name
     * \return true if valid, false otherwise
     */
    bool validateVariety();
    
    /*!
     * \brief Validate SQL query
     * \return true if valid, false otherwise
     */
    bool validateQuery();
    
    /*!
     * \brief Get or create report by name
     * \param reportName Report name
     * \return Report ID, or 0 if failed
     */
    qint64 getOrCreateReport(const QString &reportName);
    
    /*!
     * \brief Show query test results
     * \param results Query result rows
     * \param columns Column names
     */
    void showQueryResults(const QStringList &columns, const QList<QStringList> &results);
    
    /*!
     * \brief Update save button state
     */
    void updateSaveButtonState();
    
    Ui::ReportVarietyEditor *ui;
    qint64 m_sourceVarietyId;
    qint64 m_createdVarietyId;
    QString m_sourceReportName;
};

#endif // REPORTVARIETYEDITOR_H
