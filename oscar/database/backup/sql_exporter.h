/* SQL Exporter Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the SqlExporter class which exports SQLite table data
 * to SQL INSERT statement files for use in profile backup packages.
 *
 * Key features:
 *  - Generates portable INSERT INTO ... VALUES (...) statements
 *  - Handles NULL values, text (with quote-escaping), integers, reals
 *  - Encodes BLOB columns as SQLite hex literals: X'...'
 *  - Supports column-level placeholder substitution for ID remapping
 *    (e.g. profile_id → @PROFILE_ID@) so the restore phase can remap IDs
 *  - Emits progress signals for large table exports
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SQL_EXPORTER_H
#define SQL_EXPORTER_H

#include <QMap>
#include <QObject>
#include <QSqlRecord>
#include <QStringList>
#include <QVariant>

/*!
 * \class SqlExporter
 * \brief Exports SQLite table rows to SQL INSERT statement files.
 *
 * SqlExporter reads rows from a database table (optionally filtered by a
 * WHERE clause) and writes one INSERT statement per row to an output file.
 * The output is valid SQLite SQL that can be replayed during restore.
 *
 * ### Placeholder substitution
 *
 * During backup, certain integer columns (profile_id, machine_id, session_id,
 * etc.) must be replaced with named placeholders so that the restore phase can
 * remap them to new auto-generated IDs.  Use \c setColumnPlaceholders() to
 * provide this mapping before calling \c exportTable() or
 * \c exportBlobTable():
 *
 * \code
 * SqlExporter exporter;
 * exporter.setColumnPlaceholders({
 *     { "profile_id", "@PROFILE_ID@" },
 *     { "session_id", "@SESSION_ID@" }
 * });
 * exporter.exportTable("session_settings",
 *                      "session_id IN (SELECT id FROM sessions WHERE ...)",
 *                      "/tmp/backup/database/session_settings.sql");
 * \endcode
 *
 * ### BLOB encoding
 *
 * Binary columns (BLOB affinity) must be listed by name so the exporter
 * encodes them as \c X'<hex>' literals instead of treating them as text:
 *
 * \code
 * exporter.exportBlobTable("event_data",
 *                          "eventlist_id IN (...)",
 *                          "/tmp/backup/database/event_data.sql",
 *                          { "data_blob", "data_compressed",
 *                            "data2_blob", "data2_compressed",
 *                            "time_blob",  "time_compressed" });
 * \endcode
 *
 * ### Thread safety
 *
 * SqlExporter is not thread-safe.  Create one instance per thread.
 */
class SqlExporter : public QObject
{
    Q_OBJECT

public:
    /*!
     * \brief Construct a SqlExporter.
     * \param parent  Optional Qt parent object.
     */
    explicit SqlExporter(QObject* parent = nullptr);

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    /*!
     * \brief Set column-level placeholder substitutions.
     *
     * Keys are column names (case-insensitive); values are placeholder
     * strings written verbatim into the INSERT statement instead of the
     * column's actual SQL value.
     *
     * Example:
     * \code
     * exporter.setColumnPlaceholders({
     *     { "profile_id", "@PROFILE_ID@" },
     *     { "machine_id", "@MACHINE_ID@" }
     * });
     * \endcode
     *
     * Call before exportTable() / exportBlobTable().  Clear by passing an
     * empty map.
     */
    void setColumnPlaceholders(const QMap<QString, QString>& placeholders);

    // -----------------------------------------------------------------------
    //  Export methods
    // -----------------------------------------------------------------------

    /*!
     * \brief Export a table (no BLOB columns) to a SQL file.
     *
     * Executes \c SELECT * FROM tableName [WHERE whereClause] and writes one
     * INSERT statement per row to \a outputFile.  Any configured column
     * placeholders are applied.
     *
     * \param tableName   Name of the table to export.
     * \param whereClause Optional WHERE clause body (without the keyword).
     *                    Pass an empty string to export all rows.
     * \param outputFile  Full path to the output .sql file.
     * \return true on success; false on any error (call errorMessage()).
     */
    bool exportTable(const QString& tableName,
                     const QString& whereClause,
                     const QString& outputFile);

    /*!
     * \brief Export a table that contains BLOB columns.
     *
     * Like exportTable() but columns listed in \a blobColumns are encoded
     * as \c X'<hex>' literals regardless of the QVariant type reported by
     * the driver.
     *
     * \param tableName   Name of the table to export.
     * \param whereClause Optional WHERE clause body (without the keyword).
     * \param outputFile  Full path to the output .sql file.
     * \param blobColumns List of column names to treat as BLOBs.
     * \return true on success; false on any error.
     */
    bool exportBlobTable(const QString& tableName,
                         const QString& whereClause,
                         const QString& outputFile,
                         const QStringList& blobColumns);

    // -----------------------------------------------------------------------
    //  Static helpers
    // -----------------------------------------------------------------------

    /*!
     * \brief Encode a byte array as an uppercase hex string.
     *
     * Returns an empty string for an empty / null byte array.
     * Used internally and available to callers that need the same encoding.
     *
     * \param blob  Raw binary data.
     * \return Hex string (uppercase), or an empty string.
     */
    static QString blobToHex(const QByteArray& blob);

    /*!
     * \brief Decode a hex string back to a byte array.
     *
     * The string may contain upper- or lower-case hex characters and
     * must have an even number of digits.
     *
     * \param hex  Hex-encoded string.
     * \return Decoded byte array.
     */
    static QByteArray hexToBlob(const QString& hex);

    // -----------------------------------------------------------------------
    //  Status
    // -----------------------------------------------------------------------

    /*!
     * \brief Return the last error message set by a failing operation.
     */
    QString errorMessage() const;

signals:
    /*!
     * \brief Emitted periodically to report export progress.
     *
     * \param rowsWritten  Number of rows written so far.
     * \param totalRows    Total rows to write, or -1 if unknown.
     */
    void progressChanged(int rowsWritten, int totalRows);

private:
    /*!
     * \brief Build a single INSERT statement for the current query row.
     *
     * \param tableName   Table being exported (used in the INSERT keyword).
     * \param record      QSqlRecord describing column names and types.
     * \param query       QSqlQuery positioned at the current row.
     * \param blobColumns Column names to treat as BLOB (hex-encoded).
     * \return A complete INSERT INTO ... VALUES (...); statement.
     */
    QString generateInsertStatement(const QString&     tableName,
                                    const QSqlRecord&  record,
                                    const class QSqlQuery& query,
                                    const QStringList& blobColumns) const;

    /*!
     * \brief Escape and format a single column value for use in SQL.
     *
     * Rules applied (in order):
     *  1. If value is SQL NULL → returns \c "NULL"
     *  2. If \a isBlobColumn is true → returns \c "X'<hex>'"
     *  3. If the QVariant type is QByteArray → returns \c "X'<hex>'"
     *  4. If the type is an integer type → returns the decimal integer
     *  5. If the type is a floating-point type → returns full-precision real
     *  6. Otherwise → single-quoted text with internal single-quotes doubled
     *
     * \param value       Column value from QSqlQuery::value().
     * \param isBlobColumn  True if this column is in the BLOB list.
     * \return SQL literal suitable for embedding in a VALUES clause.
     */
    QString escapeValue(const QVariant& value, bool isBlobColumn) const;

    QString              m_errorMessage;    ///< Last error description.
    QMap<QString,QString> m_placeholders;  ///< Column → placeholder map.
};

#endif // SQL_EXPORTER_H
