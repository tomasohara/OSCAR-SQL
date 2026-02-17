/* Database Manager Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the DatabaseManager singleton class which handles
 * SQLite database connections, initialization, and transaction management.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QMutex>
#include <QString>

/*!
 * \class DatabaseManager
 * \brief Singleton class that manages the SQLite database connection
 *
 * This class provides thread-safe access to the OSCAR SQLite database.
 * It handles connection initialization, transaction management, and
 * provides a single point of access for all database operations.
 *
 * Usage:
 * \code
 * DatabaseManager& dbMgr = DatabaseManager::instance();
 * if (dbMgr.initialize(GetAppData() + "/oscar.db")) {
 *     // Database is ready to use
 * }
 * \endcode
 */
class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    /*!
     * \brief Get the singleton instance of DatabaseManager
     * \return Reference to the DatabaseManager instance
     */
    static DatabaseManager& instance();

    /*!
     * \brief Initialize the database connection
     * \param databasePath Full path to the database file
     * \return true if successful, false otherwise
     *
     * This method:
     * - Creates the database file if it doesn't exist
     * - Opens a connection with WAL mode enabled
     * - Enables foreign key constraints
     * - Creates the schema if needed
     */
    bool initialize(const QString& databasePath);

    /*!
     * \brief Close the database connection
     *
     * Call this during application shutdown.
     */
    void close();

    /*!
     * \brief Get the database connection
     * \return QSqlDatabase reference for query execution
     */
    QSqlDatabase database();

    /*!
     * \brief Check if database is open and ready
     * \return true if database is initialized and open
     */
    bool isOpen() const;

    /*!
     * \brief Get the path to the database file
     * \return QString containing full path to database
     */
    QString databasePath() const { return m_databasePath; }

    /*!
     * \brief Begin a database transaction
     * \return true if successful
     */
    bool transaction();

    /*!
     * \brief Commit the current transaction
     * \return true if successful
     */
    bool commit();

    /*!
     * \brief Rollback the current transaction
     * \return true if successful
     */
    bool rollback();

    /*!
     * \brief Check if currently in a transaction
     * \return true if transaction is active
     *
     * This allows nested calls to avoid starting a new transaction
     * when one is already active.
     */
    bool inTransaction() const;

    /*!
     * \brief Get the last database error
     * \return QSqlError object with error details
     */
    QSqlError lastError() const;

    /*!
     * \brief Checkpoint the WAL (Write-Ahead Log) file
     * \return true if successful
     *
     * Forces SQLite to merge the WAL file back into the main database
     * and truncate the WAL. This is useful before/after large operations
     * to improve performance and reclaim disk space.
     *
     * Uses PRAGMA wal_checkpoint(TRUNCATE) which blocks until complete.
     */
    bool checkpointWAL();

    /*!
     * \brief Counts total rows in database
     * \return total number of rows. -1 if failure.
     */
    long countRows(QString text);

signals:
    /*!
     * \brief Emitted when a database error occurs
     * \param error Description of the error
     */
    void databaseError(const QString& error);

private:
    // Singleton pattern - private constructor
    DatabaseManager();
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    /*!
     * \brief Configure database connection settings
     *
     * Sets up WAL mode, foreign keys, cache size, etc.
     */
    bool configureDatabaseSettings();

    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_databasePath;
    bool m_initialized;
    bool m_inTransaction;
    mutable QMutex m_mutex;
};

#endif // DATABASE_MANAGER_H
