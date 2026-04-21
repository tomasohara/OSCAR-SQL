/* App Preferences Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Database access for global application preferences (app_preferences table).
 * These are app-wide settings stored as key-value pairs, replacing Preferences.xml.
 * For per-profile preferences see PreferencesRepository / profile_preferences table.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef APP_PREFERENCES_REPOSITORY_H
#define APP_PREFERENCES_REPOSITORY_H

#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QList>

/*!
 * \struct AppPrefData
 * \brief One row from the app_preferences table.
 */
struct AppPrefData
{
    qint64     id        = 0;
    QString    category;
    QString    key;
    QString    value;
    QByteArray blobValue;
    QString    dataType;   //!< 'string','int','float','bool','datetime','date','time','blob'
};

/*!
 * \class AppPreferencesRepository
 * \brief CRUD for the app_preferences table.
 *
 * High-level helpers mirror the Profile-level PreferencesRepository pattern.
 * save() / load() methods work with the full QHash held by a Preferences object.
 */
class AppPreferencesRepository
{
public:
    AppPreferencesRepository();
    ~AppPreferencesRepository();

    //! \brief Save (upsert) a scalar QVariant value.
    bool save(const QString& category, const QString& key, const QVariant& value);

    //! \brief Save (upsert) a binary BLOB value.
    bool saveBlob(const QString& category, const QString& key, const QByteArray& data);

    //! \brief Load all rows for a category.
    QList<AppPrefData> loadByCategory(const QString& category);

    //! \brief Load all rows.
    QList<AppPrefData> loadAll();

    //! \brief Delete a single key.
    bool remove(const QString& category, const QString& key);

    //! \brief Delete all rows in a category.
    bool removeCategory(const QString& category);

    //! \brief Delete all rows (reset to defaults).
    bool removeAll();

    //! \brief True when the table exists and has at least one row.
    bool hasData();

    static QString    dataTypeFromVariant(const QVariant& v);
    static QVariant   variantFromString(const QString& value, const QString& dataType);

private:
};

#endif // APP_PREFERENCES_REPOSITORY_H
