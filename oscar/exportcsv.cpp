/* ExportCSV module implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QCalendarWidget>
#include <QTextCharFormat>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardItemModel>
#include "SleepLib/profiles.h"
#include "SleepLib/day.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"
#include "database/report_repository.h"
#include "database/report_contents_repository.h"
#include "exportcsv.h"
#include "translation.h"
#include "ui_exportcsv.h"
#include "mainwindow.h"
#include "sqleditor.h"

extern MainWindow *mainwin;

ExportCSV::ExportCSV(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ExportCSV)
{
    ui->setupUi(this);
    ui->resolutionCombo->setCurrentIndex(0);
    ui->quickRangeCombo->setCurrentIndex(0);
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Set Date controls locale to 4 digit years
    QLocale locale = QLocale::system();
    QString shortformat = locale.dateFormat(QLocale::ShortFormat);

    if (!shortformat.toLower().contains("yyyy")) {
        shortformat.replace("yy", "yyyy");
    }

    ui->startDate->setDisplayFormat(shortformat);
    ui->endDate->setDisplayFormat(shortformat);

    // Stop both calendar drop downs highlighting weekends in red
    QTextCharFormat format = ui->startDate->calendarWidget()->weekdayTextFormat(Qt::Saturday);
    format.setForeground(QBrush(Qt::black, Qt::SolidPattern));
    ui->startDate->calendarWidget()->setWeekdayTextFormat(Qt::Saturday, format);
    ui->startDate->calendarWidget()->setWeekdayTextFormat(Qt::Sunday, format);
    ui->endDate->calendarWidget()->setWeekdayTextFormat(Qt::Saturday, format);
    ui->endDate->calendarWidget()->setWeekdayTextFormat(Qt::Sunday, format);

    Qt::DayOfWeek dow = firstDayOfWeekFromLocale();

    ui->startDate->calendarWidget()->setFirstDayOfWeek(dow);
    ui->endDate->calendarWidget()->setFirstDayOfWeek(dow);

    // Connect the signals to update which days have CPAP data when the month is changed
    connect(ui->startDate->calendarWidget(), SIGNAL(currentPageChanged(int, int)),
            SLOT(startDate_currentPageChanged(int, int)));
    connect(ui->endDate->calendarWidget(), SIGNAL(currentPageChanged(int, int)),
            SLOT(endDate_currentPageChanged(int, int)));

    on_quickRangeCombo_currentTextChanged(tr("Most Recent Day"));
    ui->quickRangeCombo->setFocus();
    ui->exportButton->setEnabled(false);

    // Initialize member variables
    m_useCustomQuery = false;
    m_customQuery.clear();
    m_selectedReportId = 0;
    m_selectedContentId = 0;
    
    // Load reports from database
    loadReportsFromDatabase();
}

ExportCSV::~ExportCSV()
{
    delete ui;
}

void ExportCSV::on_filenameBrowseButton_clicked()
{
    QString timestamp = QString("OSCAR_");

    timestamp += p_profile->Get("UserName") + "_";
    
    // Get selected report name
    QModelIndex index = ui->reportList->currentIndex();
    if (index.isValid()) {
        QStandardItemModel *model = qobject_cast<QStandardItemModel*>(ui->reportList->model());
        if (model) {
            QString reportName = model->itemFromIndex(index)->text();
            timestamp += reportName.replace(" ", "_") + "_";
        }
    }
    
    timestamp += ui->startDate->date().toString(Qt::ISODate);

    if (ui->startDate->date() != ui->endDate->date()) { 
        timestamp += "_" + ui->endDate->date().toString(Qt::ISODate); 
    }

    timestamp += ".csv";
    QString folder = mainwin->profilePath(STR_PREF_LastExportCsvPath);

    QString name = QFileDialog::getSaveFileName(this, tr("Select file to export to"),
                   folder + QDir::separator() + timestamp, tr("CSV Files (*.csv)"),
                   nullptr, nativeDialogOption());
    if (name.isEmpty()) {
        ui->exportButton->setEnabled(false);
        return;
    }

    if (!name.toLower().endsWith(".csv")) {
        name += ".csv";
    }

    ui->filenameEdit->setText(name);
    ui->exportButton->setEnabled(true);
    folder = QFileInfo(name).absolutePath();
    mainwin->saveProfilePath(STR_PREF_LastExportCsvPath,folder);
}

void ExportCSV::on_quickRangeCombo_currentTextChanged(const QString &arg1)
{
    QDate first = p_profile->FirstDay();
    QDate last = p_profile->LastDay();

    if (arg1 == tr("Custom")) {
        ui->startDate->setEnabled(true);
        ui->endDate->setEnabled(true);
        ui->startLabel->setEnabled(true);
        ui->endLabel->setEnabled(true);
    } else {
        ui->startDate->setEnabled(false);
        ui->endDate->setEnabled(false);
        ui->startLabel->setEnabled(false);
        ui->endLabel->setEnabled(false);

        if (arg1 == tr("Everything")) {
            ui->startDate->setDate(first);
            ui->endDate->setDate(last);
        } else if (arg1 == tr("Most Recent Day")) {
            ui->startDate->setDate(last);
            ui->endDate->setDate(last);
        } else if (arg1 == tr("Last Week")) {
            ui->startDate->setDate(last.addDays(-7));
            ui->endDate->setDate(last);
        } else if (arg1 == tr("Last Fortnight")) {
            ui->startDate->setDate(last.addDays(-14));
            ui->endDate->setDate(last);
        } else if (arg1 == tr("Last Month")) {
            ui->startDate->setDate(last.addMonths(-1));
            ui->endDate->setDate(last);
        } else if (arg1 == tr("Last 6 Months")) {
            ui->startDate->setDate(last.addMonths(-6));
            ui->endDate->setDate(last);
        } else if (arg1 == tr("Last Year")) {
            ui->startDate->setDate(last.addYears(-1));
            ui->endDate->setDate(last);
        }
    }
}

void ExportCSV::on_exportButton_clicked()
{
    // Get the selected report
    QModelIndex index = ui->reportList->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Export CSV"), tr("Please select a report to export."));
        return;
    }
    
    QStandardItemModel *model = qobject_cast<QStandardItemModel*>(ui->reportList->model());
    if (!model) {
        qWarning() << "ExportCSV: Could not get report list model";
        return;
    }
    
    QString reportName = model->itemFromIndex(index)->text();
    
    QFile file(ui->filenameEdit->text());
    if (!file.open(QFile::WriteOnly)) {
        qWarning() << "ExportCSV: Could not open" << ui->filenameEdit->text() 
                   << "for writing, error code" << file.error() << file.errorString();
        QMessageBox::critical(this, tr("Export CSV"), 
                            tr("Could not open file for writing: %1").arg(file.errorString()));
        return;
    }

    // Get database connection
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "ExportCSV: Database is not open";
        QMessageBox::critical(this, tr("Export CSV"), tr("Database is not open."));
        file.close();
        return;
    }

    // Get profile ID from profile repository
    ProfileRepository profileRepo;
    QString username = p_profile->user->userName();
    ProfileData profileData = profileRepo.findByUsername(username);
    
    if (profileData.id == 0) {
        qWarning() << "ExportCSV: Could not find profile in database for username:" << username;
        QMessageBox::critical(this, tr("Export CSV"), 
                            tr("Could not find profile in database."));
        file.close();
        return;
    }
    
    qint64 profile_id = profileData.id;
    
    // Convert QDate to SQL format (yyyy-MM-dd)
    QString startDateStr = ui->startDate->date().toString(Qt::ISODate);
    QString endDateStr = ui->endDate->date().toString(Qt::ISODate);
    
    // Get resolution setting
    QString resolution = ui->resolutionCombo->currentText();
    
    const QString sep = ",";
    const QString newline = "\n";
    QString sqlQuery;
    
    // Use custom query if set, otherwise fetch from database
    if (m_useCustomQuery && !m_customQuery.isEmpty()) {
        // Use the custom query edited by the user
        sqlQuery = m_customQuery;
        // Apply macro substitution to custom query
        sqlQuery = substituteMacros(sqlQuery, profile_id, startDateStr, endDateStr);
    } else {
        // Get report content ID from resolution combo
        int currentIndex = ui->resolutionCombo->currentIndex();
        if (currentIndex >= 0) {
            m_selectedContentId = ui->resolutionCombo->itemData(currentIndex).toLongLong();
        }
        
        if (m_selectedContentId == 0) {
            QMessageBox::warning(this, tr("Export CSV"), 
                               tr("No report content selected."));
            file.close();
            return;
        }
        
        // Fetch query from database
        ReportContentsRepository contentsRepo;
        ReportContentData content = contentsRepo.findById(m_selectedContentId);
        
        if (content.id == 0 || content.query.isEmpty()) {
            QMessageBox::warning(this, tr("Export CSV"), 
                               tr("Could not find query for selected report."));
            file.close();
            return;
        }
        
        // Apply macro substitution
        sqlQuery = substituteMacros(content.query, profile_id, startDateStr, endDateStr);
    }
    
    // Execute the query
    QSqlQuery query(db);
    if (!query.exec(sqlQuery)) {
        qWarning() << "ExportCSV: SQL query failed:" << query.lastError().text();
        QMessageBox::critical(this, tr("Export CSV"), 
                            tr("SQL query failed: %1").arg(query.lastError().text()));
        file.close();
        return;
    }
    
    // Write CSV header from query column names
    QString header;
    QSqlRecord record = query.record();
    for (int i = 0; i < record.count(); ++i) {
        if (i > 0) header += sep;
        header += record.fieldName(i);
    }
    header += newline;
    file.write(header.toUtf8());
    
    // Initialize progress bar
    // First, count total rows
    int totalRows = 0;
    if (query.last()) {
        totalRows = query.at() + 1;
        query.first();
        query.previous(); // Position before first record
    }
    
    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(totalRows > 0 ? totalRows : 100);
    
    // Write data rows
    int rowCount = 0;
    while (query.next()) {
        QString row;
        for (int i = 0; i < record.count(); ++i) {
            if (i > 0) row += sep;
            QVariant value = query.value(i);
            
            // Handle different data types appropriately
            if (value.isNull()) {
                // Empty field
            } else if (value.type() == QVariant::String) {
                // Escape quotes in strings and wrap in quotes if contains comma or quotes
                QString str = value.toString();
                if (str.contains(sep) || str.contains("\"") || str.contains("\n")) {
                    str.replace("\"", "\"\""); // Escape quotes by doubling them
                    row += "\"" + str + "\"";
                } else {
                    row += str;
                }
            } else {
                // Numbers, dates, etc. - output as-is
                row += value.toString();
            }
        }
        row += newline;
        file.write(row.toUtf8());
        
        rowCount++;
        if (rowCount % 10 == 0) { // Update progress every 10 rows
            ui->progressBar->setValue(rowCount);
            QApplication::processEvents();
        }
    }
    
    ui->progressBar->setValue(totalRows);
    file.close();
    
    QMessageBox::information(this, tr("Export CSV"), 
                           tr("Export completed successfully.\n%1 rows exported.").arg(rowCount));
    
    ExportCSV::accept();
}

void ExportCSV::on_editSQLButton_clicked()
{
    // Get profile ID and date range for generating default query
    ProfileRepository profileRepo;
    QString username = p_profile->user->userName();
    ProfileData profileData = profileRepo.findByUsername(username);
    
    if (profileData.id == 0) {
        QMessageBox::warning(this, tr("Edit SQL Query"), 
                           tr("Could not find profile in database."));
        return;
    }
    
    qint64 profile_id = profileData.id;
    QString startDateStr = ui->startDate->date().toString(Qt::ISODate);
    QString endDateStr = ui->endDate->date().toString(Qt::ISODate);
    
    // Get the query to edit
    QString queryToEdit;
    
    if (m_useCustomQuery && !m_customQuery.isEmpty()) {
        // User has already customized - show their custom query
        queryToEdit = m_customQuery;
    } else {
        // Get query from database
        int currentIndex = ui->resolutionCombo->currentIndex();
        if (currentIndex >= 0) {
            m_selectedContentId = ui->resolutionCombo->itemData(currentIndex).toLongLong();
        }
        
        if (m_selectedContentId == 0) {
            QMessageBox::warning(this, tr("Edit SQL Query"), 
                               tr("No report content selected."));
            return;
        }
        
        // Fetch query from database
        ReportContentsRepository contentsRepo;
        ReportContentData content = contentsRepo.findById(m_selectedContentId);
        
        if (content.id == 0 || content.query.isEmpty()) {
            QMessageBox::warning(this, tr("Edit SQL Query"), 
                               tr("Could not find query for selected report."));
            return;
        }
        
        // Apply macro substitution before showing in editor
        queryToEdit = substituteMacros(content.query, profile_id, startDateStr, endDateStr);
    }
    
    // Show SQL editor dialog
    SQLEditor editor(this);
    editor.setQuery(queryToEdit);
    
    if (editor.exec() == QDialog::Accepted) {
        m_customQuery = editor.getQuery();
        m_useCustomQuery = true;
        
        QMessageBox::information(this, tr("Edit SQL Query"),
                               tr("Custom SQL query has been set. Click 'Export as CSV' to run it.\n\n"
                                  "Note: Macro substitution (#PROFILE_ID, #START_DATE, #END_DATE) has already been applied."));
    }
}

QString ExportCSV::getDefaultQueryForReport(const QString &reportName, qint64 profile_id,
                                            const QString &startDate, const QString &endDate,
                                            const QString &resolution)
{
    QString sqlQuery;
    
    if (reportName == tr("Daily Summaries")) {
        // For Daily Summaries, aggregation depends on resolution
        if (resolution == tr("Days")) {
            // No aggregation - one row per day
            sqlQuery = QString(
                "SELECT "
                "  ds.date as Period, "
                "  ROUND(ds.ahi, 2) as AHI, "
                "  ROUND(ds.rdi, 2) as RDI, "
                "  ds.obstructive_count as OA, "
                "  ds.unclassified_count as UA, "
                "  ds.hypopnea_count as H, "
                "  ds.clear_airway_count as CA, "
                "  ds.rera_count as RERA, "
                "  ROUND(ds.pressure_avg, 2) as Pressure_Avg, "
                "  ROUND(ds.pressure_95th, 2) as Pressure_95th, "
                "  ROUND(ds.leak_total_avg, 2) as Leak_Avg, "
                "  ROUND(ds.leak_total_95th, 2) as Leak_95th, "
                "  ROUND(ds.mask_on_hours, 2) as Hours "
                "FROM daily_summaries ds "
                "WHERE ds.profile_id = %1 "
                "  AND ds.date >= '%2' "
                "  AND ds.date <= '%3' "
                "ORDER BY ds.date"
            ).arg(profile_id).arg(startDate).arg(endDate);
            
        } else if (resolution == tr("Weeks")) {
            // Aggregate by week - recalculate AHI/RDI from summed event counts and hours
            sqlQuery = QString(
                "SELECT "
                "  strftime('%Y-W%W', ds.date) as Period, "
                "  MIN(ds.date) as Week_Start, "
                "  MAX(ds.date) as Week_End, "
                "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as AHI, "
                "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count) + SUM(ds.rera_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as RDI, "
                "  SUM(ds.obstructive_count) as OA, "
                "  SUM(ds.unclassified_count) as UA, "
                "  SUM(ds.hypopnea_count) as H, "
                "  SUM(ds.clear_airway_count) as CA, "
                "  SUM(ds.rera_count) as RERA, "
                "  ROUND(SUM(ds.pressure_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Pressure_Avg, "
                "  ROUND(AVG(ds.pressure_95th), 2) as Pressure_95th, "
                "  ROUND(SUM(ds.leak_total_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Leak_Avg, "
                "  ROUND(AVG(ds.leak_total_95th), 2) as Leak_95th, "
                "  ROUND(SUM(ds.mask_on_hours), 2) as Hours "
                "FROM daily_summaries ds "
                "WHERE ds.profile_id = %1 "
                "  AND ds.date >= '%2' "
                "  AND ds.date <= '%3' "
                "GROUP BY strftime('%Y-W%W', ds.date) "
                "ORDER BY Period"
            ).arg(profile_id).arg(startDate).arg(endDate);
            
        } else if (resolution == tr("Months")) {
            // Aggregate by month - recalculate AHI/RDI from summed event counts and hours
            sqlQuery = QString(
                "SELECT "
                "  strftime('%Y-%m', ds.date) as Period, "
                "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as AHI, "
                "  ROUND((SUM(ds.obstructive_count) + SUM(ds.unclassified_count) + SUM(ds.hypopnea_count) + SUM(ds.clear_airway_count) + SUM(ds.rera_count)) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as RDI, "
                "  SUM(ds.obstructive_count) as OA, "
                "  SUM(ds.unclassified_count) as UA, "
                "  SUM(ds.hypopnea_count) as H, "
                "  SUM(ds.clear_airway_count) as CA, "
                "  SUM(ds.rera_count) as RERA, "
                "  ROUND(SUM(ds.pressure_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Pressure_Avg, "
                "  ROUND(AVG(ds.pressure_95th), 2) as Pressure_95th, "
                "  ROUND(SUM(ds.leak_total_avg * ds.mask_on_hours) / NULLIF(SUM(ds.mask_on_hours), 0), 2) as Leak_Avg, "
                "  ROUND(AVG(ds.leak_total_95th), 2) as Leak_95th, "
                "  ROUND(SUM(ds.mask_on_hours), 2) as Hours "
                "FROM daily_summaries ds "
                "WHERE ds.profile_id = %1 "
                "  AND ds.date >= '%2' "
                "  AND ds.date <= '%3' "
                "GROUP BY strftime('%Y-%m', ds.date) "
                "ORDER BY Period"
            ).arg(profile_id).arg(startDate).arg(endDate);
        } else {
            // Default to Days if resolution not recognized
            sqlQuery = QString(
                "SELECT "
                "  ds.date as Period, "
                "  ROUND(ds.ahi, 2) as AHI, "
                "  ROUND(ds.rdi, 2) as RDI, "
                "  ds.obstructive_count as OA, "
                "  ds.unclassified_count as UA, "
                "  ds.hypopnea_count as H, "
                "  ds.clear_airway_count as CA, "
                "  ds.rera_count as RERA, "
                "  ROUND(ds.pressure_avg, 2) as Pressure_Avg, "
                "  ROUND(ds.pressure_95th, 2) as Pressure_95th, "
                "  ROUND(ds.leak_total_avg, 2) as Leak_Avg, "
                "  ROUND(ds.leak_total_95th, 2) as Leak_95th, "
                "  ROUND(ds.mask_on_hours, 2) as Hours "
                "FROM daily_summaries ds "
                "WHERE ds.profile_id = %1 "
                "  AND ds.date >= '%2' "
                "  AND ds.date <= '%3' "
                "ORDER BY ds.date"
            ).arg(profile_id).arg(startDate).arg(endDate);
        }
        
    } else if (reportName == tr("Session Statistics")) {
        // For Session Statistics, resolution determines grouping
        if (resolution == tr("Sessions")) {
            // No aggregation - one row per session
            sqlQuery = QString(
                "SELECT "
                "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date, "
                "  ROUND(ss.ahi, 2) as AHI, "
                "  ROUND(ss.mask_on_hours, 2) as Hours, "
                "  ss.obstructive_count as OA, "
                "  ss.unclassified_count as UA, "
                "  ss.hypopnea_count as H, "
                "  ss.clear_airway_count as CA, "
                "  m.model as Machine "
                "FROM session_summaries ss "
                "JOIN sessions s ON ss.session_id = s.id "
                "JOIN machines m ON s.machine_id = m.id "
                "WHERE m.profile_id = %1 "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= '%2' "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= '%3' "
                "  AND s.enabled = 1 "
                "ORDER BY s.start_time"
            ).arg(profile_id).arg(startDate).arg(endDate);
            
        } else if (resolution == tr("Days")) {
            // Aggregate by day
            sqlQuery = QString(
                "SELECT "
                "  date(s.start_time/1000, 'unixepoch', 'localtime') as Period, "
                "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI, "
                "  SUM(ss.obstructive_count) as OA, "
                "  SUM(ss.unclassified_count) as UA, "
                "  SUM(ss.hypopnea_count) as H, "
                "  SUM(ss.clear_airway_count) as CA, "
                "  ROUND(SUM(ss.mask_on_hours), 2) as Hours "
                "FROM session_summaries ss "
                "JOIN sessions s ON ss.session_id = s.id "
                "JOIN machines m ON s.machine_id = m.id "
                "WHERE m.profile_id = %1 "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= '%2' "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= '%3' "
                "  AND s.enabled = 1 "
                "GROUP BY date(s.start_time/1000, 'unixepoch', 'localtime') "
                "ORDER BY Period"
            ).arg(profile_id).arg(startDate).arg(endDate);
            
        } else if (resolution == tr("Weeks")) {
            // Aggregate by week
            sqlQuery = QString(
                "SELECT "
                "  strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period, "
                "  MIN(date(s.start_time/1000, 'unixepoch', 'localtime')) as Week_Start, "
                "  MAX(date(s.start_time/1000, 'unixepoch', 'localtime')) as Week_End, "
                "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI, "
                "  SUM(ss.obstructive_count) as OA, "
                "  SUM(ss.unclassified_count) as UA, "
                "  SUM(ss.hypopnea_count) as H, "
                "  SUM(ss.clear_airway_count) as CA, "
                "  ROUND(SUM(ss.mask_on_hours), 2) as Hours "
                "FROM session_summaries ss "
                "JOIN sessions s ON ss.session_id = s.id "
                "JOIN machines m ON s.machine_id = m.id "
                "WHERE m.profile_id = %1 "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= '%2' "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= '%3' "
                "  AND s.enabled = 1 "
                "GROUP BY strftime('%Y-W%W', date(s.start_time/1000, 'unixepoch', 'localtime')) "
                "ORDER BY Period"
            ).arg(profile_id).arg(startDate).arg(endDate);
            
        } else if (resolution == tr("Months")) {
            // Aggregate by month
            sqlQuery = QString(
                "SELECT "
                "  strftime('%Y-%m', date(s.start_time/1000, 'unixepoch', 'localtime')) as Period, "
                "  ROUND((SUM(ss.obstructive_count) + SUM(ss.unclassified_count) + SUM(ss.hypopnea_count) + SUM(ss.clear_airway_count)) / NULLIF(SUM(ss.mask_on_hours), 0), 2) as AHI, "
                "  SUM(ss.obstructive_count) as OA, "
                "  SUM(ss.unclassified_count) as UA, "
                "  SUM(ss.hypopnea_count) as H, "
                "  SUM(ss.clear_airway_count) as CA, "
                "  ROUND(SUM(ss.mask_on_hours), 2) as Hours "
                "FROM session_summaries ss "
                "JOIN sessions s ON ss.session_id = s.id "
                "JOIN machines m ON s.machine_id = m.id "
                "WHERE m.profile_id = %1 "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= '%2' "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= '%3' "
                "  AND s.enabled = 1 "
                "GROUP BY strftime('%Y-%m', date(s.start_time/1000, 'unixepoch', 'localtime')) "
                "ORDER BY Period"
            ).arg(profile_id).arg(startDate).arg(endDate);
        } else {
            // Default to Sessions
            sqlQuery = QString(
                "SELECT "
                "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date, "
                "  ROUND(ss.ahi, 2) as AHI, "
                "  ROUND(ss.mask_on_hours, 2) as Hours, "
                "  ss.obstructive_count as OA, "
                "  ss.unclassified_count as UA, "
                "  ss.hypopnea_count as H, "
                "  ss.clear_airway_count as CA, "
                "  m.model as Machine "
                "FROM session_summaries ss "
                "JOIN sessions s ON ss.session_id = s.id "
                "JOIN machines m ON s.machine_id = m.id "
                "WHERE m.profile_id = %1 "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= '%2' "
                "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= '%3' "
                "  AND s.enabled = 1 "
                "ORDER BY s.start_time"
            ).arg(profile_id).arg(startDate).arg(endDate);
        }
        
    } else if (reportName == tr("Device Settings")) {
        sqlQuery = QString(
            "SELECT "
            "  date(s.start_time/1000, 'unixepoch', 'localtime') as Date, "
            "  m.model as Machine, "
            "  st.channel_id as Channel_ID, "
            "  COALESCE(c.fullname, c.label, c.channel_code, 'Channel_' || st.channel_id) as Setting, "
            "  st.value as Value, "
            "  st.data_type as Type "
            "FROM session_settings st "
            "JOIN sessions s ON st.session_id = s.id "
            "JOIN machines m ON s.machine_id = m.id "
            "LEFT JOIN channels c ON c.profile_id = m.profile_id AND c.channel_id = st.channel_id "
            "WHERE m.profile_id = %1 "
            "  AND date(s.start_time/1000, 'unixepoch', 'localtime') >= '%2' "
            "  AND date(s.start_time/1000, 'unixepoch', 'localtime') <= '%3' "
            "ORDER BY s.start_time, st.channel_id"
        ).arg(profile_id).arg(startDate).arg(endDate);
    }
    
    return sqlQuery;
}


void ExportCSV::UpdateCalendarDay(QDateEdit *dateedit, QDate date)
{
    QCalendarWidget *calendar = dateedit->calendarWidget();

    QTextCharFormat charAttr;

    bool hascpap = p_profile->GetDay(date, MT_CPAP) != nullptr;
    bool hasoxi = p_profile->GetDay(date, MT_OXIMETER) != nullptr;

    if (hascpap) {
        if (hasoxi) {
            charAttr.setForeground(QBrush(COLOR_Purple, Qt::SolidPattern)); // CPAP + Oxi
        } else {
            charAttr.setForeground(QBrush(COLOR_Blue, Qt::SolidPattern)); // CPAP, no Oxi
        }
    } else if (hasoxi) {
        charAttr.setForeground(QBrush(COLOR_Red, Qt::SolidPattern)); // Oxi, no CPAP
    }

    calendar->setDateTextFormat(date, charAttr);
    calendar->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
}

void ExportCSV::startDate_currentPageChanged(int year, int month)
{
    QDate d(year, month, 1);
    int dom = d.daysInMonth();

    for (int i = 1; i <= dom; i++) {
        d = QDate(year, month, i);
        UpdateCalendarDay(ui->startDate, d);
    }
}

void ExportCSV::endDate_currentPageChanged(int year, int month)
{
    QDate d(year, month, 1);
    int dom = d.daysInMonth();

    for (int i = 1; i <= dom; i++) {
        d = QDate(year, month, i);
        UpdateCalendarDay(ui->endDate, d);
    }
}

void ExportCSV::loadReportsFromDatabase()
{
    ReportRepository reportRepo;
    QList<ReportData> reports = reportRepo.findAllOrdered();
    
    QStandardItemModel *model = new QStandardItemModel();
    for (const ReportData& report : reports) {
        QStandardItem* item = new QStandardItem(report.name);
        item->setData(report.id, Qt::UserRole);  // Store report ID
        model->appendRow(item);
    }
    
    ui->reportList->setModel(model);
    ui->reportList->setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Connect selection change signal
    connect(ui->reportList->selectionModel(), 
            &QItemSelectionModel::currentChanged,
            this, 
            &ExportCSV::on_reportList_selectionChanged);
    
    // Select first report by default
    if (model->rowCount() > 0) {
        ui->reportList->setCurrentIndex(model->index(0, 0));
    }
}

void ExportCSV::on_reportList_selectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    
    if (!current.isValid()) {
        m_selectedReportId = 0;
        m_selectedContentId = 0;
        return;
    }
    
    // Get report ID from model
    m_selectedReportId = current.data(Qt::UserRole).toLongLong();
    
    // Load varieties (resolutions) for this report
    loadReportVarieties(m_selectedReportId);
}

void ExportCSV::loadReportVarieties(qint64 reportId)
{
    ReportContentsRepository contentsRepo;
    QList<ReportContentData> contents = contentsRepo.findByReportIdOrdered(reportId);
    
    ui->resolutionCombo->clear();
    
    for (const ReportContentData& content : contents) {
        ui->resolutionCombo->addItem(content.variety, content.id);
    }
    
    // Disable resolution combo if only one option
    ui->resolutionCombo->setEnabled(contents.size() > 1);
    ui->label_4->setEnabled(contents.size() > 1);  // "Resolution:" label
    
    // Store first content ID
    if (!contents.isEmpty()) {
        m_selectedContentId = contents.first().id;
    } else {
        m_selectedContentId = 0;
    }
}

QString ExportCSV::substituteMacros(const QString& query, qint64 profileId, 
                                   const QString& startDate, const QString& endDate)
{
    QString result = query;
    result.replace("#PROFILE_ID", QString::number(profileId));
    result.replace("#START_DATE", "'" + startDate + "'");
    result.replace("#END_DATE", "'" + endDate + "'");
    // Future: Add more macros as needed
    // result.replace("#YEAR", QDate::currentDate().toString("yyyy"));
    return result;
}
