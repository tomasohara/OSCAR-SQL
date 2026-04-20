/* Profile Backup Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Implements ProfileBackup: orchestrates the export of a single OSCAR user
 * profile to a self-contained .oscar backup package (ZIP archive).
 *
 * Phase 2 — Full implementation of createBackup() and all helper methods.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profile_backup.h"
#include "backup_manifest.h"
#include "sql_exporter.h"
#include "../database_manager.h"
#include "../database_schema.h"
#include "../../version.h"
#include "../../zip.h"
#include "../../SleepLib/preferences.h"

// p_pref is the global Preferences object (defined in SleepLib/profiles.cpp).
// We need it to resolve the canonical Profiles directory path, using the same
// mechanism as Profiles::Scan() so the path is always consistent.
extern Preferences *p_pref;

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDebug>

// ---------------------------------------------------------------------------
//  File-scope helpers
// ---------------------------------------------------------------------------

/*!
 * \brief Compute the total byte size of all files directly in \a dirPath.
 */
static qint64 directorySize(const QString& dirPath)
{
    qint64 total = 0;
    const QFileInfoList entries =
        QDir(dirPath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        total += fi.size();
    }
    qDebug() << "Profile_backup::directorySize =" << total;
    return total;
}

/*!
 * \brief Compute SHA-256 of all *.sql files in \a dirPath (sorted by name).
 *
 * Each file's content is fed into a single hash, producing one hex digest
 * that represents the entire database export directory.
 *
 * \return Lowercase hex digest, or an empty string on I/O error.
 */
static QString hashSqlDirectory(const QString& dirPath)
{
    const QStringList files =
        QDir(dirPath).entryList(QStringList() << "*.sql", QDir::Files, QDir::Name);

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& name : files) {
        QFile f(QDir(dirPath).filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            return QString();
        }
        while (!f.atEnd()) {
            hash.addData(f.read(65536));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

/*!
 * \brief Write a privacy-redacted SQL INSERT file for a single table.
 *
 * All columns listed in \a nullColumns are written as NULL.  The
 * \c profile_id column (if present) is written as the @PROFILE_ID@ placeholder.
 * All other columns are exported verbatim.
 *
 * \param db          Live database connection.
 * \param tableName   Table to read.
 * \param whereClause SQL WHERE body (no keyword).
 * \param nullColumns Column names to replace with NULL.
 * \param outputFile  Destination .sql file path.
 * \return true on success.
 */
static bool exportPrivacyTable(QSqlDatabase& db,
                                const QString& tableName,
                                const QString& whereClause,
                                const QStringList& nullColumns,
                                const QString& outputFile)
{
    qDebug() << "profile_backup::exportPrivacyTable entered";
    QSqlQuery query(db);
    if (!query.exec(QString("SELECT * FROM %1 WHERE %2").arg(tableName, whereClause))) {
        qWarning() << "exportPrivacyTable:" << tableName << query.lastError().text();
        return false;
    }

    QFile file(outputFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "exportPrivacyTable: cannot open" << outputFile;
        return false;
    }

    QTextStream out(&file);
    const QSqlRecord rec = query.record();
    const int colCount = rec.count();

    QStringList colNames;
    for (int i = 0; i < colCount; ++i) {
        colNames << rec.fieldName(i);
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QSet<QString> nullSet(nullColumns.begin(), nullColumns.end());
#else
    QSet<QString> nullSet;
    for (const QString& s : nullColumns) nullSet.insert(s);
#endif

    while (query.next()) {
        QStringList vals;
        for (int i = 0; i < colCount; ++i) {
            const QString& col = colNames.at(i);
            if (col == QLatin1String("profile_id")) {
                // Restore placeholder — written without quotes, not as a SQL literal.
                vals << QStringLiteral("@PROFILE_ID@");
            } else if (nullSet.contains(col)) {
                vals << QStringLiteral("NULL");
            } else {
                const QVariant v = query.value(i);
                if (v.isNull()) {
                    vals << QStringLiteral("NULL");
                } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    const int tid = v.typeId();
#else
                    const int tid = static_cast<int>(v.type());
#endif
                    if (tid == QMetaType::Int     || tid == QMetaType::LongLong ||
                        tid == QMetaType::UInt    || tid == QMetaType::ULongLong) {
                        vals << v.toString();
                    } else if (tid == QMetaType::Double || tid == QMetaType::Float) {
                        vals << QString::number(v.toDouble(), 'g', 17);
                    } else {
                        QString s = v.toString();
                        s.replace(QLatin1Char('\''), QLatin1String("''"));
                        vals << QLatin1Char('\'') + s + QLatin1Char('\'');
                    }
                }
            }
        }
        out << "INSERT INTO " << tableName
            << " (" << colNames.join(", ")
            << ") VALUES (" << vals.join(", ") << ");\n";
    }
    return true;
}

/*!
 * \brief Write a privacy-redacted SQL INSERT file for the profile_preferences table.
 *
 * Rows whose \c key column matches a known personal-data key have their
 * \c value written as NULL.  The \c profile_id column is written as the
 * \@PROFILE_ID\@ placeholder.  All other rows and columns are exported verbatim.
 *
 * \param db          Live database connection.
 * \param whereClause SQL WHERE body (no keyword).
 * \param outputFile  Destination .sql file path.
 * \return true on success.
 */
static bool exportPrivacyPreferences(QSqlDatabase& db,
                                      const QString& whereClause,
                                      const QString& outputFile)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    static const QSet<QString> kPersonalKeys = {
        // UserInfo keys (STR_UI_*)
        QStringLiteral("FirstName"),   QStringLiteral("LastName"),
        QStringLiteral("DOB"),         QStringLiteral("Address"),
        QStringLiteral("Phone"),       QStringLiteral("EmailAddress"),
        QStringLiteral("Country"),     QStringLiteral("Height"),
        QStringLiteral("Gender"),      QStringLiteral("Password"),
        // DoctorInfo keys (STR_DI_*) — also stored in profile_preferences via PrefSettings
        QStringLiteral("DoctorName"),  QStringLiteral("DoctorPhone"),
        QStringLiteral("DoctorEmail"), QStringLiteral("DoctorPractice"),
        QStringLiteral("DoctorAddress"), QStringLiteral("DoctorPatientID")
    };
#else
    static const QSet<QString> kPersonalKeys = []() {
        QSet<QString> s;
        s << QStringLiteral("FirstName")   << QStringLiteral("LastName")
          << QStringLiteral("DOB")         << QStringLiteral("Address")
          << QStringLiteral("Phone")       << QStringLiteral("EmailAddress")
          << QStringLiteral("Country")     << QStringLiteral("Height")
          << QStringLiteral("Gender")      << QStringLiteral("Password")
          << QStringLiteral("DoctorName")  << QStringLiteral("DoctorPhone")
          << QStringLiteral("DoctorEmail") << QStringLiteral("DoctorPractice")
          << QStringLiteral("DoctorAddress") << QStringLiteral("DoctorPatientID");
        return s;
    }();
#endif

    QSqlQuery query(db);
    if (!query.exec(
            QString("SELECT * FROM profile_preferences WHERE %1").arg(whereClause))) {
        qWarning() << "exportPrivacyPreferences:" << query.lastError().text();
        return false;
    }

    QFile file(outputFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "exportPrivacyPreferences: cannot open" << outputFile;
        return false;
    }

    QTextStream out(&file);
    const QSqlRecord rec = query.record();
    const int colCount   = rec.count();

    QStringList colNames;
    for (int i = 0; i < colCount; ++i) {
        colNames << rec.fieldName(i);
    }
    const int keyCol = colNames.indexOf(QStringLiteral("key"));
    const int valCol = colNames.indexOf(QStringLiteral("value"));

    while (query.next()) {
        const QString rowKey    = (keyCol >= 0) ? query.value(keyCol).toString() : QString();
        const bool    blankThis = kPersonalKeys.contains(rowKey);

        QStringList vals;
        for (int i = 0; i < colCount; ++i) {
            const QString& col = colNames.at(i);
            if (col == QLatin1String("profile_id")) {
                vals << QStringLiteral("@PROFILE_ID@");
            } else if (i == valCol && blankThis) {
                vals << QStringLiteral("NULL");
            } else {
                const QVariant v = query.value(i);
                if (v.isNull()) {
                    vals << QStringLiteral("NULL");
                } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    const int tid = v.typeId();
#else
                    const int tid = static_cast<int>(v.type());
#endif
                    if (tid == QMetaType::Int     || tid == QMetaType::LongLong ||
                        tid == QMetaType::UInt    || tid == QMetaType::ULongLong) {
                        vals << v.toString();
                    } else if (tid == QMetaType::Double || tid == QMetaType::Float) {
                        vals << QString::number(v.toDouble(), 'g', 17);
                    } else {
                        QString s = v.toString();
                        s.replace(QLatin1Char('\''), QLatin1String("''"));
                        vals << QLatin1Char('\'') + s + QLatin1Char('\'');
                    }
                }
            }
        }
        out << "INSERT INTO profile_preferences"
            << " (" << colNames.join(", ")
            << ") VALUES (" << vals.join(", ") << ");\n";
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Constructor / Destructor
// ---------------------------------------------------------------------------

/*!
 * \brief Construct a ProfileBackup for the specified profile.
 *
 * The output path defaults to the user's Documents folder so that a minimal
 * call to createBackup() (after setOutputPath) does not need the caller to
 * supply a path.
 */
ProfileBackup::ProfileBackup(qint64 profileId, QObject* parent)
    : QObject(parent)
    , m_profileId(profileId)
    , m_backupSize(0)
    , m_uncompressedSize(0)
    , m_includeDisabled(true)
    , m_compress(true)
    , m_privacyMode(false)
{
    // Default output directory: Documents/OSCAR Backups
    m_outputPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                   + "/OSCAR Backups";
    qDebug() << "ProfileBackup::ProfileBackup default output path" << m_outputPath;
}

ProfileBackup::~ProfileBackup()
{
    // QTemporaryDir used inside createBackup() is auto-cleaned on scope exit.
}

// ---------------------------------------------------------------------------
//  Configuration setters
// ---------------------------------------------------------------------------

void ProfileBackup::setOutputPath(const QString& path)
{
    m_outputPath = path;
    qDebug() << "ProfileBackup::setOutputPath" << m_outputPath;
}

void ProfileBackup::setIncludeDisabledSessions(bool include)
{
    m_includeDisabled = include;
}

void ProfileBackup::setCompression(bool compress)
{
    qDebug() << "ProfileBackup::setCompression" << compress;
    m_compress = compress;
}

void ProfileBackup::setDateRange(const QDate& startDate, const QDate& endDate)
{
    m_startDate = startDate;
    m_endDate   = endDate;
    qDebug() << "ProfileBackup::setDateRange" << startDate << endDate;
}

void ProfileBackup::setPrivacyMode(bool enable)
{
    m_privacyMode = enable;
}

void ProfileBackup::setIncludeSDData(bool include)
{
    m_includeSDData = include;
}

void ProfileBackup::setFilename(const QString& filename)
{
    m_overrideFilename = filename;
}

// ---------------------------------------------------------------------------
//  Execution
// ---------------------------------------------------------------------------

/*!
 * \brief Run the full backup sequence and produce the .oscar package.
 *
 * Steps:
 *  1. Validate the profile and output directory.
 *  2. Create a QTemporaryDir working area.
 *  3. Export profile-level tables (profiles, user_info, etc.).
 *  4. Export machines and all session sub-tables.
 *  5. Export daily_summaries.
 *  6. Write manifest.json.
 *  7. ZIP everything into the .oscar file.
 *
 * Progress is reported via progressChanged() at each step.
 * On success emits backupCompleted(); on failure emits backupFailed().
 */
bool ProfileBackup::createBackup()
{
    m_errorMessage.clear();
    m_backupPath.clear();
    m_backupSize       = 0;
    m_uncompressedSize = 0;

    emit progressChanged(5, QStringLiteral("Validating profile..."));

    if (!validateProfile()) {
        emit backupFailed(m_errorMessage);
        return false;
    }

    // Determine the output file path.  Use the user-specified filename when set,
    // otherwise auto-generate from profile username and date range.
    if (!m_overrideFilename.isEmpty()) {
        m_backupPath = m_outputPath + QLatin1Char('/') + m_overrideFilename;
    } else {
        m_backupPath = generateBackupPath();
        if (m_backupPath.isEmpty()) {
            m_errorMessage = QStringLiteral("Failed to generate backup file path");
            emit backupFailed(m_errorMessage);
            return false;
        }
    }

    // Resolve SD data path now (while DB is confirmed open).
    m_profileDataDir.clear();
    if (m_includeSDData) {
        m_profileDataDir = getProfileDataDir();
        if (m_profileDataDir.isEmpty()) {
            m_errorMessage = QStringLiteral("Failed to determine profile data directory for SD backup");
            emit backupFailed(m_errorMessage);
            return false;
        }
        if (!QDir(m_profileDataDir).exists()) {
            qWarning() << "ProfileBackup: profile data directory does not exist, SD data will be skipped:"
                       << m_profileDataDir;
        }
    }

    emit progressChanged(10, QStringLiteral("Preparing temporary workspace..."));

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        m_errorMessage = QStringLiteral("Failed to create temporary directory");
        emit backupFailed(m_errorMessage);
        return false;
    }

    const QString tmpPath = tmpDir.path();
    const QString dbDir   = tmpPath + QStringLiteral("/database");
    if (!QDir().mkpath(dbDir)) {
        m_errorMessage = QString("Failed to create temp database dir: %1").arg(dbDir);
        emit backupFailed(m_errorMessage);
        return false;
    }

    // --- Export phase ------------------------------------------------------
    qDebug() << "ProfileBackup::createBackup entering export phase";
    emit progressChanged(15, QStringLiteral("Exporting profile data..."));
    if (!exportProfileMetadata(tmpPath)) {
        emit backupFailed(m_errorMessage);
        return false;
    }

    emit progressChanged(40, QStringLiteral("Exporting sessions and events..."));
    if (!exportMachinesAndSessions(tmpPath)) {
        emit backupFailed(m_errorMessage);
        return false;
    }

    emit progressChanged(70, QStringLiteral("Exporting daily summaries..."));
    if (!exportDailySummaries(tmpPath)) {
        emit backupFailed(m_errorMessage);
        return false;
    }

    // Record uncompressed size before packaging.
    m_uncompressedSize = directorySize(dbDir);

    // --- Manifest ----------------------------------------------------------
    emit progressChanged(80, QStringLiteral("Writing manifest..."));
    QJsonObject manifestJson;
    if (!createManifest(tmpPath, manifestJson)) {
        emit backupFailed(m_errorMessage);
        return false;
    }
    qDebug() << "ProfileBackup: manifest has" << manifestJson.size() << "fields";

    // --- Package -----------------------------------------------------------
    emit progressChanged(90, QStringLiteral("Packaging..."));
    if (!createPackage(tmpPath)) {
        emit backupFailed(m_errorMessage);
        return false;
    }

    // tmpDir is auto-cleaned here when it goes out of scope at function end.

    m_backupSize = QFileInfo(m_backupPath).size();

    emit progressChanged(100, QStringLiteral("Backup complete."));
    emit backupCompleted(m_backupPath);

    qDebug() << "ProfileBackup: written to" << m_backupPath
             << "| compressed:" << m_backupSize
             << "| uncompressed:" << m_uncompressedSize;

    return true;
}

// ---------------------------------------------------------------------------
//  Status / result accessors
// ---------------------------------------------------------------------------

QString ProfileBackup::getErrorMessage() const
{
    return m_errorMessage;
}

QString ProfileBackup::getBackupPath() const
{
    return m_backupPath;
}

qint64 ProfileBackup::getBackupSize() const
{
    return m_backupSize;
}

qint64 ProfileBackup::getUncompressedSize() const
{
    return m_uncompressedSize;
}

bool ProfileBackup::isPartialExport() const
{
    return m_startDate.isValid() || m_endDate.isValid();
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

/*!
 * \brief Validate the profile and output directory.
 *
 * Checks:
 *  1. The database is open.
 *  2. A profile row with m_profileId exists.
 *  3. The output directory exists (or its parent does, so mkpath will succeed)
 *     and is writable.
 */
bool ProfileBackup::validateProfile()
{
    if (!DatabaseManager::instance().isOpen()) {
        m_errorMessage = QStringLiteral("Database is not open");
        return false;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT id FROM profiles WHERE id = :id");
    query.bindValue(":id", m_profileId);
    if (!query.exec()) {
        m_errorMessage = QString("Database error validating profile: %1")
                             .arg(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        m_errorMessage = QString("Profile %1 not found in database").arg(m_profileId);
        return false;
    }

    // Verify the output directory exists and is writable.
    // Directory creation is the user's responsibility (via the Browse button).
    const QDir outDir(m_outputPath);
    if (!outDir.exists()) {
        m_errorMessage = QString("Output directory does not exist: %1").arg(m_outputPath);
        return false;
    }
    QFile probe(m_outputPath + QStringLiteral("/.oscar_write_test"));
    if (!probe.open(QIODevice::WriteOnly)) {
        m_errorMessage = QString("Output directory is not writable: %1").arg(m_outputPath);
        return false;
    }
    probe.close();
    probe.remove();
    return true;
}

/*!
 * \brief Export profile-level tables (always full, not date-filtered).
 *
 * Exports: profiles, user_info, doctor_info, profile_preferences, graph_layouts, channels.
 * When privacy mode is active, personal fields in user_info and doctor_info
 * are replaced with NULL via exportPrivacyTable().
 * All profile_id values are written as the @PROFILE_ID@ placeholder.
 */
bool ProfileBackup::exportProfileMetadata(const QString& tempDir)
{
    const QString dbDir = tempDir + QStringLiteral("/database");
    qDebug() << "ProfileBackup::exportProfileMetadata for dir" << dbDir;

    // profiles — keyed by id, no profile_id column.
    {
        SqlExporter exp;
        const QString where = QString("id = %1").arg(m_profileId);
        if (!exp.exportTable("profiles", where, dbDir + "/profiles.sql")) {
            m_errorMessage = QString("Failed to export profiles: %1").arg(exp.errorMessage());
            return false;
        }
    }

    const QString pidWhere = QString("profile_id = %1").arg(m_profileId);

    // user_info — privacy mode blanks personal fields; normal mode uses SqlExporter.
    if (m_privacyMode) {
        QSqlDatabase db = DatabaseManager::instance().database();
        const QStringList nullCols = {
            "dob", "first_name", "last_name", "address", "phone",
            "email", "country", "height", "gender", "password_hash"
        };
        if (!exportPrivacyTable(db, "user_info", pidWhere, nullCols,
                                dbDir + "/user_info.sql")) {
            m_errorMessage = QStringLiteral("Failed to export user_info (privacy mode)");
            return false;
        }
    } else {
        SqlExporter exp;
        exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});
        if (!exp.exportTable("user_info", pidWhere, dbDir + "/user_info.sql")) {
            m_errorMessage = QString("Failed to export user_info: %1").arg(exp.errorMessage());
            return false;
        }
    }

    // doctor_info — same privacy logic.
    if (m_privacyMode) {
        QSqlDatabase db = DatabaseManager::instance().database();
        const QStringList nullCols = {
            "name", "phone", "email", "practice_name", "address", "patient_id"
        };
        if (!exportPrivacyTable(db, "doctor_info", pidWhere, nullCols,
                                dbDir + "/doctor_info.sql")) {
            m_errorMessage = QStringLiteral("Failed to export doctor_info (privacy mode)");
            return false;
        }
    } else {
        SqlExporter exp;
        exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});
        if (!exp.exportTable("doctor_info", pidWhere, dbDir + "/doctor_info.sql")) {
            m_errorMessage = QString("Failed to export doctor_info: %1").arg(exp.errorMessage());
            return false;
        }
    }

    // profile_preferences — privacy mode blanks the value of personal key rows.
    if (m_privacyMode) {
        QSqlDatabase db = DatabaseManager::instance().database();
        if (!exportPrivacyPreferences(db, pidWhere,
                                      dbDir + "/profile_preferences.sql")) {
            m_errorMessage = QStringLiteral("Failed to export profile_preferences (privacy mode)");
            return false;
        }
    } else {
        SqlExporter exp;
        exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});
        if (!exp.exportTable("profile_preferences", pidWhere,
                             dbDir + "/profile_preferences.sql")) {
            m_errorMessage = QString("Failed to export profile_preferences: %1")
                                 .arg(exp.errorMessage());
            return false;
        }
    }

    // graph_layouts — per-profile current layouts (profile_id IS NOT NULL rows only)
    {
        SqlExporter exp;
        exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});
        if (!exp.exportTable("graph_layouts", pidWhere, dbDir + "/graph_layouts.sql")) {
            m_errorMessage = QString("Failed to export graph_layouts: %1").arg(exp.errorMessage());
            return false;
        }
    }

    // channels
    {
        SqlExporter exp;
        exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});
        if (!exp.exportTable("channels", pidWhere, dbDir + "/channels.sql")) {
            m_errorMessage = QString("Failed to export channels: %1").arg(exp.errorMessage());
            return false;
        }
    }

    // channel_options — lookup value mappings for LOOKUP-type channels (e.g. CPAP mode names).
    // channel_options.channel_id stores the channel code (ChannelID), not the channels.id PK,
    // so no placeholder remapping is needed.  The data is effectively global; during restore
    // INSERT OR IGNORE is used so that rows already present from other profiles are skipped.
    {
        const QString coWhere = QString(
            "channel_id IN (SELECT channel_id FROM channels WHERE profile_id = %1)")
                .arg(m_profileId);
        SqlExporter exp;
        if (!exp.exportTable("channel_options", coWhere, dbDir + "/channel_options.sql")) {
            m_errorMessage = QString("Failed to export channel_options: %1").arg(exp.errorMessage());
            return false;
        }
    }

    return true;
}

/*!
 * \brief Export machines and all session sub-tables.
 *
 * Machines are always exported in full.  Sessions are filtered by date and
 * (optionally) by the enabled flag.  All sub-tables are exported via nested
 * subqueries against the filtered sessions set.
 *
 * Placeholder strategy:
 *  - profile_id  → @PROFILE_ID@   (all tables that carry it)
 *  - machine_id, session_id, etc. → kept as original numeric values;
 *    the restore phase maintains mapping tables for these.
 */
bool ProfileBackup::exportMachinesAndSessions(const QString& tempDir)
{
    const QString dbDir = tempDir + QStringLiteral("/database");
    const QString dateFilter = buildSessionDateFilter();

    // ---- machines ---------------------------------------------------------
    {
        SqlExporter exp;
        exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});
        const QString where = QString("profile_id = %1").arg(m_profileId);
        if (!exp.exportTable("machines", where, dbDir + "/machines.sql")) {
            m_errorMessage = QString("Failed to export machines: %1").arg(exp.errorMessage());
            return false;
        }
    }

    // ---- sessions ---------------------------------------------------------
    // sessions has no profile_id column; machine_id references machines.id (PK).
    QString sessionWhere =
        QString("machine_id IN (SELECT id FROM machines WHERE profile_id = %1)")
        .arg(m_profileId);
    if (!dateFilter.isEmpty()) {
        sessionWhere += QStringLiteral(" AND ") + dateFilter;
    }
    if (!m_includeDisabled) {
        sessionWhere += QStringLiteral(" AND enabled = 1");
    }
    {
        SqlExporter exp;   // no placeholders — sessions has no profile_id
        if (!exp.exportTable("sessions", sessionWhere, dbDir + "/sessions.sql")) {
            m_errorMessage = QString("Failed to export sessions: %1").arg(exp.errorMessage());
            return false;
        }
    }

    // Subquery that produces the set of exported session IDs.
    const QString sessSubq = QString("SELECT id FROM sessions WHERE %1").arg(sessionWhere);
    // WHERE clause used by all tables that have a direct session_id FK.
    const QString bySession = QString("session_id IN (%1)").arg(sessSubq);

    SqlExporter exp;
    exp.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});

    // ---- session_settings (has profile_id) --------------------------------
    if (!exp.exportTable("session_settings", bySession,
                         dbDir + "/session_settings.sql")) {
        m_errorMessage = QString("Failed to export session_settings: %1").arg(exp.errorMessage());
        return false;
    }

    // ---- session_channels (has profile_id) --------------------------------
    if (!exp.exportTable("session_channels", bySession,
                         dbDir + "/session_channels.sql")) {
        m_errorMessage = QString("Failed to export session_channels: %1").arg(exp.errorMessage());
        return false;
    }

    // ---- session_channel_values (no profile_id — references session_channels.id) ---
    {
        SqlExporter scvExp;   // no placeholders
        const QString scvWhere =
            QString("session_channel_id IN (SELECT id FROM session_channels WHERE %1)")
            .arg(bySession);
        if (!scvExp.exportTable("session_channel_values", scvWhere,
                                dbDir + "/session_channel_values.sql")) {
            m_errorMessage = QString("Failed to export session_channel_values: %1")
                                 .arg(scvExp.errorMessage());
            return false;
        }
    }

    // ---- respiratory_events (has profile_id) ------------------------------
    if (!exp.exportTable("respiratory_events", bySession,
                         dbDir + "/respiratory_events.sql")) {
        m_errorMessage = QString("Failed to export respiratory_events: %1").arg(exp.errorMessage());
        return false;
    }

    // ---- session_summaries (has profile_id) -------------------------------
    if (!exp.exportTable("session_summaries", bySession,
                         dbDir + "/session_summaries.sql")) {
        m_errorMessage = QString("Failed to export session_summaries: %1").arg(exp.errorMessage());
        return false;
    }

    // ---- session_slices (no profile_id — references sessions.id) ----------
    {
        SqlExporter sliceExp;   // no placeholders
        if (!sliceExp.exportTable("session_slices", bySession,
                                  dbDir + "/session_slices.sql")) {
            m_errorMessage = QString("Failed to export session_slices: %1")
                                 .arg(sliceExp.errorMessage());
            return false;
        }
    }

    // ---- event_lists (has profile_id) -------------------------------------
    if (!exp.exportTable("event_lists", bySession, dbDir + "/event_lists.sql")) {
        m_errorMessage = QString("Failed to export event_lists: %1").arg(exp.errorMessage());
        return false;
    }

    // ---- event_data (no profile_id — references event_lists.id; contains BLOBs) ---
    {
        SqlExporter edExp;   // no placeholders
        const QString edWhere =
            QString("eventlist_id IN (SELECT id FROM event_lists WHERE %1)").arg(bySession);
        const QStringList blobCols = {
            "data_blob", "data_compressed",
            "data2_blob", "data2_compressed",
            "time_blob",  "time_compressed"
        };
        if (!edExp.exportBlobTable("event_data", edWhere,
                                   dbDir + "/event_data.sql", blobCols)) {
            m_errorMessage = QString("Failed to export event_data: %1").arg(edExp.errorMessage());
            return false;
        }
    }

    return true;
}

/*!
 * \brief Export the daily_summaries table (date-filtered when applicable).
 *
 * The \c date column in daily_summaries is an ISO-8601 text string
 * ("YYYY-MM-DD"), so date filtering is a direct string comparison.
 * profile_id is replaced with the @PROFILE_ID@ placeholder.
 */
bool ProfileBackup::exportDailySummaries(const QString& tempDir)
{
    const QString dbDir = tempDir + QStringLiteral("/database");

    SqlExporter exporter;
    exporter.setColumnPlaceholders({{"profile_id", "@PROFILE_ID@"}});

    QString where = QString("profile_id = %1").arg(m_profileId);
    if (m_startDate.isValid()) {
        where += QString(" AND date >= '%1'").arg(m_startDate.toString(Qt::ISODate));
    }
    if (m_endDate.isValid()) {
        where += QString(" AND date <= '%1'").arg(m_endDate.toString(Qt::ISODate));
    }

    if (!exporter.exportTable("daily_summaries", where, dbDir + "/daily_summaries.sql")) {
        m_errorMessage = QString("Failed to export daily_summaries: %1")
                             .arg(exporter.errorMessage());
        return false;
    }
    return true;
}

/*!
 * \brief Build and save manifest.json into the temporary directory.
 *
 * Queries session/machine/event counts and first/last dates, assembles a
 * BackupManifest, saves it to \a tempDir/manifest.json, and returns the
 * JSON object in \a manifest for any caller that wants to inspect it.
 *
 * m_uncompressedSize must be set before this is called (done in createBackup).
 */
bool ProfileBackup::createManifest(const QString& tempDir, QJsonObject& manifest)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    const QString dbDir = tempDir + QStringLiteral("/database");

    // --- Profile info ------------------------------------------------------
    QSqlQuery profileQ(db);
    profileQ.prepare("SELECT username, data_folder, status FROM profiles WHERE id = :id");
    profileQ.bindValue(":id", m_profileId);
    if (!profileQ.exec() || !profileQ.next()) {
        m_errorMessage = QString("Failed to read profile info for manifest: %1")
                             .arg(profileQ.lastError().text());
        return false;
    }
    const QString username   = profileQ.value(0).toString();
    const QString dataFolder = profileQ.value(1).toString();
    const QString status     = profileQ.value(2).toString();

    // --- Session WHERE (same as used during export) ------------------------
    const QString dateFilter = buildSessionDateFilter();
    QString sessionWhere =
        QString("machine_id IN (SELECT id FROM machines WHERE profile_id = %1)")
        .arg(m_profileId);
    if (!dateFilter.isEmpty()) {
        sessionWhere += QStringLiteral(" AND ") + dateFilter;
    }
    if (!m_includeDisabled) {
        sessionWhere += QStringLiteral(" AND enabled = 1");
    }

    // --- Statistics --------------------------------------------------------
    int machinesCount = 0;
    {
        QSqlQuery q(db);
        if (q.exec(QString("SELECT COUNT(*) FROM machines WHERE profile_id = %1")
                       .arg(m_profileId)) && q.next()) {
            machinesCount = q.value(0).toInt();
        }
    }

    int sessionsCount = 0;
    {
        QSqlQuery q(db);
        if (q.exec(QString("SELECT COUNT(*) FROM sessions WHERE %1").arg(sessionWhere))
                && q.next()) {
            sessionsCount = q.value(0).toInt();
        }
    }

    int eventListsCount = 0;
    {
        QSqlQuery q(db);
        const QString w =
            QString("session_id IN (SELECT id FROM sessions WHERE %1)").arg(sessionWhere);
        if (q.exec(QString("SELECT COUNT(*) FROM event_lists WHERE %1").arg(w)) && q.next()) {
            eventListsCount = q.value(0).toInt();
        }
    }

    QString firstSession, lastSession;
    {
        QSqlQuery q(db);
        const QString sql =
            QString("SELECT MIN(start_time), MAX(start_time) FROM sessions WHERE %1")
            .arg(sessionWhere);
        if (q.exec(sql) && q.next() && !q.value(0).isNull()) {
            auto epochToDate = [](qint64 epoch) {
                return QDateTime::fromMSecsSinceEpoch(epoch, Qt::UTC)
                           .date().toString(Qt::ISODate);
            };
            firstSession = epochToDate(q.value(0).toLongLong());
            lastSession  = epochToDate(q.value(1).toLongLong());
        }
    }

    // --- Assemble manifest -------------------------------------------------
    BackupManifest bm;
    bm.setFormatVersion(QStringLiteral("2.0"));
    bm.setOscarVersion(getVersion().toString());
    bm.setSchemaVersion(DatabaseSchema::CURRENT_SCHEMA_VERSION);
    bm.setProfileInfo(username, m_profileId, dataFolder, status);
    bm.setStatistics(machinesCount, sessionsCount, eventListsCount,
                     firstSession, lastSession, m_uncompressedSize);
    bm.setExportOptions(m_includeDisabled, m_compress,
                        isPartialExport(), m_startDate, m_endDate, m_privacyMode,
                        m_includeSDData);

    // Record the names of exported tables (derived from .sql file names).
    const QStringList sqlFiles =
        QDir(dbDir).entryList({"*.sql"}, QDir::Files, QDir::Name);
    for (const QString& f : sqlFiles) {
        bm.addExportedTable(QFileInfo(f).baseName());
    }

    // Integrity: hash of all SQL files.
    bm.setChecksums(QString(), hashSqlDirectory(dbDir), QString());

    // Write to disk.
    const QString manifestPath = tempDir + QStringLiteral("/manifest.json");
    if (!bm.saveToFile(manifestPath)) {
        m_errorMessage = QString("Failed to write manifest.json: %1").arg(bm.errorMessage());
        return false;
    }

    manifest = bm.toJson();
    return true;
}

/*!
 * \brief ZIP all export files into the final .oscar package.
 *
 * The archive layout is:
 *   manifest.json
 *   database/profiles.sql
 *   database/machines.sql
 *   ... (one .sql per exported table)
 *
 * Writes to m_backupPath which must already be set by createBackup().
 */
bool ProfileBackup::createPackage(const QString& tempDir)
{
    ZipFile zip;
    if (!zip.Open(m_backupPath)) {
        m_errorMessage = QString("Cannot create backup package: %1").arg(m_backupPath);
        return false;
    }

    const QString manifestPath = tempDir + QStringLiteral("/manifest.json");
    if (!zip.AddFile(manifestPath, QStringLiteral("manifest.json"))) {
        zip.Close();
        m_errorMessage = QStringLiteral("Failed to add manifest.json to package");
        return false;
    }

    const QString dbPath = tempDir + QStringLiteral("/database");
    if (!zip.AddDirectory(dbPath, QStringLiteral("database"))) {
        zip.Close();
        m_errorMessage = QStringLiteral("Failed to add database directory to package");
        return false;
    }

    // Add the profile's on-disk SD card data (only for "Everything" backups).
    if (m_includeSDData && !m_profileDataDir.isEmpty() && QDir(m_profileDataDir).exists()) {
        emit progressChanged(92, QStringLiteral("Adding SD card data (may take several minutes)..."));
        if (!zip.AddDirectory(m_profileDataDir, QStringLiteral("sddata"))) {
            zip.Close();
            m_errorMessage = QStringLiteral("Failed to add SD card data to package");
            return false;
        }
    }

    zip.Close();

    if (!QFile::exists(m_backupPath)) {
        m_errorMessage = QString("Package file was not created: %1").arg(m_backupPath);
        return false;
    }
    return true;
}

/*!
 * \brief Generate the output .oscar file path.
 *
 * Queries the profile username from the database, sanitises it for use in
 * a filename, then composes the path according to the naming convention:
 *   Full:    <outputPath>/profile_backup_<user>_<ts>.oscar
 *   Partial: <outputPath>/profile_backup_<user>_<start>_<end>_<ts>.oscar
 *
 * Returns an empty string if the database query fails.
 */
QString ProfileBackup::generateBackupPath() const
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT username FROM profiles WHERE id = :id");
    query.bindValue(":id", m_profileId);
    if (!query.exec() || !query.next()) {
        qWarning() << "ProfileBackup::generateBackupPath: cannot find profile" << m_profileId;
        return QString();
    }

    // Sanitise: keep alphanumeric, underscore, hyphen; replace everything else.
    QString username = query.value(0).toString();
    for (QChar& c : username) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-')) {
            c = QLatin1Char('_');
        }
    }

    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));

    QString fileName;
    if (isPartialExport()) {
        const QString startStr = m_startDate.isValid()
            ? m_startDate.toString(QStringLiteral("yyyyMMdd"))
            : QStringLiteral("begin");
        const QString endStr = m_endDate.isValid()
            ? m_endDate.toString(QStringLiteral("yyyyMMdd"))
            : QStringLiteral("now");
        fileName = QString("profile_backup_%1_%2_%3_%4.oscar")
                       .arg(username, startStr, endStr, ts);
    } else {
        fileName = QString("profile_backup_%1_%2.oscar").arg(username, ts);
    }

    return m_outputPath + QLatin1Char('/') + fileName;
}

/*!
 * \brief Calculate the SHA-256 hex digest of a file.
 *
 * Reads the file in 64 KiB chunks and returns a lowercase hex string
 * prefixed with "sha256:".  Returns an empty string on any I/O error.
 */
QString ProfileBackup::calculateChecksum(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ProfileBackup::calculateChecksum: cannot open" << filePath;
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(65536));
    }
    return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}

/*!
 * \brief Build the SQL WHERE clause fragment for session date filtering.
 *
 * Converts the start/end OSCAR-date QDate values to epoch-millisecond
 * boundaries and returns a fragment suitable for appending to a sessions
 * WHERE clause.  Returns an empty string when no date range is set.
 *
 * An OSCAR day runs from noon local time on the named date to noon local
 * time the following calendar day.  The sessions table stores \c start_time
 * as epoch milliseconds (UTC), so the boundaries are:
 *   Start bound:  \c start_time >= noon(startDate, local)
 *   End bound:    \c start_time <  noon(endDate+1, local)   (inclusive end)
 */
QString ProfileBackup::buildSessionDateFilter() const
{
    if (!m_startDate.isValid() && !m_endDate.isValid()) {
        return QString();
    }

    QStringList parts;
    if (m_startDate.isValid()) {
        // OSCAR day startDate begins at noon local time on that calendar date.
        const qint64 epoch = QDateTime(m_startDate, QTime(12, 0, 0), Qt::LocalTime).toMSecsSinceEpoch();
        parts << QString("start_time >= %1").arg(epoch);
    }
    if (m_endDate.isValid()) {
        // OSCAR day endDate ends at noon local time the following calendar day.
        const qint64 epoch = QDateTime(m_endDate.addDays(1), QTime(12, 0, 0), Qt::LocalTime).toMSecsSinceEpoch();
        parts << QString("start_time < %1").arg(epoch);
    }
    return parts.join(QStringLiteral(" AND "));
}

/*!
 * \brief Resolve the canonical on-disk path of the profile's data directory.
 *
 * Queries the profile username from the database and combines it with the
 * Profiles base path obtained from the global Preferences object.  This uses
 * the same \c {home}/Profiles expression as \c Profiles::Scan() so the result
 * is always consistent with what OSCAR considers the authoritative location.
 *
 * \return Absolute path such as \c C:/OSCAR_Data/Profiles/JohnDoe, or an
 *         empty string if the profile cannot be found in the database.
 */
QString ProfileBackup::getProfileDataDir() const
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT username FROM profiles WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), m_profileId);
    if (!query.exec() || !query.next()) {
        qWarning() << "ProfileBackup::getProfileDataDir: cannot find profile" << m_profileId;
        return QString();
    }
    return p_pref->Get(QStringLiteral("{home}/Profiles"))
           + QLatin1Char('/') + query.value(0).toString();
}
