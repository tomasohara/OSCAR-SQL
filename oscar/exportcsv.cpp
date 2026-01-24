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
#include "exportcsv.h"
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

    // Get list of available reports and load the report list
    QStandardItemModel *model = new QStandardItemModel();
    model->appendRow(new QStandardItem(tr("Daily Summaries")));
    model->appendRow(new QStandardItem(tr("Session Statistics")));
    model->appendRow(new QStandardItem(tr("Device Settings")));
    ui->reportList->setModel(model);
    ui->reportList->setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Select first report by default
    ui->reportList->setCurrentIndex(model->index(0, 0));
    
    // Initialize custom query flags
    m_useCustomQuery = false;
    m_customQuery.clear();
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
                   folder + QDir::separator() + timestamp, tr("CSV Files (*.csv)"));
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
    
    const QString sep = ",";
    const QString newline = "\n";
    QString sqlQuery;
    
    // Use custom query if set, otherwise generate default query for selected report
    if (m_useCustomQuery && !m_customQuery.isEmpty()) {
        // Use the custom query edited by the user
        sqlQuery = m_customQuery;
    } else {
        // Generate default query for the selected report
        sqlQuery = getDefaultQueryForReport(reportName, profile_id, startDateStr, endDateStr);
        
        if (sqlQuery.isEmpty()) {
            QMessageBox::warning(this, tr("Export CSV"), 
                               tr("Unknown report type: %1").arg(reportName));
            file.close();
            return;
        }
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
    
    // Get the selected report name
    QModelIndex index = ui->reportList->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Edit SQL Query"), 
                           tr("Please select a report first."));
        return;
    }
    
    QStandardItemModel *model = qobject_cast<QStandardItemModel*>(ui->reportList->model());
    if (!model) {
        return;
    }
    
    QString reportName = model->itemFromIndex(index)->text();
    
    // Get the default query or use custom query if already set
    QString query;
    if (m_useCustomQuery && !m_customQuery.isEmpty()) {
        query = m_customQuery;
    } else {
        query = getDefaultQueryForReport(reportName, profile_id, startDateStr, endDateStr);
    }
    
    // Show SQL editor dialog
    SQLEditor editor(this);
    editor.setQuery(query);
    
    if (editor.exec() == QDialog::Accepted) {
        m_customQuery = editor.getQuery();
        m_useCustomQuery = true;
        
        QMessageBox::information(this, tr("Edit SQL Query"),
                               tr("Custom SQL query has been set. Click 'Export as CSV' to run it."));
    }
}

QString ExportCSV::getDefaultQueryForReport(const QString &reportName, qint64 profile_id,
                                            const QString &startDate, const QString &endDate)
{
    QString sqlQuery;
    
    if (reportName == tr("Daily Summaries")) {
        sqlQuery = QString(
            "SELECT "
            "  ds.date, "
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
        
    } else if (reportName == tr("Session Statistics")) {
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
