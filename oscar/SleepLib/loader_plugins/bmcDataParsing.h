#ifndef BMCDATAPARSING_H
#define BMCDATAPARSING_H

#include <QDate>
#include <QDataStream>
#include <QList>

constexpr int kBmcLegacyWaveformSamples = 25;
constexpr int kBmcExtendedWaveformSamples = 50;

//The raw I:E Ratio value recorded by BMC is transformed using a function that maps the raw
//value to a percentage: `InspirationPercentage = (100 * rawValue) / (rawValue + 10)`
//Instead of performing this calculation for every sample, since we have discrete values
//from 0 to 100, we create a mapping table of all the valid values and simply do a look up
const static float IERatioLookup[] = {
        0, 9.1, 16.7, 23.1, 28.6, 33.3, 37.5, 41.2, 44.4, 47.4, 50, 52.4, 54.5, 56.5, 58.3,
        60, 61.5, 63, 64.3, 65.5, 66.7, 67.7, 68.8, 69.7, 70.6, 71.4, 72.2, 73, 73.7, 74.4, 75, 75.6, 76.2,
        76.7, 77.3, 77.8, 78.3, 78.7, 79.2, 79.6, 80, 80.4, 80.8, 81.1, 81.5, 81.8, 82.1, 82.5, 82.8, 83.1,
        83.3, 83.6, 83.9, 84.1, 84.4, 84.6, 84.8, 85.1, 85.3, 85.5, 85.7, 85.9, 86.1, 86.3, 86.5, 86.7,
        86.8, 87, 87.2, 87.3, 87.5, 87.7, 87.8, 88, 88.1, 88.2, 88.4, 88.5, 88.6, 88.8, 88.9, 89, 89.1, 89.2,
        89.4, 89.5, 89.6, 89.7, 89.8, 89.9, 90, 90.1, 90.2, 90.3, 90.4, 90.5, 90.6, 90.7, 90.7, 90.8
        };

class MessageItemBase {};

class MessageItem32 : public MessageItemBase
{
public:
    int MessageType;
    quint32 Data;

    MessageItem32(int msgType, quint32 data) : MessageType(msgType), Data(data) {};
};

class MessageItem24 : public MessageItemBase
{
public:
    int MessageType;
    quint8 Data1;
    quint8 Data2;
    quint8 Data3;

    MessageItem24(int msgType, quint16 data1, quint8 data2, quint8 data3) :
        MessageType(msgType),
        Data1(data1),
        Data2(data2),
        Data3(data3)
        {};
};

class MessageItem16 : public MessageItemBase
{
public:
    int MessageType;
    quint16 Data;

    MessageItem16(int msgType, quint16 data) : MessageType(msgType), Data(data) {};
};

class BmcEncodedDate{
public:
    static QDateTime DecodeDate(quint16);
};

enum class BmcRespiratoryEventType
{
    HYP = 0,
    OSA,
    CSA,
    UA,
    PB,         ///< Periodic breathing / Cheyne-Stokes respiration episode
    RERA,       ///< Respiratory Effort Related Arousal
    Unknown
};

class BmcRespiratoryEvent
{
public:
    BmcRespiratoryEventType EventType;
    QDateTime StartTime;
    QDateTime EndTime;
    int DurationSeconds;
};

/// @brief A single flow-limitation point event with a severity grade.
/// Grade 1 = Mild, 2 = Moderate, 3 = Severe.
/// DurationMs is the device-reported breath duration in milliseconds (EVT value2).
class BmcFlowLimitEvent
{
public:
    QDateTime Timestamp;
    int Grade;      ///< 1 = Mild, 2 = Moderate, 3 = Severe
    int DurationMs; ///< Device-reported breath duration in milliseconds (EVT value2)
};

/// @brief A pressure snapshot from an EVT 0x42 record.
/// Used for EVT-only sessions where no waveform packets are present.
class BmcPressureSnapshot
{
public:
    QDateTime Timestamp;
    int EpapHundredths = 0; ///< EPAP in hundredths of cmH2O
    int IpapHundredths = 0; ///< IPAP in hundredths of cmH2O
};

class BmcUsrSession
{
public:
    QDateTime StartTimestamp;
    QDateTime EndTimestamp;
    int DurationMinutes;
    QList<BmcRespiratoryEvent> RespiratoryEvents;

    BmcUsrSession();
    BmcUsrSession(QDataStream*, bool inProgressSession);

    static quint32 GetNextHistoricSessionOffset(QDataStream*);

protected:
    QList<MessageItem32> MessagesOffset45;
    QList<MessageItem32> DataMessages32;
    QList<MessageItem24> DataMessages24;
    QList<MessageItem16> DataMessages16;

    void ReadInProgressSession(QDataStream*);
    void ReadHistoricSession(QDataStream*);
};

class BmcIdxEntry{
public:
    QDateTime Timestamp;
    int Index;
    quint16 StartOffsetPacket;
    quint16 StartFileIndex;
    quint16 NextOffsetPacket;
    bool HasValidNext;
    quint8 NextFileIndex;

    QDateTime StartWaveformPacketTimestamp;

    BmcIdxEntry();
    BmcIdxEntry(QDataStream*);

    size_t StartOffsetByte();
    QString StartFileExtension();
    size_t NextOffsetByte();
    QString NextFileExtension();

    
};

enum class BmcMode
{
    CPAP = 0,
    AutoCPAP,
    S,
    ST,
    T,
    Titration,
    AutoS
};

enum class BmcMaskType
{
    FullFace = 0,
    Nasal,
    NasalPillow,
    Other
};

enum class BmcAirTubeType
{
    Unheated22mm = 0,
    Unheated15mm,
    Heated22mm,
    Heated15mm
};

class BmcMachineSettings
{
public:
    QDate Timestamp;

    quint8 Reslex;
    bool ReslexPatient;

    quint8 RampTimeMinutes;

    quint8 HumidifierLevel;

    float APAP_IntialP;
    float APAP_MinAPAP;
    float APAP_MaxAPAP;
    quint8 APAP_Sensitivity;
    bool APAP_SmartA;


    float CPAP_InitialP;
    float CPAP_TreatP;
    float CPAP_ManualP;
    bool CPAP_SmartC;


    float S_InitialEPAP;
    float S_EPAP;
    float S_IPAP;
    int S_ISENS;
    float S_ESENS;
    quint8 S_RiseTime;
    float S_TiMin;
    float S_TiMax;
    bool S_BackupRR;

    float AutoS_InitialEPAP;
    float AutoS_MinEPAP;
    float AutoS_MinIPAP;
    float AutoS_MaxIPAP;

    int AutoS_ISENS;
    float AutoS_ESENS;
    quint8 AutoS_RiseTime;

    bool AutoS_SmartB;



    bool LeakAlert;
    bool AutoOn;
    bool AutoOff;

    BmcMode Mode;

    BmcMaskType MaskType;
    BmcAirTubeType AirTubeType;
    int HeatedTubeLevel;

    BmcMachineSettings();
    BmcMachineSettings(QDataStream*);
};

class BmcWaveformCrumb
{
public:
    QString Filepath;
    quint16 FileIndex;
    quint64 ByteOffset;
    quint16 PacketOffset;
    QDateTime Timestamp;
};

class BmcDataLink
{
public:
    BmcUsrSession UsrSession;
    BmcIdxEntry IdxEntry;
    BmcWaveformCrumb WaveformCrumb;
};

#pragma pack(push, 1)
struct BmcWaveformPacketStruct{
    uint16_t Header; //00
    int16_t Offset0x02; //02
    int16_t EPAP; //04
    int16_t IPAP; //06
    int16_t PressureWave[kBmcLegacyWaveformSamples]; //08
    int16_t FlowAbnormality[kBmcLegacyWaveformSamples]; //3a
    int16_t Flow[kBmcLegacyWaveformSamples]; //6c
    int16_t Offset0x9E;
    int16_t Offset0xA0;
    int16_t Offset0xA2;
    int16_t Offset0xA4;
    int16_t Offset0xA6;
    int16_t Offset0xA8;
    int16_t Offset0xAA;
    int16_t Offset0xAC;
    int16_t Offset0xAE;
    int16_t Offset0xB0;
    int16_t Offset0xB2;
    int16_t Offset0xB4;
    int16_t Offset0xB6;
    int16_t Offset0xB8;
    int16_t Offset0xBA;
    int16_t Offset0xBC;
    int16_t Offset0xBE;
    int16_t Offset0xC0;
    int16_t Offset0xC2;
    int16_t Leak; //C4
    int16_t TidalVolume; //C6
    int16_t Offset0xC8;
    int16_t MinuteVentilation; //CA
    int16_t SpO2Pct;      // 0xCC – oxygen saturation %
    int16_t PulseRate;    // 0xCE – pulse rate in beats per minute
    int16_t RespiratoryRate; //D0
    int16_t IERatio; //D2
    int16_t Offset0xD4;
    int16_t Offset0xD6;
    int16_t Offset0xD8;
    int16_t Offset0xDA;
    int16_t Offset0xDC;
    int16_t Offset0xDE;
    int16_t Offset0xE0;
    int16_t Offset0xE2;
    int16_t Offset0xE4;
    int16_t Offset0xE6;
    int16_t Offset0xE8;
    int16_t Offset0xEA;
    int16_t Offset0xEC;
    int16_t Offset0xEE;
    int16_t Offset0xF0;
    int16_t Offset0xF2;
    int16_t Offset0xF4;
    int16_t Offset0xF6;
    uint16_t Year; //F8
    uint8_t Month; //FA
    uint8_t Day;  //FB
    uint8_t Hour;  //FC
    uint8_t Minute;  //FD
    uint8_t Second;  //FE
    uint8_t Terminator;
};
#pragma pack(pop)


class BmcWaveformPacketRaw
{
public:
    QDateTime Timestamp;
    qint16 IPAP;
    qint16 EPAP;
    qint16 PressureWave[kBmcExtendedWaveformSamples];
    qint16 MaskPressure[kBmcExtendedWaveformSamples];
    qint16 FlowAbnormality[kBmcExtendedWaveformSamples];
    qint16 Flow[kBmcExtendedWaveformSamples];
    quint16 Leak;
    qint16 TidalVolume;
    qint16 MinuteVentilation;
    quint16 SpO2Pct;
    quint16 PulseRate;
    quint16 RespiratoryRate;
    qint16 IERatioMapped;
    /// @brief EPAP pressure trend (slow-moving target/smoothed pressure), raw hundredths cmH2O.
    ///        Sourced from waveform packet offset 0x76C. Used by session-start detection.
    quint16 PressureTrend = 0;
};


class BmcWaveformPacket
{
public:
    QDateTime Timestamp;
    float IPAP;
    float EPAP;
    qint16 PressureWave[kBmcExtendedWaveformSamples];
    qint16 MaskPressure[kBmcExtendedWaveformSamples];
    quint16 FlowAbnormality[kBmcExtendedWaveformSamples];
    float Flow[kBmcExtendedWaveformSamples];
    float Leak;
    int TidalVolume;
    float MinuteVentilation;
    quint16 RespiratoryRate;
    float IERatio;
    BmcWaveformPacketRaw Raw;

    BmcWaveformPacket(char* buffer);
};

class BmcMachineInfo
{
public:
    QString SerialNumber;
    QString Model;
    QString FirmwareVersion; ///< User-facing version from .log 0x0420, e.g. "G3-2.11.02.33" or "G3-2.12.54.13".
                             ///< Falls back to IDX 0x0345 SC build string if .log unavailable.
};


//We break the data for an entire day up into actual sessions
//by looking for gaps in the waveform data
class BmcSession{
public:
    QDateTime StartTimestamp;
    QDateTime EndTimestamp;

    QList<BmcWaveformPacket> Waveforms;
    QList<BmcRespiratoryEvent> RespiratoryEvents;
    QList<BmcFlowLimitEvent> FlowLimitEvents;
    QList<BmcPressureSnapshot> PressureSnapshots; ///< From EVT 0x42; populated for EVT-only sessions (waveLen==0).
};

class BmcDateSession
{
public:
    QDateTime StartTime;
    int DurationMinutes;
    BmcMachineInfo MachineInfo;
    BmcMachineSettings MacineSettings;
    QList<BmcRespiratoryEvent> RespiratoryEvents;
    QList<BmcFlowLimitEvent> FlowLimitEvents;
    QList<BmcWaveformPacket> Waveforms;


    QList<BmcSession*> Sessions;
    ~BmcDateSession();
};

class BmcDataParser
{
public:
    virtual ~BmcDataParser() = default;
    virtual void ReadData() = 0;
    virtual BmcMachineInfo ReadMachineInfo() = 0;
    virtual BmcDateSession ReadDateSession(QDate aDate) = 0;
    virtual const QList<BmcDataLink>& GetSessionLinks() const = 0;
};

class BmcData
    : public BmcDataParser
{
public:
    QList<BmcUsrSession> AllUsrSessions;
    QList<BmcMachineSettings> AllMachineSettings;
    QList<BmcIdxEntry> AllIdxEntries;

    QList<BmcIdxEntry> ValidIdxEntries;

    QList<BmcDataLink> SessionLinks;

    QList<BmcWaveformCrumb> WaveformCrumbs;

    static bool DirectoryHasBmcData(const QString& path);

    BmcData();
    BmcData(const QString& path);

    int ReadDataCount();
    void ReadData() override;

    BmcMachineInfo ReadMachineInfo() override;

    BmcDateSession ReadDateSession(QDate aDate) override;
    const QList<BmcDataLink>& GetSessionLinks() const override;

protected:
    QString dirPath;
    QString usrFilePath;

    static QString ChangeFileExtension(const QString& path, QString newExtensionWithDot);
    static QString GetUsrFilePath(const QString& path);

    // Returns 0xFF if the file begins with a 255-byte legacy tail packet followed by
    // standard 256-byte packets (detected by 0xAAAA at bytes 0xFF–0x100), or 0 for
    // the standard all-256-byte layout.
    static int DetectFileDataOffset(const QString& path);

    QDateTime ReadWaveformPacketTimestamp(const QString& path, quint64 packetStartByte);

    void ReadIdxFile();
    void ReadAllSessions();
    void BuildWaveformCrumbs();
    QList<BmcWaveformPacket> ReadWaveforms(BmcDataLink & link);
    void FindValidSessions();
};


#endif // BMCDATAPARSING_H
