/* Report Exporter Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the ReportExporter dialog per the design
 * specification in REPORT_TREE_UI_REDESIGN.md.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "report_exporter.h"
#include "../translation.h"
#include "../database/report_tree_model.h"
#include "../database/database_manager.h"
#include "../database/orf_file_io.h"
#include "../sqleditor.h"
#include "../csv.h"
#include <functional>
#include <QSet>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTreeView>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QHeaderView>
#include <QStandardItem>
#include <QLineEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QProgressBar>
#include <QCheckBox>
#include <QGroupBox>
#include "../database/app_preferences_repository.h"
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QFile>
#include <QTextStream>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QCloseEvent>
#include <QDebug>

/*
 * Constructor
 */
ReportExporter::ReportExporter(QWidget* parent, const QString& currentProfileName)
    : QDialog(parent)
    , m_treeView(nullptr)
    , m_statusLabel(nullptr)
    , m_model(nullptr)
    , m_profileCombo(nullptr)
    , m_quickRangeCombo(nullptr)
    , m_startDateEdit(nullptr)
    , m_endDateEdit(nullptr)
    , m_filenameEdit(nullptr)
    , m_progressBar(nullptr)
    , m_openAfterExportCheck(nullptr)
    , m_programCombo(nullptr)
    , m_exportCSVButton(nullptr)
    , m_editSQLButton(nullptr)
    , m_closeButton(nullptr)
    , m_currentProfileName(currentProfileName)
{
    setWindowTitle(tr("CSV Export Wizard"));
    resize(700, 780);
    
    setupUi();

    // Restore previous tree state; fall back to expanding root nodes on first use.
    if (!restoreTreeState()) {
        m_treeView->expandToDepth(0);
    }

    qDebug() << "ReportExporter: Dialog created";
}

/*
 * Destructor
 */
ReportExporter::~ReportExporter()
{
    // QDialog handles cleanup of child widgets
}

/*
 * Save settings when the dialog closes (via any path)
 */
void ReportExporter::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QDialog::closeEvent(event);
}

/*
 * Setup user interface
 */
void ReportExporter::setupUi()
{
    // Create model
    m_model = new ReportTreeModel(this);
    
    // Main layout: splitter on top (expands), status label and buttons fixed at bottom
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Horizontal splitter for tree view and right panel — fills all available vertical space
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    
    // Left: Tree view
    m_treeView = createTreeView();
    splitter->addWidget(m_treeView);
    
    // Right: Controls panel
    QWidget* rightPanel = createRightPanel();
    splitter->addWidget(rightPanel);
    
    // Tree and right panel share space unevenly
    splitter->setStretchFactor(0, 50);
    splitter->setStretchFactor(1, 50);
    
    mainLayout->addWidget(splitter, 1);  // stretch=1: splitter expands to fill remaining height
    
    // Status label — fixed height, always visible at bottom
    m_statusLabel = new QLabel(tr("Select a report from the tree to export"), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("QLabel { padding: 5px; border-top: 1px solid #ccc; }");
    mainLayout->addWidget(m_statusLabel, 0);  // stretch=0: takes only its natural height
    
    // Button panel — fixed height, always visible at bottom
    QWidget* buttonPanel = createButtonPanel();
    mainLayout->addWidget(buttonPanel, 0);    // stretch=0: takes only its natural height
    
    setLayout(mainLayout);
    
    // Initial button state
    updateButtonStates(QModelIndex());
}

/*
 * Create tree view
 */
QTreeView* ReportExporter::createTreeView()
{
    QTreeView* tree = new QTreeView(this);
    tree->setModel(m_model);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setEditTriggers(QAbstractItemView::EditKeyPressed);  // F2 to rename
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->setAlternatingRowColors(true);
    tree->setAnimated(true);
    
    // Enable drag and drop
    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    
    // Single column - stretch to fill
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    
    // Connect signals
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ReportExporter::onSelectionChanged);
    connect(tree, &QTreeView::doubleClicked,
            this, &ReportExporter::onTreeDoubleClicked);
    connect(tree, &QTreeView::customContextMenuRequested,
            this, &ReportExporter::onTreeContextMenu);
    
    return tree;
}

/*
 * Create right panel with export controls (Section 4.4 of design doc)
 */
QWidget* ReportExporter::createRightPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* outerLayout = new QVBoxLayout(panel);

    // --- Form layout for export controls ---
    QFormLayout* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    // Profile combo
    m_profileCombo = new QComboBox(panel);
    form->addRow(tr("Profile:"), m_profileCombo);
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ReportExporter::onProfileChanged);

    // Quick range combo
    m_quickRangeCombo = new QComboBox(panel);
    m_quickRangeCombo->addItems({
        tr("Most Recent Day"),
        tr("Last Week"),
        tr("Last Fortnight"),
        tr("Last Month"),
        tr("Last 6 Months"),
        tr("Last Year"),
        tr("Everything"),
        tr("Custom")
    });
    form->addRow(tr("Quick Range:"), m_quickRangeCombo);
    connect(m_quickRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ReportExporter::onQuickRangeChanged);

    // Date range
    m_startDateEdit = new QDateEdit(QDate::currentDate().addDays(-1), panel);
    m_startDateEdit->setCalendarPopup(true);
    m_startDateEdit->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
    m_startDateEdit->setEnabled(false);  // Enabled only for Custom range
    form->addRow(tr("Start Date:"), m_startDateEdit);

    m_endDateEdit = new QDateEdit(QDate::currentDate(), panel);
    m_endDateEdit->setCalendarPopup(true);
    m_endDateEdit->setDisplayFormat("MM/dd/yyyy");
    m_endDateEdit->setEnabled(false);    // Enabled only for Custom range
    form->addRow(tr("End Date:"), m_endDateEdit);

    outerLayout->addLayout(form);
    outerLayout->addSpacing(8);

    // --- Filename row ---
    QHBoxLayout* fileRow = new QHBoxLayout();
    m_filenameEdit = new QLineEdit(panel);
    m_filenameEdit->setPlaceholderText(tr("Output filename..."));
    QPushButton* browseBtn = new QPushButton(tr("..."), panel);
    browseBtn->setFixedWidth(32);
    connect(browseBtn, &QPushButton::clicked, this, &ReportExporter::onBrowseFilename);
    fileRow->addWidget(m_filenameEdit);
    fileRow->addWidget(browseBtn);

    QWidget* fileWidget = new QWidget(panel);
    fileWidget->setLayout(fileRow);
    QFormLayout* fileForm = new QFormLayout();
    fileForm->addRow(tr("Filename:"), fileWidget);
    outerLayout->addLayout(fileForm);
    outerLayout->addSpacing(8);

    // --- Progress bar ---
    m_progressBar = new QProgressBar(panel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    outerLayout->addWidget(m_progressBar);

    // --- Open after export group ---
    QGroupBox* openGroup = new QGroupBox(tr("After Export"), panel);
    QVBoxLayout* openLayout = new QVBoxLayout(openGroup);

    m_openAfterExportCheck = new QCheckBox(tr("Open file after export"), openGroup);
    openLayout->addWidget(m_openAfterExportCheck);

    QHBoxLayout* programRow = new QHBoxLayout();
    programRow->addWidget(new QLabel(tr("Program:"), openGroup));
    m_programCombo = new QComboBox(openGroup);
    m_programCombo->addItems({
        tr("(System default)"),
        tr("Excel"),
        tr("LibreOffice Calc")
    });
    programRow->addWidget(m_programCombo);
    openLayout->addLayout(programRow);

    outerLayout->addWidget(openGroup);

    // Load profiles
    loadProfiles();

    // Initialize date edits to the actual data range before restoring settings,
    // so that "Custom" mode starts from meaningful dates rather than today.
    if (m_startDateEdit) m_startDateEdit->setDate(getFirstDataDate());
    if (m_endDateEdit)   m_endDateEdit->setDate(getLastDataDate());

    // Restore saved settings (dates, filename, profile, open-after)
    restoreSettings();

    // Push all widgets to the top; prevents them from stretching to fill extra vertical space
    outerLayout->addStretch();

    panel->setLayout(outerLayout);
    return panel;
}

/*
 * Create button panel (only 3 buttons per design spec)
 */
QWidget* ReportExporter::createButtonPanel()
{
    QWidget* panel = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(panel);
    
    layout->addStretch();
    
    // Export CSV button
    m_exportCSVButton = new QPushButton(tr("Export CSV"), panel);
    m_exportCSVButton->setEnabled(false);
    connect(m_exportCSVButton, &QPushButton::clicked,
            this, &ReportExporter::onExportCSVClicked);
    layout->addWidget(m_exportCSVButton);
    
    // Edit SQL button
    m_editSQLButton = new QPushButton(tr("Edit SQL"), panel);
    m_editSQLButton->setEnabled(false);
    connect(m_editSQLButton, &QPushButton::clicked,
            this, &ReportExporter::onEditSQLClicked);
    layout->addWidget(m_editSQLButton);
    
    // Close button
    m_closeButton = new QPushButton(tr("Close"), panel);
    connect(m_closeButton, &QPushButton::clicked,
            this, &QDialog::reject);
    layout->addWidget(m_closeButton);
    
    panel->setLayout(layout);
    return panel;
}

/*
 * Selection changed slot
 */
void ReportExporter::onSelectionChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);
    updateButtonStates(current);
    updateStatusLabel(current);
    updateFilenameField();
}

/*
 * Tree double-clicked slot
 */
void ReportExporter::onTreeDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;
    
    // Double-click on report: export (or edit if user node)
    if (isReportNode(item)) {
        if (isUserNode(item)) {
            onEditQuery();
        } else {
            // System report: just show in read-only viewer
            onEditSQLClicked();
        }
    }
}

/*
 * Context menu slot - builds dynamic menu based on selected item
 */
void ReportExporter::onTreeContextMenu(const QPoint& pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
    if (!index.isValid()) return;
    
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;
    
    QMenu menu(this);
    
    // Build menu based on node type and source (per design Section 5.1)
    if (isSystemNode(item)) {
        if (isReportNode(item)) {
            // System Report
            menu.addAction(tr("View Query"), this, &ReportExporter::onViewSubstitutedQuery);
            menu.addAction(tr("Duplicate to User"), this, &ReportExporter::onDuplicateToUser);
            if (!item->data(ReportTreeModel::DescriptionRole).toString().isEmpty()) {
                menu.addAction(tr("Show Description"), this, &ReportExporter::onShowDescription);
            }
        } else if (isFolderNode(item)) {
            // System Folder
            menu.addAction(tr("Duplicate to User"), this, &ReportExporter::onDuplicateToUser);
            menu.addSeparator();
            menu.addAction(tr("Expand All"), this, &ReportExporter::onExpandAll);
            menu.addAction(tr("Collapse All"), this, &ReportExporter::onCollapseAll);
        } else if (isRootNode(item)) {
            // System Root
            menu.addAction(tr("Expand All"), this, &ReportExporter::onExpandAll);
            menu.addAction(tr("Collapse All"), this, &ReportExporter::onCollapseAll);
        }
    } else if (isUserNode(item)) {
        if (isReportNode(item)) {
            // User Report
            menu.addAction(tr("Edit Query"), this, &ReportExporter::onEditQuery);
            menu.addAction(tr("View Query (Substituted)"), this, &ReportExporter::onViewSubstitutedQuery);
            menu.addSeparator();
            menu.addAction(tr("Rename"), this, &ReportExporter::onRenameItem);
            menu.addAction(tr("Duplicate"), this, &ReportExporter::onDuplicateItem);
            menu.addAction(tr("Delete"), this, &ReportExporter::onDeleteItem);
            menu.addSeparator();
            menu.addAction(tr("Edit Description"), this, &ReportExporter::onEditDescription);
            if (!item->data(ReportTreeModel::DescriptionRole).toString().isEmpty()) {
                menu.addAction(tr("Show Description"), this, &ReportExporter::onShowDescription);
            }
        } else if (isFolderNode(item)) {
            // User Folder
            menu.addAction(tr("New Report"), this, &ReportExporter::onNewReport);
            menu.addAction(tr("New Folder"), this, &ReportExporter::onNewFolder);
            menu.addSeparator();
            menu.addAction(tr("Rename"), this, &ReportExporter::onRenameItem);
            menu.addAction(tr("Delete"), this, &ReportExporter::onDeleteItem);
            menu.addSeparator();
            menu.addAction(tr("Import Reports..."), this, &ReportExporter::onImportReports);
            menu.addSeparator();
            menu.addAction(tr("Expand All"), this, &ReportExporter::onExpandAll);
            menu.addAction(tr("Collapse All"), this, &ReportExporter::onCollapseAll);
        } else if (isRootNode(item)) {
            // User Root
            menu.addAction(tr("New Report"), this, &ReportExporter::onNewReport);
            menu.addAction(tr("New Folder"), this, &ReportExporter::onNewFolder);
            menu.addSeparator();
            menu.addAction(tr("Import Reports..."), this, &ReportExporter::onImportReports);
            menu.addAction(tr("Export All User Reports..."), this, &ReportExporter::onExportSelected);
            menu.addSeparator();
            menu.addAction(tr("Expand All"), this, &ReportExporter::onExpandAll);
            menu.addAction(tr("Collapse All"), this, &ReportExporter::onCollapseAll);
        }
    }
    
    if (!menu.isEmpty()) {
        menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    }
}

/*
 * Export CSV button clicked
 */
void ReportExporter::onExportCSVClicked()
{
    if (m_filenameEdit && m_filenameEdit->text().trimmed().isEmpty()) {
        onBrowseFilename();
        if (m_filenameEdit->text().trimmed().isEmpty()) return;
    }
    runExport();
}

/*
 * Edit SQL / View SQL button clicked (adapts based on system vs. user report)
 */
void ReportExporter::onEditSQLClicked()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isReportNode(item)) return;

    SQLEditor dlg(this);
    dlg.setQuery(item->data(ReportTreeModel::QueryRole).toString());

    if (isSystemNode(item)) {
        // System report: read-only
        dlg.setWindowTitle(tr("View SQL — %1 (read only)").arg(item->text()));
        dlg.setReadOnly(true);
        dlg.exec();  // No need to save anything
    } else {
        // User report: editable — save on OK
        dlg.setWindowTitle(tr("Edit SQL — %1").arg(item->text()));
        dlg.setReadOnly(false);
        if (dlg.exec() == QDialog::Accepted) {
            if (!m_model->updateQuery(item, dlg.getQuery())) {
                QMessageBox::warning(this, tr("Edit SQL"), tr("Failed to save query."));
            }
        }
    }
}

//=============================================================================
// Context menu action slots
//=============================================================================

void ReportExporter::onNewReport()
{
    QStandardItem* parent = getSelectedItem();
    // If a user report is selected, use its parent folder
    if (parent && isUserNode(parent) && isReportNode(parent)) {
        parent = parent->parent() ? parent->parent() : getUserRootItem();
    }
    if (!parent || !isUserNode(parent) || isReportNode(parent)) {
        parent = getUserRootItem();
    }
    if (!parent) return;

    bool ok;
    QString name = QInputDialog::getText(this, tr("New Report"),
                                         tr("Report name:"), QLineEdit::Normal,
                                         QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString templateQuery = QString(
        "-- New Report: %1\n"
        "-- Available macros: #PROFILE_ID, #START_DATE, #END_DATE\n"
        "SELECT\n  *\nFROM daily_summaries\n"
        "WHERE profile_id = #PROFILE_ID\n"
        "  AND date >= #START_DATE\n"
        "  AND date <= #END_DATE\nORDER BY date"
    ).arg(name.trimmed());

    QStandardItem* newItem = m_model->addReport(parent, name.trimmed(), QString(), templateQuery);
    if (!newItem) {
        QMessageBox::warning(this, tr("New Report"),
                           tr("Failed to create report. A report with that name may already exist."));
        return;
    }
    QModelIndex newIndex = m_model->indexFromItem(newItem);
    m_treeView->setCurrentIndex(newIndex);
    m_treeView->scrollTo(newIndex);
}

void ReportExporter::onNewFolder()
{
    QStandardItem* parent = getSelectedItem();
    if (!parent || isReportNode(parent)) {
        parent = getUserRootItem();
    }
    if (!parent || !isUserNode(parent)) return;

    bool ok;
    QString name = QInputDialog::getText(this, tr("New Folder"),
                                         tr("Folder name:"), QLineEdit::Normal,
                                         QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QStandardItem* newItem = m_model->addFolder(parent, name.trimmed());
    if (!newItem) {
        QMessageBox::warning(this, tr("New Folder"),
                           tr("Failed to create folder. A folder with that name may already exist."));
        return;
    }
    QModelIndex newIndex = m_model->indexFromItem(newItem);
    m_treeView->setCurrentIndex(newIndex);
    m_treeView->scrollTo(newIndex);
    m_treeView->edit(newIndex);
}

void ReportExporter::onEditQuery()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isReportNode(item) || !isUserNode(item)) return;

    SQLEditor dlg(this);
    dlg.setWindowTitle(tr("Edit SQL — %1").arg(item->text()));
    dlg.setQuery(item->data(ReportTreeModel::QueryRole).toString());
    dlg.setReadOnly(false);
    if (dlg.exec() == QDialog::Accepted) {
        if (!m_model->updateQuery(item, dlg.getQuery())) {
            QMessageBox::warning(this, tr("Edit SQL"), tr("Failed to save query."));
        }
    }
}

void ReportExporter::onViewSubstitutedQuery()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isReportNode(item)) return;

    SQLEditor dlg(this);
    dlg.setWindowTitle(tr("View SQL (Substituted) — %1").arg(item->text()));
    dlg.setQuery(substituteMacros(item->data(ReportTreeModel::QueryRole).toString()));
    dlg.setReadOnly(true);
    dlg.exec();
}

void ReportExporter::onRenameItem()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isUserNode(item) || isRootNode(item)) return;
    m_treeView->edit(m_model->indexFromItem(item));
}

void ReportExporter::onDeleteItem()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isUserNode(item) || isRootNode(item)) return;

    QString typeName = isReportNode(item) ? tr("report") : tr("folder");
    int childCount = item->rowCount();
    QString msg = childCount > 0
        ? tr("Delete %1 \"%2\" and all %3 item(s) inside it?").arg(typeName, item->text()).arg(childCount)
        : tr("Delete %1 \"%2\"?").arg(typeName, item->text());

    if (QMessageBox::question(this, tr("Confirm Delete"), msg,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) return;

    if (!m_model->removeNode(item)) {
        QMessageBox::warning(this, tr("Delete"), tr("Failed to delete item."));
    }
}

void ReportExporter::onDuplicateItem()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isUserNode(item) || isRootNode(item)) return;

    QStandardItem* parent = item->parent() ? item->parent() : getUserRootItem();
    QString copyName = item->text() + tr(" (Copy)");
    QStandardItem* newItem = m_model->duplicateNode(item, parent);
    if (newItem) {
        m_model->renameNode(newItem, copyName);
        QModelIndex newIndex = m_model->indexFromItem(newItem);
        m_treeView->setCurrentIndex(newIndex);
        m_treeView->scrollTo(newIndex);
    } else {
        QMessageBox::warning(this, tr("Duplicate"), tr("Failed to duplicate item."));
    }
}

void ReportExporter::onDuplicateToUser()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isSystemNode(item)) return;

    QStandardItem* userRoot = getUserRootItem();
    if (!userRoot) return;

    QStandardItem* newItem = m_model->duplicateNode(item, userRoot);
    if (newItem) {
        m_treeView->expand(m_model->indexFromItem(userRoot));
        QModelIndex newIndex = m_model->indexFromItem(newItem);
        m_treeView->setCurrentIndex(newIndex);
        m_treeView->scrollTo(newIndex);
    } else {
        QMessageBox::warning(this, tr("Duplicate to User"), tr("Failed to copy item to User branch."));
    }
}

void ReportExporter::onShowDescription()
{
    QStandardItem* item = getSelectedItem();
    if (!item) return;
    QString desc = item->data(ReportTreeModel::DescriptionRole).toString();
    QMessageBox::information(this, tr("Description: %1").arg(item->text()),
                             desc.isEmpty() ? tr("(no description)") : desc);
}

void ReportExporter::onEditDescription()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isUserNode(item)) return;

    bool ok;
    QString newDesc = QInputDialog::getMultiLineText(
        this, tr("Edit Description"),
        tr("Description for \"%1\":").arg(item->text()),
        item->data(ReportTreeModel::DescriptionRole).toString(), &ok);

    if (ok && !m_model->updateDescription(item, newDesc)) {
        QMessageBox::warning(this, tr("Edit Description"), tr("Failed to save description."));
    }
}

void ReportExporter::onImportReports()
{
    QStandardItem* targetParent = getSelectedItem();
    if (!targetParent || !isUserNode(targetParent) || isReportNode(targetParent)) {
        targetParent = getUserRootItem();
    }
    if (!targetParent) return;

    QString filename = QFileDialog::getOpenFileName(
        this, tr("Import Reports"), QString(),
        tr("OSCAR Report Files (*.orf)"), nullptr, nativeDialogOption());
    if (filename.isEmpty()) return;

    QList<OrfReportEntry> entries;
    QString errorMessage;
    if (!OrfFileIO::readFile(filename, entries, errorMessage)) {
        QMessageBox::critical(this, tr("Import Reports"),
            tr("Failed to parse file:\n%1").arg(errorMessage));
        return;
    }

    qint64 parentId = targetParent->data(ReportTreeModel::NodeIdRole).toLongLong();
    int imported = OrfFileIO::importToDatabase(entries, parentId, "user", errorMessage);
    if (imported < 0) {
        QMessageBox::critical(this, tr("Import Reports"),
            tr("Import failed:\n%1").arg(errorMessage));
        return;
    }

    // Reload the tree to show imported items
    m_model->loadFromDatabase();
    m_treeView->expandToDepth(0);

    QMessageBox::information(this, tr("Import Reports"),
        tr("Imported %1 report(s) from:\n%2").arg(imported).arg(filename));
}

void ReportExporter::onExportSelected()
{
    QStandardItem* item = getSelectedItem();
    if (!item) return;

    QString filename = QFileDialog::getSaveFileName(
        this, tr("Export Reports"),
        item->text() + ".orf",
        tr("OSCAR Report Files (*.orf)"), nullptr, nativeDialogOption());
    if (filename.isEmpty()) return;

    qint64 nodeId = item->data(ReportTreeModel::NodeIdRole).toLongLong();
    QString source = isSystemNode(item) ? "System" : "User";

    QList<OrfReportEntry> entries = OrfFileIO::exportFromDatabase(nodeId);
    if (entries.isEmpty()) {
        QMessageBox::information(this, tr("Export"),
            tr("No reports to export under \"%1\".").arg(item->text()));
        return;
    }

    QString errorMessage;
    if (!OrfFileIO::writeFile(filename, entries, source, errorMessage)) {
        QMessageBox::critical(this, tr("Export"),
            tr("Failed to write file:\n%1").arg(errorMessage));
        return;
    }

    QMessageBox::information(this, tr("Export"),
        tr("Exported %1 report(s) to:\n%2").arg(entries.count()).arg(filename));
}

void ReportExporter::onExpandAll()
{
    QStandardItem* item = getSelectedItem();
    if (!item) return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    m_treeView->expandRecursively(m_model->indexFromItem(item));
#else
    std::function<void(const QModelIndex&)> expandAll = [&](const QModelIndex& idx) {
        m_treeView->expand(idx);
        for (int r = 0; r < m_model->rowCount(idx); ++r) {
            expandAll(m_model->index(r, 0, idx));
        }
    };
    expandAll(m_model->indexFromItem(item));
#endif
}

void ReportExporter::onCollapseAll()
{
    QStandardItem* item = getSelectedItem();
    if (!item) return;
    QModelIndex index = m_model->indexFromItem(item);
    for (int i = 0; i < item->rowCount(); ++i) {
        m_treeView->collapse(m_model->indexFromItem(item->child(i)));
    }
    m_treeView->collapse(index);
}

//=============================================================================
// UI state helpers
//=============================================================================

void ReportExporter::updateButtonStates(const QModelIndex& index)
{
    // Guard: buttons may not exist yet during construction
    if (!m_exportCSVButton || !m_editSQLButton) return;

    // Always use column 0 — currentChanged may pass any column index
    QModelIndex col0 = index.isValid() ? index.sibling(index.row(), 0) : QModelIndex();
    QStandardItem* item = col0.isValid() ? m_model->itemFromIndex(col0) : nullptr;
    bool isReport = item && isReportNode(item);

    m_exportCSVButton->setEnabled(isReport);
    m_editSQLButton->setEnabled(isReport);
    m_editSQLButton->setText(item && isSystemNode(item) ? tr("View SQL") : tr("Edit SQL"));
}

void ReportExporter::updateStatusLabel(const QModelIndex& index)
{
    if (!m_statusLabel) return;
    if (!index.isValid()) {
        m_statusLabel->setText(tr("Select a report from the tree to export"));
        return;
    }
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;

    QString source = isSystemNode(item) ? tr("System") : tr("User");

    if (isReportNode(item)) {
        QStringList path;
        QStandardItem* p = item;
        while (p) { path.prepend(p->text()); p = p->parent(); }
        m_statusLabel->setText(tr("Report: %1 (%2) — right-click for options").arg(path.join(" / "), source));
    } else if (isFolderNode(item)) {
        m_statusLabel->setText(tr("Folder: %1 — right-click for options").arg(item->text()));
    } else if (isRootNode(item)) {
        m_statusLabel->setText(tr("%1 branch — right-click for options").arg(item->text()));
    }
}

//=============================================================================
// Helper methods
//=============================================================================

QStandardItem* ReportExporter::getSelectedItem() const
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) return nullptr;
    return m_model->itemFromIndex(index.sibling(index.row(), 0));
}

QStandardItem* ReportExporter::getUserRootItem() const
{
    QStandardItem* root = m_model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); ++i) {
        QStandardItem* item = root->child(i);
        if (item && m_model->isUserNode(item) && m_model->isRootNode(item))
            return item;
    }
    return nullptr;
}

bool ReportExporter::isUserNode(QStandardItem* item) const   { return m_model->isUserNode(item); }
bool ReportExporter::isSystemNode(QStandardItem* item) const { return m_model->isSystemNode(item); }
bool ReportExporter::isReportNode(QStandardItem* item) const { return m_model->isReportNode(item); }
bool ReportExporter::isFolderNode(QStandardItem* item) const { return m_model->isFolderNode(item); }
bool ReportExporter::isRootNode(QStandardItem* item) const   { return m_model->isRootNode(item); }

QString ReportExporter::substituteMacros(const QString& query)
{
    QString result = query;
    result.replace("#PROFILE_ID", QString::number(selectedProfileId()));
    result.replace("#START_DATE", QString("'%1'").arg(m_startDateEdit ? m_startDateEdit->date().toString("yyyy-MM-dd") : "2000-01-01"));
    result.replace("#END_DATE",   QString("'%1'").arg(m_endDateEdit   ? m_endDateEdit->date().toString("yyyy-MM-dd")   : "2099-12-31"));
    return result;
}

qint64 ReportExporter::selectedProfileId() const
{
    if (!m_profileCombo || m_profileCombo->count() == 0) return 0;
    return m_profileCombo->currentData().toLongLong();
}

void ReportExporter::loadProfiles()
{
    if (!m_profileCombo) return;
    m_profileCombo->clear();
    QSqlQuery q(DatabaseManager::instance().database());
    if (!q.exec("SELECT id, username FROM profiles ORDER BY username")) return;
    while (q.next()) {
        m_profileCombo->addItem(q.value(1).toString(), q.value(0).toLongLong());
    }
    if (m_profileCombo->count() == 0) {
        m_profileCombo->addItem(tr("(No profiles)"), 0LL);
        return;
    }

    // Prefer the currently open profile; fall back to the last-used profile for this dialog.
    QString selectName = m_currentProfileName;
    if (selectName.isEmpty()) {
        AppPreferencesRepository repo;
        const auto rows = repo.loadByCategory("ReportExporter");
        for (const AppPrefData& row : rows) {
            if (row.key == "lastProfile") { selectName = row.value; break; }
        }
    }
    if (!selectName.isEmpty()) {
        int idx = m_profileCombo->findText(selectName);
        if (idx >= 0) m_profileCombo->setCurrentIndex(idx);
    }
}

void ReportExporter::restoreSettings()
{
    AppPreferencesRepository repo;
    const auto rows = repo.loadByCategory("ReportExporter");
    for (const AppPrefData& row : rows) {
        if      (row.key == "openAfterExport" && m_openAfterExportCheck)
            m_openAfterExportCheck->setChecked(row.value == "true" || row.value == "1");
        else if (row.key == "openProgram" && m_programCombo)
            m_programCombo->setCurrentIndex(row.value.toInt());
        else if (row.key == "quickRange" && m_quickRangeCombo)
            m_quickRangeCombo->setCurrentIndex(row.value.toInt());
        else if (row.key == "lastExportFolder")
            m_lastExportFolder = row.value;
    }
    setQuickDateRange(m_quickRangeCombo ? m_quickRangeCombo->currentIndex() : 0);
}

void ReportExporter::saveSettings()
{
    AppPreferencesRepository repo;
    if (m_openAfterExportCheck) repo.save("ReportExporter", "openAfterExport", m_openAfterExportCheck->isChecked());
    if (m_programCombo)         repo.save("ReportExporter", "openProgram",     m_programCombo->currentIndex());
    if (m_quickRangeCombo)      repo.save("ReportExporter", "quickRange",      m_quickRangeCombo->currentIndex());
    repo.save("ReportExporter", "lastExportFolder", m_lastExportFolder);
    if (m_profileCombo && m_profileCombo->count() > 0
            && !m_profileCombo->currentText().startsWith("(")) {
        repo.save("ReportExporter", "lastProfile", m_profileCombo->currentText());
    }
    saveTreeState();
}

/*
 * Save the tree's expanded-node set and selected node to app_preferences.
 */
void ReportExporter::saveTreeState()
{
    if (!m_treeView || !m_model) return;

    QStringList expanded;
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        for (int r = 0; r < m_model->rowCount(parent); ++r) {
            QModelIndex idx = m_model->index(r, 0, parent);
            QStandardItem* item = m_model->itemFromIndex(idx);
            if (item && m_treeView->isExpanded(idx)) {
                qint64 id = item->data(ReportTreeModel::NodeIdRole).toLongLong();
                if (id != 0) expanded << QString::number(id);
                walk(idx);  // only recurse into expanded nodes
            }
        }
    };
    walk(QModelIndex());

    qint64 selectedId = 0;
    QStandardItem* sel = getSelectedItem();
    if (sel) selectedId = sel->data(ReportTreeModel::NodeIdRole).toLongLong();

    AppPreferencesRepository repo;
    repo.save("ReportExporter", "expandedNodes", expanded.join(","));
    repo.save("ReportExporter", "selectedNode",  static_cast<qint64>(selectedId));
}

/*
 * Restore expanded-node set and selection from app_preferences.
 * Returns true if saved state was found and applied; false on first use.
 */
bool ReportExporter::restoreTreeState()
{
    if (!m_treeView || !m_model) return false;

    AppPreferencesRepository repo;
    const auto rows = repo.loadByCategory("ReportExporter");
    QString expandedStr;
    qint64  selectedId = 0;
    for (const AppPrefData& row : rows) {
        if      (row.key == "expandedNodes") expandedStr = row.value;
        else if (row.key == "selectedNode")  selectedId  = row.value.toLongLong();
    }

    if (expandedStr.isEmpty()) return false;  // No saved tree state

    // Build the set of IDs to expand.
    QSet<qint64> expandedIds;
    for (const QString& part : expandedStr.split(",")) {
        bool ok;
        qint64 id = part.toLongLong(&ok);
        if (ok && id != 0) expandedIds.insert(id);
    }

    // Walk the entire tree, expanding saved nodes and noting the selection target.
    QModelIndex selectIndex;
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        for (int r = 0; r < m_model->rowCount(parent); ++r) {
            QModelIndex idx = m_model->index(r, 0, parent);
            QStandardItem* item = m_model->itemFromIndex(idx);
            if (item) {
                qint64 id = item->data(ReportTreeModel::NodeIdRole).toLongLong();
                if (expandedIds.contains(id)) {
                    m_treeView->expand(idx);
                }
                if (selectedId != 0 && id == selectedId) {
                    selectIndex = idx;
                }
            }
            walk(idx);
        }
    };
    walk(QModelIndex());

    if (selectIndex.isValid()) {
        m_treeView->setCurrentIndex(selectIndex);
        // Only scroll to the item if its entire parent chain is expanded.
        // QTreeView::scrollTo() expands collapsed ancestors to make the item
        // visible, which would override the user's saved collapsed state.
        bool parentChainExpanded = true;
        for (QModelIndex p = selectIndex.parent(); p.isValid(); p = p.parent()) {
            if (!m_treeView->isExpanded(p)) {
                parentChainExpanded = false;
                break;
            }
        }
        if (parentChainExpanded) {
            m_treeView->scrollTo(selectIndex);
        }
    }
    return true;
}

/*
 * Return the date of the earliest session for the selected profile.
 * Uses daily_summaries.date, which stores the OSCAR date (local time +
 * day-split logic via Machine::pickDate()) rather than the raw UTC date.
 * Falls back to today if no data exists or the query fails.
 */
QDate ReportExporter::getFirstDataDate() const
{
    qint64 pid = selectedProfileId();
    if (pid == 0) return QDate::currentDate();

    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "SELECT MIN(date) FROM daily_summaries WHERE profile_id = :pid"));
    q.bindValue(QStringLiteral(":pid"), pid);
    if (q.exec() && q.next() && !q.value(0).isNull()) {
        QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        if (d.isValid()) return d;
    }
    return QDate::currentDate();
}

/*
 * Return the date of the most recent session for the selected profile.
 * Uses daily_summaries.date, which stores the OSCAR date (local time +
 * day-split logic via Machine::pickDate()) rather than the raw UTC date.
 * Falls back to today if no data exists or the query fails.
 */
QDate ReportExporter::getLastDataDate() const
{
    qint64 pid = selectedProfileId();
    if (pid == 0) return QDate::currentDate();

    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "SELECT MAX(date) FROM daily_summaries WHERE profile_id = :pid"));
    q.bindValue(QStringLiteral(":pid"), pid);
    if (q.exec() && q.next() && !q.value(0).isNull()) {
        QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        if (d.isValid()) return d;
    }
    return QDate::currentDate();
}

void ReportExporter::setQuickDateRange(int idx)
{
    // Custom (index 7): enable date edits so user can type; all others: show but disable
    bool isCustom = (idx == 7);
    if (m_startDateEdit) m_startDateEdit->setEnabled(isCustom);
    if (m_endDateEdit)   m_endDateEdit->setEnabled(isCustom);
    if (isCustom) return;  // Don't overwrite dates when Custom is chosen

    // Anchor all presets to the most recent date with data, so ranges are
    // always meaningful regardless of when the dialog is opened.
    const QDate end = getLastDataDate();
    QDate start;
    switch (idx) {
        case 0: start = end; break;                       // Most Recent Day
        case 1: start = end.addDays(-6); break;           // Last Week
        case 2: start = end.addDays(-13); break;          // Last Fortnight
        case 3: start = end.addMonths(-1); break;         // Last Month
        case 4: start = end.addMonths(-6); break;         // Last 6 Months
        case 5: start = end.addYears(-1); break;          // Last Year
        case 6: start = getFirstDataDate(); break;        // Everything
        default: return;
    }
    if (m_startDateEdit) m_startDateEdit->setDate(start);
    if (m_endDateEdit)   m_endDateEdit->setDate(end);
}

void ReportExporter::onProfileChanged(int)
{
    setQuickDateRange(m_quickRangeCombo ? m_quickRangeCombo->currentIndex() : 0);
    updateFilenameField();
}

void ReportExporter::onQuickRangeChanged(int index)
{
    setQuickDateRange(index);
    updateFilenameField();
}

/*
 * Build the default export filename (base name only, no directory).
 * Returns an empty string if no report is selected.
 * Format: OSCAR_{profile}_{report}_{startDate}[_{endDate}].csv
 */
QString ReportExporter::buildDefaultFilename() const
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isReportNode(item)) return QString();

    QString profileName = (m_profileCombo && m_profileCombo->count() > 0
                           && !m_profileCombo->currentText().startsWith("("))
                          ? m_profileCombo->currentText() : "System";

    QString reportName = item->text().replace(" ", "_");

    QString startStr = m_startDateEdit ? m_startDateEdit->date().toString("yyyy-MM-dd") : "";
    QString endStr   = m_endDateEdit   ? m_endDateEdit->date().toString("yyyy-MM-dd")   : "";

    QString name = QString("OSCAR_%1_%2_%3").arg(profileName, reportName, startStr);
    if (!endStr.isEmpty() && endStr != startStr) name += "_" + endStr;
    name += ".csv";
    return name;
}

/*
 * Update the filename field with the auto-generated default path.
 * Clears the field if no report is selected.
 */
void ReportExporter::updateFilenameField()
{
    if (!m_filenameEdit) return;
    QString name = buildDefaultFilename();
    if (name.isEmpty()) {
        m_filenameEdit->clear();
        return;
    }
    QString folder = m_lastExportFolder.isEmpty() ? QDir::homePath() : m_lastExportFolder;
    m_filenameEdit->setText(folder + QDir::separator() + name);
}

void ReportExporter::onBrowseFilename()
{
    // Use the current field value (auto-generated or previously browsed) as the dialog default.
    QString defaultName = buildDefaultFilename();
    if (defaultName.isEmpty()) defaultName = "OSCAR_Export.csv";

    // Determine the starting folder: prefer last-saved folder, then home.
    QString folder = m_lastExportFolder;
    if (folder.isEmpty()) folder = QDir::homePath();

    QString fn = QFileDialog::getSaveFileName(this, tr("Save CSV"),
        folder + QDir::separator() + defaultName,
        tr("CSV Files (*.csv)"), nullptr, nativeDialogOption());

    if (fn.isEmpty()) return;
    if (!fn.toLower().endsWith(".csv")) fn += ".csv";

    if (m_filenameEdit) m_filenameEdit->setText(fn);

    // Remember the folder for next time.
    m_lastExportFolder = QFileInfo(fn).absolutePath();
}

bool ReportExporter::runExport()
{
    QStandardItem* item = getSelectedItem();
    if (!item || !isReportNode(item)) return false;
    QString query = substituteMacros(item->data(ReportTreeModel::QueryRole).toString());
    QString filename = m_filenameEdit ? m_filenameEdit->text().trimmed() : QString();
    if (filename.isEmpty()) {
        QMessageBox::warning(this, tr("Export CSV"), tr("Please specify an output filename."));
        return false;
    }
    QSqlQuery sql(DatabaseManager::instance().database());
    if (!sql.exec(query)) {
        QMessageBox::critical(this, tr("Export CSV"),
            tr("Query failed:\n%1").arg(sql.lastError().text()));
        return false;
    }
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export CSV"),
            tr("Cannot write file:\n%1").arg(filename));
        return false;
    }
    QTextStream out(&file);
    QSqlRecord rec = sql.record();
    QStringList headers;
    for (int i = 0; i < rec.count(); i++) headers << rec.fieldName(i);
    out << headers.join(",") << "\n";
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    int rows = 0;
    while (sql.next()) {
        QStringList fields;
        for (int i = 0; i < rec.count(); i++) {
            QString v = sql.value(i).toString();
            if (v.contains(',') || v.contains('"') || v.contains('\n'))
                v = "\"" + v.replace("\"","\"\"") + "\"";
            fields << v;
        }
        out << fields.join(",") << "\n";
        rows++;
    }
    file.close();
    m_progressBar->setValue(100);
    if (m_openAfterExportCheck && m_openAfterExportCheck->isChecked())
        launchPostExportProgram(filename);
    saveSettings();
    QMessageBox::information(this, tr("Export CSV"),
        tr("Exported %1 rows to:\n%2").arg(rows).arg(filename));
    return true;
}

void ReportExporter::launchPostExportProgram(const QString& csvFilePath)
{
    int prog = m_programCombo ? m_programCombo->currentIndex() : 0;
    if (prog == 0) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(csvFilePath));
    } else {
        QString exe = (prog == 1) ? "excel" : "libreoffice";
        QProcess::startDetached(exe, QStringList() << csvFilePath);
    }
}
