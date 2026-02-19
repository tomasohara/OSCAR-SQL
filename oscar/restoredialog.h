/* Restore Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog for restoring a user profile from a .oscar backup package.
 * Wraps ProfileRestore with a UI providing file selection, automatic
 * validation, manifest information display, conflict resolution options
 * and live progress reporting.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RESTOREDIALOG_H
#define RESTOREDIALOG_H

#include <QDialog>

class BackupManifest;
class ProfileRestore;

namespace Ui {
class RestoreDialog;
}

/*!
 * \class RestoreDialog
 * \brief Modal dialog for restoring a profile from a .oscar backup package.
 *
 * The user browses for a \c .oscar file, clicks Validate to inspect the
 * package, optionally adjusts conflict-resolution settings, then clicks
 * Restore.  Progress is shown in a QProgressBar while ProfileRestore runs.
 *
 * Typical usage:
 * \code
 * RestoreDialog dialog(this);
 * dialog.exec();
 * \endcode
 */
class RestoreDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RestoreDialog(QWidget* parent = nullptr);
    ~RestoreDialog() override;

private slots:
    /*! \brief Open a file-picker filtered to *.oscar files. */
    void on_browseButton_clicked();

    /*! \brief Validate the selected package and populate the info group. */
    void on_validateButton_clicked();

    /*! \brief Launch ProfileRestore::restoreProfile(). */
    void on_restoreButton_clicked();

    /*! \brief Update the progress bar and status label. */
    void onProgressChanged(int percent, const QString& message);

    /*! \brief Show a success message, re-enable the UI, and accept the dialog. */
    void onRestoreCompleted(qint64 profileId, const QString& username);

    /*! \brief Show an error message and re-enable the UI. */
    void onRestoreFailed(const QString& error);

private:
    /*!
     * \brief Populate the info group box with the data from a loaded manifest.
     * \param manifest  Validated BackupManifest object.
     */
    void displayPackageInfo(const BackupManifest& manifest);

    /*! \brief Show or hide the package-information group box. */
    void showInfoGroup(bool visible);

    /*! \brief Show or hide the conflict-resolution group box. */
    void showConflictGroup(bool visible);

    /*!
     * \brief Enable or disable interactive controls while a restore runs.
     * \param busy  true while the restore is in progress.
     */
    void setBusy(bool busy);

    Ui::RestoreDialog*  ui;
    ProfileRestore*     m_restore = nullptr;  ///< Heap-allocated; owned by this dialog.
};

#endif // RESTOREDIALOG_H
