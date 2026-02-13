# OSCAR Report Tree UI Redesign

## Design Document

**Date:** 2026-02-12
**Status:** Proposed
**Author:** OSCAR Team

---

## 1. Overview and Goals

The current CSV report system uses two separate dialogs (ExportCSV and ReportManager)
and two database tables (reports and report_contents). The user experience requires
switching between dialogs to manage and export reports. The "variety/resolution" concept
adds complexity without clear benefit.

### Goals

1. **Single unified dialog** that combines report browsing, management, and CSV export.
2. **Tree-based organization** with System and User top-level branches.
3. **Simplified data model** merging the two tables into one hierarchical table.
4. **External file for System reports** instead of hard-coded SQL in C++ source.
5. **Rich context menu** for all tree operations (add, edit, delete, rename, duplicate).
6. **Drag-and-drop** support within the User branch.
7. **Import/Export** of report sets to/from a human-readable text file.
8. **Protected System branch** that is read-only to the user and rebuilt on OSCAR upgrades.

---

## 2. Current State Analysis

### Current Tables

- **reports**: id, name, description, display_order, is_system, timestamps
- **report_contents**: id, report_id, variety, description, query, display_order, is_system, timestamps

### Current Dialogs

- **ExportCSV**: Date range, resolution combo, flat report list, filename, export button, edit SQL.
- **ReportManager**: Report list, variety table, new/copy/delete report, view/edit/copy variety.

### Problems

- Two dialogs for one workflow forces users to switch back and forth.
- Flat list does not scale as report count grows.
- The "variety" concept is confusing; what was "Daily Summaries by Week" is really just a different report.
- System reports are hard-coded in `reports_initializer.cpp`, requiring recompilation to change.
- No way for users to organize reports into categories/folders.
- No import/export capability for sharing reports between users.

---

## 3. New Database Schema

### 3.1 New Table: `report_tree`

Replace both `reports` and `report_contents` with a single self-referencing hierarchical table.

```sql
CREATE TABLE IF NOT EXISTS report_tree (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id INTEGER,
    name TEXT NOT NULL,
    node_type TEXT NOT NULL CHECK(node_type IN ('root', 'folder', 'report')),
    source TEXT NOT NULL CHECK(source IN ('system', 'user')),
    description TEXT,
    query TEXT,
    display_order INTEGER DEFAULT 0,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (parent_id) REFERENCES report_tree(id) ON DELETE CASCADE,
    UNIQUE(parent_id, name)
);

CREATE INDEX IF NOT EXISTS idx_report_tree_parent ON report_tree(parent_id);
CREATE INDEX IF NOT EXISTS idx_report_tree_source ON report_tree(source);
CREATE INDEX IF NOT EXISTS idx_report_tree_type ON report_tree(node_type);
```

### 3.2 Column Definitions

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER PK | Auto-increment primary key |
| parent_id | INTEGER NULL | Parent node ID. NULL only for the two root nodes (System, User) |
| name | TEXT | Display name of the node |
| node_type | TEXT | `root` = System/User root; `folder` = organizational folder; `report` = leaf with SQL query |
| source | TEXT | `system` = OSCAR-supplied, immutable; `user` = user-created, fully editable |
| description | TEXT | Optional description shown via right-click "Show Description" |
| query | TEXT | SQL query template with macros. NULL for root and folder nodes |
| display_order | INTEGER | Sort order among siblings (0 = sort alphabetically by name) |
| created_at | TEXT | Creation timestamp |
| updated_at | TEXT | Last modification timestamp |

### 3.3 Root Nodes

Two permanent root nodes are created at initialization and never deleted:

| id | parent_id | name | node_type | source |
|----|-----------|------|-----------|--------|
| 1 | NULL | System | root | system |
| 2 | NULL | User | root | user |

### 3.4 Example Data

```
id  parent_id  name                 node_type  source
--  ---------  -------------------  ---------  ------
1   NULL       System               root       system
2   NULL       User                 root       user
3   1          Daily Summaries      folder     system
4   3          by Day               report     system
5   3          by Month             report     system
6   3          by Week              report     system
7   1          Other                folder     system
8   7          Channels Used        report     system
9   7          Device Settings      report     system
10  1          Session Summaries    folder     system
11  10         by Day               report     system
12  10         by Month             report     system
13  10         by Session           report     system
14  10         by Week              report     system
15  1          Statistics           folder     system
16  15         Respiratory Events   report     system
17  15         Sessions             report     system
18  2          SpO2                 folder     user
19  18         my stuff             folder     user
20  19         SpO2 plot            report     user
21  19         SpO2 statistics      report     user
```

### 3.5 Migration Strategy

A database migration will:

1. Create the `report_tree` table.
2. Insert the two root nodes (System, User).
3. Load system reports from the external file `oscar/docs/system_reports.orf`.
4. Migrate any existing user reports from the old tables into the User branch.
5. Drop the old `reports` and `report_contents` tables (or leave them as backup).

---

## 4. Unified Dialog Design: ReportExporter

### 4.1 Dialog Name and Class

- **Class:** `ReportExporter` (replaces both `ExportCSV` and `ReportManager`)
- **Files:** `reportexporter.cpp`, `reportexporter.h`, `reportexporter.ui`
- **Window Title:** "Reports and CSV Export"
- **Modeless:** No — modal dialog launched from menu (replaces current Export CSV menu item)
- **Size:** ~800 x 600, resizable, remembers last size/position

### 4.2 Layout Overview

```
+------------------------------------------------------------------+
|  Reports and CSV Export                                    [X]   |
+------------------------------------------------------------------+
|                          |                                        |
|   Report Tree            |   Quick Range: [Most Recent Day  v]   |
|   ==================     |                                        |
|   > System               |   Dates:  Start: [01/15/2026]         |
|     > Daily Summaries    |           End:   [02/12/2026]         |
|       by Day             |                                        |
|       by Month           |   ----------------------------------------|
|       by Week            |                                        |
|     > Other              |   Filename: [________________________] |
|       Channels Used      |            [...] (browse button)       |
|       Device Settings    |                                        |
|       Profiles           |   ----------------------------------------|
|     > Session Summaries  |                                        |
|       by Day             |   [============================] 0%   |
|       by Month           |                                        |
|       by Session         |   [Export CSV] [Edit SQL] [Cancel]    |
|       by Week            |                                        |
|     > Statistics         +----------------------------------------+
|       Respiratory Events |
|       Sessions           |
|   v User                 |
|     > SpO2               |
|       > my stuff         |
|         SpO2 plot        |
|         SpO2 statistics  |
|                          |
+--------------------------+
```

### 4.3 Left Panel: Report Tree (QTreeView)

- Uses `QTreeView` with a `QStandardItemModel`.
- Two root items: **System** (with lock/shield icon) and **User** (with person icon).
- Folders use a folder icon; reports use a document/page icon.
- System nodes rendered with a subtle visual distinction (e.g., slightly gray text or italic).
- The tree supports:
  - **Single-click** to select a report (populates the right panel for export).
  - **Double-click** on a user report to open SQL editor; on a system report to view SQL read-only.
  - **Right-click** for context menu (see Section 5).
  - **Drag-and-drop** within the User branch (see Section 5.5).
  - **Multi-selection** (Ctrl+click, Shift+click) for batch export/import operations.
- Expand/collapse of folders is standard QTreeView behavior.
- The tree auto-expands the System and User roots on dialog open.
- Selected report name is shown in bold or highlighted.

### 4.4 Right Panel: Export Controls

The right panel is the export workflow area, laid out with `QFormLayout`:

1. **Quick Range** combo box (same options as current: Most Recent Day, Last Week,
   Last Fortnight, Last Month, Last 6 Months, Last Year, Everything, Custom).
2. **Date range** with Start and End `QDateEdit` controls with calendar popups.
   Calendar days are color-coded for CPAP/Oximetry data presence (same as current).
3. **Filename** line edit with browse button (same as current).
4. **Progress bar** (same as current).
5. **Button row:** Export CSV, Edit SQL, Cancel.

**Note:** The Resolution combo box from the current ExportCSV dialog is **removed**.
Each tree leaf IS a specific report — "Daily Summaries by Day" and "Daily Summaries by Week"
are separate reports, not resolutions of the same report.

### 4.5 Button Behavior

| Button | Behavior |
|--------|----------|
| **Export CSV** | Runs the selected report's SQL query (with macro substitution) and writes results to CSV file. Enabled only when a report leaf is selected AND a filename is set. |
| **Edit SQL** | Opens the `SQLEditor` dialog. Behavior depends on context — see Section 5.3. |
| **Cancel** | Closes the dialog. |

### 4.6 Status Bar Area

A small status label at the bottom of the dialog shows contextual information:
- When a report is selected: "Report: Daily Summaries / by Day (System)"
- When a folder is selected: "Folder: Daily Summaries — select a report to export"
- When nothing is selected: "Select a report from the tree to export"

### 4.7 Icons

| Node Type | Icon | Description |
|-----------|------|-------------|
| System root | Shield or lock icon | Indicates protected/read-only branch |
| User root | Person or star icon | Indicates user-customizable branch |
| Folder (system) | Gray folder icon | Read-only organizational folder |
| Folder (user) | Blue/normal folder icon | User-created folder |
| Report (system) | Gray document icon | Read-only system report |
| Report (user) | Blue/normal document icon | User-editable report |

The existing OSCAR icon resources (`:/icons/`) should be checked for suitable icons.
New icons can be added to `Resources.qrc` if needed. Standard Qt icons from
`QStyle::standardIcon()` are also acceptable (e.g., `SP_DirIcon`, `SP_FileIcon`).

---

## 5. Tree Operations and Context Menu

### 5.1 Context Menu Structure

Right-clicking on a tree node shows a context menu whose items depend on the node type
and source. Items that don't apply are hidden (not grayed out).

#### 5.1.1 Right-click on a **System Report** leaf

| Menu Item | Action |
|-----------|--------|
| View Query (Unsubstituted) | Opens SQLEditor in read-only mode showing the raw query with #PROFILE_ID etc. |
| View Query (Substituted) | Opens SQLEditor in read-only mode with macros replaced by current values. User can copy but not save. |
| Duplicate to User | Creates a copy of this report under the User branch. User picks destination folder. |
| Show Description | Shows a tooltip or small popup with the report's description text. |
| Export Selected... | Exports this report (and any other selected items) to an .orf file. |

#### 5.1.2 Right-click on a **System Folder**

| Menu Item | Action |
|-----------|--------|
| Duplicate to User | Deep-copies this folder and all its reports into the User branch. |
| Export Selected... | Exports this folder and all contents to an .orf file. |
| Expand All | Expands this folder and all subfolders. |
| Collapse All | Collapses this folder and all subfolders. |

#### 5.1.3 Right-click on a **User Report** leaf

| Menu Item | Action |
|-----------|--------|
| Edit Query (Unsubstituted) | Opens SQLEditor in edit mode. User can modify and save the query template. |
| View Query (Substituted) | Opens SQLEditor in read-only mode with macros replaced. User can run but not save. |
| Rename | Enters inline rename mode on the tree item. |
| Delete | Deletes the report after confirmation. |
| Duplicate | Creates a copy of this report in the same folder (appends " (Copy)" to name). |
| Show Description | Shows the report's description. |
| Edit Description | Opens a small dialog to edit the description text. |
| Export Selected... | Exports this report to an .orf file. |

#### 5.1.4 Right-click on a **User Folder**

| Menu Item | Action |
|-----------|--------|
| New Report | Creates a new empty report in this folder. Opens a name dialog, then the SQL editor. |
| New Folder | Creates a new subfolder. User enters name inline. |
| Rename | Enters inline rename mode. |
| Delete | Deletes the folder and all contents after confirmation with count of affected items. |
| Import Reports... | Opens file dialog to select an .orf file. Imported items placed in this folder. |
| Export Selected... | Exports this folder and all contents to an .orf file. |
| Expand All | Expands this folder and all subfolders. |
| Collapse All | Collapses this folder and all subfolders. |

#### 5.1.5 Right-click on the **User Root** node

| Menu Item | Action |
|-----------|--------|
| New Report | Creates a new report directly under User root. |
| New Folder | Creates a new folder under User root. |
| Import Reports... | Imports from an .orf file into User root. |
| Export All User Reports... | Exports the entire User branch to an .orf file. |
| Expand All | Expands the entire User tree. |
| Collapse All | Collapses the entire User tree. |

#### 5.1.6 Right-click on the **System Root** node

| Menu Item | Action |
|-----------|--------|
| Export All System Reports... | Exports the entire System branch to an .orf file. |
| Expand All | Expands the entire System tree. |
| Collapse All | Collapses the entire System tree. |

#### 5.1.7 Right-click with **multiple items selected**

| Menu Item | Action |
|-----------|--------|
| Export Selected... | Exports all selected items (reports and folders) to an .orf file. |
| Delete Selected | Only shown if ALL selected items are user items. Confirms and deletes. |

### 5.2 Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Delete | Delete selected user item(s) |
| F2 | Rename selected user item |
| Ctrl+C | Copy selected report(s) to clipboard (internal) |
| Ctrl+V | Paste report(s) from clipboard into selected folder |
| Ctrl+D | Duplicate selected report |
| Enter/Return | If report selected, equivalent to Export CSV. If folder, toggle expand/collapse. |

### 5.3 Edit SQL Button Behavior (Toolbar Button)

The Edit SQL button on the right panel adapts to context:

| Selected Node | Button Text | Behavior |
|---------------|-------------|----------|
| System report | "View SQL" | Opens SQLEditor read-only with substituted query. |
| User report | "Edit SQL" | Opens SQLEditor in edit mode with unsubstituted query. On OK, saves to database. |
| Folder or nothing | (disabled) | Button is grayed out. |

### 5.4 New Report Workflow

When user creates a new report (via context menu "New Report"):

1. A dialog asks for the report name (validated for uniqueness within the parent folder).
2. Optionally, user enters a description.
3. The SQLEditor opens with a template query:
   ```sql
   -- New Report: <name>
   -- Available macros: #PROFILE_ID, #START_DATE, #END_DATE
   SELECT
     *
   FROM daily_summaries
   WHERE profile_id = #PROFILE_ID
     AND date >= #START_DATE
     AND date <= #END_DATE
   ORDER BY date
   ```
4. User edits the query and clicks OK to save, or Cancel to abort (report still created
   with the template query, which they can edit later).

### 5.5 Drag and Drop

- **Enabled for:** User branch nodes only.
- **Implementation:** `QTreeView` with `setDragEnabled(true)`, `setAcceptDrops(true)`,
  `setDragDropMode(QAbstractItemView::InternalMove)`.
- **Rules:**
  - Reports can be dragged to any User folder (including User root).
  - Folders can be dragged to any User folder (but not into themselves or their descendants).
  - Nothing can be dragged into the System branch.
  - Nothing can be dragged out of the System branch (use "Duplicate to User" instead).
  - Dropping on a report inserts as sibling (same parent folder).
  - Dropping on a folder inserts as child of that folder.
- **Visual feedback:** Standard Qt drop indicator (line between items or highlight on folder).
- **Database update:** On drop, update the `parent_id` and `display_order` of moved items.

### 5.6 Inline Rename

- Triggered by F2 or context menu "Rename" on user items.
- Uses `QTreeView`'s built-in editing via `Qt::ItemIsEditable` flag (set only on user items).
- On commit, validates:
  - Name is not empty.
  - Name is unique among siblings in the same parent folder.
  - Name does not contain characters problematic for file paths: `/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|`.
- Updates the database `name` and `updated_at` fields.

---

## 6. OSCAR Report File Format (.orf)

### 6.1 Format Requirements

The file format must be:

1. **Human-readable** and editable in any text editor (Notepad++, VS Code, etc.).
2. **Simple to parse** in C++ with Qt's text stream classes.
3. **Able to represent** the tree hierarchy (folders nested within folders, reports in folders).
4. **Able to handle multi-line SQL** queries without escaping issues.
5. **Extensible** for future metadata fields.
6. **UTF-8** encoded.

### 6.2 Chosen Format: Heredoc-style with Path Notation

After evaluating JSON (awkward multi-line strings), YAML (indentation-sensitive, needs library),
XML (verbose), and TOML (poor nesting), the best fit is a **custom text format** using
path-based folder declarations and heredoc-delimited SQL blocks.

File extension: `.orf` (OSCAR Report File)

### 6.3 Format Specification

```
# OSCAR Report File
# Format: ORF 1.0
# Exported: 2026-02-12T20:34:12
# Source: System

=== Folder: Daily Summaries ===

=== Report: Daily Summaries/by Day ===
Description: Daily data, one row per day, no aggregation
Query: <<SQL
SELECT
  ds.date as Period,
  ROUND(ds.ahi, 2) as AHI,
  ROUND(ds.rdi, 2) as RDI,
  ds.obstructive_count as OA,
  ds.unclassified_count as UA,
  ds.hypopnea_count as H,
  ds.clear_airway_count as CA,
  ds.rera_count as RERA,
  ROUND(ds.pressure_avg, 2) as Pressure_Avg,
  ROUND(ds.pressure_95th, 2) as Pressure_95th,
  ROUND(ds.leak_total_avg, 2) as Leak_Avg,
  ROUND(ds.leak_total_95th, 2) as Leak_95th,
  ROUND(ds.mask_on_hours, 2) as Hours
FROM daily_summaries ds
WHERE ds.profile_id = #PROFILE_ID
  AND ds.date >= #START_DATE
  AND ds.date <= #END_DATE
ORDER BY ds.date
SQL

=== Report: Daily Summaries/by Week ===
Description: Weekly aggregation of daily data
Query: <<SQL
SELECT
  strftime('%Y-W%W', ds.date) as Period,
  MIN(ds.date) as Week_Start,
  ...
ORDER BY Period
SQL

=== Folder: Other ===

=== Report: Other/Channels Used ===
Description: Channels used by this user
Query: <<SQL
SELECT ...
SQL

=== Folder: Statistics ===

=== Report: Statistics/Respiratory Events ===
Description: Detailed respiratory event data
Query: <<SQL
SELECT ...
SQL
```

### 6.4 Format Rules

1. **Lines starting with `#`** are comments and ignored during parsing.
2. **Header comments** (first few lines) contain metadata:
   - `# Format: ORF 1.0` — format version for forward compatibility.
   - `# Exported: <ISO timestamp>` — when the file was created.
   - `# Source: System|User|Mixed` — origin of the reports.
3. **Blank lines** are ignored (outside of heredoc blocks).
4. **Folder declarations:** `=== Folder: <path> ===`
   - `<path>` is the folder's path relative to the branch root, using `/` as separator.
   - Folders are declared before the reports they contain.
   - Nested folders: `=== Folder: SpO2/my stuff ===` creates both "SpO2" and "my stuff" within it.
   - A folder declaration implicitly creates all parent folders if they don't exist.
5. **Report declarations:** `=== Report: <path/name> ===`
   - Everything before the last `/` is the folder path; the part after is the report name.
   - Reports at the root level: `=== Report: My Report ===` (no path separator).
6. **Description:** `Description: <single line text>`
   - Optional. If omitted, description is empty.
   - Must appear after the Report header and before the Query.
7. **Query block:** Heredoc-style delimited by `Query: <<SQL` and `SQL` on its own line.
   - Everything between the opening `<<SQL` marker and the closing `SQL` line is the query text.
   - The closing `SQL` must be on a line by itself with no leading/trailing whitespace.
   - The SQL text preserves all whitespace, newlines, and comments exactly as written.
   - Macro placeholders (`#PROFILE_ID`, `#START_DATE`, `#END_DATE`) are preserved as-is.

### 6.5 Parsing Algorithm

```
1. Read file line by line (UTF-8).
2. Skip blank lines and comment lines (starting with #) unless inside a heredoc.
3. On "=== Folder: <path> ===" — create folder(s) in the tree model.
4. On "=== Report: <path/name> ===" — begin a new report record.
5. On "Description: <text>" (after report header) — set the report's description.
6. On "Query: <<SQL" — enter heredoc mode; accumulate lines until "SQL" alone.
7. On "SQL" alone (heredoc terminator) — store accumulated text as query; close report.
8. At end of file, if in the middle of a report, warn about malformed file.
```

### 6.6 Export Algorithm

```
1. Collect all selected nodes (reports and folders) from the tree.
2. For each folder, recursively include all descendants.
3. Deduplicate (in case a folder and its child were both selected).
4. Sort by tree path (depth-first) so the file reads top-down.
5. Write header comments with format version, timestamp, and source.
6. For each folder in order, write "=== Folder: <path> ===" line.
7. For each report in order:
   a. Write "=== Report: <path/name> ===" line.
   b. Write "Description: <text>" if description is non-empty.
   c. Write "Query: <<SQL", then the query text, then "SQL".
8. Save file as UTF-8 with Unix line endings (\n) for cross-platform compatibility.
```

### 6.7 Import Algorithm

```
1. Open file dialog to select .orf file.
2. Parse the file using the parsing algorithm above.
3. Determine the target parent folder (the folder the user right-clicked on, or User root).
4. For each folder in the file:
   a. Create it under the target parent, using the path to nest folders.
   b. If a folder with the same name already exists, reuse it (merge).
5. For each report in the file:
   a. Create it in the appropriate folder.
   b. If a report with the same name exists in that folder, ask the user:
      - Skip, Overwrite, or Rename (append " (Imported)").
6. Refresh the tree view to show imported items.
7. Show summary: "Imported X reports and Y folders."
```

### 6.8 System Reports File

The file `oscar/docs/system_reports.orf` contains the entire System branch.
This file is:

- **Shipped with OSCAR** in the source tree and included in releases.
- **Loaded at startup** when OSCAR detects a new version (replacing the current hard-coded
  `initializeSystemReports()` method in `reports_initializer.cpp`).
- **Editable by developers** to add, modify, or reorganize system reports without
  changing C++ code.
- **Listed in `oscar.pro`** under `OTHER_FILES` so it is visible in the IDE.

The startup sequence becomes:
1. Check if OSCAR version has changed since last run.
2. If yes: delete all `source='system'` rows from `report_tree`, then import
   `system_reports.orf` into the System root.
3. User reports (`source='user'`) are never touched during this process.

---

## 7. Class Architecture and New/Modified Files

### 7.1 New Files to Create

| File | Purpose |
|------|---------|
| `reportexporter.h` | Header for the unified ReportExporter dialog class |
| `reportexporter.cpp` | Implementation of the ReportExporter dialog |
| `reportexporter.ui` | Qt Designer form for the ReportExporter dialog layout |
| `reporttreemodel.h` | Header for ReportTreeModel (custom model for report tree) |
| `reporttreemodel.cpp` | Implementation of ReportTreeModel |
| `orf_file_io.h` | Header for ORF file reader/writer utility class |
| `orf_file_io.cpp` | Implementation of ORF file I/O (parse and write .orf files) |
| `database/report_tree_repository.h` | Header for ReportTreeRepository (CRUD for report_tree table) |
| `database/report_tree_repository.cpp` | Implementation of ReportTreeRepository |
| `docs/system_reports.orf` | System reports definition file (replaces hard-coded SQL) |

### 7.2 Files to Modify

| File | Changes |
|------|---------|
| `oscar.pro` | Add new .h, .cpp, .ui files to HEADERS, SOURCES, FORMS. Add system_reports.orf to OTHER_FILES. Remove old reportmanager and reportvarietyeditor entries. |
| `mainwindow.cpp` | Change menu action to launch ReportExporter instead of ExportCSV. Remove ReportManager menu entry. |
| `mainwindow.h` | Update forward declarations and slot signatures. |
| `database/database_schema.cpp` | Add createReportTreeTable() method. Handle migration from old tables. |
| `database/database_schema.h` | Add new method declaration. Bump CURRENT_SCHEMA_VERSION. |
| `database/reports_initializer.cpp` | Rewrite to load from .orf file instead of hard-coded SQL. Use OrfFileIO class. |
| `database/reports_initializer.h` | Update method signatures. |
| `Resources.qrc` | Add any new icon files for tree nodes. |

### 7.3 Files to Retire (Eventually Remove)

These files are superseded by the new design. They can be kept temporarily during
the transition and removed once the new system is fully tested.

| File | Replacement |
|------|-------------|
| `exportcsv.h`, `exportcsv.cpp`, `exportcsv.ui` | `reportexporter.*` |
| `reportmanager.h`, `reportmanager.cpp`, `reportmanager.ui` | `reportexporter.*` |
| `reportvarietyeditor.h`, `reportvarietyeditor.cpp`, `reportvarietyeditor.ui` | No longer needed; functionality absorbed into context menu actions. |
| `database/report_repository.h`, `database/report_repository.cpp` | `database/report_tree_repository.*` |
| `database/report_contents_repository.h`, `database/report_contents_repository.cpp` | `database/report_tree_repository.*` |

### 7.4 Class Descriptions

#### 7.4.1 ReportExporter (QDialog)

The main unified dialog. Responsibilities:

- Owns the `QTreeView` and the `ReportTreeModel`.
- Manages the right-panel export controls (date range, filename, progress bar).
- Handles context menu creation and action dispatch.
- Handles drag-and-drop coordination.
- Performs CSV export (reuses existing CSV writing logic from ExportCSV).
- Performs macro substitution on SQL queries.

Key members:
```cpp
class ReportExporter : public QDialog {
    Q_OBJECT
public:
    explicit ReportExporter(QWidget *parent = nullptr);
    ~ReportExporter();

private slots:
    // Export workflow
    void on_exportButton_clicked();
    void on_editSQLButton_clicked();
    void on_filenameBrowseButton_clicked();
    void on_quickRangeCombo_currentTextChanged(const QString &text);
    void on_cancelButton_clicked();

    // Tree interaction
    void treeSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void treeDoubleClicked(const QModelIndex &index);
    void showTreeContextMenu(const QPoint &pos);

    // Context menu actions
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

    // Calendar
    void startDate_currentPageChanged(int year, int month);
    void endDate_currentPageChanged(int year, int month);

private:
    void loadTree();
    void updateButtonStates();
    void updateStatusLabel();
    QString substituteMacros(const QString &query, qint64 profileId,
                             const QString &startDate, const QString &endDate);
    QString getNodePath(const QModelIndex &index);
    bool isSystemNode(const QModelIndex &index);
    bool isReportNode(const QModelIndex &index);
    bool isFolderNode(const QModelIndex &index);
    qint64 getNodeId(const QModelIndex &index);
    void expandAllChildren(const QModelIndex &parent);
    void collapseAllChildren(const QModelIndex &parent);

    Ui::ReportExporter *ui;
    ReportTreeModel *m_treeModel;
    qint64 m_selectedNodeId;
};
```

#### 7.4.2 ReportTreeModel (QStandardItemModel subclass)

Custom model that knows how to load from and save to the `report_tree` database table.

Key members:
```cpp
class ReportTreeModel : public QStandardItemModel {
    Q_OBJECT
public:
    explicit ReportTreeModel(QObject *parent = nullptr);

    // Data roles for tree items
    enum DataRole {
        NodeIdRole = Qt::UserRole + 1,  // qint64 database ID
        NodeTypeRole,                    // QString: "root", "folder", "report"
        SourceRole,                      // QString: "system", "user"
        DescriptionRole,                 // QString: report description
        QueryRole                        // QString: SQL query template
    };

    // Load entire tree from database
    void loadFromDatabase();

    // Drag and drop support
    Qt::DropActions supportedDropActions() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                      int row, int column, const QModelIndex &parent) override;

    // CRUD helpers that update both model and database
    QModelIndex addFolder(const QModelIndex &parent, const QString &name);
    QModelIndex addReport(const QModelIndex &parent, const QString &name,
                          const QString &description, const QString &query);
    bool removeNode(const QModelIndex &index);
    bool renameNode(const QModelIndex &index, const QString &newName);
    bool updateQuery(const QModelIndex &index, const QString &query);
    bool updateDescription(const QModelIndex &index, const QString &description);
    QModelIndex duplicateNode(const QModelIndex &sourceIndex,
                              const QModelIndex &targetParent);

private:
    void loadChildren(qint64 parentId, QStandardItem *parentItem);
    QStandardItem *createNodeItem(qint64 id, const QString &name,
                                  const QString &nodeType, const QString &source,
                                  const QString &description, const QString &query);
    QIcon iconForNode(const QString &nodeType, const QString &source);
};
```

#### 7.4.3 ReportTreeRepository

Database access layer for the `report_tree` table.

Key members:
```cpp
struct ReportTreeNode {
    qint64 id = 0;
    qint64 parentId = 0;       // 0 means NULL (root nodes)
    QString name;
    QString nodeType;           // "root", "folder", "report"
    QString source;             // "system", "user"
    QString description;
    QString query;
    int displayOrder = 0;
    QDateTime createdAt;
    QDateTime updatedAt;
};

class ReportTreeRepository {
public:
    ReportTreeRepository();

    // CRUD
    qint64 create(const ReportTreeNode &node);
    bool update(const ReportTreeNode &node);
    bool remove(qint64 id);
    ReportTreeNode findById(qint64 id);

    // Queries
    QList<ReportTreeNode> findChildren(qint64 parentId);
    QList<ReportTreeNode> findChildrenOrdered(qint64 parentId);
    QList<ReportTreeNode> findRoots();
    QList<ReportTreeNode> findBySource(const QString &source);
    bool exists(qint64 parentId, const QString &name);

    // Tree operations
    bool moveNode(qint64 nodeId, qint64 newParentId);
    bool updateDisplayOrder(qint64 nodeId, int newOrder);
    int countDescendants(qint64 nodeId);

    // System reports management
    bool deleteSystemNodes();
    qint64 findRootId(const QString &name);  // Find "System" or "User" root

private:
    QSqlDatabase getDatabase();
};
```

#### 7.4.4 OrfFileIO

Utility class for reading and writing .orf files.

Key members:
```cpp
struct OrfReportEntry {
    QString path;           // Full path e.g. "Daily Summaries/by Day"
    QString name;           // Leaf name e.g. "by Day"
    QString description;
    QString query;
    bool isFolder;          // true for folder, false for report
};

class OrfFileIO {
public:
    // Parse an .orf file into a list of entries
    static bool readFile(const QString &filePath,
                         QList<OrfReportEntry> &entries,
                         QString &errorMessage);

    // Write entries to an .orf file
    static bool writeFile(const QString &filePath,
                          const QList<OrfReportEntry> &entries,
                          const QString &source,
                          QString &errorMessage);

    // Import entries into database under a given parent node
    static int importToDatabase(const QList<OrfReportEntry> &entries,
                                qint64 parentNodeId,
                                const QString &source,
                                QString &errorMessage);

    // Export a subtree from database to entry list
    static QList<OrfReportEntry> exportFromDatabase(qint64 rootNodeId);
};
```

---

## 8. Implementation Plan — Phased Approach

The implementation is divided into phases that can each be completed, tested, and
committed independently. Each phase produces a working system.

### Phase 1: Foundation — Database and Repository Layer

**Goal:** New `report_tree` table, repository, and ORF file I/O. No UI changes yet.

**Tasks:**

1. Create `database/report_tree_repository.h` and `.cpp` with full CRUD operations.
2. Add `createReportTreeTable()` to `database_schema.cpp`.
3. Create `orf_file_io.h` and `.cpp` with read/write/import/export methods.
4. Create `docs/system_reports.orf` containing all current system reports
   (extracted from the current `reports_initializer.cpp` hard-coded SQL).
5. Update `reports_initializer.cpp` to load from `.orf` file instead of hard-coded SQL.
6. Write migration logic: if old tables exist and `report_tree` is empty, migrate
   user data from `reports`/`report_contents` into `report_tree` under a "User" root.
7. Add new files to `oscar.pro`.
8. Unit test: Verify ORF round-trip (write then read back), verify database CRUD,
   verify system reports load correctly from file.

**Deliverables:** New database table working, system reports loaded from file,
old user reports migrated. Old UI still works (it reads from old tables which still exist).

**Estimated effort:** 2-3 days.

### Phase 2: Tree Model

**Goal:** `ReportTreeModel` that loads the tree from the database and supports
all the data roles needed by the UI.

**Tasks:**

1. Create `reporttreemodel.h` and `.cpp`.
2. Implement `loadFromDatabase()` — recursive loading of the tree.
3. Implement icon assignment based on node type and source.
4. Implement `flags()` — editable flag only on user items, drag/drop flags.
5. Implement the CRUD helper methods (addFolder, addReport, removeNode, etc.).
6. Implement drag-and-drop MIME handling.
7. Add new files to `oscar.pro`.
8. Unit test: Load tree, verify structure, add/remove/rename nodes, test drag constraints.

**Deliverables:** Fully functional tree model ready for the UI.

**Estimated effort:** 2-3 days.

### Phase 3: Unified Dialog — Basic UI

**Goal:** `ReportExporter` dialog with tree view and export controls. No context menu
or drag-and-drop yet; just basic selection and CSV export.

**Tasks:**

1. Create `reportexporter.ui` in Qt Designer with the layout from Section 4.2:
   - QSplitter with QTreeView on left, QFormLayout on right.
   - Date range controls, filename controls, progress bar, buttons.
2. Create `reportexporter.h` and `.cpp`:
   - Constructor loads tree model, sets up date range, connects signals.
   - Single-click selects report, updates status label and button states.
   - Export CSV button runs query with macro substitution and writes CSV.
   - Edit SQL button opens SQLEditor (read-only for system, editable for user).
   - Cancel button closes dialog.
3. Port the CSV export logic from `exportcsv.cpp` (substituteMacros, query execution,
   CSV writing, progress bar updates).
4. Port the calendar color-coding logic from `exportcsv.cpp`.
5. Update `mainwindow.cpp` to launch `ReportExporter` instead of `ExportCSV`.
6. Add new files to `oscar.pro`.
7. Manual test: Open dialog, browse tree, select reports, export CSV, verify output.

**Deliverables:** Working replacement for ExportCSV with tree-based selection.
Old ExportCSV and ReportManager dialogs still present in code but no longer launched.

**Estimated effort:** 3-4 days.

### Phase 4: Context Menu and Tree Operations

**Goal:** Full right-click context menu with all operations from Section 5.1.

**Tasks:**

1. Implement `showTreeContextMenu()` — build menu dynamically based on selected node(s).
2. Implement each context menu action:
   - `onNewReport()` — name dialog, then SQL editor.
   - `onNewFolder()` — name dialog, create in tree and database.
   - `onEditQuery()` — open SQL editor in edit mode, save on OK.
   - `onViewSubstitutedQuery()` — open SQL editor read-only with macros replaced.
   - `onRenameItem()` — trigger inline editing on tree.
   - `onDeleteItem()` — confirm dialog, delete from tree and database.
   - `onDuplicateItem()` — copy node in same folder.
   - `onDuplicateToUser()` — copy system node(s) to User branch.
   - `onShowDescription()` — QMessageBox or tooltip.
   - `onEditDescription()` — QInputDialog.
   - `onExportSelected()` — file dialog, call OrfFileIO::writeFile().
   - `onImportReports()` — file dialog, call OrfFileIO::readFile() and importToDatabase().
   - Expand All / Collapse All — recursive expand/collapse.
3. Implement inline rename via `QStyledItemDelegate` with validation.
4. Implement keyboard shortcuts (Delete, F2, Ctrl+D).
5. Manual test: Every context menu item on every node type.

**Deliverables:** Full tree management functionality.

**Estimated effort:** 3-4 days.

### Phase 5: Drag-and-Drop

**Goal:** Drag-and-drop within the User branch.

**Tasks:**

1. Enable drag-and-drop on QTreeView.
2. Implement MIME encoding/decoding in ReportTreeModel.
3. Implement drop validation (reject drops on System branch, reject self-drops).
4. On successful drop, update `parent_id` and `display_order` in database.
5. Ensure visual feedback (highlight, insertion line) works correctly.
6. Manual test: Drag reports between folders, drag folders, verify constraints.

**Deliverables:** Drag-and-drop working for User branch.

**Estimated effort:** 1-2 days.

### Phase 6: Cleanup and Polish

**Goal:** Remove old code, final polish, documentation.

**Tasks:**

1. Remove old files from `oscar.pro`: `exportcsv.*`, `reportmanager.*`,
   `reportvarietyeditor.*`, `report_repository.*`, `report_contents_repository.*`.
2. Delete the retired source files.
3. Remove old database tables (`reports`, `report_contents`) from schema if migration
   is confirmed successful (or keep as backup with a deprecation comment).
4. Remove old menu entries and any remaining references in `mainwindow.cpp`.
5. Add Doxygen comments to all new files.
6. Update `mainwindow.ui` if menu items changed.
7. Test full workflow end-to-end:
   - Fresh install: system reports load from .orf file.
   - Upgrade: old user reports migrate correctly.
   - All tree operations work.
   - CSV export produces correct output.
   - Import/export .orf files round-trip correctly.
8. Update this design document with any deviations from the plan.

**Deliverables:** Clean codebase with no legacy report code.

**Estimated effort:** 1-2 days.

### Total Estimated Effort: 12-18 days

---

## 9. Key Design Decisions Summary

| Decision | Rationale |
|----------|-----------|
| Single `report_tree` table instead of two tables | Simpler model, natural tree hierarchy, no confusing "variety" concept |
| Tree with System/User roots | Clear separation of protected vs. editable content; familiar pattern (e.g., bookmarks in browsers) |
| ORF custom text format (not JSON/YAML/XML) | Best balance of human readability, multi-line SQL support, and parsing simplicity; editable in Notepad++ |
| Heredoc `<<SQL ... SQL` for queries | Clean multi-line support without escaping; familiar pattern from shell scripting |
| Path notation in ORF files | Unambiguous folder hierarchy without indentation sensitivity |
| System reports in external .orf file | Developers can modify reports without recompiling; version-controlled; easy to review in MRs |
| QStandardItemModel subclass (not QAbstractItemModel) | Simpler implementation; QStandardItem provides built-in icon, text, drag support |
| Context menu varies by node type | Clean UX — user only sees actions that apply; no confusing grayed-out items |
| SQLEditor reused as-is | Existing SQL editor already supports read-only and edit modes; no need to rewrite |
| Phased implementation | Each phase is independently testable and committable; reduces risk |

---

## 10. Open Questions

1. **Icon assets:** Do we need new icon files, or can we use Qt standard icons plus
   existing OSCAR icons? Need to inventory `:/icons/` resources.
   ==> Use QT standard icons for now.
2. **Multi-report CSV export:** Should selecting multiple reports export each to a
   separate CSV file, or all into one file with report name headers? Current design
   assumes single-report export. Multi-report batch export could be a future enhancement.
   ==> Future enhancement.
3. **Profiles table query:** The `Profiles` and `Profiles with data` reports mentioned
   in the user's example tree aren't currently in the system reports. These should be
   written and added to `system_reports.orf`.
   ==> Will do.
4. **Undo support:** Should tree operations (delete, move, rename) support Ctrl+Z undo?
   This would add complexity. Recommendation: defer to a future version; confirmation
   dialogs provide sufficient safety net.
   ==> Future enhancement
5. **Tree state persistence:** Should the expand/collapse state of the tree be saved
   between dialog opens? If so, store in QSettings. Recommendation: yes, save state.
   ==> Yes, save the state of the tree.

---

## Appendix A: Macro Reference

Macros available in SQL query templates:

| Macro | Replaced With | Example |
|-------|---------------|---------|
| `#PROFILE_ID` | Current profile's database ID (integer) | `42` |
| `#START_DATE` | Start date in ISO format, quoted | `'2026-01-15'` |
| `#END_DATE` | End date in ISO format, quoted | `'2026-02-12'` |

Future macros (not yet implemented but reserved):

| Macro | Description |
|-------|-------------|
| `#PROFILE_NAME` | Current profile's username |
| `#YEAR` | Current year |
| `#TODAY` | Today's date in ISO format |

---

## Appendix B: Git Branch Strategy

- Create feature branch: `feature/report-tree-ui`
- Each phase = one or more merge requests
- Phase 1-2 can be merged without affecting existing UI (backend only)
- Phase 3 is the breaking change (swaps dialog)
- Squash-merge or rebase for clean history

---

*End of design document.*
