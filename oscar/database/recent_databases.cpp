/* RecentDatabases — QSettings-backed list of recently opened OSCAR databases
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "recent_databases.h"
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QSet>

static const QString RECENT_KEY = "RecentDatabases";

QString RecentDatabases::canonicalize(const QString& path)
{
    QString clean = QDir::cleanPath(path);
    QString canonical = QFileInfo(clean).canonicalFilePath();
    // canonicalFilePath() returns "" when the path doesn't exist yet (new DB).
    return canonical.isEmpty() ? clean : canonical;
}

// Returns the key used to identify a path for deduplication.
// On case-insensitive file systems (Windows, default macOS) we fold to
// lowercase so "OSCAR21_Data" and "OSCAR21_data" are treated as the same
// database even if canonicalFilePath() preserves the caller's capitalisation.
// On case-sensitive systems (Linux) the canonical path is used unchanged.
static QString dedupKey(const QString& canonPath)
{
#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    return canonPath.toLower();
#else
    return canonPath;
#endif
}

void RecentDatabases::add(const QString& path)
{
    QString canon = canonicalize(path);
    QString key   = dedupKey(canon);
    QSettings settings;
    QStringList list = settings.value(RECENT_KEY).toStringList();
    // Remove any existing entry that refers to the same directory, regardless
    // of how it was capitalised or formatted when originally stored.
    list.removeIf([&key](const QString& p) {
        return dedupKey(canonicalize(p)) == key;
    });
    list.prepend(canon);
    while (list.size() > MaxEntries)
        list.removeLast();
    settings.setValue(RECENT_KEY, list);
}

QStringList RecentDatabases::entries()
{
    QSettings settings;
    QStringList list = settings.value(RECENT_KEY).toStringList();
    QStringList valid;
    QSet<QString> seen;

    for (const QString& path : list) {
        QString canon = canonicalize(path);
        QString key   = dedupKey(canon);
        if (QFileInfo::exists(canon + "/oscar.db") && !seen.contains(key)) {
            seen.insert(key);
            valid << canon;
        }
    }

    if (valid != list)
        settings.setValue(RECENT_KEY, valid);
    return valid;
}

void RecentDatabases::remove(const QString& path)
{
    QString key = dedupKey(canonicalize(path));
    QSettings settings;
    QStringList list = settings.value(RECENT_KEY).toStringList();
    list.removeIf([&key](const QString& p) {
        return dedupKey(canonicalize(p)) == key;
    });
    settings.setValue(RECENT_KEY, list);
}

void RecentDatabases::setActive(const QString& path)
{
    QSettings settings;
    settings.setValue("Settings/AppData", path);
    add(path);
}
