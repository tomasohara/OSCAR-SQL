/* Database Manager Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the DatabaseManager singleton class for SQLite
 * database connection management and transaction handling.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "database_manager.h"
#include "database_schema.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QThread>
#include <QMutexLocker>
#include <QCoreApplication>

/*
 * Get the singleton instance of DatabaseManager
 *
 * Returns: Reference to the single DatabaseManager instance
 */
DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

/*
 * Private constructor for singleton pattern
 */
DatabaseManager::DatabaseManager()
    : QObject(nullptr)
    , m_initialized(false)
    , m_inTransaction(false)
{
    // Create unique connection name for this thread
    m_connectionName = QString("OSCAR_DB_%1").arg(quintptr(QThread::currentThread()));
}

/*
 * Destructor - closes database connection
 */
DatabaseManager::~DatabaseManager()
{
    close();
}

/*
 * Initialize the database connection
 *
 * Parameters:
 *   databasePath - Full path to the database file
 *
 * Returns: true if successful, false otherwise
 *
 * This method creates the database file if needed, opens a connection,
 * configures settings, and creates the schema if this is a new database.
 */
bool DatabaseManager::initialize(const QString& databasePath)
{
    QMutexLocker locker(&m_mutex);

    if (m_initialized) {
        qDebug() << "DatabaseManager: Already initialized";
        return true;
    }

    m_databasePath = databasePath;

    // Ensure the directory exists
    QFileInfo fileInfo(databasePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qCritical() << "DatabaseManager: Failed to create directory:" << dir.absolutePath();
            emit databaseError("Failed to create database directory");
            return false;
        }
    }

    // Check if database file already exists
    bool isNewDatabase = !QFile::exists(databasePath);

    // Add database connection
    m_database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_database.setDatabaseName(databasePath);

    // Open the database
    if (!m_database.open()) {
        qCritical() << "DatabaseManager: Failed to open database:" << m_database.lastError().text();
        emit databaseError("Failed to open database: " + m_database.lastError().text());
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    qDebug() << "DatabaseManager: Database opened:" << databasePath;

    // Configure database settings
    if (!configureDatabaseSettings()) {
        qCritical() << "DatabaseManager: Failed to configure database settings";
        close();
        return false;
    }

    // Create schema if this is a new database
    if (isNewDatabase) {
        qDebug() << "DatabaseManager: Creating new database schema";
        if (!DatabaseSchema::createSchema(m_database)) {
            qCritical() << "DatabaseManager: Failed to create database schema";
            close();
            return false;
        }
    } else {
        qDebug() << "DatabaseManager: Using existing database";
        
        // Check schema version and upgrade if needed
        int currentVersion = DatabaseSchema::getSchemaVersion(m_database);
        qDebug() << "DatabaseManager: Current schema version:" << currentVersion;
        
        if (currentVersion < DatabaseSchema::CURRENT_SCHEMA_VERSION) {
            qDebug() << "DatabaseManager: Upgrading schema from version" << currentVersion 
                     << "to" << DatabaseSchema::CURRENT_SCHEMA_VERSION;
            
            if (!DatabaseSchema::upgradeSchema(m_database, currentVersion)) {
                qCritical() << "DatabaseManager: Failed to upgrade database schema";
                close();
                return false;
            }
            
            qDebug() << "DatabaseManager: Schema upgrade complete";
        } else if (currentVersion > DatabaseSchema::CURRENT_SCHEMA_VERSION) {
            qCritical() << "DatabaseManager: Database schema version" << currentVersion 
                        << "is newer than supported version" << DatabaseSchema::CURRENT_SCHEMA_VERSION;
            qCritical() << "DatabaseManager: Please upgrade OSCAR to the latest version";
            close();
            return false;
        } else {
            qDebug() << "DatabaseManager: Schema is up to date";
        }
    }

    // Check and update CSV report versions (runs at every startup)
    // This is separate from schema upgrades because report changes don't require schema changes
    qDebug() << "DatabaseManager: Checking CSV report versions...";
    if (!DatabaseSchema::checkAndUpdateReportVersion(m_database)) {
        qWarning() << "DatabaseManager: Failed to check/update report versions";
        // Not a critical error - continue initialization
    }

    m_initialized = true;
    qDebug() << "DatabaseManager: Initialization complete";
    return true;
}

/*
 * Close the database connection
 *
 * Call this during application shutdown to properly close the database.
 */
void DatabaseManager::close()
{
    QMutexLocker locker(&m_mutex);

    if (m_initialized && m_database.isOpen()) {
        qDebug() << "DatabaseManager: Closing database connection";
        m_database.close();
    }

    // Only remove database if QCoreApplication still exists
    // to avoid Qt warnings during shutdown
    if (QCoreApplication::instance()) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    
    m_initialized = false;
}

/*
 * Get the database connection for executing queries
 *
 * Returns: QSqlDatabase object for query execution
 */
QSqlDatabase DatabaseManager::database()
{
    return m_database;
}

/*
 * Check if database is open and ready
 *
 * Returns: true if database is initialized and open, false otherwise
 */
bool DatabaseManager::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized && m_database.isOpen();
}

/*
 * Begin a database transaction
 *
 * Returns: true if successful, false otherwise
 *
 * All database modifications between transaction() and commit() will
 * be atomic. If an error occurs, call rollback() to undo changes.
 * 
 * If already in a transaction, returns true without starting a new one.
 */
bool DatabaseManager::transaction()
{
    if (m_inTransaction) {
        // Already in a transaction, don't nest
        return true;
    }
    
    if (!m_database.transaction()) {
        qWarning() << "DatabaseManager: Failed to start transaction:" << m_database.lastError().text();
        return false;
    }
    
    m_inTransaction = true;
    return true;
}

/*
 * Commit the current transaction
 *
 * Returns: true if successful, false otherwise
 *
 * Makes all changes since transaction() permanent.
 */
bool DatabaseManager::commit()
{
    if (!m_inTransaction) {
        // Not in a transaction, nothing to commit
        return true;
    }
    
    if (!m_database.commit()) {
        qWarning() << "DatabaseManager: Failed to commit transaction:" << m_database.lastError().text();
        m_inTransaction = false;  // Clear flag even on error
        return false;
    }
    
    m_inTransaction = false;
    return true;
}

/*
 * Rollback the current transaction
 *
 * Returns: true if successful, false otherwise
 *
 * Undoes all changes since transaction() was called.
 */
bool DatabaseManager::rollback()
{
    if (!m_inTransaction) {
        // Not in a transaction, nothing to rollback
        return true;
    }
    
    if (!m_database.rollback()) {
        qWarning() << "DatabaseManager: Failed to rollback transaction:" << m_database.lastError().text();
        m_inTransaction = false;  // Clear flag even on error
        return false;
    }
    
    m_inTransaction = false;
    return true;
}

/*
 * Check if currently in a transaction
 *
 * Returns: true if transaction is active, false otherwise
 *
 * This allows repositories to avoid starting nested transactions.
 */
bool DatabaseManager::inTransaction() const
{
    return m_inTransaction;
}

/*
 * Get the last database error
 *
 * Returns: QSqlError object with error details
 */
QSqlError DatabaseManager::lastError() const
{
    return m_database.lastError();
}

/*
 * Checkpoint the WAL (Write-Ahead Log) file
 *
 * Returns: true if successful, false otherwise
 *
 * This method forces SQLite to merge the WAL file back into the main
 * database and truncate the WAL. This is particularly useful before
 * and after large delete operations to:
 * - Start with a smaller WAL (faster processing)
 * - Immediately reclaim disk space after deletion
 * - Reduce memory pressure
 */
bool DatabaseManager::checkpointWAL()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized || !m_database.isOpen()) {
        qWarning() << "DatabaseManager::checkpointWAL() - Database not initialized";
        return false;
    }
    
    QSqlQuery query(m_database);
    
    qDebug() << "DatabaseManager: Checkpointing WAL...";
    
    // PRAGMA wal_checkpoint(TRUNCATE) forces WAL to merge and truncate
    // This blocks until complete, ensuring all changes are committed
    if (!query.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
        qWarning() << "DatabaseManager: WAL checkpoint failed:" << query.lastError().text();
        return false;
    }
    
    // Query returns three values: (busy, log_frames, checkpointed_frames)
    // busy: 0 if successful, 1 if blocked
    // log_frames: Number of frames in WAL after checkpoint
    // checkpointed_frames: Number of frames checkpointed
    if (query.next()) {
        int busy = query.value(0).toInt();
        int logFrames = query.value(1).toInt();
        int checkpointedFrames = query.value(2).toInt();
        
        if (busy == 0) {
            qDebug() << "DatabaseManager: WAL checkpoint complete -" 
                     << checkpointedFrames << "frames checkpointed,"
                     << logFrames << "frames remain in WAL";
        } else {
            qWarning() << "DatabaseManager: WAL checkpoint blocked by another process";
            return false;
        }
    }
    
    return true;
}

/*
 * Configure database connection settings
 *
 * Returns: true if successful, false otherwise
 *
 * This method enables:
 * - Foreign key constraints
 * - WAL (Write-Ahead Logging) mode for better concurrency
 * - Optimized cache size
 * - Faster synchronous mode
 */
bool DatabaseManager::configureDatabaseSettings()
{
    QSqlQuery query(m_database);

    // Enable foreign key constraints
    if (!query.exec("PRAGMA foreign_keys = ON")) {
        qWarning() << "DatabaseManager: Failed to enable foreign keys:" << query.lastError().text();
        return false;
    }

    // Enable WAL mode for better concurrency
    if (!query.exec("PRAGMA journal_mode = WAL")) {
        qWarning() << "DatabaseManager: Failed to enable WAL mode:" << query.lastError().text();
        // Not critical, continue anyway
    }

    // Set synchronous mode to NORMAL for better performance
    if (!query.exec("PRAGMA synchronous = NORMAL")) {
        qWarning() << "DatabaseManager: Failed to set synchronous mode:" << query.lastError().text();
        // Not critical, continue anyway
    }

    // Set cache size to 64MB for better performance
    if (!query.exec("PRAGMA cache_size = -64000")) {
        qWarning() << "DatabaseManager: Failed to set cache size:" << query.lastError().text();
        // Not critical, continue anyway
    }

    // Store temp tables in memory for better performance
    if (!query.exec("PRAGMA temp_store = MEMORY")) {
        qWarning() << "DatabaseManager: Failed to set temp_store:" << query.lastError().text();
        // Not critical, continue anyway
    }

    qDebug() << "DatabaseManager: Database settings configured";
    return true;
}
