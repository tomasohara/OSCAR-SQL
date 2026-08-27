/* Apple Health Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "applehealthtests.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>

#include <algorithm>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/applehealthDataParsing.h"
#include "SleepLib/loader_plugins/applehealth_loader.h"
#include "database/database_manager.h"
#include "zip.h"

namespace {

const QString kProfileName = QStringLiteral("AppleHealthUnitTest");
const QString kAppleSource = QStringLiteral("test\u2019s Apple\u00A0Watch");

// Apple's fixed-width " +hhmm" offset, for the host timezone at that instant.
static QString localStamp(const QString &wallClock)
{
    const QDateTime local = QDateTime::fromString(wallClock, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const int offsetMinutes = local.offsetFromUtc() / 60;
    const int absMinutes = qAbs(offsetMinutes);
    return wallClock + (offsetMinutes < 0 ? QStringLiteral(" -") : QStringLiteral(" +"))
        + QStringLiteral("%1%2")
              .arg(absMinutes / 60, 2, 10, QLatin1Char('0'))
              .arg(absMinutes % 60, 2, 10, QLatin1Char('0'));
}

// The loader buckets nights by local noon-to-noon, so stamps pinned to -0400 would group
// differently per host timezone. Restamp to the host's offset, same local wall clock.
static QByteArray toLocalStamps(const QByteArray &xml)
{
    static const QRegularExpression stampRe(
        QStringLiteral("(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}) -0400"));
    const QString text = QString::fromUtf8(xml);
    QString out;
    qsizetype copied = 0;
    QRegularExpressionMatchIterator it = stampRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += text.mid(copied, match.capturedStart() - copied);
        out += localStamp(match.captured(1));
        copied = match.capturedEnd();
    }
    out += text.mid(copied);
    return out.toUtf8();
}

// Shifts only the Apple-source stage records of the fixture's first night; vitals keep
// their stamps, so a re-import match can only come from night-date dedupe.
static QByteArray shiftFirstNightStages(const QByteArray &xml, int seconds)
{
    QStringList lines = QString::fromUtf8(xml).split(QLatin1Char('\n'));
    static const QString sleepType =
        QStringLiteral("type=\"HKCategoryTypeIdentifierSleepAnalysis\"");
    static const QString appleSource = QStringLiteral("sourceName=\"") + kAppleSource
        + QLatin1Char('"');
    static const QRegularExpression stageValueRe(
        QStringLiteral("value=\"HKCategoryValueSleepAnalysis(?:Awake|AsleepCore|AsleepDeep|AsleepREM|AsleepUnspecified)\""));
    static const QRegularExpression timestampRe(
        QStringLiteral("(startDate|endDate)=\"(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})( [+-]\\d{4})\""));
    static const QString timestampFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");

    for (QString &line : lines) {
        if (!line.contains(sleepType) || !line.contains(appleSource)
            || !stageValueRe.match(line).hasMatch()) {
            continue;
        }
        const QRegularExpressionMatch startMatch = timestampRe.match(line);
        if (!startMatch.hasMatch()) {
            continue;
        }
        const QDateTime start = QDateTime::fromString(startMatch.captured(2), timestampFormat);
        const QDate night = start.time() < QTime(12, 0)
            ? start.date().addDays(-1) : start.date();
        if (night != QDate(2025, 7, 1)) {
            continue;
        }

        QString shiftedLine;
        qsizetype copied = 0;
        QRegularExpressionMatchIterator it = timestampRe.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            shiftedLine += line.mid(copied, match.capturedStart(2) - copied);
            const QDateTime timestamp =
                QDateTime::fromString(match.captured(2), timestampFormat);
            shiftedLine += timestamp.addSecs(seconds).toString(timestampFormat);
            copied = match.capturedEnd(2);
        }
        shiftedLine += line.mid(copied);
        line = shiftedLine;
    }
    return lines.join(QLatin1Char('\n')).toUtf8();
}

static QByteArray fixtureXml(int firstNightStageShiftSeconds = 0)
{
    QByteArray xml = toLocalStamps(QByteArray(R"XML(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE HealthData [
<!ELEMENT HealthData (ExportDate, Record*, Correlation*, Workout*)>
<!ATTLIST HealthData locale CDATA #REQUIRED>
<!ELEMENT ExportDate EMPTY>
<!ELEMENT Record EMPTY>
<!ELEMENT Correlation ANY>
<!ELEMENT Workout ANY>
]>
<HealthData locale="en_US">
  <ExportDate value="2025-07-04 12:00:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAwake" startDate="2025-07-01 22:00:00 -0400" endDate="2025-07-01 22:10:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepCore" startDate="2025-07-01 22:10:00 -0400" endDate="2025-07-01 23:10:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepDeep" startDate="2025-07-01 23:10:00 -0400" endDate="2025-07-01 23:40:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepREM" startDate="2025-07-01 23:40:00 -0400" endDate="2025-07-02 00:10:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepUnspecified" startDate="2025-07-02 00:10:00 -0400" endDate="2025-07-02 00:40:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisInBed" startDate="2025-07-01 21:50:00 -0400" endDate="2025-07-01 22:00:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisFutureStage" startDate="2025-07-02 00:40:00 -0400" endDate="2025-07-02 00:50:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="Sleep Cycle" unit="" value="HKCategoryValueSleepAnalysisAsleepUnspecified" startDate="2025-07-01 22:00:00 -0400" endDate="2025-07-01 23:00:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="Sleep Cycle" unit="" value="HKCategoryValueSleepAnalysisAsleepUnspecified" startDate="2025-07-01 23:00:00 -0400" endDate="2025-07-02 00:00:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="Sleep Cycle" unit="" value="HKCategoryValueSleepAnalysisAwake" startDate="2025-07-02 00:00:00 -0400" endDate="2025-07-02 00:30:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepUnspecified" startDate="2025-07-02 14:00:00 -0400" endDate="2025-07-02 14:20:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepCore" startDate="2025-07-02 14:20:00 -0400" endDate="2025-07-02 15:00:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepDeep" startDate="2025-07-02 22:00:00 -0400" endDate="2025-07-02 22:30:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAsleepREM" startDate="2025-07-02 22:30:00 -0400" endDate="2025-07-02 22:50:00 -0400"/>
  <Record type="HKCategoryTypeIdentifierSleepAnalysis" sourceName="test’s Apple Watch" unit="" value="HKCategoryValueSleepAnalysisAwake" startDate="2025-07-02 22:50:00 -0400" endDate="2025-07-02 23:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="59" startDate="2025-07-01 16:00:00 -0400" endDate="2025-07-01 16:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="60" startDate="2025-07-01 22:05:00 -0400" endDate="2025-07-01 22:05:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="61" startDate="2025-07-01 22:10:00 -0400" endDate="2025-07-01 22:10:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="62" startDate="2025-07-01 22:15:00 -0400" endDate="2025-07-01 22:15:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="63" startDate="2025-07-01 22:20:00 -0400" endDate="2025-07-01 22:20:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="64" startDate="2025-07-01 22:25:00 -0400" endDate="2025-07-01 22:25:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="65" startDate="2025-07-01 22:40:00 -0400" endDate="2025-07-01 22:40:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="66" startDate="2025-07-01 22:45:00 -0400" endDate="2025-07-01 22:45:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="67" startDate="2025-07-01 22:50:00 -0400" endDate="2025-07-01 22:50:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="68" startDate="2025-07-01 22:55:00 -0400" endDate="2025-07-01 22:55:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="69" startDate="2025-07-01 23:00:00 -0400" endDate="2025-07-01 23:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="70" startDate="2025-07-01 23:05:00 -0400" endDate="2025-07-01 23:05:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="71" startDate="2025-07-01 23:10:00 -0400" endDate="2025-07-01 23:10:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="72" startDate="2025-07-02 22:02:00 -0400" endDate="2025-07-02 22:02:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="73" startDate="2025-07-02 22:07:00 -0400" endDate="2025-07-02 22:07:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="74" startDate="2025-07-02 22:12:00 -0400" endDate="2025-07-02 22:12:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="75" startDate="2025-07-02 22:17:00 -0400" endDate="2025-07-02 22:17:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="76" startDate="2025-07-02 22:22:00 -0400" endDate="2025-07-02 22:22:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="77" startDate="2025-07-02 22:27:00 -0400" endDate="2025-07-02 22:27:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="78" startDate="2025-07-02 22:32:00 -0400" endDate="2025-07-02 22:32:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="79" startDate="2025-07-02 22:37:00 -0400" endDate="2025-07-02 22:37:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="80" startDate="2025-07-02 22:42:00 -0400" endDate="2025-07-02 22:42:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="81" startDate="2025-07-02 22:47:00 -0400" endDate="2025-07-02 22:47:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="88" startDate="2025-07-04 13:00:00 -0400" endDate="2025-07-04 13:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="89" startDate="2025-07-04 15:00:00 -0400" endDate="2025-07-04 15:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="90" startDate="2025-07-04 22:00:00 -0400" endDate="2025-07-04 22:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="91" startDate="2025-07-04 22:05:00 -0400" endDate="2025-07-04 22:05:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="92" startDate="2025-07-04 22:10:00 -0400" endDate="2025-07-04 22:10:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="93" startDate="2025-07-04 22:15:00 -0400" endDate="2025-07-04 22:15:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="94" startDate="2025-07-04 22:20:00 -0400" endDate="2025-07-04 22:20:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="95" startDate="2025-07-04 22:25:00 -0400" endDate="2025-07-04 22:25:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="96" startDate="2025-07-04 22:30:00 -0400" endDate="2025-07-04 22:30:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="97" startDate="2025-07-04 22:35:00 -0400" endDate="2025-07-04 22:35:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="98" startDate="2025-07-04 22:40:00 -0400" endDate="2025-07-04 22:40:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="99" startDate="2025-07-04 22:45:00 -0400" endDate="2025-07-04 22:45:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRate" sourceName="test’s Apple Watch" unit="count/min" value="99" startDate="2025-07-01 21:00:00 -0400" endDate="malformed-date"/>
  <Record type="HKQuantityTypeIdentifierOxygenSaturation" sourceName="test’s Apple Watch" unit="%" value="0.97" startDate="2025-07-01 22:12:00 -0400" endDate="2025-07-01 22:12:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierOxygenSaturation" sourceName="test’s Apple Watch" unit="%" value="0.96" startDate="2025-07-01 22:42:00 -0400" endDate="2025-07-01 22:42:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierOxygenSaturation" sourceName="test’s Apple Watch" unit="%" value="0.93" startDate="2025-07-02 01:30:00 -0400" endDate="2025-07-02 01:30:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierOxygenSaturation" sourceName="test’s Apple Watch" unit="%" value="0.95" startDate="2025-07-02 22:12:00 -0400" endDate="2025-07-02 22:12:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierOxygenSaturation" sourceName="test’s Apple Watch" unit="%" value="0.94" startDate="2025-07-02 22:42:00 -0400" endDate="2025-07-02 22:42:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierRespiratoryRate" sourceName="test’s Apple Watch" unit="count/min" value="14.5" startDate="2025-07-01 22:18:00 -0400" endDate="2025-07-01 22:18:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierRespiratoryRate" sourceName="test’s Apple Watch" unit="count/min" value="15.5" startDate="2025-07-02 22:18:00 -0400" endDate="2025-07-02 22:18:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRateVariabilitySDNN" sourceName="test’s Apple Watch" unit="ms" value="42" startDate="2025-07-01 22:22:00 -0400" endDate="2025-07-01 22:22:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierHeartRateVariabilitySDNN" sourceName="test’s Apple Watch" unit="ms" value="48" startDate="2025-07-02 22:22:00 -0400" endDate="2025-07-02 22:22:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierAppleSleepingBreathingDisturbances" sourceName="test’s Apple Watch" unit="count" value="1.25" startDate="2025-07-01 22:00:00 -0400" endDate="2025-07-02 00:40:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierAppleSleepingBreathingDisturbances" sourceName="test’s Apple Watch" unit="count" value="2.5" startDate="2025-07-02 22:00:00 -0400" endDate="2025-07-02 23:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierAppleSleepingWristTemperature" sourceName="test’s Apple Watch" unit="degF" value="98.6" startDate="2025-07-01 22:00:00 -0400" endDate="2025-07-02 00:40:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierAppleSleepingWristTemperature" sourceName="test’s Apple Watch" unit="degC" value="36.5" startDate="2025-07-02 22:00:00 -0400" endDate="2025-07-02 23:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierBodyMass" sourceName="Test Scale" unit="lb" value="180" startDate="2025-07-02 08:00:00 -0400" endDate="2025-07-02 08:00:00 -0400"/>
  <Record type="HKQuantityTypeIdentifierBodyMass" sourceName="Test Scale" unit="kg" value="75" startDate="2025-07-03 08:00:00 -0400" endDate="2025-07-03 08:00:00 -0400"/>
  <Correlation type="HKCorrelationTypeIdentifierBloodPressure">
    <Record type="HKQuantityTypeIdentifierOxygenSaturation" sourceName="Nested Test" unit="%" value="0.50" startDate="2025-07-01 22:30:00 -0400" endDate="2025-07-01 22:30:00 -0400"/>
  </Correlation>
  <Workout workoutActivityType="HKWorkoutActivityTypeWalking">
    <Record type="HKQuantityTypeIdentifierBodyMass" sourceName="Nested Test" unit="kg" value="999" startDate="2025-07-01 22:30:00 -0400" endDate="2025-07-01 22:30:00 -0400"/>
  </Workout>
</HealthData>
)XML"));
    if (firstNightStageShiftSeconds != 0) {
        xml = shiftFirstNightStages(xml, firstNightStageShiftSeconds);
    }
    return xml;
}

// Fixture times are local wall clock, matching what toLocalStamps() wrote.
static qint64 epochMs(const QString &wallClock)
{
    return QDateTime::fromString(wallClock, QStringLiteral("yyyy-MM-dd HH:mm:ss")).toMSecsSinceEpoch();
}

// Mirrors AppleHealthLoader::nightDate() (local noon-to-noon).
static QDate loaderNightDate(qint64 timeMs)
{
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(timeMs, Qt::LocalTime);
    return local.time() < QTime(12, 0) ? local.date().addDays(-1) : local.date();
}

static const AppleHealthInterval *findInterval(const AppleHealthData &data, qint64 startMs)
{
    for (const AppleHealthInterval &interval : data.sleepStages) {
        if (interval.startMs == startMs) {
            return &interval;
        }
    }
    return nullptr;
}

static QList<Session *> sortedSessions(Machine *machine)
{
    QList<Session *> sessions = machine->sessionlist.values();
    std::sort(sessions.begin(), sessions.end(), [](Session *a, Session *b) {
        return a->first() < b->first();
    });
    return sessions;
}

static int appleHealthSessionCount()
{
    int count = 0;
    for (Machine *machine : p_profile->GetMachines()) {
        if (machine->loaderName() == applehealth_class_name) {
            count += machine->sessionlist.size();
        }
    }
    return count;
}

} // namespace

// file-static (like dreemtests) so the registered loader singleton stays reachable at exit
static AppleHealthLoader *s_loader = nullptr;

void AppleHealthTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own, like EventsTabTests)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-applehealthtests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());

    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    schema::init();
    AppleHealthLoader::Register();
    Profiles::Scan();

    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) {
        p_profile = Profiles::Create(kProfileName, &profileDir);
    }
    QVERIFY(p_profile != nullptr);

    s_loader = dynamic_cast<AppleHealthLoader *>(lookupLoader(applehealth_class_name));
    QVERIFY(s_loader != nullptr);

    m_exportPath = m_tempDir->path() + QStringLiteral("/export.xml");
    QFile exportFile(m_exportPath);
    QVERIFY(exportFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray xml = fixtureXml();
    QCOMPARE(exportFile.write(xml), static_cast<qint64>(xml.size()));
    exportFile.close();

    m_garbagePath = m_tempDir->path() + QStringLiteral("/garbage.xml");
    QFile garbageFile(m_garbagePath);
    QVERIFY(garbageFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray garbage("<?xml version=\"1.0\"?><Garbage/>");
    QCOMPARE(garbageFile.write(garbage), static_cast<qint64>(garbage.size()));
    garbageFile.close();
}

void AppleHealthTests::cleanupTestCase()
{
    Profiles::profiles.clear();

    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;

    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);

    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}

void AppleHealthTests::testParser()
{
    AppleHealthParser parser;
    AppleHealthData data;
    QVERIFY2(parser.parse(m_exportPath, data), qPrintable(parser.errorString()));

    QCOMPARE(data.recordsSeen, 66LL);
    QCOMPARE(data.sleepStages.size(), 10);
    QCOMPARE(data.heartRate.size(), 35);
    QCOMPARE(data.spo2.size(), 5);
    QCOMPARE(data.respRate.size(), 2);
    QCOMPARE(data.hrv.size(), 2);
    QCOMPARE(data.breathingDisturbances.size(), 2);
    QCOMPARE(data.wristTemp.size(), 2);
    QCOMPARE(data.weights.size(), 2);

    QCOMPARE(data.typeCounts.size(), 8);
    QCOMPARE(data.typeCounts.value(QStringLiteral("SleepAnalysis")), 10LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("HeartRate")), 35LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("OxygenSaturation")), 5LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("RespiratoryRate")), 2LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("HeartRateVariabilitySDNN")), 2LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("AppleSleepingBreathingDisturbances")), 2LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("AppleSleepingWristTemperature")), 2LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("BodyMass")), 2LL);

    QCOMPARE(data.sleepSourceCounts.size(), 2);
    QCOMPARE(data.sleepSourceCounts.value(kAppleSource), 12);
    QCOMPARE(data.sleepSourceCounts.value(QStringLiteral("Sleep Cycle")), 3);

    const AppleHealthInterval *awake = findInterval(data, epochMs(QStringLiteral("2025-07-01 22:00:00")));
    const AppleHealthInterval *core = findInterval(data, epochMs(QStringLiteral("2025-07-01 22:10:00")));
    const AppleHealthInterval *deep = findInterval(data, epochMs(QStringLiteral("2025-07-01 23:10:00")));
    const AppleHealthInterval *rem = findInterval(data, epochMs(QStringLiteral("2025-07-01 23:40:00")));
    const AppleHealthInterval *unspecified = findInterval(data, epochMs(QStringLiteral("2025-07-02 00:10:00")));
    QVERIFY(awake != nullptr);
    QVERIFY(core != nullptr);
    QVERIFY(deep != nullptr);
    QVERIFY(rem != nullptr);
    QVERIFY(unspecified != nullptr);
    QCOMPARE(awake->stage, 1);
    QCOMPARE(rem->stage, 2);
    QCOMPARE(core->stage, 3);
    QCOMPARE(unspecified->stage, 3);
    QCOMPARE(deep->stage, 4);
    QVERIFY(findInterval(data, epochMs(QStringLiteral("2025-07-01 21:50:00"))) == nullptr);
    QVERIFY(findInterval(data, epochMs(QStringLiteral("2025-07-02 00:40:00"))) == nullptr);

    QVERIFY(qAbs(data.spo2.at(0).value - 97.0F) < 0.001F);
    QVERIFY(qAbs(data.weights.at(0).kg - 81.6466266) < 0.000001);
    QCOMPARE(data.weights.at(1).kg, 75.0);
    QVERIFY(qAbs(data.wristTemp.at(0).value - 37.0) < 0.000001);
    QCOMPARE(data.wristTemp.at(1).value, 36.5);
    QVERIFY(std::none_of(data.heartRate.cbegin(), data.heartRate.cend(), [](const AppleHealthSample &sample) {
        return sample.timeMs == epochMs(QStringLiteral("2025-07-01 21:00:00"));
    }));
    QVERIFY(std::none_of(data.spo2.cbegin(), data.spo2.cend(), [](const AppleHealthSample &sample) {
        return sample.value == 50.0F;
    }));
    QVERIFY(std::none_of(data.weights.cbegin(), data.weights.cend(), [](const AppleHealthWeight &weight) {
        return weight.kg == 999.0;
    }));
}

void AppleHealthTests::testParserCutoff()
{
    const qint64 cutoff = epochMs(QStringLiteral("2025-07-02 12:00:00"));
    AppleHealthParser parser;
    parser.setCutoff(cutoff);
    AppleHealthData data;
    QVERIFY2(parser.parse(m_exportPath, data), qPrintable(parser.errorString()));

    QCOMPARE(data.sleepStages.size(), 5);
    QCOMPARE(data.heartRate.size(), 22);
    QCOMPARE(data.spo2.size(), 2);
    QCOMPARE(data.respRate.size(), 1);
    QCOMPARE(data.hrv.size(), 1);
    QCOMPARE(data.breathingDisturbances.size(), 1);
    QCOMPARE(data.wristTemp.size(), 1);
    QCOMPARE(data.weights.size(), 1);
    QCOMPARE(data.sleepSourceCounts.size(), 1);
    QCOMPARE(data.sleepSourceCounts.value(kAppleSource), 5);
    QCOMPARE(data.typeCounts.value(QStringLiteral("SleepAnalysis")), 5LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("HeartRate")), 22LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("OxygenSaturation")), 2LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("RespiratoryRate")), 1LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("HeartRateVariabilitySDNN")), 1LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("AppleSleepingBreathingDisturbances")), 1LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("AppleSleepingWristTemperature")), 1LL);
    QCOMPARE(data.typeCounts.value(QStringLiteral("BodyMass")), 1LL);

    for (const AppleHealthInterval &interval : data.sleepStages) {
        QVERIFY(interval.endMs >= cutoff);
    }
    for (const AppleHealthSample &sample : data.heartRate) {
        QVERIFY(sample.timeMs >= cutoff);
    }
    for (const AppleHealthSample &sample : data.spo2) {
        QVERIFY(sample.timeMs >= cutoff);
    }
    for (const AppleHealthSample &sample : data.respRate) {
        QVERIFY(sample.timeMs >= cutoff);
    }
    for (const AppleHealthSample &sample : data.hrv) {
        QVERIFY(sample.timeMs >= cutoff);
    }
    QVERIFY(data.breathingDisturbances.constFirst().startMs >= cutoff);
    QVERIFY(data.wristTemp.constFirst().startMs >= cutoff);
    QVERIFY(data.weights.constFirst().timeMs >= cutoff);
}

void AppleHealthTests::testLoaderImport()
{
    QCOMPARE(s_loader->OpenFile(m_exportPath), 4);
    QCOMPARE(s_loader->lastImportSummary().sleepSessions, 2);
    QCOMPARE(s_loader->lastImportSummary().oxiSessions, 2);
    QCOMPARE(s_loader->lastImportSummary().chosenSleepSource, kAppleSource);

    const QList<Machine *> sleepMachines = p_profile->GetMachines(MT_SLEEPSTAGE);
    const QList<Machine *> oxiMachines = p_profile->GetMachines(MT_OXIMETER);
    QCOMPARE(sleepMachines.size(), 1);
    QCOMPARE(oxiMachines.size(), 1);
    QCOMPARE(sleepMachines.constFirst()->loaderName(), applehealth_class_name);
    QCOMPARE(oxiMachines.constFirst()->loaderName(), applehealth_class_name);
    int appleMachineCount = 0;
    for (Machine *machine : p_profile->GetMachines()) {
        if (machine->loaderName() == applehealth_class_name) {
            ++appleMachineCount;
        }
    }
    QCOMPARE(appleMachineCount, 2);
    QVERIFY(sleepMachines.constFirst()->getDatabaseId() > 0);
    QVERIFY(oxiMachines.constFirst()->getDatabaseId() > 0);

    const QList<Session *> sleepSessions = sortedSessions(sleepMachines.constFirst());
    QCOMPARE(sleepSessions.size(), 2);
    Session *night1Sleep = sleepSessions.at(0);
    Session *night2Sleep = sleepSessions.at(1);
    QVERIFY(night1Sleep->sessionRowId() > 0);
    QVERIFY(night2Sleep->sessionRowId() > 0);
    QVERIFY(night1Sleep->LoadFromDatabase());
    QVERIFY(night2Sleep->LoadFromDatabase());
    QCOMPARE(night1Sleep->m_slices.size(), 1);
    QCOMPARE(night2Sleep->m_slices.size(), 2);

    qint64 night2SliceTime = 0;
    for (const SessionSlice &slice : night2Sleep->m_slices) {
        QCOMPARE(slice.status, MaskOn);
        night2SliceTime += slice.end - slice.start;
    }
    QCOMPARE(night2SliceTime, 120LL * 60LL * 1000LL);
    QVERIFY(night2SliceTime < night2Sleep->last() - night2Sleep->first());

    QCOMPARE(night1Sleep->settings.value(ZEO_TimeInWake).toLongLong(), 10LL);
    QCOMPARE(night1Sleep->settings.value(ZEO_TimeInREM).toLongLong(), 30LL);
    QCOMPARE(night1Sleep->settings.value(ZEO_TimeInLight).toLongLong(), 90LL);
    QCOMPARE(night1Sleep->settings.value(ZEO_TimeInDeep).toLongLong(), 30LL);
    QCOMPARE(night2Sleep->settings.value(ZEO_TimeInWake).toLongLong(), 10LL);
    QCOMPARE(night2Sleep->settings.value(ZEO_TimeInREM).toLongLong(), 20LL);
    QCOMPARE(night2Sleep->settings.value(ZEO_TimeInLight).toLongLong(), 60LL);
    QCOMPARE(night2Sleep->settings.value(ZEO_TimeInDeep).toLongLong(), 30LL);

    QVERIFY(night1Sleep->settings.contains(AW_BreathingDisturbances));
    QVERIFY(night1Sleep->settings.contains(AW_WristTemp));
    QVERIFY(night2Sleep->settings.contains(AW_BreathingDisturbances));
    QVERIFY(night2Sleep->settings.contains(AW_WristTemp));
    QVERIFY(qAbs(night1Sleep->settings.value(AW_BreathingDisturbances).toDouble() - 1.25) < 0.000001);
    QVERIFY(qAbs(night1Sleep->settings.value(AW_WristTemp).toDouble() - 37.0) < 0.000001);
    QVERIFY(qAbs(night2Sleep->settings.value(AW_BreathingDisturbances).toDouble() - 2.5) < 0.000001);
    QVERIFY(qAbs(night2Sleep->settings.value(AW_WristTemp).toDouble() - 36.5) < 0.000001);

    const QList<Session *> oxiSessions = sortedSessions(oxiMachines.constFirst());
    QCOMPARE(oxiSessions.size(), 2);
    const QDate absentNight =
        loaderNightDate(epochMs(QStringLiteral("2025-07-04 22:00:00")));
    QVERIFY(std::none_of(oxiSessions.cbegin(), oxiSessions.cend(),
                         [absentNight](Session *session) {
                             return loaderNightDate(session->first()) == absentNight;
                         }));
    for (Session *session : oxiSessions) {
        QVERIFY(session->sessionRowId() > 0);
        QVERIFY(session->LoadFromDatabase());
        QVERIFY(session->OpenEvents());
    }
    for (int i = 0; i < 2; ++i) {
        Session *session = oxiSessions.at(i);
        QVERIFY(session->m_availableChannels.contains(OXI_SPO2Drop));
        QCOMPARE(session->count(OXI_SPO2Drop), 0.0F);
        QVERIFY(session->m_availableChannels.contains(OXI_PulseChange));
        QCOMPARE(session->count(OXI_PulseChange), 0.0F);
    }
    QCOMPARE(oxiSessions.at(0)->eventlist.value(OXI_Pulse).size(), 2);
    QCOMPARE(oxiSessions.at(0)->eventlist.value(OXI_Pulse).at(0)->count(), 5U);
    QCOMPARE(oxiSessions.at(0)->eventlist.value(OXI_Pulse).at(1)->count(), 7U);
    for (EventList *eventList : oxiSessions.at(0)->eventlist.value(OXI_Pulse)) {
        for (quint32 i = 0; i < eventList->count(); ++i) {
            QVERIFY(eventList->time(i) >= epochMs(QStringLiteral("2025-07-01 22:00:00")));
        }
    }
    QCOMPARE(oxiSessions.at(0)->eventlist.value(OXI_SPO2).size(), 1);
    QCOMPARE(oxiSessions.at(0)->eventlist.value(OXI_SPO2).constFirst()->count(), 2U);
    QCOMPARE(oxiSessions.at(1)->eventlist.value(OXI_Pulse).size(), 1);
}

void AppleHealthTests::testLoaderIdempotency()
{
    Machine *sleepMachine = p_profile->GetMachine(MT_SLEEPSTAGE);
    Machine *oxiMachine = p_profile->GetMachine(MT_OXIMETER);
    QVERIFY(sleepMachine != nullptr);
    QVERIFY(oxiMachine != nullptr);
    const int sleepCount = sleepMachine->sessionlist.size();
    const int oxiCount = oxiMachine->sessionlist.size();

    QCOMPARE(s_loader->OpenFile(m_exportPath), 0);
    QCOMPARE(sleepMachine->sessionlist.size(), sleepCount);
    QCOMPARE(oxiMachine->sessionlist.size(), oxiCount);
    QCOMPARE(s_loader->lastImportSummary().sleepSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().oxiSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().skippedExisting, 2);
}

void AppleHealthTests::testLoaderImportsZip()
{
    Machine *sleepMachine = p_profile->GetMachine(MT_SLEEPSTAGE);
    Machine *oxiMachine = p_profile->GetMachine(MT_OXIMETER);
    QVERIFY(sleepMachine != nullptr);
    QVERIFY(oxiMachine != nullptr);
    const int sleepCount = sleepMachine->sessionlist.size();
    const int oxiCount = oxiMachine->sessionlist.size();

    const QString exportZipPath = m_tempDir->path() + QStringLiteral("/export.zip");
    {
        ZipFile zip;
        QVERIFY(zip.Open(exportZipPath));
        QVERIFY(zip.AddFile(m_exportPath, QStringLiteral("apple_health_export/export.xml")));
        zip.Close();
    }

    QCOMPARE(s_loader->OpenFile(exportZipPath), 0);
    QCOMPARE(sleepMachine->sessionlist.size(), sleepCount);
    QCOMPARE(oxiMachine->sessionlist.size(), oxiCount);
    QVERIFY(s_loader->lastImportSummary().validFile);
    QCOMPARE(s_loader->lastImportSummary().sleepSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().oxiSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().skippedExisting, 2);

    const QString invalidZipPath = m_tempDir->path() + QStringLiteral("/invalid-export.zip");
    {
        ZipFile zip;
        QVERIFY(zip.Open(invalidZipPath));
        QVERIFY(zip.AddFile(m_garbagePath, QStringLiteral("apple_health_export/garbage.xml")));
        zip.Close();
    }

    QCOMPARE(s_loader->OpenFile(invalidZipPath), -1);
    QCOMPARE(sleepMachine->sessionlist.size(), sleepCount);
    QCOMPARE(oxiMachine->sessionlist.size(), oxiCount);
    QVERIFY(!s_loader->lastImportSummary().validFile);
}

void AppleHealthTests::testLoaderSkipsShiftedNights()
{
    Machine *sleepMachine = p_profile->GetMachine(MT_SLEEPSTAGE);
    Machine *oxiMachine = p_profile->GetMachine(MT_OXIMETER);
    QVERIFY(sleepMachine != nullptr);
    QVERIFY(oxiMachine != nullptr);
    const int sleepCount = sleepMachine->sessionlist.size();
    const int oxiCount = oxiMachine->sessionlist.size();

    const QString shiftedExportPath =
        m_tempDir->path() + QStringLiteral("/shifted-export.xml");
    QFile shiftedExport(shiftedExportPath);
    QVERIFY(shiftedExport.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray shiftedXml = fixtureXml(60);
    QCOMPARE(shiftedExport.write(shiftedXml), static_cast<qint64>(shiftedXml.size()));
    shiftedExport.close();

    QCOMPARE(s_loader->OpenFile(shiftedExportPath), 0);
    QCOMPARE(s_loader->lastImportSummary().sleepSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().oxiSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().skippedExisting, 2);
    QCOMPARE(sleepMachine->sessionlist.size(), sleepCount);
    QCOMPARE(oxiMachine->sessionlist.size(), oxiCount);
}

void AppleHealthTests::testLoaderRejectsGarbage()
{
    const int sessionCount = appleHealthSessionCount();
    QCOMPARE(s_loader->OpenFile(m_garbagePath), -1);
    QCOMPARE(appleHealthSessionCount(), sessionCount);
    QVERIFY(!s_loader->lastImportSummary().validFile);
    QCOMPARE(s_loader->lastImportSummary().sleepSessions, 0);
    QCOMPARE(s_loader->lastImportSummary().oxiSessions, 0);
}
