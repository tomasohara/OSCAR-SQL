/* System Report Translation Strings
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file exists solely so that lupdate can extract system report
 * names and descriptions for translation via Qt Linguist. It produces
 * no runtime code. Keep in sync with docs/system_reports.orf.
 *
 * Usage:
 *   - Every folder name, report name, and description that appears in
 *     system_reports.orf must have a QT_TRANSLATE_NOOP entry here.
 *   - At display time, report_tree_model.cpp translates these strings
 *     using QCoreApplication::translate("SystemReports", ...).
 *   - User-created reports are NOT translated (displayed as-is).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>

// ---- Root node names ----
static const char* const roots[] = {
    QT_TRANSLATE_NOOP("SystemReports", "System"),
    QT_TRANSLATE_NOOP("SystemReports", "User"),
};

// ---- Folder names ----
static const char* const folders[] = {
    QT_TRANSLATE_NOOP("SystemReports", "Daily Summaries"),
    QT_TRANSLATE_NOOP("SystemReports", "Session Summaries"),
    QT_TRANSLATE_NOOP("SystemReports", "Other"),
    QT_TRANSLATE_NOOP("SystemReports", "Statistics"),
};

// ---- Report names ----
static const char* const names[] = {
    QT_TRANSLATE_NOOP("SystemReports", "by Day"),
    QT_TRANSLATE_NOOP("SystemReports", "by Week"),
    QT_TRANSLATE_NOOP("SystemReports", "by Month"),
    QT_TRANSLATE_NOOP("SystemReports", "by Session"),
    QT_TRANSLATE_NOOP("SystemReports", "Channels Used"),
    QT_TRANSLATE_NOOP("SystemReports", "Device Settings"),
    QT_TRANSLATE_NOOP("SystemReports", "Profiles"),
    QT_TRANSLATE_NOOP("SystemReports", "Profiles with Data"),
    QT_TRANSLATE_NOOP("SystemReports", "Respiratory Events"),
    QT_TRANSLATE_NOOP("SystemReports", "Sessions"),
};

// ---- Report descriptions ----
static const char* const descriptions[] = {
    QT_TRANSLATE_NOOP("SystemReports", "Daily data, one row per day, no aggregation"),
    QT_TRANSLATE_NOOP("SystemReports", "Weekly aggregation of daily data"),
    QT_TRANSLATE_NOOP("SystemReports", "Monthly aggregation of daily data"),
    QT_TRANSLATE_NOOP("SystemReports", "Individual sessions, one row per session, no aggregation"),
    QT_TRANSLATE_NOOP("SystemReports", "Daily aggregation of session summaries"),
    QT_TRANSLATE_NOOP("SystemReports", "Weekly aggregation of session summaries"),
    QT_TRANSLATE_NOOP("SystemReports", "Monthly aggregation of session summaries"),
    QT_TRANSLATE_NOOP("SystemReports", "Channels used by this user's profile"),
    QT_TRANSLATE_NOOP("SystemReports", "Machine configuration settings for all sessions"),
    QT_TRANSLATE_NOOP("SystemReports", "All profiles in the database"),
    QT_TRANSLATE_NOOP("SystemReports", "Profiles that have imported CPAP data with date ranges"),
    QT_TRANSLATE_NOOP("SystemReports", "Individual respiratory events with timestamps (AHI-contributing only)"),
    QT_TRANSLATE_NOOP("SystemReports", "Detailed per-session channel statistics (selected respiratory channels)"),
};
