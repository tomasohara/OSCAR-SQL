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
    virtual bool ExportFlowAbnormalityWaveform() const override { return false; }
    // Leak is now sourced from waveform packet offset 0x52A (raw × 0.16 → L/min).
    // The old EVT-based leak source (0x0C) was discarded; see bmcG3xDataParsing.cpp.
    virtual bool ExportLeakRate() const override { return true; }
};

#endif // BMCG3XLOADER_H
