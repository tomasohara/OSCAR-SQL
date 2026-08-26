/* SleepLib Apple Health Loader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTime>

#include <algorithm>
#include <limits>

#include "applehealth_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"

AppleHealthLoader::AppleHealthLoader()
{
    m_type = MT_OXIMETER;
}

AppleHealthLoader::~AppleHealthLoader()
{
}

bool AppleHealthLoader::Detect(const QString & path)
{
    Q_UNUSED(path);
    return false;
}

int AppleHealthLoader::OpenFile(const QString & filename)
{
    m_data = AppleHealthData();
    m_session = nullptr;
    m_importChannels.clear();
    m_importLastValue.clear();

    if (filename.endsWith(".zip", Qt::CaseInsensitive)) {
        qDebug() << "AppleHealthLoader::OpenFile:" << filename << "is a ZIP archive; extract export.xml first";
        return -1;
    }

    QFileInfo fileInfo(filename);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qDebug() << "AppleHealthLoader::OpenFile: file does not exist:" << filename;
        return -1;
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "AppleHealthLoader::OpenFile: could not open:" << filename;
        return -1;
    }

    const QByteArray prefix = file.read(8192);
    if (!prefix.contains("HealthData") && !prefix.contains("HealthKit Export")) {
        qDebug() << "AppleHealthLoader::OpenFile: file does not look like an Apple Health export:" << filename;
        return -1;
    }
    file.close();

    AppleHealthParser parser;
    qint64 cutoffMs = 0;
    if (p_profile != nullptr) {
        const QDate firstCpapDay = p_profile->FirstDay(MT_CPAP);
        if (firstCpapDay.isValid() && p_profile->FindDay(firstCpapDay, MT_CPAP) != nullptr) {
            cutoffMs = QDateTime(firstCpapDay.addDays(-7), QTime(0, 0), Qt::LocalTime)
                           .toMSecsSinceEpoch();
        }
    }
    parser.setCutoff(cutoffMs);
    if (!parser.parse(filename, m_data)) {
        qWarning() << "AppleHealthLoader::OpenFile:" << parser.errorString();
        m_data = AppleHealthData();
        return -1;
    }

    if (!m_data.sleepStages.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: stages:" << m_data.sleepStages.size();
    }
    if (!m_data.heartRate.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: hr:" << m_data.heartRate.size();
    }
    if (!m_data.spo2.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: spo2:" << m_data.spo2.size();
    }
    if (!m_data.respRate.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: resp:" << m_data.respRate.size();
    }
    if (!m_data.hrv.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: hrv:" << m_data.hrv.size();
    }
    if (!m_data.breathingDisturbances.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: bd:"
                 << m_data.breathingDisturbances.size();
    }
    if (!m_data.wristTemp.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: wristTemp:" << m_data.wristTemp.size();
    }
    qDebug() << "AppleHealthLoader::OpenFile: recordsSeen:" << m_data.recordsSeen;
    qDebug() << "AppleHealthLoader::OpenFile: sleepSourceCounts:" << m_data.sleepSourceCounts;

    // TODO: import BodyMass

    QMap<QDate, QVector<AppleHealthInterval>> stagesByNight;
    QMap<QDate, QVector<AppleHealthSample>> heartRateByNight;
    QMap<QDate, QVector<AppleHealthSample>> spo2ByNight;
    QMap<QDate, QVector<AppleHealthSample>> respRateByNight;
    QMap<QDate, QVector<AppleHealthSample>> hrvByNight;
    QMap<QDate, QVector<AppleHealthNightScalar>> disturbancesByNight;
    QMap<QDate, QVector<AppleHealthNightScalar>> wristTempByNight;
    QMap<QDate, int> overnightHeartRateCounts;
    QMap<QDate, bool> nights;

    for (const AppleHealthInterval &interval : m_data.sleepStages) {
        if (interval.endMs <= interval.startMs || interval.stage < 1 || interval.stage > 4) {
            continue;
        }
        const QDate night = nightDate(interval.startMs);
        stagesByNight[night].append(interval);
        nights[night] = true;
    }
    for (const AppleHealthSample &sample : m_data.heartRate) {
        const QDate night = nightDate(sample.timeMs);
        heartRateByNight[night].append(sample);
        const QTime localTime = QDateTime::fromMSecsSinceEpoch(sample.timeMs, Qt::LocalTime).time();
        if (localTime >= QTime(20, 0) || localTime < QTime(12, 0)) {
            ++overnightHeartRateCounts[night];
        }
    }
    for (const AppleHealthSample &sample : m_data.spo2) {
        spo2ByNight[nightDate(sample.timeMs)].append(sample);
    }
    for (const AppleHealthSample &sample : m_data.respRate) {
        respRateByNight[nightDate(sample.timeMs)].append(sample);
    }
    for (const AppleHealthSample &sample : m_data.hrv) {
        hrvByNight[nightDate(sample.timeMs)].append(sample);
    }
    for (const AppleHealthNightScalar &scalar : m_data.breathingDisturbances) {
        disturbancesByNight[nightDate(scalar.startMs)].append(scalar);
    }
    for (const AppleHealthNightScalar &scalar : m_data.wristTemp) {
        wristTempByNight[nightDate(scalar.startMs)].append(scalar);
    }
    for (auto it = overnightHeartRateCounts.cbegin(); it != overnightHeartRateCounts.cend(); ++it) {
        if (it.value() >= 10) {
            nights[it.key()] = true;
        }
    }

    const auto sortSamples = [](auto &samplesByNight) {
        for (auto it = samplesByNight.begin(); it != samplesByNight.end(); ++it) {
            std::sort(it.value().begin(), it.value().end(),
                      [](const AppleHealthSample &a, const AppleHealthSample &b) {
                          return a.timeMs < b.timeMs;
                      });
        }
    };
    sortSamples(heartRateByNight);
    sortSamples(spo2ByNight);
    sortSamples(respRateByNight);
    sortSamples(hrvByNight);
    for (auto it = stagesByNight.begin(); it != stagesByNight.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [](const AppleHealthInterval &a, const AppleHealthInterval &b) {
                      if (a.startMs != b.startMs) {
                          return a.startMs < b.startMs;
                      }
                      return a.endMs < b.endMs;
                  });
    }

    const QVector<AppleHealthInterval> noStages;
    const QVector<AppleHealthSample> noSamples;
    const QVector<AppleHealthNightScalar> noScalars;
    Machine *sleepMach = nullptr;
    Machine *oxiMach = nullptr;
    bool sleepChanged = false;
    bool oxiChanged = false;
    int imported = 0;

    for (auto nightIt = nights.cbegin(); nightIt != nights.cend(); ++nightIt) {
        const QDate night = nightIt.key();
        const auto stagesIt = stagesByNight.constFind(night);
        const auto heartRateIt = heartRateByNight.constFind(night);
        const auto spo2It = spo2ByNight.constFind(night);
        const auto respRateIt = respRateByNight.constFind(night);
        const auto hrvIt = hrvByNight.constFind(night);
        const auto disturbancesIt = disturbancesByNight.constFind(night);
        const auto wristTempIt = wristTempByNight.constFind(night);

        const QVector<AppleHealthInterval> &stages =
            stagesIt != stagesByNight.cend() ? stagesIt.value() : noStages;
        const QVector<AppleHealthSample> &heartRate =
            heartRateIt != heartRateByNight.cend() ? heartRateIt.value() : noSamples;
        const QVector<AppleHealthSample> &spo2 =
            spo2It != spo2ByNight.cend() ? spo2It.value() : noSamples;
        const QVector<AppleHealthSample> &respRate =
            respRateIt != respRateByNight.cend() ? respRateIt.value() : noSamples;
        const QVector<AppleHealthSample> &hrv =
            hrvIt != hrvByNight.cend() ? hrvIt.value() : noSamples;
        const QVector<AppleHealthNightScalar> &disturbances =
            disturbancesIt != disturbancesByNight.cend() ? disturbancesIt.value() : noScalars;
        const QVector<AppleHealthNightScalar> &wristTemp =
            wristTempIt != wristTempByNight.cend() ? wristTempIt.value() : noScalars;

        if (!stages.isEmpty()) {
            if (sleepMach == nullptr) {
                sleepMach = p_profile->CreateMachine(newInfoSleep());
            }
            Session *session = buildSleepSession(sleepMach, stages, disturbances, wristTemp);
            if (session != nullptr) {
                session->SetChanged(true);
                session->UpdateSummaries();
                if (sleepMach->AddSession(session)) {
                    sleepChanged = true;
                    ++imported;
                } else {
                    delete session;
                }
            }
        }

        if (!heartRate.isEmpty() || !spo2.isEmpty() || !respRate.isEmpty() || !hrv.isEmpty()) {
            if (oxiMach == nullptr) {
                oxiMach = p_profile->CreateMachine(newInfo());
            }
            Session *session = buildOxiSession(oxiMach, heartRate, spo2, respRate, hrv);
            if (session != nullptr) {
                session->SetChanged(true);
                session->UpdateSummaries();
                if (oxiMach->AddSession(session)) {
                    oxiChanged = true;
                    ++imported;
                } else {
                    delete session;
                }
            }
        }
    }

    if (sleepChanged) {
        sleepMach->Save();
        sleepMach->SaveSummaryCache();
    }
    if (oxiChanged) {
        oxiMach->Save();
        oxiMach->SaveSummaryCache();
    }
    if (imported > 0) {
        p_profile->StoreMachines();
    }

    m_data = AppleHealthData();
    m_session = nullptr;
    m_importChannels.clear();
    m_importLastValue.clear();
    return imported;
}

QDate AppleHealthLoader::nightDate(qint64 timeMs) const
{
    const QDateTime localTime = QDateTime::fromMSecsSinceEpoch(timeMs, Qt::LocalTime);
    return localTime.time() < QTime(12, 0) ? localTime.date().addDays(-1) : localTime.date();
}

Session *AppleHealthLoader::buildSleepSession(
    Machine *mach, const QVector<AppleHealthInterval> &stages,
    const QVector<AppleHealthNightScalar> &breathingDisturbances,
    const QVector<AppleHealthNightScalar> &wristTemp)
{
    const qint64 firstMs = stages.constFirst().startMs;
    qint64 lastMs = stages.constFirst().endMs;
    for (const AppleHealthInterval &interval : stages) {
        lastMs = std::max(lastMs, interval.endMs);
    }

    const SessionID sessionId = static_cast<SessionID>(firstMs / 1000L);
    if (mach->SessionExists(sessionId)) {
        return nullptr;
    }

    Session *session = new Session(mach, sessionId);
    m_session = session;
    m_importChannels.clear();
    m_importLastValue.clear();
    session->really_set_first(firstMs);
    session->really_set_last(lastMs);

    qint64 stageTimeMs[5] = {0, 0, 0, 0, 0};
    int awakenings = 0;
    bool foundSleep = false;
    for (const AppleHealthInterval &interval : stages) {
        stageTimeMs[interval.stage] += interval.endMs - interval.startMs;
        if (interval.stage == 1) {
            if (foundSleep) {
                ++awakenings;
            }
        } else {
            foundSleep = true;
        }

        AddEvent(ZEO_SleepStage, interval.startMs, -interval.stage);
        EndEventList(ZEO_SleepStage, interval.endMs);
    }

    session->settings[ZEO_TimeInWake] = stageTimeMs[1] / 60000L;
    session->settings[ZEO_TimeInREM] = stageTimeMs[2] / 60000L;
    session->settings[ZEO_TimeInLight] = stageTimeMs[3] / 60000L;
    session->settings[ZEO_TimeInDeep] = stageTimeMs[4] / 60000L;
    session->settings[ZEO_Awakenings] = awakenings;

    if (!breathingDisturbances.isEmpty()) {
        const auto latest = std::max_element(
            breathingDisturbances.cbegin(), breathingDisturbances.cend(),
            [](const AppleHealthNightScalar &a, const AppleHealthNightScalar &b) {
                return a.startMs < b.startMs;
            });
        session->settings[AW_BreathingDisturbances] = latest->value;
    }
    if (!wristTemp.isEmpty()) {
        const auto latest = std::max_element(
            wristTemp.cbegin(), wristTemp.cend(),
            [](const AppleHealthNightScalar &a, const AppleHealthNightScalar &b) {
                return a.startMs < b.startMs;
            });
        session->settings[AW_WristTemp] = latest->value;
    }

    return session;
}

Session *AppleHealthLoader::buildOxiSession(
    Machine *mach, const QVector<AppleHealthSample> &heartRate,
    const QVector<AppleHealthSample> &spo2, const QVector<AppleHealthSample> &respRate,
    const QVector<AppleHealthSample> &hrv)
{
    qint64 firstMs = std::numeric_limits<qint64>::max();
    qint64 lastMs = 0;
    const auto updateTimes = [&firstMs, &lastMs](const QVector<AppleHealthSample> &samples) {
        if (!samples.isEmpty()) {
            firstMs = std::min(firstMs, samples.constFirst().timeMs);
            lastMs = std::max(lastMs, samples.constLast().timeMs);
        }
    };
    updateTimes(heartRate);
    updateTimes(spo2);
    updateTimes(respRate);
    updateTimes(hrv);

    const SessionID sessionId = static_cast<SessionID>(firstMs / 1000L);
    if (mach->SessionExists(sessionId)) {
        return nullptr;
    }

    Session *session = new Session(mach, sessionId);
    m_session = session;
    m_importChannels.clear();
    m_importLastValue.clear();
    session->really_set_first(firstMs);
    session->really_set_last(lastMs);

    importSamples(OXI_Pulse, heartRate, 10LL * 60LL * 1000LL);
    importSamples(OXI_SPO2, spo2, 45LL * 60LL * 1000LL);
    importSamples(AW_RespRate, respRate, 30LL * 60LL * 1000LL);
    importSamples(AW_HRV, hrv, 0);

    // Watch samples are far too sparse for OSCAR's desat/pulse-change detection to be
    // meaningful; registering the flag channels empty makes calcSPO2Drop()/calcPulseChange()
    // skip this session.
    if (!spo2.isEmpty()) {
        session->AddEventList(OXI_SPO2Drop, EVL_Event);
    }
    if (!heartRate.isEmpty()) {
        session->AddEventList(OXI_PulseChange, EVL_Event);
    }

    return session;
}

void AppleHealthLoader::importSamples(ChannelID channel,
                                      const QVector<AppleHealthSample> &samples,
                                      qint64 gapThresholdMs)
{
    qint64 previousMs = 0;
    for (const AppleHealthSample &sample : samples) {
        if (previousMs != 0 && gapThresholdMs > 0
            && sample.timeMs - previousMs > gapThresholdMs) {
            EndSampleList(channel);
        }
        AddSample(channel, sample.timeMs, sample.value);
        previousMs = sample.timeMs;
    }
    EndSampleList(channel);
}

void AppleHealthLoader::AddEvent(ChannelID channel, qint64 timeMs, EventDataType value)
{
    EventList *eventList = m_importChannels[channel];
    if (eventList == nullptr) {
        eventList = m_session->AddEventList(channel, EVL_Event, 1, 0, -5, 0);
        Q_ASSERT(eventList);
        m_importChannels[channel] = eventList;
    }
    eventList->AddEvent(timeMs, value);
    m_importLastValue[channel] = value;
}

void AppleHealthLoader::EndEventList(ChannelID channel, qint64 timeMs)
{
    EventList *eventList = m_importChannels[channel];
    if (eventList != nullptr) {
        eventList->AddEvent(timeMs, m_importLastValue[channel]);
        m_importChannels[channel] = nullptr;
    }
}

void AppleHealthLoader::AddSample(ChannelID channel, qint64 timeMs, EventDataType value)
{
    static const EventDataType gain = 0.1F;
    EventList *eventList = m_importChannels[channel];
    if (eventList == nullptr) {
        eventList = m_session->AddEventList(channel, EVL_Event, gain);
        Q_ASSERT(eventList);
        m_importChannels[channel] = eventList;
    }
    eventList->AddEvent(timeMs, qRound(value / gain));
}

void AppleHealthLoader::EndSampleList(ChannelID channel)
{
    if (m_importChannels[channel] != nullptr) {
        m_importChannels[channel] = nullptr;
    }
}

static bool applehealth_initialized = false;

void AppleHealthLoader::Register()
{
    if (applehealth_initialized) { return; }

    qDebug("Registering AppleHealthLoader");
    RegisterLoader(new AppleHealthLoader());
    applehealth_initialized = true;
}
