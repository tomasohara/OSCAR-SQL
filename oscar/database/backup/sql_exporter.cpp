/* SQL Exporter Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Implements SqlExporter: exports SQLite table rows to SQL INSERT statement
 * files for use in profile backup packages.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sql_exporter.h"

#include <QDateTime>
#include <QFile>
#include <QMetaType>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include "database/database_manager.h"
#include <QTextStream>
#include <QDebug>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#  include <QTextCodec>
#endif

// How often (in rows) to emit a progressChanged signal
static constexpr int kProgressInterval = 100;

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------

/*!
 * \brief Construct a SqlExporter.
 */
SqlExporter::SqlExporter(QObject* parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------

void SqlExporter::setColumnPlaceholders(const QMap<QString, QString>& placeholders)
{
    m_placeholders = placeholders;
}

// ---------------------------------------------------------------------------
//  Public export methods
// ---------------------------------------------------------------------------

/*!
 * \brief Export a table without BLOB columns.
 *
 * Delegates to exportBlobTable() with an empty BLOB column list.
 */
bool SqlExporter::exportTable(const QString& tableName,
                               const QString& whereClause,
                               const QString& outputFile)
{
    qDebug() << "SqlExporter:exportTable entered";
    return exportBlobTable(tableName, whereClause, outputFile, QStringList());
}

/*!
 * \brief Export a table that may contain BLOB columns.
 *
 * 1. Executes \c SELECT * FROM tableName [WHERE whereClause].
 * 2. Opens \a outputFile for writing.
 * 3. Writes a comment header.
 * 4. For each row, writes one INSERT statement (column placeholders applied).
 * 5. Emits progressChanged() every \c kProgressInterval rows.
 *
 * BLOB columns are encoded as \c X'<uppercaseHex>' SQLite hex literals.
 * Text values have internal single-quote characters doubled.
 * NULL database values are written as the SQL keyword \c NULL.
 */
bool SqlExporter::exportBlobTable(const QString&     tableName,
                                   const QString&     whereClause,
                                   const QString&     outputFile,
                                   const QStringList& blobColumns)
{
    m_errorMessage.clear();

    // --- Build and execute the SELECT query ---
    QString sql = QString("SELECT * FROM %1").arg(tableName);
    if (!whereClause.isEmpty()) {
        sql += " WHERE " + whereClause;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    if (!query.exec(sql)) {
        m_errorMessage = QString("Query failed on table '%1': %2")
                             .arg(tableName, query.lastError().text());
        return false;
    }

    // --- Open output file ---
    QFile file(outputFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_errorMessage = QString("Cannot open output file for writing: %1")
                             .arg(outputFile);
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif

    // --- Header comment ---
    stream << "-- Export of table: " << tableName << "\n";
    stream << "-- Generated:       "
           << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
    if (!whereClause.isEmpty()) {
        stream << "-- Filter:          WHERE " << whereClause << "\n";
    }
    stream << "\n";

    // --- Write rows ---
    int rowCount = 0;
    while (query.next()) {
        QSqlRecord record = query.record();
        stream << generateInsertStatement(tableName, record, query, blobColumns)
               << "\n";
        ++rowCount;

        if (rowCount % kProgressInterval == 0) {
            emit progressChanged(rowCount, -1);
        }
    }

    stream << "\n-- " << rowCount << " row(s) exported.\n";
    file.close();

    emit progressChanged(rowCount, rowCount);
    return true;
}

// ---------------------------------------------------------------------------
//  Static helpers
// ---------------------------------------------------------------------------

/*!
 * \brief Encode a byte array as an uppercase hex string.
 *
 * Returns an empty string for an empty or null input — the caller then
 * writes the SQL keyword \c NULL.
 */
QString SqlExporter::blobToHex(const QByteArray& blob)
{
    if (blob.isEmpty()) {
        return QString();
    }
    return QString::fromLatin1(blob.toHex()).toUpper();
}

/*!
 * \brief Decode a hex string back to a byte array.
 *
 * Accepts upper- or lower-case hex digits.
 */
QByteArray SqlExporter::hexToBlob(const QString& hex)
{
    return QByteArray::fromHex(hex.toLatin1());
}

// ---------------------------------------------------------------------------
//  Status
// ---------------------------------------------------------------------------

QString SqlExporter::errorMessage() const
{
    return m_errorMessage;
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

/*!
 * \brief Build a complete INSERT statement for the current query row.
 *
 * Column names are taken from \a record.  Each value is either:
 * - Replaced by a placeholder string (from m_placeholders), or
 * - Formatted by escapeValue().
 *
 * \note The \a query must already be positioned on a valid row via next().
 */
QString SqlExporter::generateInsertStatement(const QString&    tableName,
                                              const QSqlRecord& record,
                                              const QSqlQuery&  query,
                                              const QStringList& blobColumns) const
{
    const int fieldCount = record.count();

    QStringList columnNames;
    QStringList values;
    columnNames.reserve(fieldCount);
    values.reserve(fieldCount);

    for (int i = 0; i < fieldCount; ++i) {
        const QString colName = record.fieldName(i);
        columnNames << colName;

        // Check for a placeholder substitution (case-insensitive key lookup)
        bool       usedPlaceholder = false;
        for (auto it = m_placeholders.constBegin();
             it != m_placeholders.constEnd(); ++it) {
            if (colName.compare(it.key(), Qt::CaseInsensitive) == 0) {
                values << it.value();
                usedPlaceholder = true;
                break;
            }
        }

        if (!usedPlaceholder) {
            bool isBlobCol = false;
            for (const QString& bc : blobColumns) {
                if (colName.compare(bc, Qt::CaseInsensitive) == 0) {
                    isBlobCol = true;
                    break;
                }
            }
            values << escapeValue(query.value(i), isBlobCol);
        }
    }

    return QString("INSERT INTO %1 (%2) VALUES (%3);")
               .arg(tableName,
                    columnNames.join(", "),
                    values.join(", "));
}

/*!
 * \brief Format a single column value as a SQL literal.
 *
 * Decision tree:
 * 1. NULL / invalid QVariant                → \c NULL
 * 2. \a isBlobColumn is true                → \c X'<HEX>' (or NULL if empty)
 * 3. QVariant holds QByteArray              → \c X'<HEX>' (or NULL if empty)
 * 4. Integer types (Int, UInt, LongLong, …) → decimal integer literal
 * 5. Floating-point types (Double, Float)   → full-precision real literal
 * 6. Bool                                   → \c 1 or \c 0
 * 7. Everything else                        → \c 'text' (single-quotes doubled)
 *
 * \note SQLite has dynamic typing.  The driver may return any type for any
 * column.  We rely on QVariant's type to choose the correct literal, but
 * callers must explicitly list BLOB columns for reliable hex encoding.
 */
QString SqlExporter::escapeValue(const QVariant& value, bool isBlobColumn) const
{
    // Rule 1: SQL NULL
    if (value.isNull() || !value.isValid()) {
        return QStringLiteral("NULL");
    }

    // Rule 2: caller-designated BLOB column
    if (isBlobColumn) {
        QByteArray blob = value.toByteArray();
        if (blob.isEmpty()) {
            return QStringLiteral("NULL");
        }
        return "X'" + blobToHex(blob) + "'";
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int typeId = value.typeId();
#else
    const int typeId = static_cast<int>(value.type());
#endif

    // Rule 3: QByteArray variant (BLOB affinity detected by driver)
    if (typeId == QMetaType::QByteArray) {
        QByteArray blob = value.toByteArray();
        if (blob.isEmpty()) {
            return QStringLiteral("NULL");
        }
        return "X'" + blobToHex(blob) + "'";
    }

    // Rule 4: integer types
    if (typeId == QMetaType::Int      ||
        typeId == QMetaType::UInt     ||
        typeId == QMetaType::Long     ||
        typeId == QMetaType::ULong    ||
        typeId == QMetaType::LongLong ||
        typeId == QMetaType::ULongLong) {
        return QString::number(value.toLongLong());
    }

    // Rule 5: floating-point types
    if (typeId == QMetaType::Double || typeId == QMetaType::Float) {
        double d = value.toDouble();
        // Use full precision so the value round-trips exactly.
        return QString::number(d, 'g', 17);
    }

    // Rule 6: bool
    if (typeId == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    }

    // Rule 7: text (default)
    // Escape backslash first (so later replacements don't double-escape it),
    // then newlines and carriage returns so every INSERT stays on one line,
    // then single quotes using the standard SQL doubling convention.
    QString text = value.toString();
    text.replace(QLatin1Char('\\'),  QLatin1String("\\\\"));
    text.replace(QLatin1Char('\n'),  QLatin1String("\\n"));
    text.replace(QLatin1Char('\r'),  QLatin1String("\\r"));
    text.replace(QLatin1Char('\''),  QLatin1String("''"));
    return '\'' + text + '\'';
}
