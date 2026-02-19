/* Backup Manifest Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the BackupManifest class which handles creation and
 * parsing of the manifest.json file embedded in a .oscar backup package.
 *
 * The manifest records metadata about the backup: OSCAR version, schema
 * version, profile identity, statistics, export options (date range,
 * privacy mode), and checksums for integrity verification.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BACKUP_MANIFEST_H
#define BACKUP_MANIFEST_H

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/*!
 * \class BackupManifest
 * \brief Handles creation and parsing of the backup package manifest.
 *
 * A BackupManifest represents the contents of the \c manifest.json file
 * that is stored at the root of every \c .oscar backup package. It records:
 * - Format and OSCAR version information
 * - Database schema version (exact-match required for restore)
 * - Profile identity (username, original profile ID)
 * - Export statistics (session count, date range, database size)
 * - Export options (partial/full, date range, privacy mode)
 * - SHA-256 checksums for integrity verification
 *
 * Typical usage for backup:
 * \code
 * BackupManifest manifest;
 * manifest.setFormatVersion("2.0");
 * manifest.setOscarVersion(getVersion().toString());
 * manifest.setSchemaVersion(DatabaseSchema::CURRENT_SCHEMA_VERSION);
 * manifest.setProfileInfo("JohnDoe", 123, "PROF/JohnDoe", "active");
 * manifest.setStatistics(1, 365, 2920, "2025-01-01", "2025-12-31", 524288000LL);
 * manifest.addExportedTable("profiles");
 * manifest.setExportOptions(true, true, false, QDate(), QDate(), false);
 * manifest.saveToFile(outDir + "/manifest.json");
 * \endcode
 *
 * Typical usage for restore:
 * \code
 * BackupManifest manifest;
 * if (manifest.loadFromFile(path + "/manifest.json") && manifest.isValid()) {
 *     int schemaVer = manifest.schemaVersion();
 *     QString user  = manifest.username();
 * }
 * \endcode
 */
class BackupManifest
{
public:
    /*!
     * \brief Construct an empty BackupManifest.
     *
     * The export_date is automatically set to the current UTC time.
     */
    BackupManifest();

    // -----------------------------------------------------------------------
    //  Setters
    // -----------------------------------------------------------------------

    /*!
     * \brief Set the backup format version string (e.g. "2.0").
     */
    void setFormatVersion(const QString& version);

    /*!
     * \brief Set the OSCAR application version string.
     *
     * Also populates the \c exported_by field.
     */
    void setOscarVersion(const QString& version);

    /*!
     * \brief Set the database schema version (e.g. 13).
     *
     * The restore logic performs an exact-match check on this value.
     */
    void setSchemaVersion(int version);

    /*!
     * \brief Record profile identity information.
     * \param username        Profile username.
     * \param profileId       Original database profile ID.
     * \param dataFolder      Relative path to profile data folder.
     * \param status          Profile status ("active", "missing", or "archived").
     */
    void setProfileInfo(const QString& username, qint64 profileId,
                        const QString& dataFolder, const QString& status);

    /*!
     * \brief Record export statistics.
     * \param machinesCount    Number of machines exported.
     * \param sessionsCount    Number of sessions exported.
     * \param eventListsCount  Number of event lists exported.
     * \param firstSession     ISO date of earliest exported session (YYYY-MM-DD).
     * \param lastSession      ISO date of latest exported session (YYYY-MM-DD).
     * \param dbSize           Uncompressed database export size in bytes.
     */
    void setStatistics(int machinesCount, int sessionsCount,
                       int eventListsCount, const QString& firstSession,
                       const QString& lastSession, qint64 dbSize);

    /*!
     * \brief Set the compressed package size in the statistics block.
     * \param compressedSize  Compressed .oscar file size in bytes.
     */
    void setCompressedSize(qint64 compressedSize);

    /*!
     * \brief Add a table name to the exported-tables list.
     * \param tableName  Name of the table exported.
     */
    void addExportedTable(const QString& tableName);

    /*!
     * \brief Record export options.
     * \param includeDisabled  Whether disabled sessions were included.
     * \param compress         Whether the package was compressed.
     * \param isPartial        True if a date range was applied.
     * \param startDate        First date in the exported range (invalid = no bound).
     * \param endDate          Last date in the exported range (invalid = no bound).
     * \param privacyApplied   True if user_info / doctor_info fields were blanked.
     */
    void setExportOptions(bool includeDisabled, bool compress,
                          bool isPartial = false,
                          const QDate& startDate = QDate(),
                          const QDate& endDate = QDate(),
                          bool privacyApplied = false);

    /*!
     * \brief Store package integrity checksums.
     * \param manifestChecksum  SHA-256 hex digest of manifest.json content.
     * \param dbChecksum        SHA-256 hex digest of the SQL export files.
     * \param packageChecksum   SHA-256 hex digest of the final .oscar package.
     */
    void setChecksums(const QString& manifestChecksum,
                      const QString& dbChecksum,
                      const QString& packageChecksum);

    // -----------------------------------------------------------------------
    //  Getters
    // -----------------------------------------------------------------------

    /*!
     * \brief Return the backup format version string.
     */
    QString formatVersion() const;

    /*!
     * \brief Return the OSCAR application version string.
     */
    QString oscarVersion() const;

    /*!
     * \brief Return the database schema version.
     */
    int schemaVersion() const;

    /*!
     * \brief Return the profile username stored in the manifest.
     */
    QString username() const;

    /*!
     * \brief Return the original database profile ID.
     */
    qint64 originalProfileId() const;

    /*!
     * \brief Return true if the export was date-range filtered (partial).
     */
    bool isPartial() const;

    /*!
     * \brief Return the export start date (invalid if full export).
     */
    QDate startDate() const;

    /*!
     * \brief Return the export end date (invalid if full export).
     */
    QDate endDate() const;

    /*!
     * \brief Return true if user_info / doctor_info fields were blanked.
     */
    bool privacyApplied() const;

    /*!
     * \brief Return the number of sessions recorded in the statistics block.
     */
    int sessionsCount() const;

    /*!
     * \brief Return the number of event lists recorded in the statistics block.
     */
    int eventListsCount() const;

    /*!
     * \brief Return the number of machines recorded in the statistics block.
     */
    int machinesCount() const;

    /*!
     * \brief Return the export_date timestamp string (ISO 8601).
     */
    QString exportDate() const;

    // -----------------------------------------------------------------------
    //  Serialization
    // -----------------------------------------------------------------------

    /*!
     * \brief Serialize the manifest to a QJsonObject.
     * \return A QJsonObject mirroring the manifest.json schema.
     */
    QJsonObject toJson() const;

    /*!
     * \brief Populate the manifest from a QJsonObject.
     * \param json  Source JSON object (typically parsed from manifest.json).
     * \return true if the JSON was structurally valid; false otherwise
     *         (call errorMessage() for details).
     */
    bool fromJson(const QJsonObject& json);

    /*!
     * \brief Write the manifest to a file as pretty-printed JSON.
     * \param filePath  Full path to the output file.
     * \return true on success.
     */
    bool saveToFile(const QString& filePath);

    /*!
     * \brief Load and parse the manifest from a JSON file.
     * \param filePath  Full path to the manifest.json file.
     * \return true on success.
     */
    bool loadFromFile(const QString& filePath);

    // -----------------------------------------------------------------------
    //  Validation
    // -----------------------------------------------------------------------

    /*!
     * \brief Return true if the manifest contains all required fields.
     *
     * Required fields: format_version, oscar_version, schema_version,
     * export_date, profile (with username and original_profile_id).
     */
    bool isValid() const;

    /*!
     * \brief Return the last error message set by any failing operation.
     */
    QString errorMessage() const;

private:
    QJsonObject m_data;        ///< Internal JSON document backing the manifest.
    QString     m_errorMessage;///< Last error message.
};

#endif // BACKUP_MANIFEST_H
