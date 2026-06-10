/* Report Variety Editor Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "reportvarietyeditor.h"
#include "ui_reportvarietyeditor.h"
#include "database/report_repository.h"
#include "database/report_contents_repository.h"
#include "database/profile_repository.h"
#include "database/database_manager.h"
#include "SleepLib/profiles.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>

extern Profile *p_profile;

ReportVarietyEditor::ReportVarietyEditor(qint64 sourceVarietyId, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReportVarietyEditor),
    m_sourceVarietyId(sourceVarietyId),
    m_createdVarietyId(0),
    m_sourceReportName()
{
    ui->setupUi(this);
    
    // Set window properties
    setWindowTitle(tr("Report Variety Editor"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    // Load source variety if provided
    if (m_sourceVarietyId != 0) {
        loadSourceVariety(m_sourceVarietyId);
        setWindowTitle(tr("Copy Report Variety"));
    } else {
        setWindowTitle(tr("New Report Variety"));
        // Set default report name to first custom report or empty
        ReportRepository repo;
        QList<ReportData> reports = repo.findAllOrdered();
        for (const ReportData& report : reports) {
            if (!report.isSystem) {
                ui->reportNameEdit->setText(report.name);
                break;
            }
        }
    }
    
    // Connect text change signals for validation
    connect(ui->reportNameEdit, &QLineEdit::textChanged,
            this, &ReportVarietyEditor::on_reportNameEdit_textChanged);
    connect(ui->varietyNameEdit, &QLineEdit::textChanged,
            this, &ReportVarietyEditor::on_varietyNameEdit_textChanged);
    connect(ui->queryEdit, &QPlainTextEdit::textChanged,
            this, &ReportVarietyEditor::on_queryEdit_textChanged);
    
    updateSaveButtonState();
}

ReportVarietyEditor::~ReportVarietyEditor()
{
    delete ui;
}

void ReportVarietyEditor::loadSourceVariety(qint64 varietyId)
{
    ReportContentsRepository contentsRepo;
    ReportContentData content = contentsRepo.findById(varietyId);
    
    if (content.id == 0) {
        QMessageBox::warning(this, tr("Load Variety"),
                           tr("Could not load source variety."));
        return;
    }
    
    // Get source report name
    ReportRepository reportRepo;
    ReportData report = reportRepo.findById(content.reportId);
    m_sourceReportName = report.name;
    
    // Pre-fill form with source data
    ui->reportNameEdit->setText(report.name + tr(" (Copy)"));
    ui->varietyNameEdit->setText(content.variety);
    ui->descriptionEdit->setText(content.description);
    ui->queryEdit->setPlainText(content.query);
    
    // Add info label
    QString infoText = tr("Copying variety '%1' from report '%2'")
                      .arg(content.variety, report.name);
    ui->infoLabel->setText(infoText);
    ui->infoLabel->setStyleSheet("QLabel { color: blue; font-style: italic; }");
}

void ReportVarietyEditor::on_selectReportButton_clicked()
{
    ReportRepository repo;
    QList<ReportData> reports = repo.findAllOrdered();
    
    // Build list of report names (custom reports only)
    QStringList reportNames;
    for (const ReportData& report : reports) {
        if (!report.isSystem) {
            reportNames << report.name;
        }
    }
    
    if (reportNames.isEmpty()) {
        QMessageBox::information(this, tr("Select Report"),
                               tr("No custom reports available.\nEnter a new report name to create one."));
        return;
    }
    
    bool ok;
    QString selected = QInputDialog::getItem(this,
                                            tr("Select Report"),
                                            tr("Select target report:"),
                                            reportNames,
                                            0,
                                            false,
                                            &ok);
    
    if (ok && !selected.isEmpty()) {
        ui->reportNameEdit->setText(selected);
    }
}

void ReportVarietyEditor::on_testQueryButton_clicked()
{
    if (!validateQuery()) {
        return;
    }
    
    // Get profile ID
    ProfileRepository profileRepo;
    QString username = p_profile->user->userName();
    ProfileData profileData = profileRepo.findByUsername(username);
    
    if (profileData.id == 0) {
        QMessageBox::warning(this, tr("Test Query"),
                           tr("Could not find profile in database."));
        return;
    }
    
    // Get sample date range (last 30 days)
    QDate endDate = p_profile->LastDay();
    QDate startDate = endDate.addDays(-30);
    
    // Apply macro substitution
    QString query = ui->queryEdit->toPlainText();
    query.replace("#PROFILE_ID", QString::number(profileData.id));
    query.replace("#START_DATE", "'" + startDate.toString(Qt::ISODate) + "'");
    query.replace("#END_DATE", "'" + endDate.toString(Qt::ISODate) + "'");
    
    // Execute query
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery sqlQuery(db);
    
    if (!sqlQuery.exec(query)) {
        QString error = tr("Query failed:\n\n%1\n\nSQL Error:\n%2")
                       .arg(query, sqlQuery.lastError().text());
        QMessageBox::critical(this, tr("Test Query"), error);
        return;
    }
    
    // Collect results (limit to 10 rows for testing)
    QStringList columns;
    QList<QStringList> results;
    
    QSqlRecord record = sqlQuery.record();
    for (int i = 0; i < record.count(); ++i) {
        columns << record.fieldName(i);
    }
    
    int rowCount = 0;
    while (sqlQuery.next() && rowCount < 10) {
        QStringList row;
        for (int i = 0; i < record.count(); ++i) {
            row << sqlQuery.value(i).toString();
        }
        results << row;
        rowCount++;
    }
    
    if (results.isEmpty()) {
        QMessageBox::information(this, tr("Test Query"),
                               tr("Query executed successfully.\n\nNo results returned (this may be normal if no data exists for the date range)."));
    } else {
        showQueryResults(columns, results);
    }
}

void ReportVarietyEditor::showQueryResults(const QStringList &columns, const QList<QStringList> &results)
{
    QString resultText = tr("Query Test Results\n");
    resultText += tr("(Showing first %1 rows)\n\n").arg(results.count());
    
    // Build column headers
    resultText += columns.join(" | ") + "\n";
    resultText += QString("-").repeated(columns.count() * 15) + "\n";
    
    // Add rows
    for (const QStringList& row : results) {
        resultText += row.join(" | ") + "\n";
    }
    
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Test Query Results"));
    msgBox.setText(tr("Query executed successfully!"));
    msgBox.setDetailedText(resultText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

void ReportVarietyEditor::on_saveButton_clicked()
{
    if (!validateVariety()) {
        return;
    }
    
    if (!validateQuery()) {
        return;
    }
    
    // Get or create target report
    QString reportName = ui->reportNameEdit->text().trimmed();
    qint64 targetReportId = getOrCreateReport(reportName);
    
    if (targetReportId == 0) {
        QMessageBox::critical(this, tr("Save Variety"),
                            tr("Failed to get or create target report."));
        return;
    }
    
    // Create new variety
    ReportContentData content;
    content.reportId = targetReportId;
    content.variety = ui->varietyNameEdit->text().trimmed();
    content.description = ui->descriptionEdit->text().trimmed();
    content.query = ui->queryEdit->toPlainText();
    content.isSystem = false;  // User-created varieties are always custom
    
    // Get next display order
    ReportContentsRepository repo;
    QList<ReportContentData> existingContents = repo.findByReportIdOrdered(targetReportId);
    content.displayOrder = existingContents.count();
    
    if (!repo.create(content)) {
        QMessageBox::critical(this, tr("Save Variety"),
                            tr("Failed to save variety to database."));
        return;
    }
    
    m_createdVarietyId = content.id;
    
    QMessageBox::information(this, tr("Save Variety"),
                           tr("Variety '%1' saved successfully to report '%2'.")
                           .arg(content.variety, reportName));
    
    accept();
}

void ReportVarietyEditor::on_cancelButton_clicked()
{
    reject();
}

void ReportVarietyEditor::on_reportNameEdit_textChanged(const QString &text)
{
    Q_UNUSED(text);
    updateSaveButtonState();
}

void ReportVarietyEditor::on_varietyNameEdit_textChanged(const QString &text)
{
    Q_UNUSED(text);
    updateSaveButtonState();
}

void ReportVarietyEditor::on_queryEdit_textChanged()
{
    updateSaveButtonState();
}

bool ReportVarietyEditor::validateVariety()
{
    QString reportName = ui->reportNameEdit->text().trimmed();
    QString varietyName = ui->varietyNameEdit->text().trimmed();
    
    // Check empty
    if (reportName.isEmpty()) {
        QMessageBox::warning(this, tr("Validation"),
                           tr("Report name cannot be empty."));
        ui->reportNameEdit->setFocus();
        return false;
    }
    
    if (varietyName.isEmpty()) {
        QMessageBox::warning(this, tr("Validation"),
                           tr("Variety name cannot be empty."));
        ui->varietyNameEdit->setFocus();
        return false;
    }
    
    // Check length
    if (reportName.length() > 100) {
        QMessageBox::warning(this, tr("Validation"),
                           tr("Report name too long (maximum 100 characters)."));
        ui->reportNameEdit->setFocus();
        return false;
    }
    
    if (varietyName.length() > 50) {
        QMessageBox::warning(this, tr("Validation"),
                           tr("Variety name too long (maximum 50 characters)."));
        ui->varietyNameEdit->setFocus();
        return false;
    }
    
    // Check uniqueness within report
    ReportRepository reportRepo;
    ReportData report = reportRepo.findByName(reportName);
    
    if (report.id != 0) {
        // Report exists, check variety uniqueness
        ReportContentsRepository contentsRepo;
        QList<ReportContentData> contents = contentsRepo.findByReportIdOrdered(report.id);
        
        for (const ReportContentData& content : contents) {
            if (content.variety.toLower() == varietyName.toLower()) {
                QMessageBox::warning(this, tr("Validation"),
                                   tr("A variety named '%1' already exists in report '%2'.\n\nPlease choose a different variety name.")
                                   .arg(varietyName, reportName));
                ui->varietyNameEdit->setFocus();
                return false;
            }
        }
    }
    
    return true;
}

bool ReportVarietyEditor::validateQuery()
{
    QString query = ui->queryEdit->toPlainText().trimmed();
    
    if (query.isEmpty()) {
        QMessageBox::warning(this, tr("Validation"),
                           tr("SQL query cannot be empty."));
        ui->queryEdit->setFocus();
        return false;
    }
    
    // Check for macro placeholders
    if (!query.contains("#PROFILE_ID") || !query.contains("#START_DATE") || !query.contains("#END_DATE")) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                                 tr("Validation"),
                                                                 tr("Query does not contain all required macros:\n\n"
                                                                    "#PROFILE_ID, #START_DATE, #END_DATE\n\n"
                                                                    "The query may not work correctly.\n\n"
                                                                    "Continue anyway?"),
                                                                 QMessageBox::Yes | QMessageBox::No);
        
        if (reply != QMessageBox::Yes) {
            ui->queryEdit->setFocus();
            return false;
        }
    }
    
    return true;
}

qint64 ReportVarietyEditor::getOrCreateReport(const QString &reportName)
{
    ReportRepository repo;
    ReportData report = repo.findByName(reportName);
    
    if (report.id != 0) {
        // Report exists
        if (report.isSystem) {
            QMessageBox::warning(this, tr("Save Variety"),
                               tr("Cannot add varieties to system report '%1'.\n\nPlease choose a different report name.")
                               .arg(reportName));
            return 0;
        }
        return report.id;
    }
    
    // Report doesn't exist, create it
    QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                             tr("Create Report"),
                                                             tr("Report '%1' does not exist.\n\nCreate it?")
                                                             .arg(reportName),
                                                             QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return 0;
    }
    
    // Get optional description
    bool ok;
    QString description = QInputDialog::getText(this,
                                               tr("Create Report"),
                                               tr("Description (optional):"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    
    ReportData newReport;
    newReport.name = reportName;
    newReport.description = ok ? description : QString();
    newReport.isSystem = false;
    
    if (!repo.create(newReport)) {
        return 0;
    }
    
    return newReport.id;
}

void ReportVarietyEditor::updateSaveButtonState()
{
    bool valid = !ui->reportNameEdit->text().trimmed().isEmpty() &&
                 !ui->varietyNameEdit->text().trimmed().isEmpty() &&
                 !ui->queryEdit->toPlainText().trimmed().isEmpty();
    
    ui->saveButton->setEnabled(valid);
}
