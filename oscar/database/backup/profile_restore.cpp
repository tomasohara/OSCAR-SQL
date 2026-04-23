/* Profile Restore Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Implements ProfileRestore: imports a .oscar backup package into the current
 * OSCAR database with full ID remapping and transactional atomicity.
 *
 * Phase 3 — Full implementation of all restore methods.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profile_restore.h"
#include "backup_manifest.h"
#include "../database_manager.h"
#include "../database_schema.h"
#include "../../zip.h"
#include "../../SleepLib/preferences.h"

// p_pref is the global Preferences object (defined in SleepLib/profiles.cpp).
// We need it to resolve the canonical Profiles directory path, using the
// same mechanism as Profiles::Scan().
extern Preferences *p_pref;

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QDebug>

// ---------------------------------------------------------------------------
//  File-scope helpers
// ---------------------------------------------------------------------------

/*!
 * \brief Compute SHA-256 of all *.sql files in \a dirPath (sorted by name).
 *
 * Identical to the same helper in profile_backup.cpp.  All file contents are
 * fed into one hash, producing a single hex digest that represents the entire
 * database export directory.
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
 * \brief Recursively copy \a srcDir into \a destDir.
 *
 * \a destDir is created if absent.  Existing files are overwritten.
 * Hidden files and subdirectories are included.
 *
 * \return true on success; false on the first I/O error encountered.
 */
static bool copyDirectoryRecursive(const QString& srcDir, const QString& destDir)
{
    QDir src(srcDir);
    if (!src.exists()) {
        qWarning() << "copyDirectoryRecursive: source does not exist:" << srcDir;
        return false;
    }
    if (!QDir().mkpath(destDir)) {
        qWarning() << "copyDirectoryRecursive: cannot create:" << destDir;
        return false;
    }

    const QFileInfoList entries =
        src.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& fi : entries) {
        const QString srcPath  = fi.absoluteFilePath();
        const QString destPath = destDir + QLatin1Char('/') + fi.fileName();
        if (fi.isDir()) {
            if (!copyDirectoryRecursive(srcPath, destPath)) {
                return false;
            }
        } else {
            QFile::remove(destPath); // overwrite if present
            if (!QFile::copy(srcPath, destPath)) {
                qWarning() << "copyDirectoryRecursive: cannot copy" << srcPath << "to" << destPath;
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
//  SQL INSERT parser
// ---------------------------------------------------------------------------

/*!
 * \brief Holds the components of a parsed SQL INSERT statement.
 */
struct InsertStatement {
    QString     tableName; ///< Table being inserted into.
    QStringList columns;   ///< Column names in the INSERT list.
    QStringList values;    ///< Value tokens matching \c columns by index.
};

/*!
 * \brief Convert a SQL literal token (as produced by SqlExporter) to a QVariant.
 *
 * Handles:
 * - \c NULL                → invalid QVariant (SQL NULL)
 * - \c X'hex'              → QByteArray (BLOB)
 * - \c 'text'              → QString; unescapes \c '' → \c ', \c \\n → newline,
 *                            \c \\r → carriage return, \c \\\\ → backslash
 * - integer literal        → qint64
 * - real literal           → double
 *
 * \param token  A single SQL literal token as returned by tokenizeValues().
 * \return QVariant suitable for QSqlQuery::addBindValue().
 */
static QVariant sqlLiteralToVariant(const QString& token)
{
    if (token == QLatin1String("NULL")) {
        return QVariant();
    }

    // Hex BLOB literal: X'...' or x'...'
    if (token.length() >= 3
            && token[0].toUpper() == QLatin1Char('X')
            && token[1] == QLatin1Char('\'')
            && token.endsWith(QLatin1Char('\''))) {
        const QString hex = token.mid(2, token.length() - 3);
        return QVariant(QByteArray::fromHex(hex.toLatin1()));
    }

    // Single-quoted string literal
    if (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\''))) {
        const QString inner = token.mid(1, token.length() - 2);
        QString result;
        result.reserve(inner.length());
        for (int i = 0; i < inner.length(); ++i) {
            if (inner[i] == QLatin1Char('\\') && i + 1 < inner.length()) {
                const QChar next = inner[i + 1];
                if (next == QLatin1Char('n')) {
                    result += QLatin1Char('\n');
                    ++i;
                } else if (next == QLatin1Char('r')) {
                    result += QLatin1Char('\r');
                    ++i;
                } else if (next == QLatin1Char('\\')) {
                    result += QLatin1Char('\\');
                    ++i;
                } else {
                    result += inner[i]; // unknown escape: keep the backslash
                }
            } else if (inner[i] == QLatin1Char('\'')
                       && i + 1 < inner.length()
                       && inner[i + 1] == QLatin1Char('\'')) {
                result += QLatin1Char('\'');
                ++i;
            } else {
                result += inner[i];
            }
        }
        return QVariant(result);
    }

    // Integer
    bool ok = false;
    const qint64 iv = token.toLongLong(&ok);
    if (ok) return QVariant(iv);

    // Real
    const double dv = token.toDouble(&ok);
    if (ok) return QVariant(dv);

    // Fallback: return as string (e.g. unrecognised placeholder left over)
    return QVariant(token);
}

/*!
 * \brief Tokenize the VALUES list of an INSERT statement.
 *
 * Handles: NULL, integers, reals, single-quoted strings (with '' escaping),
 * X'...' hex BLOB literals, and bare placeholder tokens such as @PROFILE_ID@.
 *
 * \param s  Raw content between the outer parentheses of the VALUES clause.
 * \return List of value token strings.
 */
static QStringList tokenizeValues(const QString& s)
{
    QStringList tokens;
    int i = 0;
    const int n = s.length();

    while (i < n) {
        // Skip commas and surrounding whitespace between tokens.
        while (i < n && (s[i] == QLatin1Char(',') || s[i].isSpace())) {
            ++i;
        }
        if (i >= n) break;

        QString tok;

        if (s[i] == QLatin1Char('\'')) {
            // Single-quoted string — handle '' as escaped single quote.
            tok += s[i++];
            while (i < n) {
                if (s[i] == QLatin1Char('\'')) {
                    tok += s[i++];
                    if (i < n && s[i] == QLatin1Char('\'')) {
                        tok += s[i++]; // escaped quote inside string
                    } else {
                        break; // end of quoted string
                    }
                } else {
                    tok += s[i++];
                }
            }
        } else if (i + 1 < n
                   && s[i].toUpper() == QLatin1Char('X')
                   && s[i + 1] == QLatin1Char('\'')) {
            // Hex BLOB literal  X'...'
            tok += s[i++]; // X
            tok += s[i++]; // '
            while (i < n && s[i] != QLatin1Char('\'')) {
                tok += s[i++];
            }
            if (i < n) tok += s[i++]; // closing '
        } else {
            // NULL, number, or placeholder token — read until comma or space.
            while (i < n && s[i] != QLatin1Char(',') && !s[i].isSpace()) {
                tok += s[i++];
            }
        }

        if (!tok.isEmpty()) {
            tokens.append(tok);
        }
    }

    return tokens;
}

/*!
 * \brief Parse a single SQL INSERT statement into its components.
 *
 * Expected format (as produced by SqlExporter):
 *   INSERT INTO tablename (col1, col2, ...) VALUES (val1, val2, ...);
 *
 * \param line  A single SQL line (trimmed).
 * \param out   Populated with table name, columns, and values on success.
 * \return true if the line is a valid INSERT and columns.count() == values.count().
 */
static bool parseInsert(const QString& line, InsertStatement& out)
{
    // Must start with INSERT (case insensitive).
    if (!line.startsWith(QLatin1String("INSERT"), Qt::CaseInsensitive)) {
        return false;
    }

    // Locate "INTO ".
    int pos = line.indexOf(QLatin1String("INTO "), Qt::CaseInsensitive);
    if (pos < 0) return false;
    pos += 5;
    while (pos < line.length() && line[pos].isSpace()) ++pos;

    // Read table name (up to whitespace or '(').
    int nameEnd = pos;
    while (nameEnd < line.length()
           && !line[nameEnd].isSpace()
           && line[nameEnd] != QLatin1Char('(')) {
        ++nameEnd;
    }
    out.tableName = line.mid(pos, nameEnd - pos);
    pos = nameEnd;

    // Advance to the opening '(' of the column list.
    while (pos < line.length() && line[pos] != QLatin1Char('(')) ++pos;
    if (pos >= line.length()) return false;
    ++pos; // skip '('

    // Read column names up to the matching ')'.
    const int colClose = line.indexOf(QLatin1Char(')'), pos);
    if (colClose < 0) return false;
    const QString colStr = line.mid(pos, colClose - pos);
    out.columns.clear();
    for (const QString& c : colStr.split(QLatin1Char(','))) {
        out.columns.append(c.trimmed());
    }
    pos = colClose + 1;

    // Find "VALUES".
    pos = line.indexOf(QLatin1String("VALUES"), pos, Qt::CaseInsensitive);
    if (pos < 0) return false;
    pos += 6;
    while (pos < line.length() && line[pos].isSpace()) ++pos;

    // Advance to the '(' of the values list.
    if (pos >= line.length() || line[pos] != QLatin1Char('(')) return false;
    ++pos;

    // The exporter always writes "INSERT INTO ... VALUES (...);", with no
    // trailing whitespace.  The closing ')' is therefore always the second-to-
    // last character of the trimmed line.  Using lastIndexOf(");") is wrong
    // here because it would match ')' characters that appear inside quoted
    // string values (e.g. json_value fields that contain ");").
    int valClose = -1;
    if (line.endsWith(QLatin1String(");"))) {
        valClose = line.length() - 2; // position of the closing ')'
    } else if (line.endsWith(QLatin1Char(')'))) {
        valClose = line.length() - 1;
    }
    if (valClose < pos) return false;

    const QString valStr = line.mid(pos, valClose - pos);
    out.values = tokenizeValues(valStr);

    return out.columns.count() == out.values.count();
}

// ---------------------------------------------------------------------------
//  Constructor / Destructor
// ---------------------------------------------------------------------------

/*!
 * \brief Construct a ProfileRestore targeting the given .oscar package.
 *
 * No I/O is performed in the constructor; call validatePackage() first.
 */
ProfileRestore::ProfileRestore(const QString& packagePath, QObject* parent)
    : QObject(parent)
    , m_packagePath(packagePath)
    , m_newProfileId(-1)
    , m_resolution(ConflictResolution::Abort)
{
    qDebug() << "ProfileRestore::ProfileRestore constructor";
}

/*!
 * \brief Destructor — removes the temporary extraction directory if present.
 */
ProfileRestore::~ProfileRestore()
{
    if (!m_tempDir.isEmpty()) {
        QDir(m_tempDir).removeRecursively();
        m_tempDir.clear();
    }
}

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------

void ProfileRestore::setConflictResolution(ConflictResolution strategy)
{
    m_resolution = strategy;
}

void ProfileRestore::setNewUsername(const QString& username)
{
    m_newUsername = username;
    qDebug() << "ProfileRestore::setNewUserName" << username;
}

// ---------------------------------------------------------------------------
//  Validation — Group 1: package extraction and manifest loading
// ---------------------------------------------------------------------------

/*!
 * \brief Extract the .oscar ZIP archive to a private temporary directory.
 *
 * Populates m_tempDir on success.  The directory is removed in the
 * destructor (or on the next call to validatePackage()).
 *
 * \return true on success.
 */
bool ProfileRestore::extractPackage()
{
    // Remove any previous extraction.
    if (!m_tempDir.isEmpty()) {
        QDir(m_tempDir).removeRecursively();
        m_tempDir.clear();
    }

    // Create a unique temp directory under the system temp path.
    const QString base = QDir::tempPath()
                         + QStringLiteral("/oscar_restore_")
                         + QString::number(QDateTime::currentMSecsSinceEpoch());

    if (!QDir().mkpath(base)) {
        m_errorMessage = QStringLiteral("Failed to create temporary directory for extraction");
        return false;
    }
    m_tempDir = base;

    UnzipFile zip;
    if (!zip.Open(m_packagePath)) {
        m_errorMessage = QString("Cannot open backup package: %1").arg(m_packagePath);
        QDir(m_tempDir).removeRecursively();
        m_tempDir.clear();
        return false;
    }

    if (!zip.ExtractAll(m_tempDir)) {
        zip.Close();
        m_errorMessage = QString("Failed to extract backup package: %1").arg(m_packagePath);
        QDir(m_tempDir).removeRecursively();
        m_tempDir.clear();
        return false;
    }

    zip.Close();
    return true;
}

/*!
 * \brief Load and validate manifest.json from the extracted directory.
 *
 * On success, populates m_manifestJson and returns the same JSON via the
 * \a manifest output parameter.
 *
 * \param manifest  Output parameter filled with the loaded manifest.
 * \return true on success.
 */
bool ProfileRestore::parseManifest(QJsonObject& manifest)
{
    const QString manifestPath = m_tempDir + QStringLiteral("/manifest.json");

    qDebug() << "ProfileRestore::parseManifest" << manifestPath;

    BackupManifest bm;
    if (!bm.loadFromFile(manifestPath)) {
        m_errorMessage = QString("Cannot load manifest.json: %1").arg(bm.errorMessage());
        return false;
    }
    if (!bm.isValid()) {
        m_errorMessage = QString("manifest.json is not valid: %1").arg(bm.errorMessage());
        return false;
    }

    manifest       = bm.toJson();
    m_manifestJson = manifest;
    return true;
}

/*!
 * \brief Validate the .oscar package before attempting a restore.
 *
 * Steps:
 *  1. Verify the file exists and is readable.
 *  2. Extract the ZIP archive to a temp directory.
 *  3. Parse and structurally validate manifest.json.
 *  4. Verify the database-export checksum if one is recorded.
 *
 * \return true if the package is valid; false otherwise (call getErrorMessage()).
 */
bool ProfileRestore::validatePackage()
{
    m_errorMessage.clear();
    m_manifestJson = QJsonObject(); // reset

    qDebug() << "ProfileRestore::validatePackage entered";
    if (!QFile::exists(m_packagePath)) {
        m_errorMessage = QString("ProfileRestore::validatePackage Backup file not found: %1").arg(m_packagePath);
        return false;
    }

    if (!extractPackage()) {
        return false;
    }

    QJsonObject manifest;
    if (!parseManifest(manifest)) {
        return false;
    }

    // Optional checksum verification: compare the stored DB-export hash with
    // a fresh hash of the extracted *.sql files.
    const QString storedHash =
        manifest[QStringLiteral("checksums")].toObject()
                [QStringLiteral("database_export")].toString();

    if (!storedHash.isEmpty()) {
        const QString dbDir    = m_tempDir + QStringLiteral("/database");
        const QString computed = hashSqlDirectory(dbDir);
        // Fail if computation failed (I/O error → empty) OR hash doesn't match.
        if (computed.isEmpty() || computed != storedHash) {
            m_errorMessage = computed.isEmpty()
                ? QStringLiteral(
                    "Backup integrity check failed: could not compute checksum "
                    "of extracted database files.")
                : QStringLiteral(
                    "Backup integrity check failed: database export checksum mismatch. "
                    "The file may be corrupted or tampered with.");
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
//  Validation — Group 2: compatibility and conflict detection
// ---------------------------------------------------------------------------

/*!
 * \brief Check that the backup schema version is compatible with the live database.
 *
 * Accepts backups whose schema_version falls in the range
 * [MIN_RESTORE_SCHEMA_VERSION, CURRENT_SCHEMA_VERSION].
 *
 * - Newer than CURRENT: reject — the user must upgrade OSCAR first.
 * - Older than MIN_RESTORE: reject — the gap is too large to bridge safely.
 * - Between MIN and CURRENT (exclusive): accept with a warning surfaced by the
 *   dialog.  executeSqlFile() will silently skip any tables that no longer exist
 *   in the current schema; new columns added since the backup receive their
 *   DEFAULT values.
 * - Equal to CURRENT: accept silently.
 *
 * \return true if compatible; false otherwise.
 */
bool ProfileRestore::checkCompatibility()
{
    if (m_manifestJson.isEmpty()) {
        m_errorMessage = QStringLiteral(
            "validatePackage() must be called successfully before checkCompatibility().");
        return false;
    }

    const int backupSchema = m_manifestJson[QStringLiteral("schema_version")].toInt(0);

    if (backupSchema > DatabaseSchema::CURRENT_SCHEMA_VERSION) {
        m_errorMessage = QString(
            "This backup was created with a newer version of OSCAR (schema v%1) "
            "than the one currently installed (schema v%2).  "
            "Please upgrade OSCAR before restoring this backup.")
            .arg(backupSchema)
            .arg(DatabaseSchema::CURRENT_SCHEMA_VERSION);
        return false;
    }

    if (backupSchema < DatabaseSchema::MIN_RESTORE_SCHEMA_VERSION) {
        m_errorMessage = QString(
            "This backup was created with schema v%1, which is too old to restore "
            "directly (minimum supported version is v%2).  "
            "Please use an older version of OSCAR to restore this backup, then "
            "re-export your data.")
            .arg(backupSchema)
            .arg(DatabaseSchema::MIN_RESTORE_SCHEMA_VERSION);
        return false;
    }

    // backupSchema is in [MIN_RESTORE_SCHEMA_VERSION, CURRENT_SCHEMA_VERSION] — accept.
    // Tables present in the backup but absent from the current schema will be
    // skipped by executeSqlFile(); new columns receive their DEFAULT values.

    const QString fmtVersion =
        m_manifestJson[QStringLiteral("format_version")].toString();
    if (fmtVersion != QLatin1String("2.0")) {
        m_errorMessage = QString(
            "Unsupported backup format version '%1' (expected '2.0').  "
            "This backup may have been created by a different version of OSCAR.")
            .arg(fmtVersion);
        return false;
    }

    return true;
}

/*!
 * \brief Detect whether the backup username already exists in the database.
 *
 * \return ConflictStatus::UsernameExists if a profile with that username is
 *         already present; ConflictStatus::None otherwise (including if the
 *         manifest has not been loaded).
 */
ConflictStatus ProfileRestore::checkConflicts(const QString& targetUsername)
{
    if (m_manifestJson.isEmpty()) {
        return ConflictStatus::None;
    }

    // Use the supplied target name, or fall back to the manifest's original username.
    const QString nameToCheck = targetUsername.isEmpty()
        ? m_manifestJson[QStringLiteral("profile")].toObject()
                        [QStringLiteral("username")].toString()
        : targetUsername;

    if (nameToCheck.isEmpty()) {
        return ConflictStatus::None;
    }

    if (DatabaseManager::instance().isOpen()) {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM profiles WHERE username = :u"));
        query.bindValue(QStringLiteral(":u"), nameToCheck);

        if (query.exec() && query.next() && query.value(0).toInt() > 0) {
            return ConflictStatus::UsernameExists;
        }
    }

    // Also treat an existing profile data directory as a conflict.
    // This catches the case where the directory was left on disk after a DB entry
    // was deleted, or where a previous restore created the directory but not the
    // DB row.  Either condition is enough to require conflict resolution.
    const QString profileDir =
        p_pref->Get(QStringLiteral("{home}/Profiles")) + QLatin1Char('/') + nameToCheck;
    if (QDir(profileDir).exists()) {
        return ConflictStatus::UsernameExists;
    }

    return ConflictStatus::None;
}

// ---------------------------------------------------------------------------
//  Execution
// ---------------------------------------------------------------------------

/*!
 * \brief Import the backup package into the database.
 *
 * Orchestrates the full restore:
 *  1. Validates that validatePackage() has been called.
 *  2. Resolves any username conflict using the configured strategy.
 *  3. Executes the database restore inside a single transaction.
 *  4. Validates restored counts against the manifest statistics.
 *  5. Emits restoreCompleted() or restoreFailed() on finish.
 *
 * \return true on success; false on failure (call getErrorMessage()).
 */
bool ProfileRestore::restoreProfile()
{
    qDebug () << "ProfileRestore::restoreProfile entered";
    m_errorMessage.clear();
    m_newProfileId = -1;
    m_cancelRequested.store(false);

    // Clear all ID mapping tables from any previous run.
    m_profileIdMap.clear();
    m_machineIdMap.clear();
    m_sessionIdMap.clear();
    m_sessionChannelIdMap.clear();
    m_eventListIdMap.clear();

    emit progressChanged(0, QStringLiteral("Preparing restore..."));

    if (m_manifestJson.isEmpty()) {
        m_errorMessage = QStringLiteral(
            "validatePackage() must be called successfully before restoreProfile().");
        emit restoreFailed(m_errorMessage);
        return false;
    }

    if (!DatabaseManager::instance().isOpen()) {
        m_errorMessage = QStringLiteral("Database is not open.");
        emit restoreFailed(m_errorMessage);
        return false;
    }

    // Determine the username that will be used for the restored profile.
    const QString origUsername =
        m_manifestJson[QStringLiteral("profile")].toObject()
                      [QStringLiteral("username")].toString();

    if (m_newUsername.isEmpty()) {
        m_newUsername = origUsername;
    }

    // Resolve username conflicts, checking against the target name.
    if (checkConflicts(m_newUsername) == ConflictStatus::UsernameExists) {
        const QString resolved = resolveUsernameConflict(m_newUsername);
        if (resolved.isEmpty()) {
            // resolveUsernameConflict() already set m_errorMessage.
            emit restoreFailed(m_errorMessage);
            return false;
        }
        m_newUsername = resolved;
    }

    emit progressChanged(10, QStringLiteral("Restoring database..."));

    if (!restoreInTransaction()) {
        emit restoreFailed(m_errorMessage);
        return false;
    }

    // Create the profile's data directory on the filesystem.
    // Profiles::Scan() checks for the directory's existence and marks the
    // profile as "missing" (and skips loading it) if the directory is absent.
    // Use p_pref->Get("{home}/Profiles") — the same expression Profiles::Scan()
    // uses — so the path is guaranteed to resolve identically.
    const QString profileDataDir =
        p_pref->Get(QStringLiteral("{home}/Profiles")) + QLatin1Char('/') + m_newUsername;
    if (!QDir().mkpath(profileDataDir)) {
        // Non-fatal: the restore is committed.  The directory will be created
        // on first use (e.g., SD card import), but the profile may appear as
        // "missing" until then.
        qWarning() << "ProfileRestore: could not create profile data directory:"
                   << profileDataDir;
    } else {
        qDebug() << "ProfileRestore: created profile data directory:" << profileDataDir;
    }

    // Restore SD card data if the backup includes it.
    m_includesSDData = m_manifestJson[QStringLiteral("export_options")]
                           .toObject()[QStringLiteral("includes_sd_data")].toBool(false);
    if (m_includesSDData) {
        emit progressChanged(80, QStringLiteral("Restoring SD card data (may take several minutes)..."));
        if (!restoreSDData()) {
            // Non-fatal: the DB restore succeeded.  The profile is fully usable;
            // the user can reimport their SD card to repopulate on-disk data.
            qWarning() << "ProfileRestore: SD data restore failed (DB restore OK):" << m_errorMessage;
            m_errorMessage.clear();
        }
    }

    emit progressChanged(100, QStringLiteral("Restore complete."));
    emit restoreCompleted(m_newProfileId, m_newUsername);
    return true;
}

void ProfileRestore::requestCancel()
{
    m_cancelRequested.store(true);
}

// ---------------------------------------------------------------------------
//  Status / result accessors
// ---------------------------------------------------------------------------

QString ProfileRestore::getErrorMessage() const
{
    return m_errorMessage;
}

qint64 ProfileRestore::getRestoredProfileId() const
{
    return m_newProfileId;
}

QString ProfileRestore::getRestoredUsername() const
{
    return m_newUsername;
}

QJsonObject ProfileRestore::manifestJson() const
{
    return m_manifestJson;
}

// ---------------------------------------------------------------------------
//  Private helpers — conflict resolution
// ---------------------------------------------------------------------------

/*!
 * \brief Resolve a username conflict using the configured strategy.
 *
 * - Abort   → sets m_errorMessage and returns an empty string.
 * - Rename  → returns m_newUsername if set, otherwise generates
 *             \c <original>_restored_<yyyyMMdd_HHmmss>.
 * - Replace → returns \a originalUsername unchanged.  The actual deletion of
 *             the existing profile is deferred to restoreInTransaction() so
 *             that the DELETE and the subsequent re-insert are wrapped in the
 *             same database transaction — preventing data loss if the restore
 *             fails after the delete has already been committed.
 *
 * \param originalUsername  Username read from the backup manifest.
 * \return Resolved username, or an empty string on Abort.
 */
QString ProfileRestore::resolveUsernameConflict(const QString& originalUsername)
{
    switch (m_resolution) {

    case ConflictResolution::Abort:
        m_errorMessage = QString(
            "A profile named '%1' already exists. "
            "Set a conflict resolution strategy (Rename or Replace) and retry.")
            .arg(originalUsername);
        return QString();

    case ConflictResolution::Rename:
        if (!m_newUsername.isEmpty() && m_newUsername != originalUsername) {
            return m_newUsername;
        }
        // Auto-generate: <original>_restored_<timestamp>
        return originalUsername
               + QStringLiteral("_restored_")
               + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));

    case ConflictResolution::Replace:
        // The actual DELETE happens inside the transaction in restoreInTransaction().
        return originalUsername;

    } // switch

    return QString(); // unreachable
}

/*!
 * \brief Copy the \c sddata/ subtree from the extracted package to the
 *        restored profile's on-disk data directory.
 *
 * The target directory (\c Profiles/<newname>/) has already been created by
 * restoreProfile().  For the Replace resolution strategy, any existing content
 * in that directory is cleared first so the restored SD data is authoritative.
 *
 * \return true on success; false on any I/O error (sets m_errorMessage).
 */
bool ProfileRestore::restoreSDData()
{
    const QString sdDataDir = m_tempDir + QStringLiteral("/sddata");
    if (!QDir(sdDataDir).exists()) {
        qWarning() << "ProfileRestore::restoreSDData: no sddata/ directory found in extracted package";
        return false;
    }

    const QString targetDir =
        p_pref->Get(QStringLiteral("{home}/Profiles")) + QLatin1Char('/') + m_newUsername;

    // For Replace: clear the existing directory content before writing.
    // This ensures stale files from the old profile do not persist.
    if (m_resolution == ConflictResolution::Replace) {
        const QFileInfoList entries = QDir(targetDir).entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo& fi : entries) {
            if (fi.isDir()) {
                QDir(fi.absoluteFilePath()).removeRecursively();
            } else {
                QFile::remove(fi.absoluteFilePath());
            }
        }
    }

    qDebug() << "ProfileRestore::restoreSDData: copying to" << targetDir;
    if (!copyDirectoryRecursive(sdDataDir, targetDir)) {
        m_errorMessage = QString("Failed to copy SD card data to profile directory: %1")
                             .arg(targetDir);
        return false;
    }

    qDebug() << "ProfileRestore::restoreSDData: SD data restored successfully";
    return true;
}

// ---------------------------------------------------------------------------
//  Private helpers — SQL execution with ID remapping
// ---------------------------------------------------------------------------

/*!
 * \brief Replace the @PROFILE_ID@ placeholder in a SQL statement.
 *
 * This is the only placeholder written by the backup phase.  All other FK
 * remapping is performed directly in executeSqlFile() when the column names
 * are known.
 *
 * \param sql  Raw INSERT statement from a .sql export file.
 * \return Statement with @PROFILE_ID@ replaced by the new profile ID, or
 *         the original string unchanged if the profile ID is not yet known.
 */
QString ProfileRestore::parseAndRemapSql(const QString& sql)
{
    if (m_newProfileId < 0) {
        return sql;
    }
    QString result = sql;
    result.replace(QStringLiteral("@PROFILE_ID@"),
                   QString::number(m_newProfileId));
    return result;
}

/*!
 * \brief Execute all INSERT statements from one SQL export file.
 *
 * For each INSERT:
 *  - Strips the auto-increment \c id column (recording its old value).
 *  - Replaces the \c @PROFILE_ID@ placeholder with the new profile ID.
 *  - Remaps FK columns (machine_id, session_id, session_channel_id,
 *    eventlist_id) using the incremental mapping tables.
 *  - Substitutes the resolved username when inserting the profiles row.
 *  - After inserting a "parent" table row, captures
 *    \c last_insert_rowid() and records the old→new ID mapping.
 *
 * \param sqlFile  Absolute path to a .sql export file.
 * \return true on success; false on the first error.
 */
bool ProfileRestore::executeSqlFile(const QString& sqlFile)
{
    const QString tableName = QFileInfo(sqlFile).baseName();

    qDebug() << "ProfileRestore::executeSqlFile()";

    // Skip files for tables that no longer exist in the current schema.
    // This allows backups from older schema versions to restore cleanly: the
    // removed tables (e.g. reports/report_contents from schema v12) are simply
    // bypassed and OSCAR regenerates their data on first run.
    {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery tableCheck(db);
        tableCheck.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?"));
        tableCheck.addBindValue(tableName);
        if (tableCheck.exec() && tableCheck.next()
                && tableCheck.value(0).toInt() == 0) {
            qDebug() << "ProfileRestore::executeSqlFile: skipping" << tableName
                     << "(table not present in current schema)";
            return true;
        }
    }

    // Build the set of columns that currently exist in this table so that
    // INSERT statements from older backups can silently drop removed columns
    // (e.g. dst_enabled removed in v15, events_file/summary_file in v12).
    QSet<QString> validColumns;
    {
        QSqlDatabase db2 = DatabaseManager::instance().database();
        QSqlQuery pragma(db2);
        pragma.exec(QString("PRAGMA table_info(%1)").arg(tableName));
        while (pragma.next()) {
            validColumns.insert(pragma.value(1).toString()); // column 1 = name
        }
    }

    qDebug() << "ProfileRestore::executeSqlFile(): set up mappingTables";
    // Tables whose auto-increment PK we must track for FK remapping.
    static const QSet<QString> mappingTables = {
        QStringLiteral("profiles"),
        QStringLiteral("machines"),
        QStringLiteral("sessions"),
        QStringLiteral("session_channels"),
        QStringLiteral("event_lists")
    };

    QFile file(sqlFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_errorMessage = QString("Cannot open SQL file: %1").arg(sqlFile);
        return false;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QTextStream  in(&file);

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        InsertStatement stmt;
        if (!parseInsert(line, stmt)) {
            continue; // skip blank lines, comments, etc.
        }

        // Extract the original auto-increment id value (before stripping).
        qint64 originalId = -1;
        {
            const int idx = stmt.columns.indexOf(QStringLiteral("id"));
            if (idx >= 0) {
                bool ok = false;
                originalId = stmt.values.value(idx).toLongLong(&ok);
                if (!ok) originalId = -1;
            }
        }

        // Build the transformed column/value lists.
        QStringList newCols, newVals;
        newCols.reserve(stmt.columns.count());
        newVals.reserve(stmt.values.count());

        for (int i = 0; i < stmt.columns.count(); ++i) {
            const QString& col = stmt.columns.at(i);
            const QString& val = stmt.values.at(i);

            // 0. Drop columns removed from the schema since the backup was taken.
            if (!validColumns.isEmpty() && !validColumns.contains(col)) {
                continue;
            }

            // 1. Strip the primary key column.
            if (col == QLatin1String("id")) {
                continue;
            }

            // 2. Replace @PROFILE_ID@ placeholder.
            if (val == QLatin1String("@PROFILE_ID@")) {
                if (m_newProfileId < 0) {
                    m_errorMessage = QString(
                        "Encountered @PROFILE_ID@ in %1 before profiles.sql was processed.")
                        .arg(sqlFile);
                    return false;
                }
                newCols.append(col);
                newVals.append(QString::number(m_newProfileId));
                continue;
            }

            // 3. Substitute the resolved username in the profiles table.
//            qDebug() << "ProfileRestore::executeSqlFile() step 3a";
            if (tableName == QLatin1String("profiles")
                && col == QLatin1String("username")) {
                QString safeName = m_newUsername;
                safeName.replace(QLatin1Char('\''), QLatin1String("''"));
                newCols.append(col);
                newVals.append(QLatin1Char('\'') + safeName + QLatin1Char('\''));
                continue;
            }

            // 3b. Update data_folder when the username changes (Rename strategy).
            // The profile data folder is normally "%PROFDIR%/<username>".  When
            // restoring under a different name we must update that path so the
            // restored profile does not share the original profile's directory.
//            qDebug() << "ProfileRestore::executeSqlFile() step 3b";
            if (tableName == QLatin1String("profiles")
                && col == QLatin1String("data_folder")) {
                const QString origUsername =
                    m_manifestJson[QStringLiteral("profile")].toObject()
                                  [QStringLiteral("username")].toString();
                if (!origUsername.isEmpty() && m_newUsername != origUsername) {
                    // Decode the SQL string literal (strip outer single-quotes,
                    // then unescape '' → ').
                    QString folder = val;
                    if (folder.startsWith(QLatin1Char('\''))
                        && folder.endsWith(QLatin1Char('\''))) {
                        folder = folder.mid(1, folder.length() - 2);
                        folder.replace(QLatin1String("''"), QLatin1String("'"));
                    }
                    // Replace the original username wherever it appears in the path.
                    folder.replace(origUsername, m_newUsername);
                    // Re-encode as a SQL string literal.
                    folder.replace(QLatin1Char('\''), QLatin1String("''"));
                    newCols.append(col);
                    newVals.append(QLatin1Char('\'') + folder + QLatin1Char('\''));
                    continue;
                }
            }

            // 4. Remap FK columns.
//            qDebug() << "ProfileRestore::executeSqlFile() step 4 Remap FK columns";
            QString remapped = val;
            if (val != QLatin1String("NULL")) {
                bool ok = false;
                const qint64 oldFk = val.toLongLong(&ok);
                if (ok) {
                    qint64 newFk = -1;

                    if (col == QLatin1String("machine_id")
                        && (tableName == QLatin1String("sessions")
                            || tableName == QLatin1String("daily_summaries"))) {
                        newFk = m_machineIdMap.value(oldFk, -1);

                    } else if (col == QLatin1String("session_id")
                               && tableName != QLatin1String("sessions")) {
                        // sessions.session_id is the machine's native session ID
                        // (natural key), not a FK to sessions.id — never remap it.
                        newFk = m_sessionIdMap.value(oldFk, -1);

                    } else if (col == QLatin1String("session_channel_id")) {
                        newFk = m_sessionChannelIdMap.value(oldFk, -1);

                    } else if (col == QLatin1String("eventlist_id")) {
                        newFk = m_eventListIdMap.value(oldFk, -1);
                    }

                    if (newFk >= 0) {
                        remapped = QString::number(newFk);
                    } else if (newFk == -1) {
                        // Mapping not found — determine whether this is fatal.
                        if ((col == QLatin1String("session_id")
                             && tableName != QLatin1String("sessions"))
                         || (col == QLatin1String("session_channel_id"))
                         || (col == QLatin1String("eventlist_id"))
                         || (col == QLatin1String("machine_id")
                             && tableName == QLatin1String("sessions"))) {
                            // Required FK — restore order error.
                            m_errorMessage = QString(
                                "No ID mapping found for %1 = %2 in table %3. "
                                "Restore order may be incorrect.")
                                .arg(col).arg(oldFk).arg(tableName);
                            return false;
                        } else if (col == QLatin1String("machine_id")
                                   && tableName == QLatin1String("daily_summaries")) {
                            // Nullable FK — use NULL if machine was not exported.
                            remapped = QStringLiteral("NULL");
                        }
                        // All other columns: keep original value (no remapping needed).
                    }
                }
            }

            newCols.append(col);
            newVals.append(remapped);
        }

        if (newCols.isEmpty()) {
            // Extremely unlikely — profiles table with only an id column.
            continue;
        }

        // Execute the INSERT using parameterized binding so that text values
        // containing newlines or other special characters are stored correctly.
        // channel_options uses INSERT OR IGNORE because the data is global (keyed
        // on channel code, not profile) — rows already present from other profiles
        // are identical and should be silently skipped.
//        qDebug() << "ProfileRestore::executeSqlFile() execute the INSERT";
        const QStringList placeholders(newVals.count(), QStringLiteral("?"));
        const bool useIgnore = (tableName == QLatin1String("channel_options"));
        const QString sql = QString(useIgnore ? "INSERT OR IGNORE INTO %1 (%2) VALUES (%3)"
                                              : "INSERT INTO %1 (%2) VALUES (%3)")
                                .arg(stmt.tableName,
                                     newCols.join(QStringLiteral(", ")),
                                     placeholders.join(QStringLiteral(", ")));

        QSqlQuery q(db);
        if (!q.prepare(sql)) {
            m_errorMessage = QString("Failed to prepare INSERT for %1: %2")
                                 .arg(tableName, q.lastError().text());
            return false;
        }
        for (const QString& val : newVals) {
            q.addBindValue(sqlLiteralToVariant(val));
        }
        if (!q.exec()) {
            m_errorMessage = QString("INSERT failed in %1: %2")
                                 .arg(tableName, q.lastError().text());
            qWarning() << "ProfileRestore: failed prepared INSERT for table:" << tableName;
            return false;
        }

        // For parent tables, capture the new auto-increment PK and record the mapping.
//        qDebug() << "ProfileRestore::executeSqlFile() capture the new auto-increment PK and record the mapping";
        if (originalId >= 0 && mappingTables.contains(tableName)) {
            QSqlQuery rowIdQ(db);
            if (!rowIdQ.exec(QStringLiteral("SELECT last_insert_rowid()"))
                    || !rowIdQ.next()) {
                m_errorMessage = QStringLiteral("Failed to retrieve last_insert_rowid()");
                return false;
            }
            const qint64 newId = rowIdQ.value(0).toLongLong();

            if (tableName == QLatin1String("profiles")) {
                addProfileIdMapping(originalId, newId);
                m_newProfileId = newId;
            } else if (tableName == QLatin1String("machines")) {
                addMachineIdMapping(originalId, newId);
            } else if (tableName == QLatin1String("sessions")) {
                addSessionIdMapping(originalId, newId);
            } else if (tableName == QLatin1String("session_channels")) {
                addSessionChannelIdMapping(originalId, newId);
            } else if (tableName == QLatin1String("event_lists")) {
                addEventListIdMapping(originalId, newId);
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
//  Private helpers — transactional restore orchestration
// ---------------------------------------------------------------------------

/*!
 * \brief Restore all SQL export files inside a single database transaction.
 *
 * Files are processed in dependency order so that parent rows always exist
 * before child rows are inserted.  Any failure causes a full rollback,
 * leaving the database unchanged.
 *
 * \return true on success (transaction committed).
 */
bool ProfileRestore::restoreInTransaction()
{
    const QString dbDir = m_tempDir + QStringLiteral("/database");

    // Processing order respects foreign-key dependencies.
    static const QStringList restoreOrder = {
        QStringLiteral("profiles"),
        QStringLiteral("user_info"),
        QStringLiteral("doctor_info"),
        QStringLiteral("profile_preferences"),
        QStringLiteral("graph_layouts"),
        QStringLiteral("channels"),
        QStringLiteral("channel_options"),
        QStringLiteral("machines"),
        QStringLiteral("sessions"),
        QStringLiteral("session_settings"),
        QStringLiteral("session_channels"),
        QStringLiteral("session_channel_values"),
        QStringLiteral("respiratory_events"),
        QStringLiteral("session_summaries"),
        QStringLiteral("session_slices"),
        QStringLiteral("event_lists"),
        QStringLiteral("event_data"),
        QStringLiteral("daily_summaries")
    };

    qDebug() << "ProfileRestore::restoreInTransaction entered";

    QSqlDatabase db = DatabaseManager::instance().database();

    if (!db.transaction()) {
        m_errorMessage = QStringLiteral("Failed to start database transaction.");
        return false;
    }

    // For Replace mode, delete the existing profile inside the transaction.
    // ON DELETE CASCADE removes all child rows (machines, sessions, etc.)
    // atomically with the re-insert, so a restore failure leaves the database
    // untouched rather than deleting the old profile without replacing it.
    if (m_resolution == ConflictResolution::Replace) {
        QSqlQuery delQ(db);
        delQ.prepare(QStringLiteral("DELETE FROM profiles WHERE username = :u"));
        delQ.bindValue(QStringLiteral(":u"), m_newUsername);
        if (!delQ.exec()) {
            m_errorMessage = QString(
                "Failed to delete existing profile '%1' for Replace: %2")
                .arg(m_newUsername, delQ.lastError().text());
            db.rollback();
            return false;
        }
    }

    const int total = restoreOrder.count();
    int tableNum = 0;

    for (const QString& tableName : restoreOrder) {
        const QString sqlFile = dbDir + QLatin1Char('/') + tableName + QStringLiteral(".sql");

        ++tableNum;

        if (!QFile::exists(sqlFile)) {
            // An empty (or absent) file is valid — skip silently.
            continue;
        }

        const int pct = 20 + (tableNum * 60) / total;
        emit progressChanged(pct, QString("Restoring %1...").arg(tableName));
        QCoreApplication::processEvents();
        if (m_cancelRequested.load()) {
            m_errorMessage = QStringLiteral("Operation cancelled.");
            db.rollback();
            return false;
        }

        if (!executeSqlFile(sqlFile)) {
            db.rollback();
            return false;
        }
    }

    // Validate counts before committing.
    emit progressChanged(85, QStringLiteral("Validating restore..."));
    if (!validateRestore(m_manifestJson)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        m_errorMessage = QStringLiteral("Failed to commit database transaction.");
        db.rollback();
        return false;
    }

    return true;
}

/*!
 * \brief Validate the restored profile against the manifest statistics.
 *
 * Checks that:
 *  - The restored session count matches the manifest.
 *  - The restored event_list count matches the manifest.
 *
 * Called inside restoreInTransaction() before the final COMMIT, so any
 * mismatch causes a rollback of the entire restore.
 *
 * \param manifest  The parsed manifest JSON object.
 * \return true if all counts match.
 */
bool ProfileRestore::validateRestore(const QJsonObject& manifest)
{
    qDebug() << "ProfileRestore::validateRestore"    ;
    if (m_newProfileId < 0) {
        m_errorMessage = QStringLiteral("Profile was not restored (profile ID unknown).");
        return false;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    const QJsonObject stats = manifest[QStringLiteral("statistics")].toObject();

    // Verify session count.
    const int expectedSessions = stats[QStringLiteral("sessions_count")].toInt(0);
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sessions "
            "WHERE machine_id IN (SELECT id FROM machines WHERE profile_id = :pid)"));
        q.bindValue(QStringLiteral(":pid"), m_newProfileId);
        if (!q.exec() || !q.next()) {
            m_errorMessage = QStringLiteral("Failed to count restored sessions.");
            return false;
        }
        const int actual = q.value(0).toInt();
        if (actual != expectedSessions) {
            m_errorMessage = QString(
                "Session count mismatch after restore: expected %1, found %2.")
                .arg(expectedSessions).arg(actual);
            return false;
        }
    }

    // Verify event_list count.
    const int expectedEventLists = stats[QStringLiteral("event_lists_count")].toInt(0);
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM event_lists WHERE profile_id = :pid"));
        q.bindValue(QStringLiteral(":pid"), m_newProfileId);
        if (!q.exec() || !q.next()) {
            m_errorMessage = QStringLiteral("Failed to count restored event lists.");
            return false;
        }
        const int actual = q.value(0).toInt();
        if (actual != expectedEventLists) {
            m_errorMessage = QString(
                "Event list count mismatch after restore: expected %1, found %2.")
                .arg(expectedEventLists).arg(actual);
            return false;
        }
    }

    qDebug() << "ProfileRestore: validation passed — profile" << m_newProfileId
             << "| sessions:" << expectedSessions
             << "| event_lists:" << expectedEventLists;

    return true;
}

// ---------------------------------------------------------------------------
//  ID remapping helpers
// ---------------------------------------------------------------------------

void ProfileRestore::addProfileIdMapping(qint64 oldId, qint64 newId)
{
    m_profileIdMap[oldId] = newId;
}

void ProfileRestore::addMachineIdMapping(qint64 oldId, qint64 newId)
{
    m_machineIdMap[oldId] = newId;
}

void ProfileRestore::addSessionIdMapping(qint64 oldId, qint64 newId)
{
    m_sessionIdMap[oldId] = newId;
}

void ProfileRestore::addSessionChannelIdMapping(qint64 oldId, qint64 newId)
{
    m_sessionChannelIdMap[oldId] = newId;
}

void ProfileRestore::addEventListIdMapping(qint64 oldId, qint64 newId)
{
    m_eventListIdMap[oldId] = newId;
}

qint64 ProfileRestore::remapProfileId(qint64 oldId) const
{
    return m_profileIdMap.value(oldId, -1);
}

qint64 ProfileRestore::remapMachineId(qint64 oldId) const
{
    return m_machineIdMap.value(oldId, -1);
}

qint64 ProfileRestore::remapSessionId(qint64 oldId) const
{
    return m_sessionIdMap.value(oldId, -1);
}

qint64 ProfileRestore::remapSessionChannelId(qint64 oldId) const
{
    return m_sessionChannelIdMap.value(oldId, -1);
}

qint64 ProfileRestore::remapEventListId(qint64 oldId) const
{
    return m_eventListIdMap.value(oldId, -1);
}
