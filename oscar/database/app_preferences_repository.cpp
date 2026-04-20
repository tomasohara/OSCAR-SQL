/* App Preferences Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "app_preferences_repository.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QTime>

AppPreferencesRepository::AppPreferencesRepository() {}
AppPreferencesRepository::~AppPreferencesRepository() {}

// ---------------------------------------------------------------------------
// Private helpers (mirror PreferencesRepository)
// ---------------------------------------------------------------------------

QString AppPreferencesRepository::dataTypeFromVariant(const QVariant& v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:      return "bool";
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong: return "int";
    case QMetaType::Float:
    case QMetaType::Double:    return "float";
    case QMetaType::QDateTime: return "datetime";
    case QMetaType::QDate:     return "date";
    case QMetaType::QTime:     return "time";
    default:                   return "string";
    }
}

QVariant AppPreferencesRepository::variantFromString(const QString& value, const QString& dataType)
{
    if (dataType == "bool")     return QVariant(value.toLower() == "true" || value == "1");
    if (dataType == "int")      return QVariant(value.toLongLong());
    if (dataType == "float")    return QVariant(value.toDouble());
    if (dataType == "datetime") return QVariant(QDateTime::fromString(value, Qt::ISODate));
    if (dataType == "date")     return QVariant(QDate::fromString(value, Qt::ISODate));
    if (dataType == "time")     return QVariant(QTime::fromString(value, Qt::ISODate));
    return QVariant(value);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool AppPreferencesRepository::save(const QString& category, const QString& key, const QVariant& value)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "AppPreferencesRepository::save() - Database not open";
        return false;
    }

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO app_preferences (category, key, value, data_type, updated_at) "
        "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(category, key) DO UPDATE SET "
        "  value = excluded.value, data_type = excluded.data_type, "
        "  updated_at = CURRENT_TIMESTAMP"
    );
    // Serialize QDateTime/QDate/QTime to ISO 8601 so variantFromString() can round-trip them.
    // value.toString() would produce Qt::TextDate for temporal types, which Qt::ISODate can't parse.
    QString serialized;
    switch (value.typeId()) {
    case QMetaType::QDateTime: serialized = value.toDateTime().toString(Qt::ISODate); break;
    case QMetaType::QDate:     serialized = value.toDate().toString(Qt::ISODate);     break;
    case QMetaType::QTime:     serialized = value.toTime().toString(Qt::ISODate);     break;
    default:                   serialized = value.toString();                         break;
    }

    q.addBindValue(category);
    q.addBindValue(key);
    q.addBindValue(serialized);
    q.addBindValue(dataTypeFromVariant(value));

    if (!q.exec()) {
        qWarning() << "AppPreferencesRepository::save() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AppPreferencesRepository::saveBlob(const QString& category, const QString& key, const QByteArray& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "AppPreferencesRepository::saveBlob() - Database not open";
        return false;
    }

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO app_preferences (category, key, blob_value, data_type, updated_at) "
        "VALUES (?, ?, ?, 'blob', CURRENT_TIMESTAMP) "
        "ON CONFLICT(category, key) DO UPDATE SET "
        "  blob_value = excluded.blob_value, data_type = 'blob', "
        "  updated_at = CURRENT_TIMESTAMP"
    );
    q.addBindValue(category);
    q.addBindValue(key);
    q.addBindValue(data);

    if (!q.exec()) {
        qWarning() << "AppPreferencesRepository::saveBlob() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QList<AppPrefData> AppPreferencesRepository::loadByCategory(const QString& category)
{
    QList<AppPrefData> result;
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, category, key, value, blob_value, data_type "
        "FROM app_preferences WHERE category = ?"
    );
    q.addBindValue(category);
    if (!q.exec()) {
        qWarning() << "AppPreferencesRepository::loadByCategory() failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        AppPrefData d;
        d.id        = q.value(0).toLongLong();
        d.category  = q.value(1).toString();
        d.key       = q.value(2).toString();
        d.value     = q.value(3).toString();
        d.blobValue = q.value(4).toByteArray();
        d.dataType  = q.value(5).toString();
        result.append(d);
    }
    return result;
}

QList<AppPrefData> AppPreferencesRepository::loadAll()
{
    QList<AppPrefData> result;
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return result;

    QSqlQuery q(db);
    if (!q.exec("SELECT id, category, key, value, blob_value, data_type FROM app_preferences")) {
        qWarning() << "AppPreferencesRepository::loadAll() failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        AppPrefData d;
        d.id        = q.value(0).toLongLong();
        d.category  = q.value(1).toString();
        d.key       = q.value(2).toString();
        d.value     = q.value(3).toString();
        d.blobValue = q.value(4).toByteArray();
        d.dataType  = q.value(5).toString();
        result.append(d);
    }
    return result;
}

bool AppPreferencesRepository::remove(const QString& category, const QString& key)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare("DELETE FROM app_preferences WHERE category = ? AND key = ?");
    q.addBindValue(category);
    q.addBindValue(key);
    if (!q.exec()) {
        qWarning() << "AppPreferencesRepository::remove() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AppPreferencesRepository::removeCategory(const QString& category)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare("DELETE FROM app_preferences WHERE category = ?");
    q.addBindValue(category);
    if (!q.exec()) {
        qWarning() << "AppPreferencesRepository::removeCategory() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AppPreferencesRepository::removeAll()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    if (!q.exec("DELETE FROM app_preferences")) {
        qWarning() << "AppPreferencesRepository::removeAll() failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AppPreferencesRepository::hasData()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    if (!q.exec("SELECT COUNT(*) FROM app_preferences")) return false;
    return q.next() && q.value(0).toInt() > 0;
}
