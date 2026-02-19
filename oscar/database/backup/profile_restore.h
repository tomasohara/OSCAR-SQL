/* Profile Restore Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ProfileRestore class which imports a .oscar backup
 * package into the current OSCAR database, together with the ConflictStatus
 * and ConflictResolution enumerations used during conflict handling.
 *
 * A restore operation:
 *   1. Validates the .oscar package (checksums, manifest structure).
 *   2. Checks schema version compatibility (exact match required for v13+).
 *   3. Detects username conflicts with existing profiles.
 *   4. Resolves conflicts according to the configured strategy.
 *   5. Remaps all auto-increment IDs to avoid collisions with existing rows.
 *   6. Executes the restore inside a single database transaction (atomic).
 *   7. Validates the restored data against the manifest statistics.
 *
 * Implementation phases:
 *   Phase 1 — Infrastructure (this file; stub .cpp provided)
 *   Phase 3 — Full implementation of restoreProfile() and all helpers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PROFILE_RESTORE_H
#define PROFILE_RESTORE_H

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>

/*!
 * \enum ConflictResolution
 * \brief Strategy to apply when the backup username already exists in the DB.
 */
enum class ConflictResolution {
    Abort,  ///< Stop the restore immediately without making any changes.
    Rename, ///< Import under a new unique name (e.g. JohnDoe_restored_<ts>).
    Replace ///< Overwrite the existing profile. DANGEROUS — requires explicit
            ///< confirmation from the user.
};

/*!
 * \enum ConflictStatus
 * \brief Describes whether a username conflict exists before restoring.
 */
enum class ConflictStatus {
    None,           ///< No conflict — the username is available.
    UsernameExists  ///< A profile with the same username is already in the DB.
};

/*!
 * \class ProfileRestore
 * \brief Restores an OSCAR user profile from a .oscar backup package.
 *
 * ### Basic usage
 * \code
 * ProfileRestore restore("C:/Backups/OSCAR/profile_backup_JohnDoe_20260201.oscar");
 *
 * // Step 1: validate
 * if (!restore.validatePackage()) {
 *     qCritical() << restore.getErrorMessage();
 *     return;
 * }
 *
 * // Step 2: check compatibility
 * if (!restore.checkCompatibility()) {
 *     qCritical() << restore.getErrorMessage();
 *     return;
 * }
 *
 * // Step 3: handle conflicts
 * if (restore.checkConflicts() == ConflictStatus::UsernameExists) {
 *     restore.setConflictResolution(ConflictResolution::Rename);
 *     restore.setNewUsername("JohnDoe_restored");
 * }
 *
 * // Step 4: restore
 * connect(&restore, &ProfileRestore::progressChanged,
 *         [](int pct, QString msg){ qDebug() << pct << msg; });
 *
 * if (restore.restoreProfile()) {
 *     qDebug() << "Restored profile ID:" << restore.getRestoredProfileId();
 * }
 * \endcode
 *
 * ### ID remapping
 * All auto-increment primary keys in the backup are remapped to fresh IDs
 * assigned by the database during the restore INSERT statements.  Foreign key
 * references within the backup are updated consistently using mapping tables
 * built incrementally as each row is inserted.
 *
 * ### Atomicity
 * The entire database restore is wrapped in a single transaction.  Any
 * failure causes a complete rollback, leaving the database unchanged.
 *
 * ### Partial packages
 * A partial (date-range) backup has \c is_partial: true in its manifest.
 * Restoring such a package adds the exported sessions to the profile without
 * removing sessions already present in the database.
 */
class ProfileRestore : public QObject
{
    Q_OBJECT

public:
    /*!
     * \brief Construct a ProfileRestore targeting the given package file.
     * \param packagePath  Absolute path to the .oscar backup package.
     * \param parent       Optional Qt parent object.
     */
    explicit ProfileRestore(const QString& packagePath,
                            QObject*       parent = nullptr);

    /*!
     * \brief Destructor.  Removes any temporary extraction directory.
     */
    ~ProfileRestore() override;

    // -----------------------------------------------------------------------
    //  Configuration (call before restoreProfile())
    // -----------------------------------------------------------------------

    /*!
     * \brief Set the strategy for resolving a username conflict.
     *
     * Must be called if checkConflicts() returns
     * \c ConflictStatus::UsernameExists.  Defaults to \c Abort.
     *
     * \param strategy  One of Abort, Rename, or Replace.
     */
    void setConflictResolution(ConflictResolution strategy);

    /*!
     * \brief Override the username used when importing the profile.
     *
     * Use this together with \c ConflictResolution::Rename.  If left empty,
     * a name such as \c <original>_restored_<timestamp> is generated
     * automatically.
     *
     * \param username  Desired username for the restored profile.
     */
    void setNewUsername(const QString& username);

    // -----------------------------------------------------------------------
    //  Validation
    // -----------------------------------------------------------------------

    /*!
     * \brief Validate the .oscar package before attempting a restore.
     *
     * Checks:
     *  - The file exists and is readable.
     *  - The file can be extracted as a ZIP archive.
     *  - \c manifest.json is present and structurally valid.
     *  - Package checksums match the recorded values.
     *
     * \return true if the package is valid; false otherwise
     *         (call getErrorMessage() for details).
     *
     * \note Implemented in Phase 3.
     */
    bool validatePackage();

    /*!
     * \brief Check that the backup's schema version matches the database.
     *
     * Schema v13+ policy: only an exact schema version match is acceptable.
     * If the versions differ, the restore is rejected with a clear message.
     *
     * \return true if compatible; false otherwise.
     *
     * \note Must be called after validatePackage().
     * \note Implemented in Phase 3.
     */
    bool checkCompatibility();

    /*!
     * \brief Detect whether the backup username already exists in the DB.
     *
     * \return \c ConflictStatus::UsernameExists if a profile with the backup
     *         username is already present; \c ConflictStatus::None otherwise.
     *
     * \note Must be called after validatePackage().
     * \note Implemented in Phase 3.
     */
    ConflictStatus checkConflicts();

    // -----------------------------------------------------------------------
    //  Execution
    // -----------------------------------------------------------------------

    /*!
     * \brief Import the backup package into the database.
     *
     * Orchestrates the full restore:
     *  1. Extracts the package to a temp directory.
     *  2. Resolves any username conflict.
     *  3. Restores all tables inside a single database transaction.
     *  4. Validates the restored data against manifest statistics.
     *  5. Emits restoreCompleted() or restoreFailed() on finish.
     *
     * \return true on success; false on failure (call getErrorMessage()).
     *
     * \note Implemented in Phase 3.
     */
    bool restoreProfile();

    // -----------------------------------------------------------------------
    //  Status / result accessors
    // -----------------------------------------------------------------------

    /*!
     * \brief Return the human-readable description of the last error.
     */
    QString getErrorMessage() const;

    /*!
     * \brief Return the database primary key of the newly restored profile.
     *
     * Only valid after a successful restoreProfile() call.
     */
    qint64 getRestoredProfileId() const;

    /*!
     * \brief Return the username of the newly restored profile.
     *
     * May differ from the original if a rename conflict resolution was used.
     * Only valid after a successful restoreProfile() call.
     */
    QString getRestoredUsername() const;

    /*!
     * \brief Return the parsed manifest JSON object.
     *
     * Valid after a successful validatePackage() call.  Returns an empty
     * object if the manifest has not been loaded.
     */
    QJsonObject manifestJson() const;

signals:
    /*!
     * \brief Emitted periodically during restoreProfile() to report progress.
     * \param percent  Completion percentage (0–100).
     * \param message  Human-readable description of the current step.
     */
    void progressChanged(int percent, const QString& message);

    /*!
     * \brief Emitted when restoreProfile() completes successfully.
     * \param profileId  Database primary key of the restored profile.
     * \param username   Username of the restored profile.
     */
    void restoreCompleted(qint64 profileId, const QString& username);

    /*!
     * \brief Emitted when restoreProfile() fails.
     * \param error  Human-readable error description.
     */
    void restoreFailed(const QString& error);

private:
    // -----------------------------------------------------------------------
    //  Private helpers (implemented in Phase 3)
    // -----------------------------------------------------------------------

    /*!
     * \brief Extract the .oscar ZIP archive to a temporary directory.
     * \return true on success.
     */
    bool extractPackage();

    /*!
     * \brief Load and validate the manifest from the extracted directory.
     * \param manifest  Output parameter populated with manifest data.
     * \return true on success.
     */
    bool parseManifest(QJsonObject& manifest);

    /*!
     * \brief Execute the transactional database restore.
     *
     * Wraps all INSERT operations in a single transaction.  On any failure,
     * issues a ROLLBACK and leaves the database unchanged.
     *
     * \return true on success.
     */
    bool restoreInTransaction();

    /*!
     * \brief Execute all INSERT statements from a SQL export file.
     *
     * Reads lines from \a sqlFile, replaces ID placeholders using the
     * current mapping tables, and executes each INSERT.  After each INSERT
     * with a placeholder for the newly generated ID, calls
     * LAST_INSERT_ROWID() and records the mapping.
     *
     * \param sqlFile  Absolute path to a .sql export file.
     * \return true on success.
     */
    bool executeSqlFile(const QString& sqlFile);

    /*!
     * \brief Validate the restored profile against the manifest statistics.
     *
     * Checks that session counts and foreign-key integrity match what the
     * manifest records.
     *
     * \param manifest  The parsed manifest object.
     * \return true if validation passes.
     */
    bool validateRestore(const QJsonObject& manifest);

    /*!
     * \brief Resolve a username conflict using the configured strategy.
     *
     * - \c Abort   → returns an empty string (caller must stop).
     * - \c Rename  → returns \a originalUsername_restored_<timestamp>,
     *               or m_newUsername if explicitly set.
     * - \c Replace → removes the existing profile and returns
     *                \a originalUsername.
     *
     * \param originalUsername  Username read from the backup manifest.
     * \return Resolved username, or empty string on Abort.
     */
    QString resolveUsernameConflict(const QString& originalUsername);

    /*!
     * \brief Parse a SQL INSERT statement and replace all ID placeholders.
     *
     * Scans for tokens such as \c @PROFILE_ID@, \c @MACHINE_ID@,
     * \c @SESSION_ID@, etc., and replaces them with the corresponding
     * remapped IDs from the mapping tables.
     *
     * \param sql  Raw INSERT statement read from a .sql file.
     * \return Modified INSERT statement ready for execution.
     */
    QString parseAndRemapSql(const QString& sql);

    // -----------------------------------------------------------------------
    //  ID remapping helpers
    // -----------------------------------------------------------------------

    /*!
     * \brief Record that old profile ID \a oldId maps to \a newId.
     */
    void addProfileIdMapping(qint64 oldId, qint64 newId);

    /*!
     * \brief Record that old machine ID \a oldId maps to \a newId.
     */
    void addMachineIdMapping(qint64 oldId, qint64 newId);

    /*!
     * \brief Record that old session ID \a oldId maps to \a newId.
     */
    void addSessionIdMapping(qint64 oldId, qint64 newId);

    /*!
     * \brief Record that old session_channel ID \a oldId maps to \a newId.
     */
    void addSessionChannelIdMapping(qint64 oldId, qint64 newId);

    /*!
     * \brief Record that old event_list ID \a oldId maps to \a newId.
     */
    void addEventListIdMapping(qint64 oldId, qint64 newId);

    /*!
     * \brief Look up the remapped profile ID.
     * \param oldId  Original profile ID from the backup.
     * \return New database profile ID, or -1 if not yet mapped.
     */
    qint64 remapProfileId(qint64 oldId) const;

    /*!
     * \brief Look up the remapped machine ID.
     */
    qint64 remapMachineId(qint64 oldId) const;

    /*!
     * \brief Look up the remapped session ID.
     */
    qint64 remapSessionId(qint64 oldId) const;

    /*!
     * \brief Look up the remapped session_channel ID.
     */
    qint64 remapSessionChannelId(qint64 oldId) const;

    /*!
     * \brief Look up the remapped event_list ID.
     */
    qint64 remapEventListId(qint64 oldId) const;

    // -----------------------------------------------------------------------
    //  Member variables
    // -----------------------------------------------------------------------

    QString            m_packagePath;         ///< Absolute path to .oscar file.
    QString            m_errorMessage;        ///< Last error description.
    QString            m_tempDir;             ///< Temporary extraction directory.
    qint64             m_newProfileId  = -1;  ///< Restored profile DB ID.
    QString            m_newUsername;         ///< Resolved username after conflict handling.
    ConflictResolution m_resolution = ConflictResolution::Abort; ///< Conflict strategy.

    QJsonObject        m_manifestJson;        ///< Loaded manifest (populated by validatePackage).

    // ID mapping tables (old backup ID → new database ID)
    QMap<qint64, qint64> m_profileIdMap;        ///< profile_id remapping.
    QMap<qint64, qint64> m_machineIdMap;        ///< machine_id remapping.
    QMap<qint64, qint64> m_sessionIdMap;        ///< session_id remapping.
    QMap<qint64, qint64> m_sessionChannelIdMap; ///< session_channel_id remapping.
    QMap<qint64, qint64> m_eventListIdMap;      ///< event_list_id remapping.
};

#endif // PROFILE_RESTORE_H
