/* Profile Backup Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ProfileBackup class which orchestrates the export of
 * a single OSCAR user profile to a self-contained .oscar backup package.
 *
 * A .oscar package is a ZIP archive containing:
 *   manifest.json           — package metadata (format/schema version,
 *                             profile identity, statistics, checksums)
 *   database/               — SQL export files, one per table
 *
 * The export supports:
 *   - Full profile export (all sessions)
 *   - Partial (date-range) export — only sessions in [startDate, endDate]
 *   - Privacy mode — personal data fields in user_info / doctor_info blanked
 *   - Optional inclusion of disabled sessions
 *
 * Implementation phases:
 *   Phase 1 — Infrastructure (this file; stub .cpp provided)
 *   Phase 2 — Full implementation of createBackup() and all helpers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PROFILE_BACKUP_H
#define PROFILE_BACKUP_H

#include <atomic>

#include <QDate>
#include <QJsonObject>
#include <QObject>
#include <QString>

/*!
 * \class ProfileBackup
 * \brief Backs up a single OSCAR user profile to a .oscar package file.
 *
 * ### Basic usage
 * \code
 * ProfileBackup backup(profileId);
 * backup.setOutputPath("C:/Backups/OSCAR/");
 * backup.setCompression(true);
 *
 * connect(&backup, &ProfileBackup::progressChanged,
 *         [](int pct, const QString& msg) { qDebug() << pct << msg; });
 *
 * if (backup.createBackup()) {
 *     qDebug() << "Backup written to:" << backup.getBackupPath();
 * } else {
 *     qCritical() << backup.getErrorMessage();
 * }
 * \endcode
 *
 * ### Partial export with privacy mode
 * \code
 * ProfileBackup backup(profileId);
 * backup.setOutputPath("C:/Backups/OSCAR/");
 * backup.setDateRange(QDate(2025, 7, 1), QDate::currentDate());
 * backup.setPrivacyMode(true);
 * backup.createBackup();
 * \endcode
 *
 * ### Output filename convention
 * - Full export:    \c profile_backup_<username>_<timestamp>.oscar
 * - Partial export: \c profile_backup_<username>_<startdate>_<enddate>_<timestamp>.oscar
 *
 * ### Thread notes
 * ProfileBackup emits signals, so it should be used on a thread that has an
 * event loop, or signals connected with Qt::QueuedConnection.
 */
class ProfileBackup : public QObject
{
    Q_OBJECT

public:
    /*!
     * \brief Construct a ProfileBackup for the given profile.
     * \param profileId  Database primary key of the profile to back up.
     * \param parent     Optional Qt parent object.
     */
    explicit ProfileBackup(qint64 profileId, QObject* parent = nullptr);

    /*!
     * \brief Destructor.  Cleans up any temporary files left behind.
     */
    ~ProfileBackup() override;

    // -----------------------------------------------------------------------
    //  Configuration (call before createBackup())
    // -----------------------------------------------------------------------

    /*!
     * \brief Set the directory where the finished .oscar file will be saved.
     *
     * The directory must exist and be writable.  If not called, the user's
     * Documents folder is used as a fallback.
     *
     * \param path  Absolute path to the output directory.
     */
    void setOutputPath(const QString& path);

    /*!
     * \brief Control whether disabled sessions are included in the export.
     *
     * Default: true (disabled sessions are exported).
     *
     * \param include  true to include disabled sessions.
     */
    void setIncludeDisabledSessions(bool include);

    /*!
     * \brief Enable or disable ZIP compression of the .oscar package.
     *
     * Default: true (compression on).  Disable only for debugging.
     *
     * \param compress  true to compress.
     */
    void setCompression(bool compress);

    /*!
     * \brief Set a date range for a partial export.
     *
     * When both dates are valid, only sessions whose \c start_time falls
     * within \c [startDate, endDate] (inclusive) are exported.  The
     * daily_summaries table is filtered by its \c date column.
     *
     * Profile-level tables (profiles, user_info, doctor_info,
     * profile_preferences, channels, machines) are always exported in full
     * regardless of the date range.
     *
     * Pass two invalid (default-constructed) QDate objects, or call
     * \c setDateRange(QDate(), QDate()), to revert to a full export.
     *
     * \param startDate  First session date to include (inclusive).
     * \param endDate    Last session date to include (inclusive).
     */
    void setDateRange(const QDate& startDate, const QDate& endDate);

    /*!
     * \brief Enable or disable privacy mode.
     *
     * When privacy mode is enabled, all personal data fields in
     * \c user_info and \c doctor_info are replaced with empty strings /
     * NULL before export.  The \c id and \c profile_id columns are preserved
     * so the restore phase can insert the rows correctly.
     *
     * Fields blanked in \c user_info: first_name, last_name, dob, email,
     *   phone, address, country, height, gender.
     * Fields blanked in \c doctor_info: name, phone, email, practice_name,
     *   address, patient_id.
     *
     * The manifest records \c privacy_applied: true so the recipient knows
     * personal data was removed.
     *
     * Default: false (full data exported).
     *
     * \param enable  true to blank personal data fields.
     */
    void setPrivacyMode(bool enable);

    /*!
     * \brief Enable or disable inclusion of the profile's on-disk SD card data.
     *
     * When enabled, the entire \c Profiles/<username>/ directory tree is added
     * to the .oscar package under the \c sddata/ prefix.  This is only
     * meaningful for a full ("Everything") export — it is automatically set
     * by the Backup dialog when the user selects that option.
     *
     * Default: false (database-only export).
     *
     * \param include  true to include the SD card data directory.
     */
    void setIncludeSDData(bool include);

    /*!
     * \brief Override the auto-generated output filename.
     *
     * When set (non-empty), \c createBackup() uses this filename (combined with
     * the output path) instead of calling \c generateBackupPath().  This allows
     * the dialog to pass the user-edited filename directly.
     *
     * \param filename  Bare filename (no directory), e.g. \c "profile_p3_20260116_7.oscar".
     *                  Pass an empty string to revert to auto-generation.
     */
    void setFilename(const QString& filename);

    // -----------------------------------------------------------------------
    //  Execution
    // -----------------------------------------------------------------------

    /*!
     * \brief Run the backup and create the .oscar package file.
     *
     * This is the main entry point.  The method:
     *   1. Validates the profile and output directory.
     *   2. Creates a temporary working directory.
     *   3. Exports all required tables to SQL files.
     *   4. Writes manifest.json.
     *   5. Packages everything into a .oscar ZIP archive.
     *   6. Emits backupCompleted() or backupFailed() on finish.
     *
     * Progress is reported via progressChanged() as the operation proceeds.
     *
     * \return true on success; false on failure (call getErrorMessage()).
     *
     * \note Implemented in Phase 2.
     */
    bool createBackup();

    /*!
     * \brief Request cancellation of a running createBackup() call.
     *
     * Thread-safe.  The backup checks this flag between major phases and
     * emits backupFailed() with "Operation cancelled." at the next checkpoint.
     */
    void requestCancel();

    // -----------------------------------------------------------------------
    //  Status / result accessors
    // -----------------------------------------------------------------------

    /*!
     * \brief Return the human-readable description of the last error.
     */
    QString getErrorMessage() const;

    /*!
     * \brief Return the absolute path of the finished .oscar file.
     *
     * Only valid after a successful createBackup() call.
     */
    QString getBackupPath() const;

    /*!
     * \brief Return the compressed size of the finished .oscar file in bytes.
     *
     * Only valid after a successful createBackup() call.
     */
    qint64 getBackupSize() const;

    /*!
     * \brief Return the uncompressed size of the export data in bytes.
     *
     * Only valid after a successful createBackup() call.
     */
    qint64 getUncompressedSize() const;

    /*!
     * \brief Return true if the export was date-range filtered (partial).
     */
    bool isPartialExport() const;

signals:
    /*!
     * \brief Emitted periodically during createBackup() to report progress.
     * \param percent  Completion percentage (0–100).
     * \param message  Human-readable description of the current step.
     */
    void progressChanged(int percent, const QString& message);

    /*!
     * \brief Emitted when createBackup() completes successfully.
     * \param path  Absolute path to the finished .oscar file.
     */
    void backupCompleted(const QString& path);

    /*!
     * \brief Emitted when createBackup() fails.
     * \param error  Human-readable error description.
     */
    void backupFailed(const QString& error);

private:
    // -----------------------------------------------------------------------
    //  Private helpers (implemented in Phase 2)
    // -----------------------------------------------------------------------

    /*!
     * \brief Verify the profile exists, the database is open, and the output
     *        directory is writable.
     */
    bool validateProfile();

    /*!
     * \brief Export profile-level tables (always full, not date-filtered).
     *
     * Exports: profiles, user_info, doctor_info, profile_preferences, channels.
     * When m_privacyMode is true, personal fields in user_info and doctor_info
     * are replaced with empty strings / NULL before export.
     *
     * \param tempDir  Path to the temporary working directory.
     */
    bool exportProfileMetadata(const QString& tempDir);

    /*!
     * \brief Export machines and all session sub-tables.
     *
     * Machines are always exported in full.  Sessions are date-filtered when
     * m_startDate / m_endDate are set.  All session sub-tables
     * (session_settings, session_channels, session_channel_values,
     * respiratory_events, session_summaries, session_slices, event_lists,
     * event_data) are exported via the session_id filter.
     *
     * \param tempDir  Path to the temporary working directory.
     */
    bool exportMachinesAndSessions(const QString& tempDir);

    /*!
     * \brief Export the daily_summaries table (date-filtered when applicable).
     *
     * \param tempDir  Path to the temporary working directory.
     */
    bool exportDailySummaries(const QString& tempDir);

    /*!
     * \brief Build and save the manifest.json file.
     *
     * \param tempDir  Path to the temporary working directory.
     * \param manifest Output parameter populated with the manifest data
     *                 (used by createPackage() for checksum recording).
     */
    bool createManifest(const QString& tempDir, QJsonObject& manifest);

    /*!
     * \brief ZIP all export files into the final .oscar package.
     *
     * \param tempDir  Path to the temporary working directory.
     */
    bool createPackage(const QString& tempDir);

    /*!
     * \brief Generate the output .oscar file path from profile username,
     *        date range, and timestamp.
     *
     * Full export:    \c <outputPath>/profile_backup_<username>_<ts>.oscar
     * Partial export: \c <outputPath>/profile_backup_<username>_<s>_<e>_<ts>.oscar
     */
    QString generateBackupPath() const;

    /*!
     * \brief Process pending Qt events and return true if cancellation was requested.
     *
     * Sets m_errorMessage to "Operation cancelled." before returning true so
     * callers can simply \c return false after a positive result.
     */
    bool checkCancelled();

    /*!
     * \brief Calculate the SHA-256 hex digest of a file.
     *
     * \param filePath  Absolute path to the file to hash.
     * \return Lowercase hex string prefixed with "sha256:", or empty on error.
     */
    QString calculateChecksum(const QString& filePath) const;

    /*!
     * \brief Build the SQL subquery used to filter sessions by date range.
     *
     * When m_startDate and/or m_endDate are valid, returns a WHERE clause
     * fragment such as:
     *   \c start_time >= 1735689600 AND start_time <= 1767225600
     *
     * Returns an empty string for a full (un-filtered) export.
     */
    QString buildSessionDateFilter() const;

    /*!
     * \brief Resolve the profile's on-disk data directory path.
     *
     * Queries the profile username from the database and returns the canonical
     * \c Profiles/<username> path, using the same \c {home}/Profiles expression
     * as \c Profiles::Scan() so the path is always consistent.
     *
     * \return Absolute path, or an empty string on error.
     */
    QString getProfileDataDir() const;

    // -----------------------------------------------------------------------
    //  Member variables
    // -----------------------------------------------------------------------

    qint64  m_profileId;          ///< Profile database primary key.
    QString m_outputPath;         ///< Destination directory for .oscar file.
    QString m_overrideFilename;   ///< If non-empty, used instead of auto-generated filename.
    QString m_errorMessage;       ///< Last error description.
    QString m_backupPath;         ///< Absolute path of the finished .oscar file.
    qint64  m_backupSize      = 0;///< Compressed .oscar file size in bytes.
    qint64  m_uncompressedSize = 0;///< Uncompressed export data size in bytes.
    bool    m_includeDisabled = true; ///< Include disabled sessions.
    bool    m_compress        = true; ///< Compress the .oscar package.
    bool    m_privacyMode     = false;///< Blank personal data fields.
    bool    m_includeSDData   = false;///< Include profile's on-disk SD card data.
    QDate   m_startDate;          ///< Export start date (invalid = no filter).
    QDate   m_endDate;            ///< Export end date (invalid = no filter).
    QString m_profileDataDir;     ///< Resolved Profiles/<username> path (set in createBackup).
    std::atomic<bool> m_cancelRequested{false}; ///< Set by requestCancel(); checked between phases.
};

#endif // PROFILE_BACKUP_H
