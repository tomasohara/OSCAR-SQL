 /* Report Manager Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This dialog allows users to view system reports, copy reports/varieties,
 * create custom reports, and manage their report library. System reports
 * are protected and cannot be edited or deleted.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORTMANAGER_H
#define REPORTMANAGER_H

#include <QDialog>
#include <QStandardItemModel>

namespace Ui {
class ReportManager;
}

/*!
 * \class ReportManager
 * \brief Dialog for managing CSV export reports
 *
 * Provides functionality to:
 * - View all reports (system and custom)
 * - Copy reports and varieties
 * - Create new custom reports
 * - Delete custom reports
 * - View queries in read-only mode
 *
 * System reports are protected and cannot be edited or deleted.
 */
class ReportManager : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \brief Constructor
     * \param parent Parent widget
     */
    explicit ReportManager(QWidget *parent = nullptr);
    
    /*!
     * \brief Destructor
     */
    ~ReportManager();

private slots:
    /*!
     * \brief Handle report selection change
     * \param current Current index
     * \param previous Previous index
     */
    void reportSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    
    /*!
     * \brief Handle variety selection change
     * \param current Current index
     * \param previous Previous index
     */
    void varietySelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    
    /*!
     * \brief Handle variety double-click (view query)
     * \param index Index of double-clicked item
     */
    void varietyDoubleClicked(const QModelIndex &index);
    
    /*!
     * \brief Create new blank custom report
     */
    void on_newReportButton_clicked();
    
    /*!
     * \brief Copy selected report (all varieties)
     */
    void on_copyReportButton_clicked();
    
    /*!
     * \brief Delete selected custom report
     */
    void on_deleteReportButton_clicked();
    
    /*!
     * \brief Create new variety for selected report
     */
    void on_newVarietyButton_clicked();
    
    /*!
     * \brief View selected variety's query (read-only)
     */
    void on_viewQueryButton_clicked();
    
    /*!
     * \brief Copy selected variety to create custom version
     */
    void on_copyVarietyButton_clicked();
    
    /*!
     * \brief Close the dialog
     */
    void on_closeButton_clicked();

private:
    /*!
     * \brief Load all reports from database
     */
    void loadReports();
    
    /*!
     * \brief Load varieties for selected report
     * \param reportId Report ID to load varieties for
     */
    void loadReportVarieties(qint64 reportId);
    
    /*!
     * \brief Update button enabled states based on selection
     */
    void updateButtonStates();
    
    /*!
     * \brief Check if report is a system report
     * \param reportId Report ID to check
     * \return true if system report, false otherwise
     */
    bool isSystemReport(qint64 reportId);
    
    /*!
     * \brief Check if variety is a system variety
     * \param varietyId Variety (report content) ID to check
     * \return true if system variety, false otherwise
     */
    bool isSystemVariety(qint64 varietyId);
    
    /*!
     * \brief Get report ID from list item
     * \param index Model index
     * \return Report ID or 0 if invalid
     */
    qint64 getReportId(const QModelIndex &index);
    
    /*!
     * \brief Get variety ID from list item
     * \param index Model index
     * \return Variety (report content) ID or 0 if invalid
     */
    qint64 getVarietyId(const QModelIndex &index);
    
    Ui::ReportManager *ui;
    QStandardItemModel *m_reportModel;
    QStandardItemModel *m_varietyModel;
    qint64 m_currentReportId;
    qint64 m_currentVarietyId;
};

#endif // REPORTMANAGER_H
