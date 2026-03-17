#ifndef BMCG3XLOADER_H
#define BMCG3XLOADER_H

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
};

#endif // BMCG3XLOADER_H
