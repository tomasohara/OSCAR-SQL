/* Daily Summary Repository Implementation
 *
 * Copyright (c) 2025 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "daily_summary_repository.h"
#include "database_manager.h"
#include "profile_repository.h"
#include "../SleepLib/profiles.h"
#include "../SleepLib/day.h"
#include "../SleepLib/session.h"
#include "../SleepLib/machine.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QCryptographicHash>

DailySummaryRepository::DailySummaryRepository()
    : db(DatabaseManager::instance().database())
{
}

DailySummaryRepository::DailySummaryRepository(const QSqlDatabase& database)
    : db(database)
{
}

qint64 DailySummaryRepository::create(const DailySummaryData& data)
{
    QSqlQuery query(db);
    
    // Use INSERT OR REPLACE for idempotent operation
    query.prepare(R"(
        INSERT OR REPLACE INTO daily_summaries (
            profile_id, date, machine_id,
            session_count, enabled_session_count,
            total_hours, mask_on_hours,
            ahi, rdi, obstructive_count, central_count, hypopnea_count, rera_count, clear_airway_count,
            pressure_avg, pressure_min, pressure_max, pressure_95th,
            leak_total_avg, leak_total_95th, leak_total_max, leak_unintentional_avg,
            spo2_avg, spo2_min, pulse_avg, pulse_min, pulse_max,
            is_compliant, has_oximetry, sessions_hash
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    
    query.addBindValue(data.profileId);
    query.addBindValue(data.date);
    query.addBindValue(data.machineId > 0 ? data.machineId : QVariant());
    query.addBindValue(data.sessionCount);
    query.addBindValue(data.enabledSessionCount);
    query.addBindValue(data.totalHours);
    query.addBindValue(data.maskOnHours);
    query.addBindValue(data.ahi);
    query.addBindValue(data.rdi);
    query.addBindValue(data.obstructiveCount);
    query.addBindValue(data.centralCount);
    query.addBindValue(data.hypopneaCount);
    query.addBindValue(data.reraCount);
    query.addBindValue(data.clearAirwayCount);
    query.addBindValue(data.pressureAvg);
    query.addBindValue(data.pressureMin);
    query.addBindValue(data.pressureMax);
    query.addBindValue(data.pressure95th);
    query.addBindValue(data.leakTotalAvg);
    query.addBindValue(data.leakTotal95th);
    query.addBindValue(data.leakTotalMax);
    query.addBindValue(data.leakUnintentionalAvg);
    query.addBindValue(data.spo2Avg);
    query.addBindValue(data.spo2Min);
    query.addBindValue(data.pulseAvg);
    query.addBindValue(data.pulseMin);
    query.addBindValue(data.pulseMax);
    query.addBindValue(data.isCompliant);
    query.addBindValue(data.hasOximetry);
    query.addBindValue(data.sessionsHash);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::create failed:" << query.lastError().text();
        return -1;
    }
    
    return query.lastInsertId().toLongLong();
}

bool DailySummaryRepository::update(const DailySummaryData& data)
{
    // For daily_summaries, we use INSERT OR REPLACE in create()
    // This method is here for compatibility but just calls create
    return create(data) > 0;
}

bool DailySummaryRepository::remove(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM daily_summaries WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::remove failed:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

DailySummaryData DailySummaryRepository::findById(qint64 id)
{
    QSqlQuery query(db);
    query.prepare("SELECT * FROM daily_summaries WHERE id = ?");
    query.addBindValue(id);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findById failed:" << query.lastError().text();
        return DailySummaryData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return DailySummaryData();
}

DailySummaryData DailySummaryRepository::findByProfileAndDate(qint64 profileId, const QDate& date, qint64 machineId)
{
    QSqlQuery query(db);
    
    if (machineId > 0) {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id = ?");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
        query.addBindValue(machineId);
    } else {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id IS NULL");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
    }
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findByProfileAndDate failed:" << query.lastError().text();
        return DailySummaryData();
    }
    
    if (query.next()) {
        return mapResultToData(query);
    }
    
    return DailySummaryData();
}

QList<DailySummaryData> DailySummaryRepository::findByProfile(qint64 profileId)
{
    QList<DailySummaryData> summaries;
    
    QSqlQuery query(db);
    query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? ORDER BY date DESC");
    query.addBindValue(profileId);
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findByProfile failed:" << query.lastError().text();
        return summaries;
    }
    
    while (query.next()) {
        summaries.append(mapResultToData(query));
    }
    
    return summaries;
}

QList<DailySummaryData> DailySummaryRepository::findRange(qint64 profileId, const QDate& startDate, const QDate& endDate, qint64 machineId)
{
    QList<DailySummaryData> summaries;
    
    QSqlQuery query(db);
    
    if (machineId > 0) {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ? AND machine_id = ? ORDER BY date");
        query.addBindValue(profileId);
        query.addBindValue(startDate.toString(Qt::ISODate));
        query.addBindValue(endDate.toString(Qt::ISODate));
        query.addBindValue(machineId);
    } else {
        query.prepare("SELECT * FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ? AND machine_id IS NULL ORDER BY date");
        query.addBindValue(profileId);
        query.addBindValue(startDate.toString(Qt::ISODate));
        query.addBindValue(endDate.toString(Qt::ISODate));
    }
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::findRange failed:" << query.lastError().text();
        return summaries;
    }
    
    while (query.next()) {
        summaries.append(mapResultToData(query));
    }
    
    return summaries;
}

bool DailySummaryRepository::calculateAndStore(qint64 profileId, const QDate& date, qint64 machineId)
{
    // Get the Day object from profile
    Profile* profile = p_profile;  // Global profile pointer
    if (!profile) {
        qWarning() << "DailySummaryRepository::calculateAndStore - No active profile";
        return false;
    }
    
    Day* day = profile->GetDay(date, MT_UNKNOWN);
    if (!day) {
        qDebug() << "DailySummaryRepository::calculateAndStore - No data for date" << date.toString();
        return false;
    }
    
    return calculateAndStoreFromDay(day, profileId, machineId);
}

bool DailySummaryRepository::calculateAndStoreFromDay(Day* day, qint64 profileId, qint64 machineId)
{
    if (!day) {
        qWarning() << "DailySummaryRepository::calculateAndStoreFromDay - Day is null";
        return false;
    }
    
    // Calculate statistics from day
    DailySummaryData data = calculateFromDay(day, machineId);
    data.profileId = profileId;
    data.date = day->date().toString(Qt::ISODate);
    
    // Don't overwrite machineId if calculateFromDay found one
    if (data.machineId == 0) {
        data.machineId = machineId;
    }
    
    // Check if we got any meaningful data
    if (data.totalHours <= 0) {
        qDebug() << "DailySummaryRepository: Skipping" << data.date << "- no CPAP hours";
        return false;
    }
    
    // Store in database
    qint64 id = create(data);
    
    if (id > 0) {
        qDebug() << "DailySummaryRepository: Stored daily summary for" << data.date 
                 << "AHI:" << data.ahi << "Hours:" << data.totalHours 
                 << "Machine:" << data.machineId;
        return true;
    } else {
        qWarning() << "DailySummaryRepository: Failed to store daily summary for" << data.date;
    }
    
    return false;
}

DailySummaryData DailySummaryRepository::calculateFromDay(Day* day, qint64 machineId)
{
    DailySummaryData data;
    
    if (!day) {
        return data;
    }
    
    // Ensure summaries are loaded
    day->OpenSummary();
    
    // Count sessions and get hours using Day's built-in method
    data.sessionCount = day->sessions.size();
    data.enabledSessionCount = 0;
    
    // Count enabled sessions and get machine ID from first CPAP session if not specified
    for (Session* sess : day->sessions) {
        if (!sess) continue;
        
        if (sess->enabled()) {
            data.enabledSessionCount++;
            
            // Get machine ID from first enabled CPAP session if not specified
            if (machineId == 0 && sess->type() == MT_CPAP && sess->machine()) {
                machineId = sess->machine()->getDatabaseId();
            }
        }
    }
    
    // Use Day's hours() method for accurate time calculation
    data.totalHours = day->hours(MT_CPAP);
    data.maskOnHours = data.totalHours;  // For CPAP, mask-on time is total time
    
    // Use Day's built-in AHI/RDI calculation methods
    if (day->hasMachine(MT_CPAP) && data.totalHours > 0) {
        data.ahi = day->calcAHI();
        data.rdi = day->calcRDI();
    }
    
    // Event counts using Day's count() method
    data.obstructiveCount = static_cast<int>(day->count(CPAP_Obstructive));
    data.centralCount = static_cast<int>(day->count(CPAP_Apnea));
    data.hypopneaCount = static_cast<int>(day->count(CPAP_Hypopnea));
    data.reraCount = static_cast<int>(day->count(CPAP_RERA));
    data.clearAirwayCount = static_cast<int>(day->count(CPAP_ClearAirway));
    
    // Pressure statistics using Day's aggregation methods
    if (day->channelHasData(CPAP_Pressure)) {
        data.pressureAvg = day->wavg(CPAP_Pressure);
        data.pressureMin = day->Min(CPAP_Pressure);
        data.pressureMax = day->Max(CPAP_Pressure);
        data.pressure95th = day->p90(CPAP_Pressure);
    }
    
    // Leak statistics
    if (day->channelHasData(CPAP_Leak)) {
        data.leakTotalAvg = day->wavg(CPAP_Leak);
        data.leakTotal95th = day->p90(CPAP_Leak);
        data.leakTotalMax = day->Max(CPAP_Leak);
    }
    
    if (day->channelHasData(CPAP_LeakFlag)) {
        data.leakUnintentionalAvg = day->wavg(CPAP_LeakFlag);
    }
    
    // Oximetry statistics
    if (day->channelHasData(OXI_SPO2)) {
        data.hasOximetry = true;
        data.spo2Avg = day->wavg(OXI_SPO2);
        data.spo2Min = day->Min(OXI_SPO2);
    }
    
    if (day->channelHasData(OXI_Pulse)) {
        data.hasOximetry = true;
        data.pulseAvg = day->wavg(OXI_Pulse);
        data.pulseMin = day->Min(OXI_Pulse);
        data.pulseMax = day->Max(OXI_Pulse);
    }
    
    // Compliance flag (4 hours minimum by default)
    data.isCompliant = (data.totalHours >= 4.0);
    
    // Store the machine ID we found
    data.machineId = machineId;
    
    // Generate hash for cache invalidation
    data.sessionsHash = generateSessionsHash(0, day->date());
    
    return data;
}

bool DailySummaryRepository::calculateRange(qint64 profileId, const QDate& startDate, const QDate& endDate)
{
    Profile* profile = p_profile;
    if (!profile) {
        qWarning() << "DailySummaryRepository::calculateRange - No active profile";
        return false;
    }
    
    int successCount = 0;
    int totalDays = startDate.daysTo(endDate) + 1;
    
    qDebug() << "DailySummaryRepository: Calculating summaries for" << totalDays << "days";
    
    QDate date = startDate;
    while (date <= endDate) {
        Day* day = profile->GetDay(date, MT_UNKNOWN);
        if (day && day->hasEnabledSessions()) {
            if (calculateAndStoreFromDay(day, profileId, 0)) {
                successCount++;
            }
        }
        date = date.addDays(1);
    }
    
    qDebug() << "DailySummaryRepository: Calculated" << successCount << "of" << totalDays << "days";
    
    return successCount > 0;
}

bool DailySummaryRepository::invalidateDate(qint64 profileId, const QDate& date)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM daily_summaries WHERE profile_id = ? AND date = ?");
    query.addBindValue(profileId);
    query.addBindValue(date.toString(Qt::ISODate));
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::invalidateDate failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool DailySummaryRepository::invalidateRange(qint64 profileId, const QDate& startDate, const QDate& endDate)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ?");
    query.addBindValue(profileId);
    query.addBindValue(startDate.toString(Qt::ISODate));
    query.addBindValue(endDate.toString(Qt::ISODate));
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::invalidateRange failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool DailySummaryRepository::exists(qint64 profileId, const QDate& date, qint64 machineId)
{
    QSqlQuery query(db);
    
    if (machineId > 0) {
        query.prepare("SELECT COUNT(*) FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id = ?");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
        query.addBindValue(machineId);
    } else {
        query.prepare("SELECT COUNT(*) FROM daily_summaries WHERE profile_id = ? AND date = ? AND machine_id IS NULL");
        query.addBindValue(profileId);
        query.addBindValue(date.toString(Qt::ISODate));
    }
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::exists failed:" << query.lastError().text();
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

int DailySummaryRepository::countDays(qint64 profileId, const QDate& startDate, const QDate& endDate)
{
    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM daily_summaries WHERE profile_id = ? AND date BETWEEN ? AND ?");
    query.addBindValue(profileId);
    query.addBindValue(startDate.toString(Qt::ISODate));
    query.addBindValue(endDate.toString(Qt::ISODate));
    
    if (!query.exec()) {
        qWarning() << "DailySummaryRepository::countDays failed:" << query.lastError().text();
        return 0;
    }
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

QString DailySummaryRepository::generateSessionsHash(qint64 profileId, const QDate& date)
{
    Q_UNUSED(profileId)
    // Generate a hash based on date for basic cache invalidation
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(date.toString(Qt::ISODate).toUtf8());
    hash.addData(QDateTime::currentDateTime().toString().toUtf8());
    return QString(hash.result().toHex());
}

DailySummaryData DailySummaryRepository::mapResultToData(const QSqlQuery& query)
{
    DailySummaryData data;
    
    data.id = query.value("id").toLongLong();
    data.profileId = query.value("profile_id").toLongLong();
    data.date = query.value("date").toString();
    data.machineId = query.value("machine_id").toLongLong();
    
    data.sessionCount = query.value("session_count").toInt();
    data.enabledSessionCount = query.value("enabled_session_count").toInt();
    
    data.totalHours = query.value("total_hours").toDouble();
    data.maskOnHours = query.value("mask_on_hours").toDouble();
    
    data.ahi = query.value("ahi").toDouble();
    data.rdi = query.value("rdi").toDouble();
    data.obstructiveCount = query.value("obstructive_count").toInt();
    data.centralCount = query.value("central_count").toInt();
    data.hypopneaCount = query.value("hypopnea_count").toInt();
    data.reraCount = query.value("rera_count").toInt();
    data.clearAirwayCount = query.value("clear_airway_count").toInt();
    
    data.pressureAvg = query.value("pressure_avg").toDouble();
    data.pressureMin = query.value("pressure_min").toDouble();
    data.pressureMax = query.value("pressure_max").toDouble();
    data.pressure95th = query.value("pressure_95th").toDouble();
    
    data.leakTotalAvg = query.value("leak_total_avg").toDouble();
    data.leakTotal95th = query.value("leak_total_95th").toDouble();
    data.leakTotalMax = query.value("leak_total_max").toDouble();
    data.leakUnintentionalAvg = query.value("leak_unintentional_avg").toDouble();
    
    data.spo2Avg = query.value("spo2_avg").toDouble();
    data.spo2Min = query.value("spo2_min").toDouble();
    data.pulseAvg = query.value("pulse_avg").toDouble();
    data.pulseMin = query.value("pulse_min").toDouble();
    data.pulseMax = query.value("pulse_max").toDouble();
    
    data.isCompliant = query.value("is_compliant").toBool();
    data.hasOximetry = query.value("has_oximetry").toBool();
    
    data.calculatedAt = query.value("calculated_at").toString();
    data.sessionsHash = query.value("sessions_hash").toString();
    
    return data;
}
