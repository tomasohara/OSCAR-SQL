#ifndef BMCG3XLOADER_H
#define BMCG3XLOADER_H

#include <QDebug>
#include "SleepLib/loader_plugins/bmc_loader.h"

const int bmcg3x_version = 1;
const QString bmcg3x_class_name = "BMCG3X";

class BmcG3xLoader : public BmcLoader
{
public:
    BmcG3xLoader();

    virtual bool Detect(const QString& path) override;
    virtual MachineInfo PeekInfo(const QString& path) override;
    virtual int Open(const QString& path) override;
    virtual void initChannels() override;

    static void Register();

    virtual const QString& loaderName() override { return bmcg3x_class_name; }
    virtual int Version() override { return bmcg3x_version; }
    virtual MachineInfo newInfo() override {
        return MachineInfo(MT_CPAP, 0, bmcg3x_class_name, QObject::tr("BMC"), QString(),
                           QString(), QString(), QObject::tr("BMC"), QDateTime::currentDateTime(), bmcg3x_version);
    }

    // G3X pressure-wave bytes appear to be in finer units than legacy BMC packets.
    // Start with a conservative scale to put the plotted range near clinical values.
    virtual double PressureWaveformGain() const override { return 0.01; }
    virtual double WaveformSampleIntervalMs() const override { return 20.0; }
    virtual int WaveformSamplesPerPacket() const override { return 50; }
    virtual qint64 WaveformPacketDurationMs() const override { return 1000; }
    virtual bool ExportPressureWaveform() const override { return false; }
    virtual bool ExportFlowAbnormalityWaveform() const override { return false; }

    /// Advances the session start past machine startup noise to the first
    /// therapeutically meaningful moment.
    ///
    /// Phase 1 — skip idle: the machine idles at minimum APAP pressure before
    ///   the patient puts on the mask; scan forward until PressureTrend rises
    ///   above the initial value.
    /// Phase 2 — skip ramp: once rising, scan until PressureTrend stops
    ///   increasing, i.e., the ramp peak has been reached.
    ///
    /// Falls back to StartTimestamp if the pressure never rises (short/idle session).
    virtual qint64 findStableStartMs(BmcSession* bmcSession) const override {
        const auto & waveforms = bmcSession->Waveforms;
        if (waveforms.size() < 2) {
            return bmcSession->StartTimestamp.toMSecsSinceEpoch();
        }

        // Phase 1: skip packets where pressure is at or below the initial idle value.
        const quint16 idlePt = waveforms[0].Raw.PressureTrend;
        int rampStart = 0;
        for (int i = 1; i < waveforms.size(); ++i) {
            if (waveforms[i].Raw.PressureTrend > idlePt) {
                rampStart = i;
                break;
            }
        }
        if (rampStart == 0) {
            // Pressure never rose — session is entirely at idle, no adjustment needed.
            return bmcSession->StartTimestamp.toMSecsSinceEpoch();
        }

        // Phase 2: from rampStart, find first packet where pressure stops rising.
        for (int i = rampStart; i + 1 < waveforms.size(); ++i) {
            quint16 pt0 = waveforms[i].Raw.PressureTrend;
            quint16 pt1 = waveforms[i + 1].Raw.PressureTrend;
            if (pt0 >= pt1) {
                return waveforms[i].Timestamp.toMSecsSinceEpoch();
            }
        }

        // Ramp never completed within this session.
        return bmcSession->StartTimestamp.toMSecsSinceEpoch();
    }
    // Leak is now sourced from waveform packet offset 0x52A (raw × 0.16 → L/min).
    // The old EVT-based leak source (0x0C) was discarded; see bmcG3xDataParsing.cpp.
    virtual bool ExportLeakRate() const override { return true; }

    /// Detects the mask-off point by finding the last waveform packet with
    /// meaningful respiratory flow, followed by a sustained period of near-zero
    /// flow at the end of the session.
    ///
    /// After mask removal, the patient's respiratory flow through the mask drops
    /// to the instrument noise floor (±2–18 raw units at offset 0x56E).  The
    /// machine may continue running for many minutes (e.g. until the user presses
    /// the stop button), so the leak profile is not a reliable indicator.
    ///
    /// Detection rule:
    ///   1. Scan backward to find the last packet where any flow sample exceeds
    ///      kG3xMaskOffFlowNoise (i.e. has real respiratory signal).
    ///   2. If the trailing inactive period (from that packet to the last packet)
    ///      is at least kG3xMaskOffSustainSec seconds, declare mask-off at that
    ///      packet's timestamp.
    ///   3. Otherwise fall back to the full session end (mask on the whole time).
    virtual qint64 findStableEndMs(BmcSession* bmcSession) const override {
        /// Raw flow threshold above the noise floor (noise = ±2–18, breathing >> 25).
        static constexpr qint16 kG3xMaskOffFlowNoise  = 25;
        /// Minimum seconds of no-flow at session end to declare mask removal.
        static constexpr int    kG3xMaskOffSustainSec = 300;

        const auto& waveforms = bmcSession->Waveforms;
        const int n = waveforms.size();
        if (n < 2) {
            return bmcSession->EndTimestamp.toMSecsSinceEpoch();
        }

        // Scan backward: find the last packet with any flow sample above the noise floor.
        int lastActivePacket = -1;
        for (int i = n - 1; i >= 0; --i) {
            const qint16* flow = waveforms[i].Raw.Flow;
            for (int s = 0; s < kBmcExtendedWaveformSamples; ++s) {
                if (flow[s] > kG3xMaskOffFlowNoise || flow[s] < -kG3xMaskOffFlowNoise) {
                    lastActivePacket = i;
                    break;
                }
            }
            if (lastActivePacket >= 0) break;
        }

        if (lastActivePacket < 0) {
            // Entire session has near-zero flow — no useful mask-on period.
            return bmcSession->EndTimestamp.toMSecsSinceEpoch();
        }

        const int inactiveSec = static_cast<int>(
            waveforms[lastActivePacket].Timestamp.secsTo(waveforms[n - 1].Timestamp));

        if (inactiveSec < kG3xMaskOffSustainSec) {
            // Inactive period too short — could be an apnea at session end.
            return bmcSession->EndTimestamp.toMSecsSinceEpoch();
        }

        qDebug() << "BmcG3xLoader: mask-off detected at"
                 << waveforms[lastActivePacket].Timestamp.toString(Qt::ISODate)
                 << "inactiveSec" << inactiveSec;
        return waveforms[lastActivePacket].Timestamp.toMSecsSinceEpoch();
    }
};

#endif // BMCG3XLOADER_H
