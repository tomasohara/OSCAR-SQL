/* Share Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog for preparing a .oscar profile package optimized for
 * sharing with another user.  Wraps ProfileBackup with a UI that
 * defaults to privacy mode and simplified filenames, excludes SD card
 * data, and provides next-steps guidance for the user.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SHAREDIALOG_H
#define SHAREDIALOG_H

#include <QDate>
#include <QDialog>

namespace Ui {
class ShareDialog;
}

/*!
 * \class ShareDialog
 * \brief Modal dialog for creating a .oscar profile package for sharing.
 *
 * Similar to BackupDialog but optimized for the sharing use case:
 * privacy mode and simplified filenames are always on, SD card data
 * is never included, and the default date range is "Last Week".
 *
 * Typical usage:
 * \code
 * ShareDialog dialog(this);
 * dialog.exec();
 * \endcode
 */
class ShareDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShareDialog(QWidget* parent = nullptr);
    ~ShareDialog() override;

private slots:
    /*! \brief Refresh the filename preview when the selected profile changes. */
    void on_profileCombo_currentIndexChanged(int index);

    /*! \brief Adjust From/To date edits and preview when the range preset changes. */
    void on_rangeCombo_currentTextChanged(const QString& text);

    /*! \brief Open a directory-picker and populate outputDirEdit. */
    void on_browseButton_clicked();

    /*! \brief Show sharing warning then launch ProfileBackup::createBackup(). */
    void on_shareButton_clicked();

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

    /*! \brief Rebuild filenameEdit based on the current profile and date range. */
    void updateFilenamePreview();

    /*! \brief Apply locale-aware formatting and remove weekend red-highlight from calendar widgets. */
    void setupCalendarFormatting();

    /*! \brief Persist the current output directory to QSettings. */
    void saveSettings();

    /*! \brief Restore the last-used output directory from QSettings. */
    void restoreSettings();

    /*!
     * \brief Query the earliest session date for the currently selected profile.
     * \return The first date on which a session exists, or today as a fallback.
     */
    QDate getFirstDataDate() const;

    /*!
     * \brief Query the most recent session date for the currently selected profile.
     * \return The last date on which a session exists, or today as a fallback.
     */
    QDate getLastDataDate() const;

    /*!
     * \brief Show a modal sharing-warning dialog before creating the file.
     * \return true if the user confirmed and clicked Continue.
     */
    bool showSharingWarning();

    Ui::ShareDialog* ui;
    QList<qint64>    m_profileIds;    ///< DB IDs parallel to profileCombo entries.
};

#endif // SHAREDIALOG_H
