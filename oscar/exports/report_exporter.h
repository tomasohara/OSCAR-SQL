/* Report Exporter Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ReportExporter dialog which provides a unified
 * interface for managing and exporting CSV reports according to the
 * design specification in REPORT_TREE_UI_REDESIGN.md.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORT_EXPORTER_H
#define REPORT_EXPORTER_H

#include <QDialog>
#include <QModelIndex>
#include <QDate>

// Forward declarations
class QTreeView;
class QTextEdit;
class QPushButton;
class QSplitter;
class QLabel;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QProgressBar;
class QCheckBox;
class ReportTreeModel;
class QStandardItem;

/*!
 * \class ReportExporter
 * \brief Unified dialog for managing and exporting CSV reports
 *
 * Per REPORT_TREE_UI_REDESIGN.md Section 4:
 * - LEFT: Tree view with System and User roots
 * - RIGHT: Profile, date range, filename, progress, post-export options
 * - BOTTOM: 3 buttons: [Export CSV] [Edit SQL/View SQL] [Cancel]
 * - ALL tree operations via right-click context menus (Section 5)
 */
class ReportExporter : public QDialog
{
    Q_OBJECT

public:
    explicit ReportExporter(QWidget* parent = nullptr);
    ~ReportExporter() override;

private slots:
    // Tree interaction
    void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onTreeDoubleClicked(const QModelIndex& index);
    void onTreeContextMenu(const QPoint& pos);

    // Right panel slots
    void onProfileChanged(int index);
    void onQuickRangeChanged(int index);
    void onBrowseFilename();

    // Button slots
    void onExportCSVClicked();
    void onEditSQLClicked();

    // Context menu action slots
    void onNewReport();
    void onNewFolder();
    void onEditQuery();
    void onViewSubstitutedQuery();
    void onRenameItem();
    void onDeleteItem();
    void onDuplicateItem();
    void onDuplicateToUser();
    void onShowDescription();
    void onEditDescription();
    void onImportReports();
    void onExportSelected();
    void onExpandAll();
    void onCollapseAll();

private:
    // UI setup
    void setupUi();
    QTreeView* createTreeView();
    QWidget* createRightPanel();
    QWidget* createButtonPanel();

    // Data loading
    void loadProfiles();
    void restoreSettings();
    void saveSettings();

    // UI state
    void updateButtonStates(const QModelIndex& index);
    void updateStatusLabel(const QModelIndex& index);
    void setQuickDateRange(int comboIndex);

    // Export helpers
    bool runExport();
    QString substituteMacros(const QString& query);
    qint64 selectedProfileId() const;
    void launchPostExportProgram(const QString& csvFilePath);

    // Tree helpers
    QStandardItem* getSelectedItem() const;
    QStandardItem* getUserRootItem() const;
    bool isUserNode(QStandardItem* item) const;
    bool isSystemNode(QStandardItem* item) const;
    bool isReportNode(QStandardItem* item) const;
    bool isFolderNode(QStandardItem* item) const;
    bool isRootNode(QStandardItem* item) const;

    // Widgets
    QTreeView*   m_treeView;
    QLabel*      m_statusLabel;
    ReportTreeModel* m_model;

    // Right panel widgets
    QComboBox*   m_profileCombo;
    QComboBox*   m_quickRangeCombo;
    QDateEdit*   m_startDateEdit;
    QDateEdit*   m_endDateEdit;
    QLineEdit*   m_filenameEdit;
    QProgressBar* m_progressBar;
    QCheckBox*   m_openAfterExportCheck;
    QComboBox*   m_programCombo;

    // Buttons (3 only)
    QPushButton* m_exportCSVButton;
    QPushButton* m_editSQLButton;
    QPushButton* m_closeButton;
};

#endif // REPORT_EXPORTER_H
