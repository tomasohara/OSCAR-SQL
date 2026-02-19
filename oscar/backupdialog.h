/* Backup Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog for creating a .oscar profile backup package.  Wraps
 * ProfileBackup with a UI providing profile selection, date-range
 * filtering, privacy mode, output directory selection and live progress
 * reporting.  A security-warning confirmation is shown before any backup
 * starts.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BACKUPDIALOG_H
#define BACKUPDIALOG_H

#include <QDate>
#include <QDialog>

namespace Ui {
class BackupDialog;
}

/*!
 * \class BackupDialog
 * \brief Modal dialog for creating a .oscar profile backup.
 *
 * Presents profile selection, date-range and privacy controls, output
 * directory selection, and a progress bar that tracks the backup as it
 * runs.  The user must confirm a security-warning dialog before the
 * backup begins.
 *
 * Typical usage:
 * \code
 * BackupDialog dialog(this);
 * dialog.exec();
 * \endcode
 */
class BackupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BackupDialog(QWidget* parent = nullptr);
    ~BackupDialog() override;

private slots:
    /*! \brief Refresh the filename preview when the selected profile changes. */
    void on_profileCombo_currentIndexChanged(int index);

    /*! \brief Adjust From/To date edits and preview when the range preset changes. */
    void on_rangeCombo_currentTextChanged(const QString& text);

    /*! \brief Open a directory-picker and populate outputDirEdit. */
    void on_browseButton_clicked();

    /*! \brief Show security warning then launch ProfileBackup::createBackup(). */
    void on_backupButton_clicked();

    /*! \brief Update the progress bar and status label. */
    void onProgressChanged(int percent, const QString& message);

    /*! \brief Show a success message and re-enable the UI. */
    void onBackupCompleted(const QString& path);

    /*! \brief Show an error message and re-enable the UI. */
    void onBackupFailed(const QString& error);

private:
    /*! \brief Populate profileCombo from the database, pre-selecting the current profile. */
    void populateProfiles();

    /*!
     * \brief Set fromDate / toDate from a named range preset.
     * \param rangeText  One of the rangeCombo item strings.
     */
    void applyDateRange(const QString& rangeText);

    /*! \brief Rebuild filenameLabel based on the current profile and date range. */
    void updateFilenamePreview();

    /*! \brief Apply locale-aware formatting and remove weekend red-highlight from calendar widgets. */
    void setupCalendarFormatting();

    /*!
     * \brief Show a modal security-warning dialog before backup starts.
     * \return true if the user ticked the confirmation checkbox and clicked Continue.
     */
    bool showSecurityWarning();

    Ui::BackupDialog* ui;
    QList<qint64>     m_profileIds;  ///< DB IDs parallel to profileCombo entries.
};

#endif // BACKUPDIALOG_H
