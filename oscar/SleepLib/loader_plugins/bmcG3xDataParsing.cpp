#include "SleepLib/loader_plugins/bmcG3xDataParsing.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QIODevice>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
constexpr int kG3xIdxRecordOffset = 0x800;
constexpr int kG3xIdxRecordSize = 0x800;
constexpr int kG3xEvtRecordSize = 0x20;
constexpr quint32 kG3xWaveformPacketSize = 0x800;
constexpr int kLegacyWaveformPacketSize = 0x100;
constexpr qint64 kG3xWaveformFileSpan = 64 * 1024 * 1024; // 64 MiB
constexpr int kG3xFlowRegionOffset = 0x576;
constexpr int kG3xFlowRegionSampleCount = 96;
constexpr qint16 kG3xFlowRawClamp = 2000;
constexpr int kG3xWaveformOutputSampleCount = kBmcExtendedWaveformSamples;
constexpr int kG3xPressureWaveRegionOffset = 0x24A;
constexpr int kG3xPressureWaveRegionSampleCount = 96;
constexpr qint16 kG3xPressureWaveRawClamp = 4000;
constexpr int kG3xFlowAbnormalityRegionOffset = 0x510;
constexpr int kG3xFlowAbnormalityRegionSampleCount = 47;
constexpr qint16 kG3xFlowAbnormalityRawClamp = 4000;

struct G3xSampleValues
{
    bool hasEPAP = false;
    bool hasIPAP = false;
    bool hasLeak = false;
    bool hasTidalVolume = false;
    bool hasMinuteVentilation = false;
    bool hasRespiratoryRate = false;

    int epapHundredths = 0;
    int ipapHundredths = 0;
    int leakTenths = 0;
    int tidalVolume = 0;
    int minuteVentilationTenths = 0;
    int respiratoryRate = 0;
};

struct G3xTimedSampleUpdate
{
    qint64 TimestampSec = 0;
    G3xSampleValues Values;
};

struct G3xTimedLeakUpdate
{
    qint64 TimestampSec = 0;
    int LeakTenths = 0;
};

struct G3xRawRespEvent
{
    int MessageType = 0;
    int Value1 = 0;
    QDateTime Timestamp;
};

struct G3xDiagRow
{
    qint64 TimestampSec = 0;
    quint64 VirtualOffset = 0;

    int Raw0x0A = 0;
    int Raw0x74 = 0;
    int Raw0x76 = 0;
    int Raw0x7C = 0;
    int Raw0x7E = 0;

    int CurrentIPAPHundredths = -1;
    int CurrentEPAPHundredths = -1;
    int CurrentLeakTenths = -1;
    int CurrentRespiratoryRate = -1;
    int CurrentTidalVolume = -1;
    int CurrentMinuteVentilationTenths = -1;

    int OutputRawIPAPHalfCm = 0;
    int OutputRawEPAPHalfCm = 0;

    int FlowMin = 0;
    int FlowMax = 0;
    int PressureWaveMin = 0;
    int PressureWaveMax = 0;
    int FlowAbnormalityMin = 0;
    int FlowAbnormalityMax = 0;
};

bool IsG3xDiagnosticsEnabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool ok = false;
    const int val = qEnvironmentVariableIntValue("OSCAR_BMC_G3X_DIAG", &ok);
    if (ok) {
        enabled = (val != 0) ? 1 : 0;
    } else {
        enabled = qEnvironmentVariableIsSet("OSCAR_BMC_G3X_DIAG") ? 1 : 0;
    }
    return enabled != 0;
}

bool IsReasonablePressureHundredths(int pressureHundredths)
{
    return pressureHundredths >= 400 && pressureHundredths <= 3500;
}

double G3xLeakScaleTenthsPerRawUnit()
{
    // G3X EVT(0x42) leak field does not use legacy BMC tenths units directly.
    // Empirical default maps typical raw ranges into plausible L/min.
    static bool initialized = false;
    static double scale = 0.16;
    if (!initialized) {
        bool ok = false;
        const QByteArray envValue = qgetenv("OSCAR_BMC_G3X_LEAK_SCALE");
        const double envScale = envValue.toDouble(&ok);
        if (ok && envScale > 0.0 && envScale < 10.0) {
            scale = envScale;
        }
        initialized = true;
    }
    return scale;
}

int ConvertG3xLeakRawToTenths(int rawLeak)
{
    const double scaledTenths = static_cast<double>(qMax(0, rawLeak)) * G3xLeakScaleTenthsPerRawUnit();
    const int leakTenths = qRound(scaledTenths);
    return qBound(0, leakTenths, 5000);
}

double G3xWaveLeakSlopeTenthsPerRawUnit()
{
    static bool initialized = false;
    static double slope = 0.091;
    if (!initialized) {
        bool ok = false;
        const QByteArray envValue = qgetenv("OSCAR_BMC_G3X_WAVE_LEAK_SLOPE");
        const double envSlope = envValue.toDouble(&ok);
        if (ok && envSlope > 0.0 && envSlope < 10.0) {
            slope = envSlope;
        }
        initialized = true;
    }
    return slope;
}

double G3xWaveLeakInterceptTenths()
{
    static bool initialized = false;
    static double intercept = 38.0;
    if (!initialized) {
        bool ok = false;
        const QByteArray envValue = qgetenv("OSCAR_BMC_G3X_WAVE_LEAK_INTERCEPT");
        const double envIntercept = envValue.toDouble(&ok);
        if (ok && envIntercept > -5000.0 && envIntercept < 5000.0) {
            intercept = envIntercept;
        }
        initialized = true;
    }
    return intercept;
}

int ConvertG3xWaveLeakRawToTenths(int rawLeak)
{
    if (rawLeak <= 0 || rawLeak >= 60000) {
        return -1;
    }
    const double scaledTenths = (static_cast<double>(rawLeak) * G3xWaveLeakSlopeTenthsPerRawUnit()) +
                                G3xWaveLeakInterceptTenths();
    return qBound(0, qRound(scaledTenths), 5000);
}

int G3xLeakSpikeThresholdTenths()
{
    static bool initialized = false;
    static int thresholdTenths = 45; // 4.5 L/min isolated jump
    if (!initialized) {
        bool ok = false;
        const int envThreshold = qEnvironmentVariableIntValue("OSCAR_BMC_G3X_LEAK_SPIKE_TENTHS", &ok);
        if (ok && envThreshold >= 10 && envThreshold <= 500) {
            thresholdTenths = envThreshold;
        }
        initialized = true;
    }
    return thresholdTenths;
}

void SuppressIsolatedLeakSpikes(QVector<G3xTimedLeakUpdate>* leakUpdates)
{
    if (!leakUpdates || leakUpdates->size() < 3) {
        return;
    }

    const int spikeThreshold = G3xLeakSpikeThresholdTenths();
    const int neighborTolerance = spikeThreshold / 2;

    QVector<G3xTimedLeakUpdate>& updates = *leakUpdates;
    for (int i = 1; i + 1 < updates.size(); ++i) {
        const int prev = updates.at(i - 1).LeakTenths;
        const int curr = updates.at(i).LeakTenths;
        const int next = updates.at(i + 1).LeakTenths;

        // Only suppress isolated one-sample excursions where neighbors agree.
        if (qAbs(curr - prev) >= spikeThreshold &&
            qAbs(curr - next) >= spikeThreshold &&
            qAbs(prev - next) <= neighborTolerance) {
            updates[i].LeakTenths = qBound(0, (prev + next) / 2, 5000);
        }
    }
}

bool IsG3xLeakLinearInterpolationEnabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool ok = false;
    const int val = qEnvironmentVariableIntValue("OSCAR_BMC_G3X_LEAK_LINEAR_INTERP", &ok);
    if (ok) {
        enabled = (val != 0) ? 1 : 0;
    } else {
        enabled = 0; // default to raw-cadence step-hold
    }
    return enabled != 0;
}

int G3xFastLeakFreshnessSeconds()
{
    static bool initialized = false;
    static int seconds = 20;
    if (!initialized) {
        bool ok = false;
        const int envSeconds = qEnvironmentVariableIntValue("OSCAR_BMC_G3X_LEAK_FAST_FRESH_SEC", &ok);
        if (ok && envSeconds >= 1 && envSeconds <= 300) {
            seconds = envSeconds;
        }
        initialized = true;
    }
    return seconds;
}

bool ParseEnvBool(const char* name, bool* hasValueOut)
{
    if (hasValueOut) {
        *hasValueOut = false;
    }

    bool ok = false;
    const int val = qEnvironmentVariableIntValue(name, &ok);
    if (ok) {
        if (hasValueOut) {
            *hasValueOut = true;
        }
        return val != 0;
    }

    if (qEnvironmentVariableIsSet(name)) {
        if (hasValueOut) {
            *hasValueOut = true;
        }
        return true;
    }

    return false;
}

bool IsG3xExperimentalTimingFromHeaderEnabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool hasValue = false;
    const bool dedicated = ParseEnvBool("OSCAR_BMC_G3X_EXPERIMENTAL_TIMING_747E", &hasValue);
    if (hasValue) {
        enabled = dedicated ? 1 : 0;
        return enabled != 0;
    }

    // Experimental decode must be explicitly enabled.
    enabled = 0;
    return false;
}

bool IsG3xPressureWaveSigned10Enabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool hasValue = false;
    const bool dedicated = ParseEnvBool("OSCAR_BMC_G3X_PRESSURE_WAVE_SIGN10", &hasValue);
    if (hasValue) {
        enabled = dedicated ? 1 : 0;
        return enabled != 0;
    }

    const bool legacy = ParseEnvBool("OSCAR_BMC_G3X_AUX_WAVE_SIGN10", &hasValue);
    if (hasValue) {
        enabled = legacy ? 1 : 0;
        return enabled != 0;
    }

    enabled = 0; // default off: pressure wave appears signed-16 on current G3X samples.
    return false;
}

bool IsG3xFlowAbnormalitySigned10Enabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool hasValue = false;
    const bool dedicated = ParseEnvBool("OSCAR_BMC_G3X_FLOW_ABN_SIGN10", &hasValue);
    if (hasValue) {
        enabled = dedicated ? 1 : 0;
        return enabled != 0;
    }

    const bool legacy = ParseEnvBool("OSCAR_BMC_G3X_AUX_WAVE_SIGN10", &hasValue);
    if (hasValue) {
        enabled = legacy ? 1 : 0;
        return enabled != 0;
    }

    enabled = 1; // default on: flow-abnormality has 10-bit wrap pattern on G3X.
    return true;
}

bool IsG3xFlowAbnormalityFrom0510Enabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool hasValue = false;
    const bool dedicated = ParseEnvBool("OSCAR_BMC_G3X_FLOW_ABN_0510", &hasValue);
    if (hasValue) {
        enabled = dedicated ? 1 : 0;
        return enabled != 0;
    }

    enabled = 1; // default on for G3X: 0x510 behaves like flow-limitation candidate.
    return true;
}

bool IsG3xFlowAbnormalityPositiveOnlyEnabled()
{
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    bool hasValue = false;
    const bool dedicated = ParseEnvBool("OSCAR_BMC_G3X_FLOW_ABN_POSITIVE_ONLY", &hasValue);
    if (hasValue) {
        enabled = dedicated ? 1 : 0;
        return enabled != 0;
    }

    enabled = 1; // default on: hide baseline and below-baseline component.
    return true;
}

double G3xWaveLeakBlendWeight()
{
    static bool initialized = false;
    static double weight = 0.0; // off by default until waveform leak field is validated
    if (!initialized) {
        bool ok = false;
        const QByteArray envValue = qgetenv("OSCAR_BMC_G3X_WAVE_LEAK_BLEND");
        const double envWeight = envValue.toDouble(&ok);
        if (ok && envWeight >= 0.0 && envWeight <= 1.0) {
            weight = envWeight;
        }
        initialized = true;
    }
    return weight;
}

QString BuildG3xDiagnosticsPath(const QString& serial, const QDate& day)
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::tempPath();
    }

    QDir dir(baseDir);
    if (!dir.exists("g3x-diagnostics")) {
        dir.mkpath("g3x-diagnostics");
    }

    const QString safeSerial = serial.isEmpty() ? QString("unknown") : serial;
    const QString filename = QString("%1_%2_%3.csv")
                                 .arg(safeSerial)
                                 .arg(day.toString("yyyyMMdd"))
                                 .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    return dir.absoluteFilePath(QString("g3x-diagnostics/%1").arg(filename));
}

void ComputePacketMinMax(const qint16* values, int count, int* minOut, int* maxOut)
{
    if (!values || count <= 0 || !minOut || !maxOut) {
        return;
    }

    int mn = values[0];
    int mx = values[0];
    for (int i = 1; i < count; ++i) {
        mn = std::min<int>(mn, values[i]);
        mx = std::max<int>(mx, values[i]);
    }

    *minOut = mn;
    *maxOut = mx;
}

bool WriteG3xDiagnosticsCsv(const QString& outputPath,
                            const QVector<G3xDiagRow>& rows,
                            const QDate& day,
                            quint32 waveStartOffset,
                            quint32 waveEndOffset,
                            quint32 eventStartOffset,
                            quint32 eventEndOffset)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << "# day=" << day.toString(Qt::ISODate)
        << ", wave_start=" << waveStartOffset
        << ", wave_end=" << waveEndOffset
        << ", event_start=" << eventStartOffset
        << ", event_end=" << eventEndOffset
        << ", rows=" << rows.size()
        << "\n";

    out << "timestamp_iso,timestamp_sec,virtual_offset,raw_0x0A,raw_0x74,raw_0x76,raw_0x7C,raw_0x7E,"
           "current_ipap_hundredths,current_epap_hundredths,current_leak_tenths,current_rr,current_tv,current_mv_tenths,"
           "output_raw_ipap_halfcm,output_raw_epap_halfcm,flow_min,flow_max,pressurewave_min,pressurewave_max,flowabn_min,flowabn_max\n";

    for (const G3xDiagRow& row : rows) {
        out << QDateTime::fromSecsSinceEpoch(row.TimestampSec).toString(Qt::ISODate) << ","
            << row.TimestampSec << ","
            << row.VirtualOffset << ","
            << row.Raw0x0A << ","
            << row.Raw0x74 << ","
            << row.Raw0x76 << ","
            << row.Raw0x7C << ","
            << row.Raw0x7E << ","
            << row.CurrentIPAPHundredths << ","
            << row.CurrentEPAPHundredths << ","
            << row.CurrentLeakTenths << ","
            << row.CurrentRespiratoryRate << ","
            << row.CurrentTidalVolume << ","
            << row.CurrentMinuteVentilationTenths << ","
            << row.OutputRawIPAPHalfCm << ","
            << row.OutputRawEPAPHalfCm << ","
            << row.FlowMin << ","
            << row.FlowMax << ","
            << row.PressureWaveMin << ","
            << row.PressureWaveMax << ","
            << row.FlowAbnormalityMin << ","
            << row.FlowAbnormalityMax
            << "\n";
    }

    file.close();
    return true;
}

bool DecodeG3xTimestamp(const char* p, int baseOffset, QDateTime* out)
{
    const int year = 1900 + static_cast<unsigned char>(p[baseOffset + 0]);
    const int month = static_cast<unsigned char>(p[baseOffset + 1]);
    const int day = static_cast<unsigned char>(p[baseOffset + 2]);
    const int hour = static_cast<unsigned char>(p[baseOffset + 3]);
    const int minute = static_cast<unsigned char>(p[baseOffset + 4]);
    const int second = static_cast<unsigned char>(p[baseOffset + 5]);

    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid()) {
        return false;
    }

    *out = QDateTime(date, time);
    return true;
}

void SetInt16LE(char* p, int offset, qint16 value)
{
    p[offset] = static_cast<char>(value & 0xff);
    p[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void SetUInt16LE(char* p, int offset, quint16 value)
{
    p[offset] = static_cast<char>(value & 0xff);
    p[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

qint16 PressureHundredthsToRawHalfCm(int pressureHundredths)
{
    const int halfCm = qBound(0, (pressureHundredths + 25) / 50, 100);
    return static_cast<qint16>(halfCm);
}

BmcWaveformPacket BuildLegacyCompatiblePacket(const QDateTime& timestamp, const G3xSampleValues& values)
{
    char buffer[kLegacyWaveformPacketSize] = {0};
    SetUInt16LE(buffer, 0x00, 0xAAAD);

    if (values.hasIPAP) {
        SetInt16LE(buffer, 0x04, PressureHundredthsToRawHalfCm(values.ipapHundredths));
    }
    if (values.hasEPAP) {
        SetInt16LE(buffer, 0x06, PressureHundredthsToRawHalfCm(values.epapHundredths));
    }

    if (values.hasLeak) {
        SetInt16LE(buffer, 0xC4, static_cast<qint16>(qBound(0, values.leakTenths, 5000)));
    }
    if (values.hasTidalVolume) {
        SetInt16LE(buffer, 0xC6, static_cast<qint16>(qBound(0, values.tidalVolume, 5000)));
    }
    if (values.hasMinuteVentilation) {
        SetInt16LE(buffer, 0xCA, static_cast<qint16>(qBound(0, values.minuteVentilationTenths, 2000)));
    }
    if (values.hasRespiratoryRate) {
        SetInt16LE(buffer, 0xD0, static_cast<qint16>(qBound(0, values.respiratoryRate, 80)));
    }

    SetInt16LE(buffer, 0xD2, 20); // Reasonable default to derive I:E.

    SetUInt16LE(buffer, 0xF8, static_cast<quint16>(timestamp.date().year()));
    buffer[0xFA] = static_cast<char>(timestamp.date().month());
    buffer[0xFB] = static_cast<char>(timestamp.date().day());
    buffer[0xFC] = static_cast<char>(timestamp.time().hour());
    buffer[0xFD] = static_cast<char>(timestamp.time().minute());
    buffer[0xFE] = static_cast<char>(timestamp.time().second());

    return BmcWaveformPacket(buffer);
}

qint32 ReadUInt16LEPtr(const char* p, int offset)
{
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p + offset);
    return static_cast<quint16>(b[0] | (b[1] << 8));
}

qint16 ReadInt16LEPtr(const char* p, int offset)
{
    return static_cast<qint16>(ReadUInt16LEPtr(p, offset));
}

void MergeSampleValues(const G3xSampleValues& update, G3xSampleValues* current)
{
    if (!current) {
        return;
    }

    if (update.hasEPAP) {
        current->hasEPAP = true;
        current->epapHundredths = update.epapHundredths;
    }
    if (update.hasIPAP) {
        current->hasIPAP = true;
        current->ipapHundredths = update.ipapHundredths;
    }
    if (update.hasLeak) {
        current->hasLeak = true;
        current->leakTenths = update.leakTenths;
    }
    if (update.hasTidalVolume) {
        current->hasTidalVolume = true;
        current->tidalVolume = update.tidalVolume;
    }
    if (update.hasMinuteVentilation) {
        current->hasMinuteVentilation = true;
        current->minuteVentilationTenths = update.minuteVentilationTenths;
    }
    if (update.hasRespiratoryRate) {
        current->hasRespiratoryRate = true;
        current->respiratoryRate = update.respiratoryRate;
    }
}

int ReadPacketPressureHundredths(const QByteArray& waveformPacket)
{
    if (waveformPacket.size() < 12) {
        return -1;
    }

    const int pressureHundredths = ReadUInt16LEPtr(waveformPacket.constData(), 0x0A);
    if (!IsReasonablePressureHundredths(pressureHundredths)) {
        return -1;
    }

    return pressureHundredths;
}

qint16 DecodeSigned10FromUInt16(quint16 rawValue)
{
    int value = static_cast<int>(rawValue & 0x03FF);
    if ((value & 0x0200) != 0) {
        value -= 0x0400;
    }
    return static_cast<qint16>(value);
}

int ReadMedianInt16LE(const char* packetData, int offset, int sampleCount)
{
    if (!packetData || sampleCount <= 0) {
        return 0;
    }

    QVector<int> values;
    values.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        values.append(ReadInt16LEPtr(packetData, offset + (i * 2)));
    }

    auto mid = values.begin() + (values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

void ApplyFlowWaveformFromG3xPacket(const QByteArray& waveformPacket, BmcWaveformPacket* legacyPacket)
{
    if (!legacyPacket ||
        waveformPacket.size() < (kG3xFlowRegionOffset + (kG3xFlowRegionSampleCount * 2)) ||
        waveformPacket.size() < (kG3xPressureWaveRegionOffset + (kG3xPressureWaveRegionSampleCount * 2)) ||
        waveformPacket.size() < (kG3xFlowAbnormalityRegionOffset + (kG3xFlowAbnormalityRegionSampleCount * 2))) {
        return;
    }

    const char* packetData = waveformPacket.constData();
    const bool useSigned10PressureWave = IsG3xPressureWaveSigned10Enabled();
    const bool useSigned10FlowAbnormality = IsG3xFlowAbnormalitySigned10Enabled();
    const bool use0510FlowAbnormality = IsG3xFlowAbnormalityFrom0510Enabled();
    const bool flowAbnormalityPositiveOnly = IsG3xFlowAbnormalityPositiveOnlyEnabled();
    const int flowAbnormalityBaseline = use0510FlowAbnormality
                                            ? ReadMedianInt16LE(packetData,
                                                                kG3xFlowAbnormalityRegionOffset,
                                                                kG3xFlowAbnormalityRegionSampleCount)
                                            : 0;
    for (int i = 0; i < kG3xWaveformOutputSampleCount; ++i) {
        const int srcIndexFlow = (i * (kG3xFlowRegionSampleCount - 1)) / (kG3xWaveformOutputSampleCount - 1);
        const qint16 rawFlow = qBound<qint16>(
            -kG3xFlowRawClamp,
            ReadInt16LEPtr(packetData, kG3xFlowRegionOffset + (srcIndexFlow * 2)),
            kG3xFlowRawClamp);
        legacyPacket->Raw.Flow[i] = rawFlow;
        legacyPacket->Flow[i] = rawFlow / 10.0f;

        const int srcIndexPressure = (i * (kG3xPressureWaveRegionSampleCount - 1)) / (kG3xWaveformOutputSampleCount - 1);
        const quint16 rawPressureWord = static_cast<quint16>(
            ReadUInt16LEPtr(packetData, kG3xPressureWaveRegionOffset + (srcIndexPressure * 2)));
        const qint16 decodedPressureWave = useSigned10PressureWave
                                               ? DecodeSigned10FromUInt16(rawPressureWord)
                                               : static_cast<qint16>(rawPressureWord);
        const qint16 rawPressureWave = qBound<qint16>(
            -kG3xPressureWaveRawClamp,
            decodedPressureWave,
            kG3xPressureWaveRawClamp);
        legacyPacket->Raw.PressureWave[i] = rawPressureWave;
        legacyPacket->PressureWave[i] = rawPressureWave;

        const int srcIndexAbnormality = (i * (kG3xFlowAbnormalityRegionSampleCount - 1)) / (kG3xWaveformOutputSampleCount - 1);
        qint16 decodedFlowAbnormality = 0;
        if (use0510FlowAbnormality) {
            const int raw = ReadInt16LEPtr(packetData, kG3xFlowAbnormalityRegionOffset + (srcIndexAbnormality * 2));
            decodedFlowAbnormality = static_cast<qint16>(raw - flowAbnormalityBaseline);
        } else {
            const quint16 rawAbnormalityWord = static_cast<quint16>(
                ReadUInt16LEPtr(packetData, kG3xFlowAbnormalityRegionOffset + (srcIndexAbnormality * 2)));
            decodedFlowAbnormality = useSigned10FlowAbnormality
                                         ? DecodeSigned10FromUInt16(rawAbnormalityWord)
                                         : static_cast<qint16>(rawAbnormalityWord);
        }
        if (flowAbnormalityPositiveOnly && decodedFlowAbnormality < 0) {
            decodedFlowAbnormality = 0;
        }
        const qint16 rawFlowAbnormality = qBound<qint16>(
            flowAbnormalityPositiveOnly ? 0 : -kG3xFlowAbnormalityRawClamp,
            decodedFlowAbnormality,
            kG3xFlowAbnormalityRawClamp);
        legacyPacket->Raw.FlowAbnormality[i] = rawFlowAbnormality;
        legacyPacket->FlowAbnormality[i] = static_cast<quint16>(qMax<qint16>(0, rawFlowAbnormality));
    }
}
}

BmcG3xData::BmcG3xData() { }

BmcG3xData::BmcG3xData(const QString& path)
    : BmcG3xData()
{
    QString tmpPath(path);
    if (!tmpPath.endsWith(QDir::separator())) {
        tmpPath.append(QDir::separator());
    }

    dirPath = tmpPath;
    ResolveIdxFile();
}

bool BmcG3xData::DirectoryHasBmcG3xData(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return false;
    }

    const QFileInfoList idxCandidates = dir.entryInfoList(QStringList() << "*.idx", QDir::Files);
    for (const QFileInfo& idxInfo : idxCandidates) {
        QFile idxFile(idxInfo.absoluteFilePath());
        if (!idxFile.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QByteArray header = idxFile.read(32);
        idxFile.close();
        if (!IsG3xIdxHeader(header)) {
            continue;
        }

        const QString basePath = idxInfo.absolutePath() + QDir::separator() + idxInfo.completeBaseName();
        if (QFile::exists(basePath + ".000")) {
            return true;
        }
    }

    return false;
}

void BmcG3xData::ReadData()
{
    if (!ResolveIdxFile()) {
        throw std::invalid_argument("BmcG3xData: Missing G3X index file");
    }

    QFile idxFile(idxFilePath);
    if (!idxFile.open(QIODevice::ReadOnly)) {
        throw std::invalid_argument("BmcG3xData: Could not open G3X index file");
    }

    const QByteArray idxBytes = idxFile.readAll();
    idxFile.close();

    ParseMachineInfo(idxBytes);
    ParseIdxRecords(idxBytes);
}

BmcMachineInfo BmcG3xData::ReadMachineInfo()
{
    if (!machineInfo.SerialNumber.isEmpty() || !machineInfo.Model.isEmpty()) {
        return machineInfo;
    }

    if (!ResolveIdxFile()) {
        return machineInfo;
    }

    QFile idxFile(idxFilePath);
    if (!idxFile.open(QIODevice::ReadOnly)) {
        return machineInfo;
    }

    const QByteArray idxBytes = idxFile.readAll();
    idxFile.close();

    ParseMachineInfo(idxBytes);
    return machineInfo;
}

BmcDateSession BmcG3xData::ReadDateSession(QDate aDate)
{
    const G3xDayEntry* dayEntry = nullptr;
    for (const G3xDayEntry& entry : dayEntries) {
        if (entry.Date == aDate) {
            dayEntry = &entry;
            break;
        }
    }

    // Backward-compatible fallback: if no explicit day match exists, try the
    // waveform timestamp day.
    if (!dayEntry) {
        for (const G3xDayEntry& entry : dayEntries) {
            if (entry.StartTimestamp.date() == aDate) {
                dayEntry = &entry;
                break;
            }
        }
    }

    if (!dayEntry) {
        throw std::invalid_argument("No G3X session with that date could be found");
    }

    BmcDateSession dateSession;
    dateSession.StartTime = dayEntry->StartTimestamp;
    dateSession.DurationMinutes = std::max(1, static_cast<int>(dayEntry->StartTimestamp.secsTo(dayEntry->EndTimestamp) / 60));
    dateSession.MachineInfo = ReadMachineInfo();
    std::memset(&dateSession.MacineSettings, 0, sizeof(BmcMachineSettings));
    dateSession.MacineSettings.Mode = BmcMode::CPAP;

    QList<G3xTimedSampleUpdate> timedSampleUpdates;
    QVector<G3xTimedLeakUpdate> fastLeakUpdates;
    QVector<G3xTimedLeakUpdate> fallbackLeakUpdates;
    bool hasEvtPressureUpdates = false;
    int evtPressureUpdateCount = 0;
    int evt0cLeakUpdateCount = 0;
    const bool experimentalTiming747E = IsG3xExperimentalTimingFromHeaderEnabled();
    QVector<G3xRawRespEvent> rawRespEvents;
    int rawRespType02Count = 0;
    int rawRespType03Count = 0;
    int rawRespType04Count = 0;
    int rawRespType07Count = 0;
    int rawRespType08Count = 0;
    int rawRespType09Count = 0;
    int rawRespType0ACount = 0;
    int rawUnknownType0BCount = 0;
    int rawUnknownType0ECount = 0;
    int rawUnknownType0FCount = 0;
    QVector<G3xRawRespEvent> rawResp09Examples;

    const QString evtFilePath = fileBasePath + ".evt";
    QFile evtFile(evtFilePath);
    if (evtFile.open(QIODevice::ReadOnly)) {
        const qint64 safeStart = std::min<qint64>(dayEntry->EventStartOffset, evtFile.size());
        const qint64 safeEnd = std::min<qint64>(dayEntry->EventEndOffset, evtFile.size());
        if (safeEnd > safeStart) {
            evtFile.seek(safeStart);
            const QByteArray evtBytes = evtFile.read(static_cast<qint64>(safeEnd - safeStart));

            for (int offset = 0; offset + kG3xEvtRecordSize <= evtBytes.size(); offset += kG3xEvtRecordSize) {
                const char* rec = evtBytes.constData() + offset;
                if (static_cast<unsigned char>(rec[0]) != 0xAE ||
                    static_cast<unsigned char>(rec[1]) != 0xAA) {
                    continue;
                }

                QDateTime evtTime;
                if (!DecodeG3xTimestamp(rec, 0x14, &evtTime)) {
                    continue;
                }
                const int messageType = static_cast<unsigned char>(rec[0x10]);
                const int value1 = ReadUInt16LEPtr(rec, 0x1A);
                const int value2 = ReadUInt16LEPtr(rec, 0x1C);

                G3xSampleValues sample;
                switch (messageType) {
                case 0x0C:
                    // High-rate leak candidate for G3X: value2 in 0x0C tracks
                    // leak behavior better than sparse 0x42 records.
                    fastLeakUpdates.append(G3xTimedLeakUpdate{
                        evtTime.toSecsSinceEpoch(),
                        ConvertG3xLeakRawToTenths(value2)
                    });
                    ++evt0cLeakUpdateCount;
                    break;
                case 0x0D:
                    // Keep 0x0D ignored for leak until validated.
                    break;
                case 0x42:
                    if (IsReasonablePressureHundredths(value2)) {
                        // In G3X EVT, message 0x42 appears to carry therapy pressure
                        // in hundredths of cmH2O.
                        sample.hasIPAP = true;
                        sample.ipapHundredths = value2;
                        sample.hasEPAP = true;
                        sample.epapHundredths = value2;
                        fallbackLeakUpdates.append(G3xTimedLeakUpdate{
                            evtTime.toSecsSinceEpoch(),
                            ConvertG3xLeakRawToTenths(value1)
                        });
                        hasEvtPressureUpdates = true;
                        ++evtPressureUpdateCount;
                        timedSampleUpdates.append(G3xTimedSampleUpdate{evtTime.toSecsSinceEpoch(), sample});
                    }
                    break;
                case 0x0E:
                    // Previously treated as tidal-volume candidate (experimental).
                    // Current evidence suggests this is not TV; keep unknown for now.
                    ++rawUnknownType0ECount;
                    break;
                case 0x0F:
                    // Previously treated as minute-ventilation candidate (experimental).
                    // Current evidence suggests this is not MV; keep unknown for now.
                    ++rawUnknownType0FCount;
                    break;
                case 0x0B:
                    // 0x0B is not respiratory rate on current G3X samples.
                    // Keep as unknown until semantics are validated.
                    ++rawUnknownType0BCount;
                    break;
                case 0x02:
                case 0x07:
                case 0x08:
                case 0x09:
                case 0x03:
                case 0x04:
                case 0x0A:
                    switch (messageType) {
                    case 0x02: ++rawRespType02Count; break;
                    case 0x03: ++rawRespType03Count; break;
                    case 0x04: ++rawRespType04Count; break;
                    case 0x07: ++rawRespType07Count; break;
                    case 0x08: ++rawRespType08Count; break;
                    case 0x09:
                        ++rawRespType09Count;
                        if (rawResp09Examples.size() < 8) {
                            rawResp09Examples.append(G3xRawRespEvent{messageType, value1, evtTime});
                        }
                        break;
                    case 0x0A: ++rawRespType0ACount; break;
                    default: break;
                    }
                    rawRespEvents.append(G3xRawRespEvent{messageType, value1, evtTime});
                    break;
                default:
                    break;
                }
            }
        }
        evtFile.close();
    }

    if (!rawRespEvents.isEmpty()) {
        // G3X respiratory coding currently mapped as:
        // 0x02=UA, 0x03=OSA, 0x04=CSA, 0x07/0x08/0x09=hypopnea subtypes.
        int mappedOsaCount = 0;
        int mappedCsaCount = 0;
        int mappedHypCount = 0;
        int mappedUaCount = 0;
        int ignoredRespCount = 0;

        for (const G3xRawRespEvent& rawEvt : rawRespEvents) {
            BmcRespiratoryEventType mappedType = BmcRespiratoryEventType::Unknown;
            bool hasMappedType = true;

            switch (rawEvt.MessageType) {
            case 0x02: mappedType = BmcRespiratoryEventType::UA; break;
            case 0x03: mappedType = BmcRespiratoryEventType::OSA; break;
            case 0x04: mappedType = BmcRespiratoryEventType::CSA; break;
            case 0x07:
            case 0x08:
            case 0x09: mappedType = BmcRespiratoryEventType::HYP; break;
            default: hasMappedType = false; break;
            }

            if (!hasMappedType) {
                ++ignoredRespCount;
                continue;
            }

            BmcRespiratoryEvent evt;
            evt.EventType = mappedType;
            evt.StartTime = rawEvt.Timestamp;
            evt.DurationSeconds = qBound(10, rawEvt.Value1, 180);
            evt.EndTime = evt.StartTime.addSecs(evt.DurationSeconds);
            dateSession.RespiratoryEvents.append(evt);

            switch (mappedType) {
            case BmcRespiratoryEventType::OSA: ++mappedOsaCount; break;
            case BmcRespiratoryEventType::CSA: ++mappedCsaCount; break;
            case BmcRespiratoryEventType::HYP: ++mappedHypCount; break;
            case BmcRespiratoryEventType::UA: ++mappedUaCount; break;
            default: break;
            }
        }

        qDebug() << "BmcG3xData respiratory summary day" << aDate.toString(Qt::ISODate)
                 << "mapping" << "fixed(02 UA,03 OSA,04 CSA,07/08/09 HYP)"
                 << "raw0B_unknown" << rawUnknownType0BCount
                 << "raw0E_unknown" << rawUnknownType0ECount
                 << "raw0F_unknown" << rawUnknownType0FCount
                 << "raw02" << rawRespType02Count
                 << "raw03" << rawRespType03Count
                 << "raw04" << rawRespType04Count
                 << "raw07" << rawRespType07Count
                 << "raw08" << rawRespType08Count
                 << "raw09" << rawRespType09Count
                 << "raw0A" << rawRespType0ACount
                 << "mappedUA" << mappedUaCount
                 << "mappedOSA" << mappedOsaCount
                 << "mappedCSA" << mappedCsaCount
                 << "mappedHYP" << mappedHypCount
                 << "ignored" << ignoredRespCount;

        if (rawRespType09Count > 0) {
            qDebug() << "BmcG3xData respiratory 0x09 diagnostics day" << aDate.toString(Qt::ISODate)
                     << "count" << rawRespType09Count
                     << "sample_count" << rawResp09Examples.size();
            for (int i = 0; i < rawResp09Examples.size(); ++i) {
                const G3xRawRespEvent& sample = rawResp09Examples.at(i);
                qDebug() << "BmcG3xData respiratory 0x09 sample"
                         << (i + 1)
                         << "ts" << sample.Timestamp.toString(Qt::ISODate)
                         << "value1" << sample.Value1;
            }
        }
    }

    if (!hasEvtPressureUpdates) {
        QFile evtFallbackFile(evtFilePath);
        if (evtFallbackFile.open(QIODevice::ReadOnly)) {
            const QByteArray evtBytes = evtFallbackFile.readAll();
            evtFallbackFile.close();

            // Be generous with time windows because EVT timestamps may be in a
            // different timezone or shifted across day boundaries.
            const qint64 startWindowSec = dayEntry->StartTimestamp.toSecsSinceEpoch() - (12 * 60 * 60);
            const qint64 endWindowSec = dayEntry->EndTimestamp.toSecsSinceEpoch() + (12 * 60 * 60);
            const QDate dateWindowStart = aDate.addDays(-1);
            const QDate dateWindowEnd = aDate.addDays(1);
            int fallbackPressureUpdates = 0;
            int scanned42 = 0;
            int inTimeWindow42 = 0;
            int inDateWindow42 = 0;

            for (int offset = 0; offset + kG3xEvtRecordSize <= evtBytes.size(); offset += kG3xEvtRecordSize) {
                const char* rec = evtBytes.constData() + offset;
                if (static_cast<unsigned char>(rec[0]) != 0xAE ||
                    static_cast<unsigned char>(rec[1]) != 0xAA) {
                    continue;
                }

                if (static_cast<unsigned char>(rec[0x10]) != 0x42) {
                    continue;
                }
                ++scanned42;

                QDateTime evtTime;
                if (!DecodeG3xTimestamp(rec, 0x14, &evtTime)) {
                    continue;
                }

                const qint64 evtSec = evtTime.toSecsSinceEpoch();
                const bool inTimeWindow = (evtSec >= startWindowSec && evtSec <= endWindowSec);
                const bool inDateWindow = (evtTime.date() >= dateWindowStart && evtTime.date() <= dateWindowEnd);
                if (inTimeWindow) {
                    ++inTimeWindow42;
                }
                if (inDateWindow) {
                    ++inDateWindow42;
                }

                // Prefer precise time range, but also allow date-nearby records if
                // absolute timestamps differ by timezone encoding.
                if (!inTimeWindow && !inDateWindow) {
                    continue;
                }

                const int value1 = ReadUInt16LEPtr(rec, 0x1A);
                const int value2 = ReadUInt16LEPtr(rec, 0x1C);
                if (!IsReasonablePressureHundredths(value2)) {
                    continue;
                }

                G3xSampleValues sample;
                sample.hasIPAP = true;
                sample.ipapHundredths = value2;
                sample.hasEPAP = true;
                sample.epapHundredths = value2;
                fallbackLeakUpdates.append(G3xTimedLeakUpdate{
                    evtSec,
                    ConvertG3xLeakRawToTenths(value1)
                });
                timedSampleUpdates.append(G3xTimedSampleUpdate{evtSec, sample});
                hasEvtPressureUpdates = true;
                ++evtPressureUpdateCount;
                ++fallbackPressureUpdates;
            }

            qDebug() << "BmcG3xData: EVT pressure fallback scan day" << aDate.toString(Qt::ISODate)
                     << "scanned42" << scanned42
                     << "timeWindow42" << inTimeWindow42
                     << "dateWindow42" << inDateWindow42
                     << "recovered" << fallbackPressureUpdates;
        }
    }

    std::sort(timedSampleUpdates.begin(), timedSampleUpdates.end(),
              [](const G3xTimedSampleUpdate& a, const G3xTimedSampleUpdate& b) {
                  return a.TimestampSec < b.TimestampSec;
              });

    const auto normalizeLeakUpdates = [](QVector<G3xTimedLeakUpdate>* updates) {
        if (!updates || updates->isEmpty()) {
            return;
        }

        std::sort(updates->begin(), updates->end(),
                  [](const G3xTimedLeakUpdate& a, const G3xTimedLeakUpdate& b) {
                      return a.TimestampSec < b.TimestampSec;
                  });

        QVector<G3xTimedLeakUpdate> deduped;
        deduped.reserve(updates->size());
        for (const G3xTimedLeakUpdate& update : *updates) {
            const int leakTenths = qBound(0, update.LeakTenths, 5000);
            if (!deduped.isEmpty() && deduped.last().TimestampSec == update.TimestampSec) {
                deduped.last().LeakTenths = leakTenths;
            } else {
                deduped.append(G3xTimedLeakUpdate{update.TimestampSec, leakTenths});
            }
        }

        *updates = deduped;
        SuppressIsolatedLeakSpikes(updates);
    };
    normalizeLeakUpdates(&fastLeakUpdates);
    normalizeLeakUpdates(&fallbackLeakUpdates);

    const bool diagnosticsEnabled = IsG3xDiagnosticsEnabled();
    QVector<G3xDiagRow> diagRows;
    if (diagnosticsEnabled) {
        const quint32 packetCountEstimate = std::max<quint32>(1, dayEntry->WaveLength / kG3xWaveformPacketSize);
        diagRows.reserve(static_cast<int>(packetCountEstimate));
    }

    int idxPressureSeedHundredths = 0;
    if (IsReasonablePressureHundredths(dayEntry->ItPressureEPAPHundredths)) {
        idxPressureSeedHundredths = dayEntry->ItPressureEPAPHundredths;
    } else if (IsReasonablePressureHundredths(dayEntry->ItPressureMinHundredths)) {
        idxPressureSeedHundredths = dayEntry->ItPressureMinHundredths;
    } else if (IsReasonablePressureHundredths(dayEntry->TsPressureMinHundredths)) {
        idxPressureSeedHundredths = dayEntry->TsPressureMinHundredths;
    }

    G3xSampleValues currentValues;
    bool seededPressureFromEvt = false;
    for (const G3xTimedSampleUpdate& update : timedSampleUpdates) {
        if (update.Values.hasIPAP) {
            currentValues.hasIPAP = true;
            currentValues.ipapHundredths = update.Values.ipapHundredths;
            seededPressureFromEvt = true;
        }
        if (update.Values.hasEPAP) {
            currentValues.hasEPAP = true;
            currentValues.epapHundredths = update.Values.epapHundredths;
            seededPressureFromEvt = true;
        }
        if (seededPressureFromEvt) {
            if (!currentValues.hasIPAP && currentValues.hasEPAP) {
                currentValues.hasIPAP = true;
                currentValues.ipapHundredths = currentValues.epapHundredths;
            }
            if (!currentValues.hasEPAP && currentValues.hasIPAP) {
                currentValues.hasEPAP = true;
                currentValues.epapHundredths = currentValues.ipapHundredths;
            }
            break;
        }
    }
    if (!seededPressureFromEvt && idxPressureSeedHundredths > 0) {
        currentValues.hasIPAP = true;
        currentValues.ipapHundredths = idxPressureSeedHundredths;
        currentValues.hasEPAP = true;
        currentValues.epapHundredths = idxPressureSeedHundredths;
    } else if (!seededPressureFromEvt) {
        currentValues.hasIPAP = true;
        currentValues.ipapHundredths = 400;
        currentValues.hasEPAP = true;
        currentValues.epapHundredths = 400;
    }

    float firstPressureCmH2O = -1.0f;
    qint16 waveformRawIpapMin = std::numeric_limits<qint16>::max();
    qint16 waveformRawIpapMax = std::numeric_limits<qint16>::min();
    int waveformLeakTenthsMin = std::numeric_limits<int>::max();
    int waveformLeakTenthsMax = std::numeric_limits<int>::min();
    int outOfOrderPacketCount = 0;
    int headerTimingAppliedPackets = 0;
    int headerTimingRrMin = std::numeric_limits<int>::max();
    int headerTimingRrMax = std::numeric_limits<int>::min();
    int headerTimingIePermilleMin = std::numeric_limits<int>::max();
    int headerTimingIePermilleMax = std::numeric_limits<int>::min();
    int fastLeakUpdateIndex = 0;
    int fallbackLeakUpdateIndex = 0;
    int fastLeakAppliedPackets = 0;
    int fallbackLeakAppliedPackets = 0;
    qint64 lastTimestampKey = std::numeric_limits<qint64>::min();
    int timedUpdateIndex = 0;
    const int startFileIndex = static_cast<int>(dayEntry->WaveStartOffset / kG3xWaveformFileSpan);
    const int endFileIndex = static_cast<int>((dayEntry->WaveEndOffset - 1) / kG3xWaveformFileSpan);
    for (int fileIndex = startFileIndex; fileIndex <= endFileIndex; ++fileIndex) {
        const QString filepath = QString("%1.%2")
                                     .arg(fileBasePath)
                                     .arg(fileIndex, 3, 10, QLatin1Char('0'));

        QFile waveformFile(filepath);
        if (!waveformFile.open(QIODevice::ReadOnly)) {
            continue;
        }

        qint64 localStart = (fileIndex == startFileIndex) ? (dayEntry->WaveStartOffset % kG3xWaveformFileSpan) : 0;
        qint64 localEnd = (fileIndex == endFileIndex) ? (dayEntry->WaveEndOffset % kG3xWaveformFileSpan) : waveformFile.size();
        if (fileIndex == endFileIndex && localEnd == 0) {
            localEnd = waveformFile.size();
        }

        localStart = std::max<qint64>(0, std::min(localStart, waveformFile.size()));
        localEnd = std::max<qint64>(0, std::min(localEnd, waveformFile.size()));
        if (localEnd <= localStart) {
            waveformFile.close();
            continue;
        }

        qint64 cursor = (localStart / kG3xWaveformPacketSize) * kG3xWaveformPacketSize;
        while (cursor + kG3xWaveformPacketSize <= localEnd) {
            waveformFile.seek(cursor);
            const QByteArray waveformPacket = waveformFile.read(kG3xWaveformPacketSize);
            if (waveformPacket.size() < kG3xWaveformPacketSize) {
                break;
            }

            if (static_cast<unsigned char>(waveformPacket.at(0)) != 0xAD ||
                static_cast<unsigned char>(waveformPacket.at(1)) != 0xAA) {
                cursor += kG3xWaveformPacketSize;
                continue;
            }

            QDateTime packetTimestamp;
            if (!DecodeG3xTimestamp(waveformPacket.constData(), 0x04, &packetTimestamp)) {
                cursor += kG3xWaveformPacketSize;
                continue;
            }

            const qint64 timestampKey = packetTimestamp.toSecsSinceEpoch();
            if (lastTimestampKey != std::numeric_limits<qint64>::min() &&
                timestampKey <= lastTimestampKey) {
                if (timestampKey < lastTimestampKey) {
                    ++outOfOrderPacketCount;
                }
                cursor += kG3xWaveformPacketSize;
                continue;
            }

            while (timedUpdateIndex < timedSampleUpdates.size() &&
                   timedSampleUpdates.at(timedUpdateIndex).TimestampSec <= timestampKey) {
                MergeSampleValues(timedSampleUpdates.at(timedUpdateIndex).Values, &currentValues);
                ++timedUpdateIndex;
            }

            const auto sampleLeakSeries = [timestampKey](const QVector<G3xTimedLeakUpdate>& updates,
                                                         int* index,
                                                         bool allowInterpolation,
                                                         bool enforceFreshness,
                                                         int freshnessSec,
                                                         int* leakTenthsOut) -> bool {
                if (!index || !leakTenthsOut || updates.isEmpty()) {
                    return false;
                }

                while (*index + 1 < updates.size() &&
                       updates.at(*index + 1).TimestampSec <= timestampKey) {
                    ++(*index);
                }

                const G3xTimedLeakUpdate& left = updates.at(*index);
                if (timestampKey < left.TimestampSec) {
                    return false;
                }
                if (enforceFreshness && (timestampKey - left.TimestampSec) > freshnessSec) {
                    return false;
                }

                int leakTenths = left.LeakTenths;
                if (allowInterpolation && *index + 1 < updates.size()) {
                    const G3xTimedLeakUpdate& right = updates.at(*index + 1);
                    if (timestampKey >= right.TimestampSec) {
                        leakTenths = right.LeakTenths;
                    } else if (timestampKey > left.TimestampSec) {
                        const qint64 dt = right.TimestampSec - left.TimestampSec;
                        if (dt > 0) {
                            const qint64 rel = timestampKey - left.TimestampSec;
                            const double alpha = static_cast<double>(rel) / static_cast<double>(dt);
                            const double interp = static_cast<double>(left.LeakTenths) +
                                                  alpha * static_cast<double>(right.LeakTenths - left.LeakTenths);
                            leakTenths = qRound(interp);
                        }
                    }
                }

                *leakTenthsOut = qBound(0, leakTenths, 5000);
                return true;
            };

            int sampledLeakTenths = 0;
            const bool allowLeakInterp = IsG3xLeakLinearInterpolationEnabled();
            const bool useFastLeakAsPrimary = !fastLeakUpdates.isEmpty();
            bool hasFastLeak = false;
            bool hasFallbackLeak = false;
            if (useFastLeakAsPrimary) {
                // Avoid mixing two different leak sources within one session:
                // when high-rate 0x0C exists, prefer it exclusively.
                hasFastLeak = sampleLeakSeries(fastLeakUpdates,
                                               &fastLeakUpdateIndex,
                                               allowLeakInterp,
                                               false,
                                               0,
                                               &sampledLeakTenths);
            } else {
                hasFallbackLeak = sampleLeakSeries(fallbackLeakUpdates,
                                                   &fallbackLeakUpdateIndex,
                                                   allowLeakInterp,
                                                   false,
                                                   0,
                                                   &sampledLeakTenths);
            }

            if (hasFastLeak || hasFallbackLeak) {
                currentValues.hasLeak = true;
                currentValues.leakTenths = sampledLeakTenths;
                if (hasFastLeak) {
                    ++fastLeakAppliedPackets;
                } else {
                    ++fallbackLeakAppliedPackets;
                }
            } else {
                currentValues.hasLeak = false;
                currentValues.leakTenths = 0;
            }

            const double waveLeakBlendWeight = G3xWaveLeakBlendWeight();
            if (waveLeakBlendWeight > 0.0) {
            const int packetWaveLeakTenths = ConvertG3xWaveLeakRawToTenths(
                ReadUInt16LEPtr(waveformPacket.constData(), 0x74));
            if (packetWaveLeakTenths >= 0) {
                waveformLeakTenthsMin = std::min<int>(waveformLeakTenthsMin, packetWaveLeakTenths);
                waveformLeakTenthsMax = std::max<int>(waveformLeakTenthsMax, packetWaveLeakTenths);

                if (currentValues.hasLeak) {
                    const double evtWeight = 1.0 - waveLeakBlendWeight;
                    const int blendedLeak = qRound((evtWeight * static_cast<double>(currentValues.leakTenths)) +
                                                   (waveLeakBlendWeight * static_cast<double>(packetWaveLeakTenths)));
                    currentValues.leakTenths = qBound(0, blendedLeak, 5000);
                } else {
                    currentValues.hasLeak = true;
                    currentValues.leakTenths = packetWaveLeakTenths;
                }
            }
            }

            const int packetPressureHundredths = ReadPacketPressureHundredths(waveformPacket);
            if (!hasEvtPressureUpdates && idxPressureSeedHundredths <= 0 && packetPressureHundredths > 0) {
                currentValues.hasIPAP = true;
                currentValues.ipapHundredths = packetPressureHundredths;
                currentValues.hasEPAP = true;
                currentValues.epapHundredths = packetPressureHundredths;
            }

            int packetTimingRr = -1;
            int packetTimingIePermille = -1;
            if (experimentalTiming747E) {
                const int raw74 = ReadUInt16LEPtr(waveformPacket.constData(), 0x74);
                const int raw7E = ReadUInt16LEPtr(waveformPacket.constData(), 0x7E);
                const int cycleCentiseconds = raw74 + raw7E;
                if (raw74 > 0 &&
                    raw7E > 0 &&
                    cycleCentiseconds >= 200 &&
                    cycleCentiseconds <= 2400) {
                    packetTimingRr = qBound(1, qRound(6000.0 / static_cast<double>(cycleCentiseconds)), 80);
                    packetTimingIePermille = qBound(50,
                                                    qRound((1000.0 * static_cast<double>(raw74)) /
                                                           static_cast<double>(cycleCentiseconds)),
                                                    950);
                    ++headerTimingAppliedPackets;
                    headerTimingRrMin = std::min(headerTimingRrMin, packetTimingRr);
                    headerTimingRrMax = std::max(headerTimingRrMax, packetTimingRr);
                    headerTimingIePermilleMin = std::min(headerTimingIePermilleMin, packetTimingIePermille);
                    headerTimingIePermilleMax = std::max(headerTimingIePermilleMax, packetTimingIePermille);
                }
            }

            if (firstPressureCmH2O < 0.0f) {
                if (currentValues.hasIPAP) {
                    firstPressureCmH2O = currentValues.ipapHundredths / 100.0f;
                } else if (currentValues.hasEPAP) {
                    firstPressureCmH2O = currentValues.epapHundredths / 100.0f;
                } else if (packetPressureHundredths > 0) {
                    firstPressureCmH2O = packetPressureHundredths / 100.0f;
                }
            }

            BmcWaveformPacket legacyPacket = BuildLegacyCompatiblePacket(packetTimestamp, currentValues);
            ApplyFlowWaveformFromG3xPacket(waveformPacket, &legacyPacket);
            if (packetTimingRr > 0) {
                legacyPacket.Raw.RespiratoryRate = static_cast<quint16>(packetTimingRr);
                legacyPacket.RespiratoryRate = static_cast<quint16>(packetTimingRr);
            }
            if (packetTimingIePermille > 0) {
                // Experimental: derive inspiratory fraction from 0x74/(0x74+0x7E)
                // and store as per-mille (0..1000) for Ti/Te derivation in loader.
                legacyPacket.Raw.IERatioMapped = static_cast<qint16>(packetTimingIePermille);
                legacyPacket.IERatio = static_cast<float>(packetTimingIePermille) / 10.0f;
            }
            dateSession.Waveforms.append(legacyPacket);
            waveformRawIpapMin = std::min<qint16>(waveformRawIpapMin, legacyPacket.Raw.IPAP);
            waveformRawIpapMax = std::max<qint16>(waveformRawIpapMax, legacyPacket.Raw.IPAP);

            if (diagnosticsEnabled) {
                G3xDiagRow row;
                row.TimestampSec = timestampKey;
                row.VirtualOffset = (static_cast<quint64>(fileIndex) * static_cast<quint64>(kG3xWaveformFileSpan)) +
                                    static_cast<quint64>(cursor);

                row.Raw0x0A = ReadUInt16LEPtr(waveformPacket.constData(), 0x0A);
                row.Raw0x74 = ReadUInt16LEPtr(waveformPacket.constData(), 0x74);
                row.Raw0x76 = ReadUInt16LEPtr(waveformPacket.constData(), 0x76);
                row.Raw0x7C = ReadUInt16LEPtr(waveformPacket.constData(), 0x7C);
                row.Raw0x7E = ReadUInt16LEPtr(waveformPacket.constData(), 0x7E);

                row.CurrentIPAPHundredths = currentValues.hasIPAP ? currentValues.ipapHundredths : -1;
                row.CurrentEPAPHundredths = currentValues.hasEPAP ? currentValues.epapHundredths : -1;
                row.CurrentLeakTenths = currentValues.hasLeak ? currentValues.leakTenths : -1;
                row.CurrentRespiratoryRate = (packetTimingRr > 0) ? packetTimingRr : -1;
                row.CurrentTidalVolume = currentValues.hasTidalVolume ? currentValues.tidalVolume : -1;
                row.CurrentMinuteVentilationTenths = currentValues.hasMinuteVentilation ? currentValues.minuteVentilationTenths : -1;

                row.OutputRawIPAPHalfCm = legacyPacket.Raw.IPAP;
                row.OutputRawEPAPHalfCm = legacyPacket.Raw.EPAP;

                ComputePacketMinMax(legacyPacket.Raw.Flow,
                                    kG3xWaveformOutputSampleCount,
                                    &row.FlowMin,
                                    &row.FlowMax);
                ComputePacketMinMax(legacyPacket.Raw.PressureWave,
                                    kG3xWaveformOutputSampleCount,
                                    &row.PressureWaveMin,
                                    &row.PressureWaveMax);
                ComputePacketMinMax(legacyPacket.Raw.FlowAbnormality,
                                    kG3xWaveformOutputSampleCount,
                                    &row.FlowAbnormalityMin,
                                    &row.FlowAbnormalityMax);

                diagRows.append(row);
            }

            lastTimestampKey = timestampKey;
            cursor += kG3xWaveformPacketSize;
        }

        waveformFile.close();
    }

    if (dateSession.Waveforms.isEmpty() && dayEntry->StartTimestamp.isValid()) {
        dateSession.Waveforms.append(BuildLegacyCompatiblePacket(dayEntry->StartTimestamp, currentValues));
    }

    if (firstPressureCmH2O <= 0.0f) {
        firstPressureCmH2O = 4.0f;
    }

    if (diagnosticsEnabled && !diagRows.isEmpty()) {
        const QString diagPath = BuildG3xDiagnosticsPath(dateSession.MachineInfo.SerialNumber, aDate);
        if (!WriteG3xDiagnosticsCsv(diagPath,
                                    diagRows,
                                    aDate,
                                    dayEntry->WaveStartOffset,
                                    dayEntry->WaveEndOffset,
                                    dayEntry->EventStartOffset,
                                    dayEntry->EventEndOffset)) {
            qWarning() << "BmcG3xData: Failed to write diagnostics CSV:" << diagPath;
        } else {
            qDebug() << "BmcG3xData: Wrote diagnostics CSV:" << diagPath;
        }
    }

    dateSession.MacineSettings.CPAP_TreatP = firstPressureCmH2O;
    dateSession.MacineSettings.CPAP_InitialP = firstPressureCmH2O;
    dateSession.MacineSettings.CPAP_ManualP = firstPressureCmH2O;

    int minPressureHundredths = 0;
    if (IsReasonablePressureHundredths(dayEntry->TsPressureMinHundredths)) {
        minPressureHundredths = dayEntry->TsPressureMinHundredths;
    } else if (IsReasonablePressureHundredths(dayEntry->ItPressureMinHundredths)) {
        minPressureHundredths = dayEntry->ItPressureMinHundredths;
    }

    int maxPressureHundredths = 0;
    if (IsReasonablePressureHundredths(dayEntry->TsPressureMaxHundredths)) {
        maxPressureHundredths = dayEntry->TsPressureMaxHundredths;
    } else if (IsReasonablePressureHundredths(dayEntry->ItPressureMaxHundredths)) {
        maxPressureHundredths = dayEntry->ItPressureMaxHundredths;
    }

    if (minPressureHundredths > 0) {
        dateSession.MacineSettings.APAP_IntialP = minPressureHundredths / 100.0f;
        dateSession.MacineSettings.APAP_MinAPAP = minPressureHundredths / 100.0f;
    }
    if (maxPressureHundredths > 0) {
        dateSession.MacineSettings.APAP_MaxAPAP = maxPressureHundredths / 100.0f;
    }

    if (minPressureHundredths > 0 && maxPressureHundredths > minPressureHundredths) {
        dateSession.MacineSettings.Mode = BmcMode::AutoCPAP;
    } else {
        dateSession.MacineSettings.Mode = BmcMode::CPAP;
    }

    const auto idxToDouble = [](int x100) -> double {
        return (x100 >= 0) ? (static_cast<double>(x100) / 100.0) : -1.0;
    };
    qDebug() << "BmcG3xData idx summary day" << aDate.toString(Qt::ISODate)
             << "ahi" << idxToDouble(dayEntry->ItAhiX100)
             << "ai" << idxToDouble(dayEntry->ItAiX100)
             << "hi" << idxToDouble(dayEntry->ItHiX100)
             << "oai" << idxToDouble(dayEntry->ItOaiX100)
             << "cai" << idxToDouble(dayEntry->ItCaiX100)
             << "rerai" << idxToDouble(dayEntry->ItReraIndexX100)
             << "counts_total" << dayEntry->ItEventTotalCount
             << "counts_oa" << dayEntry->ItEventObstructiveCount
             << "counts_ca" << dayEntry->ItEventCentralCount
             << "counts_h" << dayEntry->ItEventHypopneaCount
             << "counts_rera" << dayEntry->ItEventReraCount
             << "counts_other" << dayEntry->ItEventOtherCount;

    if (!dateSession.Waveforms.isEmpty()) {
        qDebug() << "BmcG3xData pressure summary day" << aDate.toString(Qt::ISODate)
                 << "experimental_timing_747e" << experimentalTiming747E
                 << "leak_scale_tenths_per_raw" << G3xLeakScaleTenthsPerRawUnit()
                 << "leak_linear_interp" << IsG3xLeakLinearInterpolationEnabled()
                 << "wave_leak_slope_tenths_per_raw" << G3xWaveLeakSlopeTenthsPerRawUnit()
                 << "wave_leak_intercept_tenths" << G3xWaveLeakInterceptTenths()
                 << "wave_leak_blend_weight" << G3xWaveLeakBlendWeight()
                 << "pressure_wave_signed10" << IsG3xPressureWaveSigned10Enabled()
                 << "flow_abn_signed10" << IsG3xFlowAbnormalitySigned10Enabled()
                 << "flow_abn_from_0510" << IsG3xFlowAbnormalityFrom0510Enabled()
                 << "flow_abn_positive_only" << IsG3xFlowAbnormalityPositiveOnlyEnabled()
                 << "leak_fast_fresh_sec" << G3xFastLeakFreshnessSeconds()
                 << "leak_source_policy" << (fastLeakUpdates.isEmpty() ? "evt42_only" : "evt0c_primary")
                 << "evt0c_leak_updates" << evt0cLeakUpdateCount
                 << "evt42_leak_updates" << fallbackLeakUpdates.size()
                 << "leak_fast_applied_packets" << fastLeakAppliedPackets
                 << "leak_fallback_applied_packets" << fallbackLeakAppliedPackets
                 << "evt42_updates" << evtPressureUpdateCount
                 << "wave_packets" << dateSession.Waveforms.size()
                 << "out_of_order_packets" << outOfOrderPacketCount
                 << "timing_hdr_packets" << headerTimingAppliedPackets
                 << "timing_rr_min_bpm" << (headerTimingRrMin == std::numeric_limits<int>::max() ? -1 : headerTimingRrMin)
                 << "timing_rr_max_bpm" << (headerTimingRrMax == std::numeric_limits<int>::min() ? -1 : headerTimingRrMax)
                 << "timing_ie_permille_min" << (headerTimingIePermilleMin == std::numeric_limits<int>::max() ? -1 : headerTimingIePermilleMin)
                 << "timing_ie_permille_max" << (headerTimingIePermilleMax == std::numeric_limits<int>::min() ? -1 : headerTimingIePermilleMax)
                 << "raw_ipap_min_halfcm" << waveformRawIpapMin
                 << "raw_ipap_max_halfcm" << waveformRawIpapMax
                 << "wave_leak_min_tenths" << (waveformLeakTenthsMin == std::numeric_limits<int>::max() ? -1 : waveformLeakTenthsMin)
                 << "wave_leak_max_tenths" << (waveformLeakTenthsMax == std::numeric_limits<int>::min() ? -1 : waveformLeakTenthsMax)
                 << "mode" << static_cast<int>(dateSession.MacineSettings.Mode)
                 << "ts_minmax_hundredths" << minPressureHundredths << maxPressureHundredths;
    }

    BmcSession* currentSession = new BmcSession();
    QDateTime lastPacketTimestamp;
    for (const BmcWaveformPacket& packet : dateSession.Waveforms) {
        if (lastPacketTimestamp.isValid() &&
            qAbs(lastPacketTimestamp.secsTo(packet.Timestamp)) >= 5 &&
            !currentSession->Waveforms.isEmpty()) {
            currentSession->StartTimestamp = currentSession->Waveforms.first().Timestamp;
            currentSession->EndTimestamp = currentSession->Waveforms.last().Timestamp.addSecs(1);
            dateSession.Sessions.append(currentSession);
            currentSession = new BmcSession();
        }

        currentSession->Waveforms.append(packet);
        lastPacketTimestamp = packet.Timestamp;
    }

    if (!currentSession->Waveforms.isEmpty()) {
        currentSession->StartTimestamp = currentSession->Waveforms.first().Timestamp;
        currentSession->EndTimestamp = currentSession->Waveforms.last().Timestamp.addSecs(1);
        dateSession.Sessions.append(currentSession);
    } else {
        delete currentSession;
    }

    for (BmcSession* s : dateSession.Sessions) {
        for (const BmcRespiratoryEvent& evt : dateSession.RespiratoryEvents) {
            if (evt.StartTime >= s->StartTimestamp && evt.StartTime <= s->EndTimestamp) {
                s->RespiratoryEvents.append(evt);
            }
        }
    }

    return dateSession;
}

const QList<BmcDataLink>& BmcG3xData::GetSessionLinks() const
{
    return sessionLinks;
}

bool BmcG3xData::ResolveIdxFile()
{
    if (!idxFilePath.isEmpty() && QFile::exists(idxFilePath)) {
        return true;
    }

    QDir dir(dirPath);
    if (!dir.exists()) {
        return false;
    }

    const QFileInfoList idxCandidates = dir.entryInfoList(QStringList() << "*.idx", QDir::Files);
    for (const QFileInfo& idxInfo : idxCandidates) {
        QFile idxFile(idxInfo.absoluteFilePath());
        if (!idxFile.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QByteArray header = idxFile.read(32);
        idxFile.close();
        if (!IsG3xIdxHeader(header)) {
            continue;
        }

        const QString candidateBase = idxInfo.absolutePath() + QDir::separator() + idxInfo.completeBaseName();
        if (!QFile::exists(candidateBase + ".000")) {
            continue;
        }

        idxFilePath = idxInfo.absoluteFilePath();
        fileBasePath = candidateBase;
        return true;
    }

    return false;
}

void BmcG3xData::ParseMachineInfo(const QByteArray& idxBytes)
{
    machineInfo.SerialNumber = ReadAscii(idxBytes, 0x30, 16);
    machineInfo.Model = ReadAscii(idxBytes, 0x48, 16);

    if (machineInfo.SerialNumber.isEmpty()) {
        machineInfo.SerialNumber = QFileInfo(idxFilePath).completeBaseName();
    }
    if (machineInfo.Model.isEmpty()) {
        machineInfo.Model = QString("Luna G3X");
    }
}

void BmcG3xData::ParseIdxRecords(const QByteArray& idxBytes)
{
    sessionLinks.clear();
    dayEntries.clear();

    const auto decodePressureField = [](quint16 rawValue) -> int {
        return (rawValue == 0xFFFF) ? 0 : static_cast<int>(rawValue);
    };
    const auto decodeOptionalU16 = [](quint16 rawValue) -> int {
        return (rawValue == 0xFFFF) ? -1 : static_cast<int>(rawValue);
    };

    for (int offset = kG3xIdxRecordOffset; offset + 0x34 <= idxBytes.size(); offset += kG3xIdxRecordSize) {
        if (ReadUInt16LE(idxBytes, offset) != 0xAAAA) {
            continue;
        }

        const int year = 1900 + static_cast<unsigned char>(idxBytes.at(offset + 0x08));
        const int month = static_cast<unsigned char>(idxBytes.at(offset + 0x09));
        const int day = static_cast<unsigned char>(idxBytes.at(offset + 0x0A));

        if (!QDate::isValid(year, month, day)) {
            continue;
        }

        const quint32 waveStart = ReadUInt32LE(idxBytes, offset + 0x10);
        const quint32 waveEnd = ReadUInt32LE(idxBytes, offset + 0x14);
        const quint32 waveLen = ReadUInt32LE(idxBytes, offset + 0x18);
        const quint32 eventStart = ReadUInt32LE(idxBytes, offset + 0x1C);
        const quint32 eventEnd = ReadUInt32LE(idxBytes, offset + 0x20);
        const quint32 eventLen = ReadUInt32LE(idxBytes, offset + 0x24);
        const quint32 logStart = ReadUInt32LE(idxBytes, offset + 0x28);
        const quint32 logEnd = ReadUInt32LE(idxBytes, offset + 0x2C);
        const quint32 logLen = ReadUInt32LE(idxBytes, offset + 0x30);

        if (waveLen == 0 || waveEnd <= waveStart) {
            continue;
        }

        const QDateTime startTs = ReadWaveformPacketTimestamp(waveStart);
        const quint32 endPacketOffset = (waveEnd >= kG3xWaveformPacketSize)
                                            ? (waveEnd - kG3xWaveformPacketSize)
                                            : waveStart;
        QDateTime endTs = ReadWaveformPacketTimestamp(endPacketOffset);

        if (!startTs.isValid()) {
            continue;
        }

        if (!endTs.isValid() || endTs < startTs) {
            const quint32 packetCount = std::max<quint32>(1, waveLen / kG3xWaveformPacketSize);
            endTs = startTs.addSecs(static_cast<int>(packetCount));
        }

        G3xDayEntry dayEntry;
        dayEntry.Date = QDate(year, month, day);
        dayEntry.StartTimestamp = startTs;
        dayEntry.EndTimestamp = endTs.addSecs(1);
        dayEntry.WaveStartOffset = waveStart;
        dayEntry.WaveEndOffset = waveEnd;
        dayEntry.WaveLength = waveLen;
        dayEntry.EventStartOffset = eventStart;
        dayEntry.EventEndOffset = eventEnd;
        dayEntry.EventLength = eventLen;
        dayEntry.LogStartOffset = logStart;
        dayEntry.LogEndOffset = logEnd;
        dayEntry.LogLength = logLen;

        const int itOffset = offset + 0x80;
        if (itOffset + 0xD4 <= idxBytes.size() &&
            idxBytes.at(itOffset) == 'I' &&
            idxBytes.at(itOffset + 1) == 'T') {
            dayEntry.ItDurationSeconds = static_cast<int>(ReadUInt32LE(idxBytes, itOffset + 0x14));
            dayEntry.ItSessionCount = static_cast<unsigned char>(idxBytes.at(itOffset + 0x24));
            dayEntry.ItPressureMinHundredths = decodePressureField(ReadUInt16LE(idxBytes, itOffset + 0x28));
            dayEntry.ItPressureMaxHundredths = decodePressureField(ReadUInt16LE(idxBytes, itOffset + 0x2A));
            dayEntry.ItPressureP95Hundredths = decodePressureField(ReadUInt16LE(idxBytes, itOffset + 0x2C));
            dayEntry.ItPressureEPAPHundredths = decodePressureField(ReadUInt16LE(idxBytes, itOffset + 0x30));
            dayEntry.ItAhiX100 = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xBC));
            dayEntry.ItAiX100 = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xBE));
            dayEntry.ItHiX100 = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xC0));
            dayEntry.ItOaiX100 = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xC2));
            dayEntry.ItCaiX100 = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xC4));
            dayEntry.ItReraIndexX100 = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xC6));
            dayEntry.ItEventTotalCount = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xC8));
            dayEntry.ItEventObstructiveCount = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xCA));
            dayEntry.ItEventCentralCount = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xCC));
            dayEntry.ItEventHypopneaCount = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xCE));
            dayEntry.ItEventReraCount = decodeOptionalU16(ReadUInt16LE(idxBytes, itOffset + 0xD0));
            dayEntry.ItEventOtherCount = static_cast<qint16>(ReadUInt16LE(idxBytes, itOffset + 0xD2));
        }

        const int tsOffset = offset + 0x280;
        if (tsOffset + 0x12 <= idxBytes.size() &&
            idxBytes.at(tsOffset) == 'T' &&
            idxBytes.at(tsOffset + 1) == 'S') {
            dayEntry.TsPressureMinHundredths = decodePressureField(ReadUInt16LE(idxBytes, tsOffset + 0x0E));
            dayEntry.TsPressureMaxHundredths = decodePressureField(ReadUInt16LE(idxBytes, tsOffset + 0x10));
        }

        dayEntries.append(dayEntry);

        BmcDataLink link;
        // Use idx record day for link identity/filtering to avoid duplicate day imports
        // when waveform timestamps cross midnight.
        link.UsrSession.StartTimestamp = QDateTime(dayEntry.Date, QTime(12, 0, 0));
        link.UsrSession.EndTimestamp = link.UsrSession.StartTimestamp.addDays(1).addSecs(-1);
        const qint64 durationMinutes = std::max<qint64>(1, dayEntry.StartTimestamp.secsTo(dayEntry.EndTimestamp) / 60);
        link.UsrSession.DurationMinutes = static_cast<int>(durationMinutes);

        sessionLinks.append(link);
    }

    std::sort(sessionLinks.begin(), sessionLinks.end(), [](const BmcDataLink& a, const BmcDataLink& b) {
        return a.UsrSession.StartTimestamp < b.UsrSession.StartTimestamp;
    });
    std::sort(dayEntries.begin(), dayEntries.end(), [](const G3xDayEntry& a, const G3xDayEntry& b) {
        return a.StartTimestamp < b.StartTimestamp;
    });
}

QDateTime BmcG3xData::ReadWaveformPacketTimestamp(quint32 virtualByteOffset) const
{
    const QByteArray header = ReadVirtualWaveformBytes(virtualByteOffset, 10);
    if (header.size() < 10) {
        return QDateTime();
    }

    if (static_cast<unsigned char>(header.at(0)) != 0xAD || static_cast<unsigned char>(header.at(1)) != 0xAA) {
        return QDateTime();
    }

    const int year = 1900 + static_cast<unsigned char>(header.at(0x04));
    const int month = static_cast<unsigned char>(header.at(0x05));
    const int day = static_cast<unsigned char>(header.at(0x06));
    const int hour = static_cast<unsigned char>(header.at(0x07));
    const int minute = static_cast<unsigned char>(header.at(0x08));
    const int second = static_cast<unsigned char>(header.at(0x09));

    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid()) {
        return QDateTime();
    }

    return QDateTime(date, time);
}

QList<QDateTime> BmcG3xData::ReadWaveformPacketTimestamps(quint32 startOffset, quint32 endOffset) const
{
    QList<QDateTime> timestamps;
    if (endOffset <= startOffset) {
        return timestamps;
    }

    const int startFileIndex = static_cast<int>(startOffset / kG3xWaveformFileSpan);
    const int endFileIndex = static_cast<int>((endOffset - 1) / kG3xWaveformFileSpan);

    for (int fileIndex = startFileIndex; fileIndex <= endFileIndex; ++fileIndex) {
        const QString filepath = QString("%1.%2")
                                     .arg(fileBasePath)
                                     .arg(fileIndex, 3, 10, QLatin1Char('0'));

        QFile waveformFile(filepath);
        if (!waveformFile.open(QIODevice::ReadOnly)) {
            continue;
        }

        qint64 localStart = (fileIndex == startFileIndex) ? (startOffset % kG3xWaveformFileSpan) : 0;
        qint64 localEnd = (fileIndex == endFileIndex) ? (endOffset % kG3xWaveformFileSpan) : waveformFile.size();

        if (fileIndex == endFileIndex && localEnd == 0) {
            localEnd = waveformFile.size();
        }

        localStart = std::max<qint64>(0, std::min(localStart, waveformFile.size()));
        localEnd = std::max<qint64>(0, std::min(localEnd, waveformFile.size()));
        if (localEnd <= localStart) {
            waveformFile.close();
            continue;
        }

        const qint64 firstAligned = (localStart / kG3xWaveformPacketSize) * kG3xWaveformPacketSize;
        qint64 cursor = firstAligned;
        while (cursor + kG3xWaveformPacketSize <= localEnd) {
            waveformFile.seek(cursor);
            const QByteArray packetHeader = waveformFile.read(10);
            if (packetHeader.size() < 10) {
                break;
            }

            if (static_cast<unsigned char>(packetHeader.at(0)) == 0xAD &&
                static_cast<unsigned char>(packetHeader.at(1)) == 0xAA) {
                QDateTime timestamp;
                if (DecodeG3xTimestamp(packetHeader.constData(), 0x04, &timestamp)) {
                    timestamps.append(timestamp);
                }
            }

            cursor += kG3xWaveformPacketSize;
        }

        waveformFile.close();
    }

    return timestamps;
}

QByteArray BmcG3xData::ReadVirtualWaveformBytes(quint32 virtualByteOffset, int byteCount) const
{
    QByteArray output;
    output.reserve(byteCount);

    qint64 currentOffset = virtualByteOffset;
    int remaining = byteCount;

    while (remaining > 0) {
        const int fileIndex = static_cast<int>(currentOffset / kG3xWaveformFileSpan);
        const qint64 fileOffset = currentOffset % kG3xWaveformFileSpan;

        const QString filepath = QString("%1.%2")
                                     .arg(fileBasePath)
                                     .arg(fileIndex, 3, 10, QLatin1Char('0'));

        QFile waveformFile(filepath);
        if (!waveformFile.open(QIODevice::ReadOnly)) {
            break;
        }

        if (!waveformFile.seek(fileOffset)) {
            waveformFile.close();
            break;
        }

        const qint64 maxReadable = waveformFile.size() - fileOffset;
        const int chunkSize = static_cast<int>(std::min<qint64>(remaining, maxReadable));
        if (chunkSize <= 0) {
            waveformFile.close();
            break;
        }

        output.append(waveformFile.read(chunkSize));
        waveformFile.close();

        remaining -= chunkSize;
        currentOffset += chunkSize;
    }

    return output;
}

bool BmcG3xData::IsG3xIdxHeader(const QByteArray& headerBytes)
{
    return headerBytes.startsWith("BMC G/E/P INDEX");
}

QString BmcG3xData::ReadAscii(const QByteArray& bytes, int offset, int length)
{
    if (offset < 0 || length <= 0 || offset >= bytes.size()) {
        return QString();
    }

    const int remainingBytes = static_cast<int>(bytes.size() - offset);
    const int safeLength = std::min(length, remainingBytes);
    QByteArray str = bytes.mid(offset, safeLength);

    const int nullIndex = str.indexOf('\0');
    if (nullIndex >= 0) {
        str.truncate(nullIndex);
    }

    return QString::fromLatin1(str).trimmed();
}

quint16 BmcG3xData::ReadUInt16LE(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 2 > bytes.size()) {
        return 0;
    }

    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
    return static_cast<quint16>(p[0] | (p[1] << 8));
}

quint32 BmcG3xData::ReadUInt32LE(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 4 > bytes.size()) {
        return 0;
    }

    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
    return static_cast<quint32>(p[0]) |
           (static_cast<quint32>(p[1]) << 8) |
           (static_cast<quint32>(p[2]) << 16) |
           (static_cast<quint32>(p[3]) << 24);
}
