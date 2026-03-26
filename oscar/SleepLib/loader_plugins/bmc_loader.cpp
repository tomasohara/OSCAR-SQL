#include <QCoreApplication>
#include <QString>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QDebug>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <QMutexLocker>
#include <QSet>
#include <cmath>
#include <QMessageBox>
#include <limits>
#include <memory>
#include <algorithm>
#include "SleepLib/loader_plugins/bmcDataParsing.h"
#include "SleepLib/loader_plugins/bmc_loader.h"
#include "../../database/session_repository.h"


ChannelID BMC_MODE, BMC_RESLEX, BMC_HUMIDIFIER, BMC_SMARTA, BMC_SMARTC, BMC_SMARTB,
    BMC_AUTO_ON, BMC_AUTO_OFF, BMC_LEAK_ALERT, BMC_AIRTUBE_TYPE, BMC_MASKTYPE,
    BMC_HEATEDTUBE_LEVEL, BMC_RAMPTIME, BMC_RAMPTIME_AUTO,
    BMC_INITIALP, BMC_TREATP, BMC_MANUALP,
    BMC_MIN_APAP, BMC_MAX_APAP, BMC_SENSITIVITY,
    BMC_INITIAL_EPAP, BMC_EPAP, BMC_IPAP, BMC_ISENS, BMC_ESENS, BMC_RISE_TIME, BMC_TI_MIN, BMC_TI_MAX, BMC_BACKUP_RR,
    BMC_MIN_EPAP, BMC_MIN_IPAP, BMC_MAX_IPAP, BMC_SMART_EPAP, BMC_SMART_MIN_EPAP, BMC_SMART_MIN_IPAP, BMC_SMART_MAX_IPAP;

ChannelID BMC_RESLEX_MODE, BMC_RESLEX_PATIENT;


const QDate baseDate(2010 , 1, 1);
static bool bmc_channels_initialized = false;


/*
  Import Task - This is the primary part of the loader: where sessions are added to OSCAR
    with machine settings, waveforms etc.
  Each day recorded by BMC may have multiple sessions. This method takes in a
  a day and creates a OSCAR session for each session in the recorded day.
*/
void BmcLoaderTask::run()
{
    try
    {
        BmcDateSession bmcDateSession = bmc->ReadDateSession(this->bmcLink->UsrSession.StartTimestamp.date());

        const auto computeSessionId = [this](BmcSession* bmcSession, int index) -> SessionID {
            const qint64 sessionStartSecs = bmcSession->StartTimestamp.toSecsSinceEpoch();
            if (sessionStartSecs > 0 &&
                sessionStartSecs <= static_cast<qint64>(std::numeric_limits<SessionID>::max())) {
                return static_cast<SessionID>(sessionStartSecs);
            }
            return (baseDate.daysTo(this->bmcLink->UsrSession.StartTimestamp.date()) * 64) + index;
        };

        qint64 importStartMs = std::numeric_limits<qint64>::max();
        qint64 importEndMs = std::numeric_limits<qint64>::min();
        QSet<SessionID> plannedSessionIds;
        for (int j = 0; j < bmcDateSession.Sessions.length(); ++j) {
            BmcSession* bmcSession = bmcDateSession.Sessions.at(j);
            if (!bmcSession) {
                continue;
            }
            if (bmcSession->Waveforms.length() == 0 && bmcSession->RespiratoryEvents.length() == 0) {
                continue;
            }

            plannedSessionIds.insert(computeSessionId(bmcSession, j));
            importStartMs = std::min(importStartMs, bmcSession->StartTimestamp.toMSecsSinceEpoch());
            importEndMs = std::max(importEndMs, bmcSession->EndTimestamp.addSecs(-1).toMSecsSinceEpoch());
        }

        if (!plannedSessionIds.isEmpty() && importStartMs <= importEndMs) {
            QMutexLocker locker(&bmcLoader->sessionMutex);
            int removedOverlappingSessions = 0;
            int removedOverlappingDbSessions = 0;
            QSet<SessionID> removedSessionIds;

            if (mach->getDatabaseId() > 0) {
                SessionRepository sessionRepo;
                const QList<SessionData> existingDbSessions = sessionRepo.findByMachine(mach->getDatabaseId());
                for (const SessionData& dbSession : existingDbSessions) {
                    if (dbSession.id <= 0 || dbSession.startTime <= 0 || dbSession.endTime <= 0) {
                        continue;
                    }

                    const SessionID existingSessionId = static_cast<SessionID>(dbSession.sessionId);
                    const bool overlapsImportWindow = (dbSession.startTime <= importEndMs) && (importStartMs <= dbSession.endTime);
                    const bool isPlannedReplacement = plannedSessionIds.contains(existingSessionId);
                    if (!overlapsImportWindow || isPlannedReplacement) {
                        continue;
                    }

                    if (sessionRepo.remove(dbSession.id)) {
                        ++removedOverlappingDbSessions;
                    } else {
                        qWarning() << "BmcLoaderTask::run() - Failed to remove stale DB session row"
                                   << dbSession.id << "session_id" << dbSession.sessionId;
                    }

                    if (Session* existingSession = mach->SessionExists(existingSessionId)) {
                        if (mach->unlinkSession(existingSession)) {
                            ++removedOverlappingSessions;
                        }
                        removedSessionIds.insert(existingSessionId);
                    }
                }
            }

            const QDate cleanupStartDate = QDateTime::fromMSecsSinceEpoch(importStartMs).date().addDays(-1);
            const QDate cleanupEndDate = QDateTime::fromMSecsSinceEpoch(importEndMs).date().addDays(1);

            for (QDate day = cleanupStartDate; day <= cleanupEndDate; day = day.addDays(1)) {
                Day* oscarDay = p_profile->GetDay(day, MT_CPAP);
                if (!oscarDay || !oscarDay->hasMachine(mach)) {
                    continue;
                }

                const QList<Session*> existingSessions = oscarDay->getSessions(MT_CPAP, true);
                for (Session* existingSession : existingSessions) {
                    if (!existingSession || existingSession->machine() != mach) {
                        continue;
                    }
                    if (removedSessionIds.contains(existingSession->session())) {
                        continue;
                    }

                    const qint64 existingStartMs = existingSession->realFirst();
                    const qint64 existingEndMs = existingSession->realLast();
                    if (existingStartMs <= 0 || existingEndMs <= 0) {
                        continue;
                    }

                    const bool overlapsImportWindow = (existingStartMs <= importEndMs) && (importStartMs <= existingEndMs);
                    const bool isPlannedReplacement = plannedSessionIds.contains(existingSession->session());
                    if (overlapsImportWindow && !isPlannedReplacement) {
                        if (mach->unlinkSession(existingSession)) {
                            ++removedOverlappingSessions;
                            removedSessionIds.insert(existingSession->session());
                        }
                    }
                }
            }

            if (removedOverlappingSessions > 0 || removedOverlappingDbSessions > 0) {
                qDebug() << "BmcLoaderTask::run() - Removed overlapping stale sessions:"
                         << removedOverlappingSessions
                         << "db_rows"
                         << removedOverlappingDbSessions
                         << "range"
                         << QDateTime::fromMSecsSinceEpoch(importStartMs).toString(Qt::ISODate)
                         << "to"
                         << QDateTime::fromMSecsSinceEpoch(importEndMs).toString(Qt::ISODate);
            }
        }

        for (int j = 0; j < bmcDateSession.Sessions.length(); j++)
        {
            emit bmcLoader->setProgressValue(this->currentLinkIndex);
            emit bmcLoader->updateMessage(QString("Import day %1 of %2\n%3")
                                              .arg(this->currentLinkIndex+1)
                                              .arg(this->totalLinksToImport)
                                              .arg(this->bmcLink->UsrSession.StartTimestamp.date().toString("yyyy/MM/dd"))
                                          );
            QCoreApplication::processEvents();

            BmcSession* bmcSession = bmcDateSession.Sessions.at(j);
            if (!bmcSession) {
                continue;
            }
            SessionID sessionID = computeSessionId(bmcSession, j);

            if (bmcSession->Waveforms.length() == 0 && bmcSession->RespiratoryEvents.length() == 0)
                continue;

            //Import the session
            Session* session = new Session(mach, sessionID);

            bmcLoader->setSessionMachineSettings(&bmcDateSession, session);
            bmcLoader->setSessionRespiratoryEvents(bmcSession, session);
            bmcLoader->setSessionWaveforms(bmcSession, session);


            const qint64 sessionFirstMs = bmcLoader->findStableStartMs(bmcSession);
            const qint64 sessionLastMs  = bmcSession->EndTimestamp.addSecs(-1).toMSecsSinceEpoch();
            session->really_set_first(sessionFirstMs);
            session->really_set_last(sessionLastMs);

            // Populate a MaskOn slice so that session->hours() returns mask-on time
            // rather than total session length.  findStableEndMs() detects mask removal
            // via a sustained leak spike; if no mask-off is detected it returns the full
            // session end, making the slice cover the whole session (no change in hours).
            const qint64 maskOffMs = bmcLoader->findStableEndMs(bmcSession);
            session->m_slices.append(SessionSlice(sessionFirstMs, maskOffMs, MaskOn));

            session->SetChanged(true);
            session->setNoSettings(false);
            session->UpdateSummaries();

            QMutexLocker locker(&bmcLoader->sessionMutex);

            // We always re-import the final day since a session might still be in progress.
            // If this session id already exists for the day, remove it before adding replacement data.
            if (Session* existingSession = mach->SessionExists(sessionID)) {
                mach->unlinkSession(existingSession);
            }

            auto oscarDay = p_profile->GetDay(bmcSession->StartTimestamp.date(), MT_CPAP);
            if (oscarDay && oscarDay->hasMachine(mach))
            {
                auto oscarSessionToDelete = oscarDay->find(sessionID, MT_CPAP);
                if (oscarSessionToDelete)
                {
                    oscarDay->removeSession(oscarSessionToDelete);
                }
            }

            if (!session->Store(mach->getDataPath())) {
                qWarning() << "BmcLoaderTask::run() - Failed to store session" << sessionID;
            }
            if (!mach->AddSession(session)) {
                qWarning() << "BmcLoaderTask::run() - Failed to add session" << sessionID;
                delete session;
                continue;
            }

            bmcLoader->sessionsLoaded++;
        }
    }
    catch (std::bad_alloc& e)
    {
        qDebug() << "Bad memory allocation while loading a BMC session";
        QMessageBox::warning(nullptr, QObject::tr("Import Error - Out of Memory"),
                             QObject::tr("Additional memory could not be allocated during the import.")+"\n\n"+
                                 QObject::tr("Please try switching to 64-bit OSCAR or setting your preferences to ignore older sessions."),
                             QMessageBox::Ok);
        throw;
    }
    catch (...)
    {
        throw;
    }
}



/*
  Constructor. Tell OSCAR where to get icons for each machine series.
*/

BmcLoader::BmcLoader()
{
    const QString BMC_ICON = ":/icons/bmc.png";

    QString s = newInfo().series;
    m_pixmap_paths[s] = BMC_ICON;
    m_pixmaps[s] = QPixmap(BMC_ICON);
    m_type = MT_CPAP;
}

//Given a created session, we add the BMC machine settings to the OSCAR session
void BmcLoader::setSessionMachineSettings(BmcDateSession* bmcSession, Session* oscarSession)
{
    BmcMachineSettings machineSettings = bmcSession->MacineSettings;

    oscarSession->settings[BMC_MODE] = (int)machineSettings.Mode;

    if (machineSettings.Mode == BmcMode::CPAP)
    {
        oscarSession->settings[CPAP_Mode] = (int)CPAPMode::MODE_CPAP;
        oscarSession->settings[CPAP_Pressure] = machineSettings.CPAP_TreatP;

        oscarSession->settings[BMC_SMARTC] = machineSettings.CPAP_SmartC;
        oscarSession->settings[BMC_INITIALP] = machineSettings.CPAP_InitialP;
        oscarSession->settings[BMC_TREATP] = machineSettings.CPAP_TreatP;
        oscarSession->settings[BMC_MANUALP] = machineSettings.CPAP_ManualP;

    }

    if (machineSettings.Mode == BmcMode::AutoCPAP)
    {
        oscarSession->settings[CPAP_Mode] = (int)CPAPMode::MODE_APAP;
        oscarSession->settings[CPAP_PressureMin] = machineSettings.APAP_MinAPAP;
        oscarSession->settings[CPAP_PressureMax] = machineSettings.APAP_MaxAPAP;

        oscarSession->settings[BMC_SMARTA] = machineSettings.APAP_SmartA ? 1 : 0;
        oscarSession->settings[BMC_INITIALP] = machineSettings.APAP_IntialP;
        oscarSession->settings[BMC_MIN_APAP] = machineSettings.APAP_MinAPAP;
        oscarSession->settings[BMC_MAX_APAP] = machineSettings.APAP_MaxAPAP;
        oscarSession->settings[BMC_SENSITIVITY] = machineSettings.APAP_Sensitivity;

    }

    if (machineSettings.Mode == BmcMode::S)
    {
        oscarSession->settings[CPAP_Mode] = (int)CPAPMode::MODE_BILEVEL_FIXED;
        oscarSession->settings[CPAP_EPAP] = machineSettings.S_EPAP;
        oscarSession->settings[CPAP_IPAP] = machineSettings.S_IPAP;
        oscarSession->settings[CPAP_PS] = 0;

        oscarSession->settings[BMC_INITIAL_EPAP] = machineSettings.S_InitialEPAP;
        oscarSession->settings[BMC_EPAP] = machineSettings.S_EPAP;
        oscarSession->settings[BMC_IPAP] = machineSettings.S_IPAP;
        oscarSession->settings[BMC_ISENS] = machineSettings.S_ISENS;
        oscarSession->settings[BMC_ESENS] = machineSettings.S_ESENS;
        oscarSession->settings[BMC_RISE_TIME] = machineSettings.S_RiseTime;
        oscarSession->settings[BMC_TI_MIN] = machineSettings.S_TiMin;
        oscarSession->settings[BMC_TI_MAX] = machineSettings.S_TiMax;
        oscarSession->settings[BMC_BACKUP_RR] = machineSettings.S_BackupRR ? 1 : 0;
    }

    if (machineSettings.Mode == BmcMode::AutoS)
    {
        oscarSession->settings[CPAP_Mode] = (int)CPAPMode::MODE_BILEVEL_AUTO_FIXED_PS;
        oscarSession->settings[CPAP_EPAPLo] = machineSettings.AutoS_MinEPAP;
        oscarSession->settings[CPAP_IPAPHi] = machineSettings.AutoS_MaxIPAP;
        oscarSession->settings[CPAP_PS] = 0;

        oscarSession->settings[BMC_SMARTB] = machineSettings.AutoS_SmartB ? 1 : 0;
        oscarSession->settings[BMC_INITIAL_EPAP] = machineSettings.AutoS_InitialEPAP;
        oscarSession->settings[BMC_MIN_EPAP] = machineSettings.AutoS_MinEPAP;
        oscarSession->settings[BMC_MIN_IPAP] = machineSettings.AutoS_MinIPAP;
        oscarSession->settings[BMC_MAX_IPAP] = machineSettings.AutoS_MaxIPAP;
        oscarSession->settings[BMC_ISENS] = machineSettings.AutoS_ISENS;
        oscarSession->settings[BMC_ESENS] = machineSettings.AutoS_ESENS;
        oscarSession->settings[BMC_RISE_TIME] = machineSettings.AutoS_RiseTime;
    }

    //Comfort settings common to all machines
    if (machineSettings.RampTimeMinutes == 0xff)
        oscarSession->settings[BMC_RAMPTIME_AUTO] = 0;
    else
        oscarSession->settings[BMC_RAMPTIME] = machineSettings.RampTimeMinutes;

    oscarSession->settings[BMC_RESLEX] = machineSettings.Reslex;
    oscarSession->settings[BMC_RESLEX_PATIENT] = machineSettings.ReslexPatient ? 1 : 0;
    oscarSession->settings[BMC_AUTO_ON] = machineSettings.AutoOn;
    oscarSession->settings[BMC_AUTO_OFF] = machineSettings.AutoOff;
    oscarSession->settings[BMC_HUMIDIFIER] = machineSettings.HumidifierLevel;
    oscarSession->settings[BMC_MASKTYPE] = (int)machineSettings.MaskType;
    oscarSession->settings[BMC_AIRTUBE_TYPE] = (int)machineSettings.AirTubeType;
    oscarSession->settings[BMC_LEAK_ALERT] = machineSettings.LeakAlert;

    if (machineSettings.AirTubeType == BmcAirTubeType::Heated15mm || machineSettings.AirTubeType == BmcAirTubeType::Heated22mm){
        oscarSession->settings[BMC_HEATEDTUBE_LEVEL] = machineSettings.HeatedTubeLevel;
    }

    oscarSession->settings[BMC_RESLEX_MODE] = 0;

}

//Given a created session, we add the BMC respiratory events to the OSCAR session
void BmcLoader::setSessionRespiratoryEvents(BmcSession* bmcSession, Session* oscarSession)
{
    EventList* oscarOsaList  = oscarSession->AddEventList(CPAP_Obstructive, EVL_Event);
    EventList* oscarCsaList  = oscarSession->AddEventList(CPAP_ClearAirway, EVL_Event);
    EventList* oscarHypList  = oscarSession->AddEventList(CPAP_Hypopnea,    EVL_Event);
    EventList* oscarUaList   = oscarSession->AddEventList(CPAP_Apnea,       EVL_Event);
    EventList* oscarPbList   = ExportPeriodicBreathing()
                               ? oscarSession->AddEventList(CPAP_PB, EVL_Event)
                               : nullptr;
    EventList* oscarReraList = oscarSession->AddEventList(CPAP_RERA,        EVL_Event);

    for (auto & bmcEvent : bmcSession->RespiratoryEvents)
    {
        switch (bmcEvent.EventType)
        {
        case BmcRespiratoryEventType::OSA:  oscarOsaList->AddEvent(bmcEvent.EndTime.toMSecsSinceEpoch(),   bmcEvent.DurationSeconds); break;
        case BmcRespiratoryEventType::CSA:  oscarCsaList->AddEvent(bmcEvent.EndTime.toMSecsSinceEpoch(),   bmcEvent.DurationSeconds); break;
        case BmcRespiratoryEventType::HYP:  oscarHypList->AddEvent(bmcEvent.EndTime.toMSecsSinceEpoch(),   bmcEvent.DurationSeconds); break;
        case BmcRespiratoryEventType::UA:   oscarUaList->AddEvent(bmcEvent.EndTime.toMSecsSinceEpoch(),    bmcEvent.DurationSeconds); break;
        case BmcRespiratoryEventType::PB:   if (oscarPbList) oscarPbList->AddEvent(bmcEvent.StartTime.toMSecsSinceEpoch(), bmcEvent.DurationSeconds); break;
        case BmcRespiratoryEventType::RERA: oscarReraList->AddEvent(bmcEvent.EndTime.toMSecsSinceEpoch(),  bmcEvent.DurationSeconds); break;
        default: qDebug() << "Unknown BMC respiratory event type not added to OSCAR";
        }
    }

    if (!bmcSession->FlowLimitEvents.isEmpty()) {
        // EVL_Event preserves actual timestamps (avoids waveform compression).
        // Zero-value samples 100 ms before and after each event create isolated
        // bars so grades don't connect across gaps.
        // setPhysMin/Max anchors the y-axis at 0–3 so mild (grade 1) bars are visible.
        EventList* oscarFlgList = oscarSession->AddEventList(CPAP_FLG, EVL_Event, 1.0, 0.0, 0.0, 0.0, 1000);
        for (const BmcFlowLimitEvent& flEvt : bmcSession->FlowLimitEvents) {
            const qint64 ts = flEvt.Timestamp.toMSecsSinceEpoch();
            oscarFlgList->AddEvent(ts - 100, 0);
            oscarFlgList->AddEvent(ts,       flEvt.Grade);
            oscarFlgList->AddEvent(ts + 100, 0);
        }
        oscarSession->setPhysMin(CPAP_FLG, 0.0);
        oscarSession->setPhysMax(CPAP_FLG, 3.0);
    }
}

//Given a created session, we add the BMC waveforms for the session to the OSCAR session
void BmcLoader::setSessionWaveforms(BmcSession* bmcSession, Session* oscarSession)
{
    const double waveformSampleIntervalMs = WaveformSampleIntervalMs();
    const int waveformSamplesPerPacket = WaveformSamplesPerPacket();
    const qint64 waveformPacketDurationMs = WaveformPacketDurationMs();

    const double pressureGain = PressureChannelGain();
    auto wPressure = oscarSession->AddEventList(CPAP_Pressure, EVL_Event, pressureGain, 0.0, 0.0, 0.0, 1000);
    auto wIPAP = oscarSession->AddEventList(CPAP_IPAP, EVL_Event, pressureGain, 0.0, 0.0, 0.0, 1000);
    auto wEPAP = oscarSession->AddEventList(CPAP_EPAP, EVL_Event, pressureGain, 0.0, 0.0, 0.0, 1000);

    auto wFlow = oscarSession->AddEventList(CPAP_FlowRate, EVL_Waveform, FlowWaveformGain(), 0.0, 0.0, 0.0, waveformSampleIntervalMs);
    EventList* wPressureWave = nullptr;
    EventList* wFlowAbnormality = nullptr;
    if (ExportPressureWaveform()) {
        wPressureWave = oscarSession->AddEventList(BMC_PressureWave,
                                                   EVL_Waveform,
                                                   PressureWaveformGain(),
                                                   0.0, 0.0, 0.0,
                                                   waveformSampleIntervalMs);
    }
    if (ExportFlowAbnormalityWaveform()) {
        wFlowAbnormality = oscarSession->AddEventList(BMC_FlowAbnormality,
                                                      EVL_Waveform,
                                                      FlowAbnormalityWaveformGain(),
                                                      0.0, 0.0, 0.0,
                                                      waveformSampleIntervalMs);
    }
    // Mask pressure waveform. Legacy BMC: sourced from PressureWave[25] at packet offset 0x08,
    // gain 0.1 → cmH2O. G3X: sourced from Raw.MaskPressure, gain 0.01 → cmH2O.
    auto wMaskPressure = oscarSession->AddEventList(CPAP_MaskPressure, EVL_Waveform, PressureWaveformGain(), 0.0, 0.0, 0.0, waveformSampleIntervalMs);

    EventList* wLeak = ExportLeakRate() ? oscarSession->AddEventList(CPAP_Leak, EVL_Event, 0.1, 0.0, 0.0, 0.0, 1000) : nullptr;
    auto wTidalVolume = oscarSession->AddEventList(CPAP_TidalVolume, EVL_Event, 1.0, 0.0, 0.0, 0.0, 1000);
    auto wMinuteVentilation = oscarSession->AddEventList(CPAP_MinuteVent, EVL_Event, 0.1, 0.0, 0.0, 0.0, 1000);
    auto wRespiratoryRate = oscarSession->AddEventList(CPAP_RespRate, EVL_Event, 1.0, 0.0, 0.0, 0.0, 1000);
    EventList* wIEValue = ExportTimingChannels() ? oscarSession->AddEventList(CPAP_IE,       EVL_Event, 0.001, 0.0, 0.0, 0.0, 1000) : nullptr;
    EventList* wIERatio = ExportTimingChannels() ? oscarSession->AddEventList(BMC_IE_Ratio,  EVL_Event, 0.1,   0.0, 0.0, 0.0, 1000) : nullptr;
    auto wSpO2 = oscarSession->AddEventList(OXI_SPO2, EVL_Event, 1.0, 0.0, 0.0, 0.0, 1000);
    auto wPulse = oscarSession->AddEventList(OXI_Pulse, EVL_Event, 1.0, 0.0, 0.0, 0.0, 1000);
    // Always create Ti/Te event lists so calcs.cpp doesn't attempt to derive them
    // from the flow waveform (which fails for BMC due to the non-zero baseline).
    // Events are only populated when ExportTimingChannels() confirms the source
    // data is valid; otherwise the lists remain empty and display nothing.
    EventList* wInspTime = oscarSession->AddEventList(CPAP_Ti, EVL_Event, 0.001, 0.0, 0.0, 0.0, 1000);
    EventList* wExpTime  = oscarSession->AddEventList(CPAP_Te, EVL_Event, 0.001, 0.0, 0.0, 0.0, 1000);

    qint16 rawIpapMin = std::numeric_limits<qint16>::max();
    qint16 rawIpapMax = std::numeric_limits<qint16>::min();
    qint16 rawEpapMin = std::numeric_limits<qint16>::max();
    qint16 rawEpapMax = std::numeric_limits<qint16>::min();
    quint16 rawLeakMin = std::numeric_limits<quint16>::max();
    quint16 rawLeakMax = std::numeric_limits<quint16>::min();
    qint16 rawTvMin = std::numeric_limits<qint16>::max();
    qint16 rawTvMax = std::numeric_limits<qint16>::min();
    qint16 rawMvMin = std::numeric_limits<qint16>::max();
    qint16 rawMvMax = std::numeric_limits<qint16>::min();
    quint16 rawRrMin = std::numeric_limits<quint16>::max();
    quint16 rawRrMax = std::numeric_limits<quint16>::min();
    int pressureEventCount = 0;

    for (auto & bmcWaveform : bmcSession->Waveforms)
    {
        qint64 timestamp = bmcWaveform.Timestamp.toMSecsSinceEpoch();

        wFlow->AddWaveform(timestamp, bmcWaveform.Raw.Flow, waveformSamplesPerPacket, waveformPacketDurationMs);
        if (wPressureWave) {
            wPressureWave->AddWaveform(timestamp, bmcWaveform.Raw.PressureWave, waveformSamplesPerPacket, waveformPacketDurationMs);
        }
        // PressureWave[25] at packet offset 0x08 is confirmed as mask pressure (÷10 = cmH2O).
        // Raw.MaskPressure is populated by the G3X loader; for legacy BMC use Raw.PressureWave.
        wMaskPressure->AddWaveform(timestamp, bmcWaveform.Raw.PressureWave, waveformSamplesPerPacket, waveformPacketDurationMs);
        if (wFlowAbnormality) {
            wFlowAbnormality->AddWaveform(timestamp, bmcWaveform.Raw.FlowAbnormality, waveformSamplesPerPacket, waveformPacketDurationMs);
        }

        if (bmcWaveform.Raw.IPAP > 0) {
            wPressure->AddEvent(timestamp, bmcWaveform.Raw.IPAP);
            wIPAP->AddEvent(timestamp, bmcWaveform.Raw.IPAP);
        }
        if (bmcWaveform.Raw.EPAP > 0)
            wEPAP->AddEvent(timestamp, bmcWaveform.Raw.EPAP);
        rawIpapMin = std::min<qint16>(rawIpapMin, bmcWaveform.Raw.IPAP);
        rawIpapMax = std::max<qint16>(rawIpapMax, bmcWaveform.Raw.IPAP);
        rawEpapMin = std::min<qint16>(rawEpapMin, bmcWaveform.Raw.EPAP);
        rawEpapMax = std::max<qint16>(rawEpapMax, bmcWaveform.Raw.EPAP);
        rawLeakMin = std::min<quint16>(rawLeakMin, bmcWaveform.Raw.Leak);
        rawLeakMax = std::max<quint16>(rawLeakMax, bmcWaveform.Raw.Leak);
        rawTvMin = std::min<qint16>(rawTvMin, bmcWaveform.Raw.TidalVolume);
        rawTvMax = std::max<qint16>(rawTvMax, bmcWaveform.Raw.TidalVolume);
        rawMvMin = std::min<qint16>(rawMvMin, bmcWaveform.Raw.MinuteVentilation);
        rawMvMax = std::max<qint16>(rawMvMax, bmcWaveform.Raw.MinuteVentilation);
        rawRrMin = std::min<quint16>(rawRrMin, bmcWaveform.Raw.RespiratoryRate);
        rawRrMax = std::max<quint16>(rawRrMax, bmcWaveform.Raw.RespiratoryRate);
        ++pressureEventCount;
        if (wLeak && bmcWaveform.Raw.Leak > 0)
            wLeak->AddEvent(timestamp, bmcWaveform.Raw.Leak);
        if (bmcWaveform.Raw.TidalVolume > 0)
            wTidalVolume->AddEvent(timestamp, bmcWaveform.Raw.TidalVolume);
        if (bmcWaveform.Raw.MinuteVentilation > 0)
            wMinuteVentilation->AddEvent(timestamp, bmcWaveform.Raw.MinuteVentilation);
        if (bmcWaveform.Raw.SpO2Pct > 0)
            wSpO2->AddEvent(timestamp, bmcWaveform.Raw.SpO2Pct);
        if (bmcWaveform.Raw.PulseRate > 0)
            wPulse->AddEvent(timestamp, bmcWaveform.Raw.PulseRate);
        if (bmcWaveform.Raw.RespiratoryRate > 0)
            wRespiratoryRate->AddEvent(timestamp, bmcWaveform.Raw.RespiratoryRate);
        if (wIEValue || wIERatio) {
            const int ieMapped = bmcWaveform.Raw.IERatioMapped;
            const bool ieMappedPermille = (ieMapped > 100);
            if (wIEValue) {
                if (ieMapped > 0) {
                    const double ieNumerator = ieMappedPermille ? 1000.0 : 100.0;
                    const qint16 ieValue = static_cast<qint16>(
                        1000.0 * (ieNumerator - ieMapped) / ieMapped);
                    wIEValue->AddEvent(timestamp, ieValue);
                } else {
                    wIEValue->AddEvent(timestamp, 0);
                }
            }
            if (wIERatio) {
                const qint16 ieRatioDisplayValue = static_cast<qint16>(
                    ieMappedPermille ? qRound(static_cast<double>(ieMapped) / 10.0) : ieMapped);
                wIERatio->AddEvent(timestamp, ieRatioDisplayValue);
            }
        }

        /* Compute Ti/Te from I:E ratio and respiratory rate so OSCAR doesn't
         * need to estimate them from flow waveform peaks.  Only populated when
         * ExportTimingChannels() is true and the source data is confirmed valid. */
        if (ExportTimingChannels() && bmcWaveform.RespiratoryRate > 0) {
            double respTime = 60.0 / bmcWaveform.RespiratoryRate;
            double inspTime = respTime * (bmcWaveform.Raw.IERatioMapped / 100.0);
            double expTime  = respTime * ((100 - bmcWaveform.Raw.IERatioMapped) / 100.0);
            qint16 inspMs = static_cast<qint16>(inspTime * 1000);
            qint16 expMs  = static_cast<qint16>(expTime * 1000);
            if (wInspTime && inspMs > 0)
                wInspTime->AddEvent(timestamp, inspMs);
            if (wExpTime && expMs > 0)
                wExpTime->AddEvent(timestamp, expMs);
        }

    }

#ifdef BMCDEBUG
    if (pressureEventCount > 0) {
        qDebug() << "BmcLoader::setSessionWaveforms pressure export"
                 << "events" << pressureEventCount
                 << "raw_ipap_min" << rawIpapMin
                 << "raw_ipap_max" << rawIpapMax
                 << "raw_epap_min" << rawEpapMin
                 << "raw_epap_max" << rawEpapMax
                 << "raw_leak_min_tenths" << rawLeakMin
                 << "raw_leak_max_tenths" << rawLeakMax
                 << "raw_tv_min_ml" << rawTvMin
                 << "raw_tv_max_ml" << rawTvMax
                 << "raw_mv_min_tenths" << rawMvMin
                 << "raw_mv_max_tenths" << rawMvMax
                 << "raw_rr_min_bpm" << rawRrMin
                 << "raw_rr_max_bpm" << rawRrMax;
    }
#endif // BMCDEBUG
}



//****************************************************************************************
//* All below are implementations of virtual methods of MachineLoader and derived CpapLoader
//****************************************************************************************


/*
  Base Class Implementation. Checks if a path contains data that this loader can import
*/
bool BmcLoader::Detect(const QString & givenpath)
{
    QDir dir(givenpath);

    if (!dir.exists()) {
        return false;
    }

    return BmcData::DirectoryHasBmcData(givenpath);
}

/*
  Base Class Implementation. While importing data, the machine's info and icon is displayed
*/
MachineInfo BmcLoader::PeekInfo(const QString & path)
{
    if (!Detect(path)) {
        return MachineInfo();
    }

    auto parser = std::make_unique<BmcData>(path);
    auto bmcMachineInfo = parser->ReadMachineInfo();

    MachineInfo info = newInfo();
    info.type = MachineType::MT_CPAP;
    info.brand = "BMC";
    info.model = bmcMachineInfo.Model;
    info.modelnumber = bmcMachineInfo.Model;
    info.series = "BMC";
    info.serial = bmcMachineInfo.SerialNumber;
    info.version = bmc_version;

    return info;
}

/*
  Base Class Implementation. Create all the settings that will be displayed to the user in "Device Settings"
*/
void BmcLoader::initChannels()
{
    if (bmc_channels_initialized) {
        return;
    }

    using namespace schema;

    const int BMC_CHANNEL_IDX = 0xe930;

    //Mode
    //---------------------------------------------------------------------------
    Channel * chan = new Channel(BMC_MODE = BMC_CHANNEL_IDX + 0, SETTING, MT_CPAP, SESSION,
                                "BMC_Mode", QObject::tr("BMC Mode"), QObject::tr("BMC Mode"), QObject::tr("BMC Mode"), "", LOOKUP, Qt::green);
    channel.add(GRP_CPAP, chan);
    chan->addOption(0, QObject::tr("CPAP"));
    chan->addOption(1, QObject::tr("AutoCPAP"));
    chan->addOption(2, QObject::tr("S"));
    chan->addOption(3, QObject::tr("S/T"));
    chan->addOption(4, QObject::tr("T"));
    chan->addOption(5, QObject::tr("Titration"));
    chan->addOption(6, QObject::tr("AutoS"));
    chan->addOption(7, QObject::tr("Unknown"));


    channel.add(GRP_CPAP, chan = new Channel(BMC_RESLEX = BMC_CHANNEL_IDX + 1, SETTING, MT_CPAP,   SESSION,
                                             "Reslex", QObject::tr("Reslex"), QObject::tr("BMC Reslex is an exhalation pressure relief feature"), QObject::tr("Reslex"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, QObject::tr("1"));
    chan->addOption(2, QObject::tr("2"));
    chan->addOption(3, QObject::tr("3"));
    chan->addOption(4, QObject::tr("Patient"));


    channel.add(GRP_CPAP, chan = new Channel(BMC_RESLEX_MODE = BMC_CHANNEL_IDX + 2, SETTING, MT_CPAP,   SESSION,
                                             "ReslexMode", QObject::tr("Reslex Mode"), QObject::tr("Reslex Mode"), QObject::tr("Reslex Mode"), "", LOOKUP, Qt::green));
    chan->addOption(0, "Full Time");

    channel.add(GRP_CPAP, chan = new Channel(BMC_HUMIDIFIER = BMC_CHANNEL_IDX + 3, SETTING, MT_CPAP,   SESSION,
                                             "Humidifier", QObject::tr("Humidifier"), QObject::tr("Humidifier"), QObject::tr("Humidifier"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, QObject::tr("1"));
    chan->addOption(2, QObject::tr("2"));
    chan->addOption(3, QObject::tr("3"));
    chan->addOption(4, QObject::tr("4"));
    chan->addOption(5, QObject::tr("5"));
    chan->addOption(6, STR_TR_Auto);

    channel.add(GRP_CPAP, chan = new Channel(BMC_SMARTA = BMC_CHANNEL_IDX + 4, SETTING, MT_CPAP,   SESSION,
                                             "SmartA", QObject::tr("SmartA"), QObject::tr("SmartA"), QObject::tr("SmartA"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_SMARTB = BMC_CHANNEL_IDX + 5, SETTING, MT_CPAP,   SESSION,
                                             "SmartB", QObject::tr("SmartB"), QObject::tr("SmartB"), QObject::tr("SmartB"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_SMARTC = BMC_CHANNEL_IDX + 6, SETTING, MT_CPAP,   SESSION,
                                             "SmartC", QObject::tr("SmartC"), QObject::tr("SmartC"), QObject::tr("SmartC"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_AUTO_ON = BMC_CHANNEL_IDX + 7, SETTING, MT_CPAP,   SESSION,
                                             "AutoOn", QObject::tr("Auto On"), QObject::tr("Auto On"), QObject::tr("Auto On"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_AUTO_OFF = BMC_CHANNEL_IDX + 8, SETTING, MT_CPAP,   SESSION,
                                             "AutoOff", QObject::tr("Auto Off"), QObject::tr("Auto Off"), QObject::tr("Auto Off"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_LEAK_ALERT = BMC_CHANNEL_IDX + 9, SETTING, MT_CPAP,   SESSION,
                                             "LeakAlert", QObject::tr("Leak Alert"), QObject::tr("Leak Alert"), QObject::tr("Leak Alert"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_AIRTUBE_TYPE = BMC_CHANNEL_IDX + 10, SETTING, MT_CPAP,   SESSION,
                                             "AirTubeType", QObject::tr("Air Tube Type"), QObject::tr("Air Tube Type"), QObject::tr("Air Tube Type"), "", LOOKUP, Qt::green));
    chan->addOption(0, QObject::tr("Normal 22mm"));
    chan->addOption(1, QObject::tr("Normal 15mm"));
    chan->addOption(2, QObject::tr("Heated 22mm"));
    chan->addOption(3, QObject::tr("Heated 22mm"));

    channel.add(GRP_CPAP, chan = new Channel(BMC_MASKTYPE = BMC_CHANNEL_IDX + 11, SETTING, MT_CPAP,   SESSION,
                                             "MaskType", QObject::tr("Mask"), QObject::tr("Mask"), QObject::tr("Mask"), "", LOOKUP, Qt::green));
    chan->addOption(0, QObject::tr("Full Face"));
    chan->addOption(1, QObject::tr("Nasal"));
    chan->addOption(2, QObject::tr("Nasal Pillows"));
    chan->addOption(3, QObject::tr("Unknown"));


    channel.add(GRP_CPAP, chan = new Channel(BMC_HEATEDTUBE_LEVEL = BMC_CHANNEL_IDX + 12, SETTING, MT_CPAP,   SESSION,
                                             "HeatedTubeLevel", QObject::tr("Heated Tube Level"), QObject::tr("Heated Tube Level"), QObject::tr("Heated Tube Level"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, QObject::tr("1"));
    chan->addOption(2, QObject::tr("2"));
    chan->addOption(3, QObject::tr("3"));
    chan->addOption(4, QObject::tr("4"));
    chan->addOption(5, QObject::tr("5"));
    chan->addOption(6, STR_TR_Auto);


    channel.add(GRP_CPAP, chan = new Channel(BMC_RAMPTIME = BMC_CHANNEL_IDX + 13, SETTING, MT_CPAP,   SESSION,
                                             "BmcRampTime", QObject::tr("BmcRampTime"), QObject::tr("Ramp Time "), QObject::tr("Ramp Time "), STR_UNIT_Minutes, INTEGER, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_RAMPTIME_AUTO = BMC_CHANNEL_IDX + 14, SETTING, MT_CPAP,   SESSION,
                                             "BmcRampAuto", QObject::tr("BmcRampAuto"), QObject::tr("Ramp Time "), QObject::tr("Ramp Time "), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Auto);


    channel.add(GRP_CPAP, chan = new Channel(BMC_INITIALP = BMC_CHANNEL_IDX + 15, SETTING, MT_CPAP,   SESSION,
                                             "InitialP", QObject::tr("InitialP"), QObject::tr("Initial P"), QObject::tr("Initial P"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_TREATP = BMC_CHANNEL_IDX + 16, SETTING, MT_CPAP,   SESSION,
                                             "TreatP", QObject::tr("TreatP"), QObject::tr("Treat P"), QObject::tr("Treat P"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_MANUALP = BMC_CHANNEL_IDX + 17, SETTING, MT_CPAP,   SESSION,
                                             "ManualP", QObject::tr("ManualP"), QObject::tr("Manual P"), QObject::tr("Manual P"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_MIN_APAP = BMC_CHANNEL_IDX + 18, SETTING, MT_CPAP,   SESSION,
                                             "MinAPAP", QObject::tr("Min APAP"), QObject::tr("Min APAP"), QObject::tr("Min APAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_MAX_APAP = BMC_CHANNEL_IDX + 19, SETTING, MT_CPAP,   SESSION,
                                             "MaxAPAP", QObject::tr("Max APAP"), QObject::tr("Max APAP"), QObject::tr("Max APAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_SENSITIVITY = BMC_CHANNEL_IDX + 20, SETTING, MT_CPAP,   SESSION,
                                             "Sensitivity", QObject::tr("Sensitivity"), QObject::tr("Sensitivity"), QObject::tr("Sensitivity"), "", INTEGER, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_INITIAL_EPAP = BMC_CHANNEL_IDX + 21, SETTING, MT_CPAP,   SESSION,
                                             "InitialEPAP", QObject::tr("Initial EPAP"), QObject::tr("Initial EPAP"), QObject::tr("Initial EPAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_EPAP = BMC_CHANNEL_IDX + 22, SETTING, MT_CPAP,   SESSION,
                                             "BmcEPAP", QObject::tr("EPAP"), QObject::tr("EPAP"), QObject::tr("EPAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_IPAP = BMC_CHANNEL_IDX + 23, SETTING, MT_CPAP,   SESSION,
                                             "BmcIPAP", QObject::tr("IPAP"), QObject::tr("IPAP"), QObject::tr("IPAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_ISENS = BMC_CHANNEL_IDX + 24, SETTING, MT_CPAP,   SESSION,
                                             "ISens", QObject::tr("ISens"), QObject::tr("I Sens"), QObject::tr("I Sens"), "", INTEGER, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_ESENS = BMC_CHANNEL_IDX + 25, SETTING, MT_CPAP,   SESSION,
                                             "ESens", QObject::tr("ESens"), QObject::tr("E Sens"), QObject::tr("E Sens"), "", INTEGER, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_RISE_TIME = BMC_CHANNEL_IDX + 26, SETTING, MT_CPAP,   SESSION,
                                             "RiseTime", QObject::tr("RiseTime"), QObject::tr("Rise Time"), QObject::tr("Rise Time"), STR_UNIT_Seconds, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_TI_MIN = BMC_CHANNEL_IDX + 27, SETTING, MT_CPAP,   SESSION,
                                             "TiMin", QObject::tr("TiMin"), QObject::tr("Ti Min"), QObject::tr("Ti Min"), STR_UNIT_Seconds, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_TI_MAX = BMC_CHANNEL_IDX + 28, SETTING, MT_CPAP,   SESSION,
                                             "TiMax", QObject::tr("TiMax"), QObject::tr("Ti Max"), QObject::tr("Ti Max"), STR_UNIT_Seconds, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_BACKUP_RR = BMC_CHANNEL_IDX + 29, SETTING, MT_CPAP,   SESSION,
                                             "BackupRR", QObject::tr("BackupRR"), QObject::tr("Backup RR"), QObject::tr("Backup RR"), "", LOOKUP, Qt::green));
    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(BMC_MIN_EPAP = BMC_CHANNEL_IDX + 30, SETTING, MT_CPAP,   SESSION,
                                             "MinEPAP", QObject::tr("MinEPAP"), QObject::tr("Min EPAP"), QObject::tr("Min EPAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_MIN_IPAP = BMC_CHANNEL_IDX + 31, SETTING, MT_CPAP,   SESSION,
                                             "MinIPAP", QObject::tr("MinIPAP"), QObject::tr("Min IPAP"), QObject::tr("Min IPAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(BMC_MAX_IPAP = BMC_CHANNEL_IDX + 32, SETTING, MT_CPAP,   SESSION,
                                             "BmcMaxIPAP", QObject::tr("MaxIPAP"), QObject::tr("Max IPAP"), QObject::tr("Max IPAP"), STR_UNIT_CMH2O, DOUBLE, Qt::green));


    channel.add(GRP_CPAP, chan = new Channel(BMC_RESLEX_PATIENT = BMC_CHANNEL_IDX + 33, SETTING, MT_CPAP,   SESSION,
                                             "ReslexAvailability", QObject::tr("Reslex Availability"), QObject::tr("Reslex setting can be restricted to only clinician menu or may be made available for the user to change"), QObject::tr("Reslex Availability"), "", LOOKUP, Qt::green));
    chan->addOption(0, QObject::tr("Clinician"));
    chan->addOption(1, QObject::tr("Patient"));

    bmc_channels_initialized = true;
}

/*
  Base Class Implementation. Returns various channels and names for mapping in OSCAR.
*/

QString BmcLoader::PresReliefLabel() { return QString("Reslex"); }
ChannelID BmcLoader::PresReliefMode() { return BMC_RESLEX_MODE; }
ChannelID BmcLoader::PresReliefLevel() { return BMC_RESLEX; }
ChannelID BmcLoader::CPAPModeChannel() { return BMC_MODE; }

/*
  Base Class Implementation. "Open" is called to import all data.
  The loader must create the machine, add new sessions with settings, waveforms etc to the profile
  Perform a backup of the SD card if enabled
  The methodology was changed to allow for multithreaded importing so the importing of sessions
  is now added as tasks which are all run by OSCAR's machine class instance.
*/
int BmcLoader::Open(const QString & dirpath)
{
    this->sessionsLoaded = 0;


    //#region Open the BMC data files and ready out
    //******************************************************************************
    QCoreApplication::processEvents();
    emit updateMessage(QObject::tr("Reading data..."));
    QCoreApplication::processEvents();

    const auto machine_info = PeekInfo(dirpath);
    std::unique_ptr<BmcDataParser> parser = std::make_unique<BmcData>(dirpath);
    parser->ReadData();

    //******************************************************************************
    //#endregion



    //#region Find or create the OSCAR machine and determine the date to import from
    //******************************************************************************

    QCoreApplication::processEvents();
    emit updateMessage(QObject::tr("Find sessions to import..."));
    QCoreApplication::processEvents();

    QDate firstImportDay = QDate(2000,1,1);

    Machine *mach = p_profile->lookupMachine(machine_info.serial, machine_info.loadername);
    if ( mach ) {       // we have seen this device
        qDebug() << "We have imported data for this machine before";
        mach->setInfo( machine_info );                      // update info
        QDate lastDate = mach->LastDay();           // use the last day for this device
        firstImportDay = lastDate;                  // re-import the last day, to  pick up partial days
        QDate purgeDate = mach->purgeDate();
        if (purgeDate.isValid()) {
            firstImportDay = min(firstImportDay, purgeDate);
        }
    } else {            // Starting from new beginnings - new or purged
        qDebug() << "We haven't imported data for this machine before";
        mach = p_profile->CreateMachine( machine_info );
    }
    QDateTime ignoreBefore = p_profile->session->ignoreOlderSessionsDate();
    bool ignoreOldSessions = p_profile->session->ignoreOlderSessions();

    if (ignoreOldSessions && (ignoreBefore.date() > firstImportDay))
        firstImportDay = ignoreBefore.date();
    qDebug() << "First day to import: " << firstImportDay.toString();

    // Session::Store() requires a machine database id. Ensure the machine row exists
    // before import tasks start storing session/event data.
    if (mach->getDatabaseId() == 0) {
        qDebug() << "BmcLoader::Open: Saving machine to database before runTasks()";
        if (!mach->SaveToDatabase()) {
            qWarning() << "BmcLoader::Open: Failed to save machine to database - session/event tables may stay empty";
        } else {
            qDebug() << "BmcLoader::Open: Machine saved to database with ID" << mach->getDatabaseId();
        }
    }
    //******************************************************************************
    //#endregion


    //#region Create backup
    //******************************************************************************

    QString backupPath = mach->getBackupPath();
    if (QDir::cleanPath(dirpath) == QDir::cleanPath(backupPath)) {
        qDebug() << "BmcLoader::Open: input is the backup directory, skipping backup creation";
    } else {
        emit updateMessage(QObject::tr("Creating data backup..."));
        QCoreApplication::processEvents();

        QDir backupDir(backupPath);
        if (backupDir.exists(backupPath))
            backupDir.removeRecursively();

        backupDir.mkpath(backupPath);

        copyPath(dirpath, backupPath);
    }

    //******************************************************************************
    //#endregion


    //#region Determine the set of sessions to import and set the progress bar values
    //******************************************************************************

    QList<BmcDataLink> linksToImport;
    const QList<BmcDataLink>& parserLinks = parser->GetSessionLinks();
    for (auto & link : parserLinks)
    {
        if (link.UsrSession.StartTimestamp.date() >= firstImportDay){
            linksToImport.append(link);
        }
    }

    emit updateMessage(QObject::tr("Starting import..."));
    emit setProgressMax(linksToImport.length());    // add one to include Save in progress.
    emit setProgressValue(0);
    QCoreApplication::processEvents();

    //32-bit users import an entire SD card of data may get a bad memory allocation exception
    //while OSCAR tries to store the session.
    /*if (linksToImport.length() > 10 && QSysInfo::WordSize == 32){
        QMessageBox::warning(nullptr, QObject::tr("Import Warning"),
                             QObject::tr("You are about to import a large number of sessions and may run out of memory.")+"\n\n"+
                             QObject::tr("Please try switching to 64-bit OSCAR or setting your preferences to ignore older sessions."),
                             QMessageBox::Ok);
    }*/


    //******************************************************************************
    //#endregion

    // For each BMC session, create and queue an import task that will transform
    // our BMC session into an OSCAR session.
    for (int i = 0; i < linksToImport.length(); i++){

        BmcDataLink *link;
        link = (BmcDataLink*)&linksToImport.at(i);

        queTask(new BmcLoaderTask(this, mach, parser.get(), link, linksToImport.length(), i));
    }


    runTasks();

    //When everything is imported, we can finish up and return the number of new sessions imported.

    mach->Save();

    QCoreApplication::processEvents();

    return this->sessionsLoaded;
}

/*
  Base Class Implementation. Register this loader with OSCAR
*/

bool bmc_initialized = false;
void BmcLoader::Register()
{
    if (bmc_initialized) { return; }

    qDebug() << "Registering BMC Loader";
    RegisterLoader(new BmcLoader());

    bmc_initialized = true;
}

