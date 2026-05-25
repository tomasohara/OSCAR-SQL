/* Backup Manifest Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Implements BackupManifest: serialization/deserialization of the
 * manifest.json file embedded in every .oscar backup package.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "backup_manifest.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------

/*!
 * \brief Construct an empty BackupManifest.
 *
 * Automatically stamps the current UTC time into the \c export_date field so
 * callers do not need to set it explicitly.
 */
BackupManifest::BackupManifest()
{
    m_data["export_date"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// ---------------------------------------------------------------------------
//  Setters
// ---------------------------------------------------------------------------

void BackupManifest::setFormatVersion(const QString& version)
{
    m_data["format_version"] = version;
}

/*!
 * \brief Set the OSCAR application version and derive the \c exported_by field.
 */
void BackupManifest::setOscarVersion(const QString& version)
{
    m_data["oscar_version"] = version;
    m_data["exported_by"]   = QString("OSCAR Profile Backup v%1").arg(version);
}

void BackupManifest::setSchemaVersion(int version)
{
    m_data["schema_version"] = version;
}

void BackupManifest::setProfileInfo(const QString& username,
                                     qint64          profileId,
                                     const QString&  dataFolder,
                                     const QString&  status)
{
    QJsonObject profile;
    profile["username"]            = username;
    profile["original_profile_id"] = profileId;
    profile["data_folder"]         = dataFolder;
    profile["status"]              = status;
    m_data["profile"]              = profile;
}

void BackupManifest::setStatistics(int machinesCount,
                                    int sessionsCount,
                                    int eventListsCount,
                                    const QString& firstSession,
                                    const QString& lastSession,
                                    qint64 dbSize)
{
    QJsonObject stats = m_data.contains("statistics")
                            ? m_data["statistics"].toObject()
                            : QJsonObject();

    stats["machines_count"]    = machinesCount;
    stats["sessions_count"]    = sessionsCount;
    stats["event_lists_count"] = eventListsCount;

    QJsonObject dateRange;
    dateRange["first_session"] = firstSession.isEmpty()
                                     ? QJsonValue(QJsonValue::Null)
                                     : QJsonValue(firstSession);
    dateRange["last_session"]  = lastSession.isEmpty()
                                     ? QJsonValue(QJsonValue::Null)
                                     : QJsonValue(lastSession);
    stats["date_range"]             = dateRange;
    stats["database_size_bytes"]    = dbSize;

    // compressed_size_bytes is set separately via setCompressedSize()
    if (!stats.contains("compressed_size_bytes")) {
        stats["compressed_size_bytes"] = 0;
    }

    m_data["statistics"] = stats;
}

void BackupManifest::setCompressedSize(qint64 compressedSize)
{
    QJsonObject stats = m_data.contains("statistics")
                            ? m_data["statistics"].toObject()
                            : QJsonObject();
    stats["compressed_size_bytes"] = compressedSize;
    m_data["statistics"]           = stats;
}

void BackupManifest::addExportedTable(const QString& tableName)
{
    QJsonArray tables = m_data.contains("tables_exported")
                            ? m_data["tables_exported"].toArray()
                            : QJsonArray();
    tables.append(tableName);
    m_data["tables_exported"] = tables;
}

void BackupManifest::setPackageType(const QString& type)
{
    m_data["package_type"] = type;
}

void BackupManifest::setExportOptions(bool          includeDisabled,
                                       bool          compress,
                                       bool          isPartialExport,
                                       const QDate&  startDate,
                                       const QDate&  endDate,
                                       bool          privacyMode,
                                       bool          includesSDData)
{
    QJsonObject options;
    options["is_partial"] = isPartialExport;

    QJsonObject dateRange;
    dateRange["start_date"] = startDate.isValid()
                                  ? QJsonValue(startDate.toString(Qt::ISODate))
                                  : QJsonValue(QJsonValue::Null);
    dateRange["end_date"]   = endDate.isValid()
                                  ? QJsonValue(endDate.toString(Qt::ISODate))
                                  : QJsonValue(QJsonValue::Null);
    options["date_range"]   = dateRange;

    options["privacy_applied"]           = privacyMode;
    options["include_disabled_sessions"] = includeDisabled;
    options["compress_package"]          = compress;
    options["includes_sd_data"]          = includesSDData;
    options["database_only"]             = !includesSDData;

    m_data["export_options"] = options;
}

void BackupManifest::setChecksums(const QString& manifestChecksum,
                                   const QString& dbChecksum,
                                   const QString& packageChecksum)
{
    QJsonObject checksums;
    checksums["manifest"]        = manifestChecksum;
    checksums["database_export"] = dbChecksum;
    checksums["package"]         = packageChecksum;
    m_data["checksums"]          = checksums;
}

// ---------------------------------------------------------------------------
//  Getters
// ---------------------------------------------------------------------------

QString BackupManifest::formatVersion() const
{
    return m_data["format_version"].toString();
}

QString BackupManifest::oscarVersion() const
{
    return m_data["oscar_version"].toString();
}

int BackupManifest::schemaVersion() const
{
    return m_data["schema_version"].toInt(0);
}

QString BackupManifest::username() const
{
    return m_data["profile"].toObject()["username"].toString();
}

qint64 BackupManifest::originalProfileId() const
{
    // JSON integers are stored as double internally; cast safely.
    return static_cast<qint64>(
        m_data["profile"].toObject()["original_profile_id"].toDouble(0.0));
}

bool BackupManifest::isPartial() const
{
    return m_data["export_options"].toObject()["is_partial"].toBool(false);
}

QDate BackupManifest::startDate() const
{
    QString s = m_data["export_options"]
                    .toObject()["date_range"]
                    .toObject()["start_date"]
                    .toString();
    return s.isEmpty() ? QDate() : QDate::fromString(s, Qt::ISODate);
}

QDate BackupManifest::endDate() const
{
    QString s = m_data["export_options"]
                    .toObject()["date_range"]
                    .toObject()["end_date"]
                    .toString();
    return s.isEmpty() ? QDate() : QDate::fromString(s, Qt::ISODate);
}

bool BackupManifest::privacyApplied() const
{
    return m_data["export_options"].toObject()["privacy_applied"].toBool(false);
}

bool BackupManifest::includesSDData() const
{
    return m_data["export_options"].toObject()["includes_sd_data"].toBool(false);
}

int BackupManifest::sessionsCount() const
{
    return m_data["statistics"].toObject()["sessions_count"].toInt(0);
}

int BackupManifest::eventListsCount() const
{
    return m_data["statistics"].toObject()["event_lists_count"].toInt(0);
}

int BackupManifest::machinesCount() const
{
    return m_data["statistics"].toObject()["machines_count"].toInt(0);
}

QString BackupManifest::packageType() const
{
    return m_data["package_type"].toString();
}

QString BackupManifest::exportDate() const
{
    return m_data["export_date"].toString();
}

// ---------------------------------------------------------------------------
//  Serialization
// ---------------------------------------------------------------------------

QJsonObject BackupManifest::toJson() const
{
    return m_data;
}

/*!
 * \brief Populate the manifest from a parsed QJsonObject.
 *
 * Validates that all required top-level keys are present. On failure,
 * sets the error message and returns false.
 */
bool BackupManifest::fromJson(const QJsonObject& json)
{
    // Check required fields
    static const QStringList required = {
        "format_version", "oscar_version", "schema_version",
        "export_date", "profile"
    };

    for (const QString& field : required) {
        if (!json.contains(field)) {
            m_errorMessage = QString("Manifest is missing required field: '%1'")
                                 .arg(field);
            return false;
        }
    }

    // Check profile sub-object has the required keys
    QJsonObject profile = json["profile"].toObject();
    if (!profile.contains("username") ||
        !profile.contains("original_profile_id")) {
        m_errorMessage = "Manifest profile block is missing 'username' or "
                         "'original_profile_id'.";
        return false;
    }

    m_data         = json;
    m_errorMessage.clear();
    return true;
}

/*!
 * \brief Write the manifest to a file as pretty-printed UTF-8 JSON.
 */
bool BackupManifest::saveToFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_errorMessage = QString("Cannot open manifest file for writing: %1")
                             .arg(filePath);
        return false;
    }

    QJsonDocument doc(m_data);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

/*!
 * \brief Load a manifest from a JSON file and validate its structure.
 */
bool BackupManifest::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_errorMessage = QString("Cannot open manifest file for reading: %1")
                             .arg(filePath);
        return false;
    }

    QByteArray rawData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument   doc = QJsonDocument::fromJson(rawData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        m_errorMessage = QString("Manifest JSON parse error at offset %1: %2")
                             .arg(parseError.offset)
                             .arg(parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        m_errorMessage = "Manifest file does not contain a JSON object.";
        return false;
    }

    return fromJson(doc.object());
}

// ---------------------------------------------------------------------------
//  Validation
// ---------------------------------------------------------------------------

/*!
 * \brief Return true when all required manifest fields are populated.
 */
bool BackupManifest::isValid() const
{
    if (!m_data.contains("format_version") ||
        !m_data.contains("oscar_version")  ||
        !m_data.contains("schema_version") ||
        !m_data.contains("export_date")    ||
        !m_data.contains("profile")) {
        return false;
    }

    QJsonObject profile = m_data["profile"].toObject();
    if (!profile.contains("username") ||
        !profile.contains("original_profile_id")) {
        return false;
    }

    // schema_version must be a positive integer
    if (m_data["schema_version"].toInt(0) <= 0) {
        return false;
    }

    return true;
}

QString BackupManifest::errorMessage() const
{
    return m_errorMessage;
}
