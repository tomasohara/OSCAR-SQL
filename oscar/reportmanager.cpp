/* Report Manager Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "reportmanager.h"
#include "ui_reportmanager.h"
#include "database/report_repository.h"
#include "database/report_contents_repository.h"
#include "sqleditor.h"
#include "reportvarietyeditor.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QIcon>
#include <QDebug>

ReportManager::ReportManager(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReportManager),
    m_reportModel(new QStandardItemModel(this)),
    m_varietyModel(new QStandardItemModel(this)),
    m_currentReportId(0),
    m_currentVarietyId(0)
{
    ui->setupUi(this);
    
    // Set window properties
    setWindowTitle(tr("Report Manager"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    // Setup report list
    ui->reportList->setModel(m_reportModel);
    ui->reportList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->reportList->setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Setup variety table
    m_varietyModel->setHorizontalHeaderLabels(QStringList() << tr("Variety") << tr("Description"));
    ui->varietyTable->setModel(m_varietyModel);
    ui->varietyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->varietyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->varietyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->varietyTable->horizontalHeader()->setStretchLastSection(true);
    ui->varietyTable->verticalHeader()->setVisible(false);
    
    // Connect signals
    connect(ui->reportList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ReportManager::reportSelectionChanged);
    connect(ui->varietyTable->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ReportManager::varietySelectionChanged);
    connect(ui->varietyTable, &QTableView::doubleClicked,
            this, &ReportManager::varietyDoubleClicked);
    
    // Load data
    loadReports();
    updateButtonStates();
}

ReportManager::~ReportManager()
{
    delete ui;
}

void ReportManager::loadReports()
{
    m_reportModel->clear();
    
    ReportRepository repo;
    QList<ReportData> reports = repo.findAllOrdered();
    
    for (const ReportData& report : reports) {
        QStandardItem *item = new QStandardItem();
        
        // Set display text with icon indicator
        QString displayText = report.name;
        if (report.isSystem) {
            item->setIcon(QIcon(":/icons/lock.png"));  // System report icon
            item->setToolTip(tr("System Report (Read-Only)"));
        } else {
            item->setIcon(QIcon(":/icons/document.png"));  // Custom report icon
            item->setToolTip(tr("Custom Report"));
        }
        
        item->setText(displayText);
        item->setData(report.id, Qt::UserRole);  // Store report ID
        item->setData(report.isSystem, Qt::UserRole + 1);  // Store system flag
        
        m_reportModel->appendRow(item);
    }
    
    // Select first report if available
    if (m_reportModel->rowCount() > 0) {
        ui->reportList->setCurrentIndex(m_reportModel->index(0, 0));
    }
}

void ReportManager::loadReportVarieties(qint64 reportId)
{
    m_varietyModel->removeRows(0, m_varietyModel->rowCount());
    
    if (reportId == 0) {
        return;
    }
    
    ReportContentsRepository repo;
    QList<ReportContentData> contents = repo.findByReportIdOrdered(reportId);
    
    for (const ReportContentData& content : contents) {
        QList<QStandardItem*> row;
        
        QStandardItem *varietyItem = new QStandardItem(content.variety);
        varietyItem->setData(content.id, Qt::UserRole);  // Store content ID
        varietyItem->setData(content.isSystem, Qt::UserRole + 1);  // Store system flag
        
        QStandardItem *descItem = new QStandardItem(content.description);
        
        row << varietyItem << descItem;
        m_varietyModel->appendRow(row);
    }
    
    // Resize columns
    ui->varietyTable->resizeColumnToContents(0);
    
    // Select first variety if available
    if (m_varietyModel->rowCount() > 0) {
        ui->varietyTable->selectRow(0);
    }
}

void ReportManager::updateButtonStates()
{
    bool hasReport = (m_currentReportId != 0);
    bool hasVariety = (m_currentVarietyId != 0);
    bool isSystem = isSystemReport(m_currentReportId);
    bool varietyIsSystem = isSystemVariety(m_currentVarietyId);
    
    // Copy Report button - always enabled if report selected
    ui->copyReportButton->setEnabled(hasReport);
    
    // Delete Report button - only for custom reports
    ui->deleteReportButton->setEnabled(hasReport && !isSystem);
    if (isSystem) {
        ui->deleteReportButton->setToolTip(tr("System reports cannot be deleted"));
    } else {
        ui->deleteReportButton->setToolTip(tr("Delete this custom report"));
    }
    
    // New Variety button - enabled if report selected
    ui->newVarietyButton->setEnabled(hasReport);
    
    // View/Edit Query button - enabled if variety selected
    // Change text and tooltip based on whether it's a system variety
    ui->viewQueryButton->setEnabled(hasVariety);
    if (varietyIsSystem) {
        ui->viewQueryButton->setText(tr("View Query"));
        ui->viewQueryButton->setToolTip(tr("View the SQL query (read-only)"));
    } else {
        ui->viewQueryButton->setText(tr("Edit Query"));
        ui->viewQueryButton->setToolTip(tr("Edit the SQL query"));
    }
    
    // Copy Variety button - enabled if variety selected
    ui->copyVarietyButton->setEnabled(hasVariety);
    
    // Update report details
    if (hasReport) {
        ReportRepository repo;
        ReportData report = repo.findById(m_currentReportId);
        
        ui->reportNameLabel->setText(report.name);
        ui->reportDescLabel->setText(report.description);
        
        if (report.isSystem) {
            ui->reportTypeLabel->setText(tr("System Report (Read-Only)"));
            ui->reportTypeLabel->setStyleSheet("QLabel { color: blue; font-weight: bold; }");
        } else {
            ui->reportTypeLabel->setText(tr("Custom Report"));
            ui->reportTypeLabel->setStyleSheet("QLabel { color: green; }");
        }
    } else {
        ui->reportNameLabel->clear();
        ui->reportDescLabel->clear();
        ui->reportTypeLabel->clear();
    }
}

bool ReportManager::isSystemReport(qint64 reportId)
{
    if (reportId == 0) {
        return false;
    }
    
    ReportRepository repo;
    ReportData report = repo.findById(reportId);
    return report.isSystem;
}

bool ReportManager::isSystemVariety(qint64 varietyId)
{
    if (varietyId == 0) {
        return false;
    }
    
    ReportContentsRepository repo;
    ReportContentData content = repo.findById(varietyId);
    return content.isSystem;
}

qint64 ReportManager::getReportId(const QModelIndex &index)
{
    if (!index.isValid()) {
        return 0;
    }
    
    QStandardItem *item = m_reportModel->itemFromIndex(index);
    if (!item) {
        return 0;
    }
    
    return item->data(Qt::UserRole).toLongLong();
}

qint64 ReportManager::getVarietyId(const QModelIndex &index)
{
    if (!index.isValid()) {
        return 0;
    }
    
    QStandardItem *item = m_varietyModel->item(index.row(), 0);
    if (!item) {
        return 0;
    }
    
    return item->data(Qt::UserRole).toLongLong();
}

void ReportManager::reportSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    
    m_currentReportId = getReportId(current);
    m_currentVarietyId = 0;
    
    loadReportVarieties(m_currentReportId);
    updateButtonStates();
}

void ReportManager::varietySelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    
    m_currentVarietyId = getVarietyId(current);
    updateButtonStates();
}

void ReportManager::varietyDoubleClicked(const QModelIndex &index)
{
    Q_UNUSED(index);
    // Double-click on variety opens query view
    on_viewQueryButton_clicked();
}

void ReportManager::on_newReportButton_clicked()
{
    bool ok;
    QString reportName = QInputDialog::getText(this, 
                                              tr("New Report"),
                                              tr("Report Name:"),
                                              QLineEdit::Normal,
                                              QString(),
                                              &ok);
    
    if (!ok || reportName.isEmpty()) {
        return;
    }
    
    // Validate uniqueness
    ReportRepository repo;
    if (repo.findByName(reportName).id != 0) {
        QMessageBox::warning(this, tr("New Report"),
                           tr("A report with this name already exists.\nPlease choose a different name."));
        return;
    }
    
    // Get optional description
    QString description = QInputDialog::getText(this,
                                               tr("New Report"),
                                               tr("Description (optional):"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    
    // Create report
    ReportData report;
    report.name = reportName;
    report.description = ok ? description : QString();
    report.isSystem = false;
    
    if (!repo.create(report)) {
        QMessageBox::critical(this, tr("New Report"),
                            tr("Failed to create report."));
        return;
    }
    
    // Reload and select new report
    loadReports();
    
    // Find and select the new report
    for (int i = 0; i < m_reportModel->rowCount(); ++i) {
        if (m_reportModel->item(i)->text() == reportName) {
            ui->reportList->setCurrentIndex(m_reportModel->index(i, 0));
            break;
        }
    }
    
    QMessageBox::information(this, tr("New Report"),
                           tr("Report '%1' created.\nUse 'Copy Variety' to add queries to this report.").arg(reportName));
}

void ReportManager::on_copyReportButton_clicked()
{
    if (m_currentReportId == 0) {
        return;
    }
    
    ReportRepository reportRepo;
    ReportData sourceReport = reportRepo.findById(m_currentReportId);
    
    bool ok;
    QString newName = QInputDialog::getText(this,
                                           tr("Copy Report"),
                                           tr("Enter name for copied report:"),
                                           QLineEdit::Normal,
                                           sourceReport.name + tr(" (Copy)"),
                                           &ok);
    
    if (!ok || newName.isEmpty()) {
        return;
    }
    
    // Validate uniqueness
    if (reportRepo.findByName(newName).id != 0) {
        QMessageBox::warning(this, tr("Copy Report"),
                           tr("A report with this name already exists.\nPlease choose a different name."));
        return;
    }
    
    // Create new report
    ReportData newReport;
    newReport.name = newName;
    newReport.description = sourceReport.description;
    newReport.isSystem = false;  // Copies are always custom reports
    
    if (!reportRepo.create(newReport)) {
        QMessageBox::critical(this, tr("Copy Report"),
                            tr("Failed to create report copy."));
        return;
    }
    
    // Copy all varieties
    ReportContentsRepository contentsRepo;
    QList<ReportContentData> sourceContents = contentsRepo.findByReportIdOrdered(m_currentReportId);
    
    int copiedCount = 0;
    for (const ReportContentData& sourceContent : sourceContents) {
        ReportContentData newContent;
        newContent.reportId = newReport.id;
        newContent.variety = sourceContent.variety;
        newContent.description = sourceContent.description;
        newContent.query = sourceContent.query;
        newContent.displayOrder = sourceContent.displayOrder;
        newContent.isSystem = false;  // Copies are always custom
        
        if (contentsRepo.create(newContent)) {
            copiedCount++;
        }
    }
    
    // Reload and select new report
    loadReports();
    
    // Find and select the new report
    for (int i = 0; i < m_reportModel->rowCount(); ++i) {
        if (m_reportModel->item(i)->text() == newName) {
            ui->reportList->setCurrentIndex(m_reportModel->index(i, 0));
            break;
        }
    }
    
    QMessageBox::information(this, tr("Copy Report"),
                           tr("Report copied successfully.\n%1 varieties copied.").arg(copiedCount));
}

void ReportManager::on_deleteReportButton_clicked()
{
    if (m_currentReportId == 0) {
        return;
    }
    
    // Double-check not system report
    if (isSystemReport(m_currentReportId)) {
        QMessageBox::warning(this, tr("Delete Report"),
                           tr("System reports cannot be deleted."));
        return;
    }
    
    ReportRepository reportRepo;
    ReportData report = reportRepo.findById(m_currentReportId);
    
    // Count varieties
    ReportContentsRepository contentsRepo;
    int varietyCount = contentsRepo.findByReportIdOrdered(m_currentReportId).count();
    
    // Confirm deletion
    QString message = tr("Delete report '%1'?").arg(report.name);
    if (varietyCount > 0) {
        message += tr("\n\nThis will also delete %1 report varieties.").arg(varietyCount);
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Delete Report"),
                                                             message,
                                                             QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Delete report (cascade will delete varieties)
    if (!reportRepo.deleteById(m_currentReportId)) {
        QMessageBox::critical(this, tr("Delete Report"),
                            tr("Failed to delete report."));
        return;
    }
    
    // Reload reports
    m_currentReportId = 0;
    m_currentVarietyId = 0;
    loadReports();
    updateButtonStates();
}

void ReportManager::on_newVarietyButton_clicked()
{
    if (m_currentReportId == 0) {
        QMessageBox::warning(this, tr("No Report Selected"),
            tr("Please select a report first."));
        return;
    }
    
    // Get report name for context
    ReportRepository reportRepo;
    ReportData report = reportRepo.findById(m_currentReportId);
    
    // Open ReportVarietyEditor in new mode (no source variety)
    // User will select or enter the target report name in the editor
    ReportVarietyEditor editor(0, this);  // 0 = create new variety
    
    if (editor.exec() == QDialog::Accepted) {
        // Reload current report's varieties to show the new variety
        loadReportVarieties(m_currentReportId);
        
        QMessageBox::information(this, tr("New Variety"),
                               tr("Variety created successfully."));
    }
}

void ReportManager::on_viewQueryButton_clicked()
{
    if (m_currentVarietyId == 0) {
        return;
    }
    
    ReportContentsRepository contentsRepo;
    ReportContentData content = contentsRepo.findById(m_currentVarietyId);
    
    if (content.id == 0) {
        QMessageBox::warning(this, tr("View Query"),
                           tr("Could not load query."));
        return;
    }
    
    // Get report name for title
    ReportRepository reportRepo;
    ReportData report = reportRepo.findById(content.reportId);
    
    // Create SQL editor
    SQLEditor editor(this);
    
    // Configure based on system vs custom variety
    if (content.isSystem) {
        // System variety - read-only
        editor.setWindowTitle(tr("View Query: %1 - %2").arg(report.name, content.variety));
        editor.setQuery(content.query);
        editor.setReadOnly(true);
        editor.exec();
    } else {
        // Custom variety - editable
        editor.setWindowTitle(tr("Edit Query: %1 - %2").arg(report.name, content.variety));
        editor.setQuery(content.query);
        editor.setReadOnly(false);
        
        if (editor.exec() == QDialog::Accepted) {
            // Save the modified query
            QString newQuery = editor.getQuery();
            
            if (newQuery != content.query) {
                content.query = newQuery;
                
                if (contentsRepo.update(content)) {
                    QMessageBox::information(this, tr("Edit Query"),
                                           tr("Query saved successfully."));
                } else {
                    QMessageBox::critical(this, tr("Edit Query"),
                                        tr("Failed to save query."));
                }
            }
        }
    }
}

void ReportManager::on_copyVarietyButton_clicked()
{
    if (m_currentVarietyId == 0) {
        return;
    }
    
    // Open ReportVarietyEditor with source variety ID
    ReportVarietyEditor editor(m_currentVarietyId, this);
    
    if (editor.exec() == QDialog::Accepted) {
        // Reload current report's varieties to show any changes
        loadReportVarieties(m_currentReportId);
        
        QMessageBox::information(this, tr("Copy Variety"),
                               tr("Variety copied successfully."));
    }
}

void ReportManager::on_closeButton_clicked()
{
    accept();
}
