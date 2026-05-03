/* RecentDatabases — QSettings-backed list of recently opened OSCAR databases
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RECENT_DATABASES_H
#define RECENT_DATABASES_H

#include <QStringList>

/*!
 * \class RecentDatabases
 * \brief Static helper that persists a list of recently opened OSCAR database
 *        folders in QSettings.
 *
 * Each entry is the absolute path to a folder that contains oscar.db.
 * Labels displayed in menus are derived on the fly as QFileInfo(path).fileName().
 *
 * Dead entries (folders that no longer contain oscar.db) are pruned lazily
 * whenever entries() is called.
 */
class RecentDatabases
{
public:
    /*! \brief Add or promote a path to the top of the recent list (cap: 10). */
    static void add(const QString& path);

    /*! \brief Return the list of valid recent paths, pruning dead and duplicate entries. */
    static QStringList entries();

    /*! \brief Remove a path from the recent list. */
    static void remove(const QString& path);

    /*! \brief Switch the active database: write path to Settings/AppData and call add(). */
    static void setActive(const QString& path);

    /*!
     * \brief Resolve a path to its canonical (authoritative) form.
     *
     * Normalises separators, then asks the OS for the real path via
     * QFileInfo::canonicalFilePath().  On Windows this also fixes case
     * (NTFS stores one true case per entry), so two differently-cased
     * strings for the same directory produce identical results.  On
     * case-sensitive systems the path is returned unchanged.
     * Falls back to QDir::cleanPath() if the path does not exist yet.
     */
    static QString canonicalize(const QString& path);

    static constexpr int MaxEntries = 10;
};

#endif // RECENT_DATABASES_H
