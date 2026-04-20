/* Restore Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog for restoring a user profile from a .oscar backup package.
 * Wraps ProfileRestore with a UI providing file selection (local or cloud
 * URL), automatic validation, manifest information display, conflict
 * resolution options and live progress reporting.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RESTOREDIALOG_H
#define RESTOREDIALOG_H

#include <QDialog>
#include <QFutureWatcher>

class BackupManifest;
class CloudDownloader;
class ProfileRestore;

namespace Ui {
class RestoreDialog;
}

/*!
 * \class RestoreDialog
 * \brief Modal dialog for restoring a profile from a .oscar backup package.
 *
 * The user browses for a local \c .oscar file or pastes a cloud share link,
 * clicks Validate to inspect the package, optionally adjusts conflict-
 * resolution settings, then clicks Restore.  Progress is shown in a
 * QProgressBar while ProfileRestore runs.
 *
 * Cloud share links from Dropbox, Google Drive, OneDrive, Box, and 0x0.st
 * are recognized and downloaded automatically.  Unsupported URLs prompt the
 * user to download the file manually.
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

    /*! \brief Re-check conflicts and update the UI when the profile name changes. */
    void on_profileNameEdit_textChanged(const QString& text);

    /*! \brief Toggle UI between local-file and cloud-URL modes. */
    void on_sourceLocalRadio_toggled(bool checked);

    /*! \brief Start downloading the .oscar file from the entered URL. */
    void on_downloadButton_clicked();

    /*! \brief Update the provider hint label as the user types a URL. */
    void onUrlTextChanged(const QString& text);

    /*! \brief Update the progress bar during download. */
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

    /*! \brief Handle successful download: set path and auto-validate. */
    void onDownloadFinished(const QString& localPath);

    /*! \brief Handle download failure: show error and re-enable controls. */
    void onDownloadFailed(const QString& error);

    /*! \brief Handle completion of the background validatePackage() call. */
    void onValidationFinished();

    /*! \brief Update the progress bar and status label during restore. */
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

    /*! \brief Show or hide the restore-options (profile name) group box. */
    void showNameGroup(bool visible);

    /*! \brief Show or hide the conflict-resolution group box. */
    void showConflictGroup(bool visible);

    /*!
     * \brief Re-evaluate whether \a name conflicts with an existing profile
     *        and update the conflict group visibility and Restore button state.
     * \param name  The profile name currently entered by the user.
     */
    void updateConflictForName(const QString& name);

    /*!
     * \brief Enable or disable interactive controls while a restore runs.
     * \param busy  true while the restore is in progress.
     */
    void setBusy(bool busy);

    /*! \brief Reset validation state (info/name/conflict groups, restore button). */
    void resetValidation();

    /*!
     * \brief Recompute and set the Restore button enabled state.
     *
     * The button is enabled only when: a validated package is loaded
     * (\a m_restore is non-null), the profile name is non-empty, and if a
     * conflict was detected the user has chosen Rename or Replace (not Abort).
     */
    void updateRestoreButtonState();

    /*!
     * \brief Rebuild the status-label content from current dialog state.
     *
     * Shows the normal conflict/validation message, and appends a red
     * warning when Replace is selected and both the incoming package and
     * the existing profile contain SD card data.
     */
    void updateStatusLabel();

    /*! \brief Persist the last-used package directory to QSettings. */
    void saveSettings();

    /*! \brief Restore the last-used package directory from QSettings. */
    void restoreSettings();

    Ui::RestoreDialog*      ui;
    ProfileRestore*         m_restore          = nullptr; ///< Heap-allocated; owned by this dialog.
    CloudDownloader*        m_downloader       = nullptr; ///< Heap-allocated; owned by this dialog.
    QFutureWatcher<bool>*   m_validateWatcher  = nullptr; ///< Tracks the background validation future.
    QString             m_lastPackageDir;         ///< Last directory used to browse for a package.
    bool                m_packageIsShare    = false; ///< True if filename begins with "share_".
    bool                m_backupHasSD       = false; ///< True if the package includes SD card data.
    bool                m_existingHasSD     = false; ///< True if the target profile has a Backup dir.
    bool                m_restoreInProgress = false; ///< True while restoreProfile() is running.
    bool                m_cancelRequested   = false; ///< True after user clicks Cancel during restore.
};

#endif // RESTOREDIALOG_H
