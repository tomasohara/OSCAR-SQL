/* SEFAM Loader Implementation
 *
 * Detects and imports SEFAM CPAP SD cards. See sefam_loader.h for the class
 * interface, Notes/loaders/SEFAM_LOADER_DESIGN.md for the design rationale and
 * Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md for the card format.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sefam_loader.h"
#include "sefamDataParsing.h"

#include "SleepLib/importcontext.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

using SefamParsing::ChannelSpec;

/*! \name Hypopnea flag placement
    The card records no duration for hypopneas — the log argument is zero in
    about 97% of code 5 and 6 records — so their flags cannot be placed at the
    event start the way apneas can. Left at the log timestamp they render at the
    event end, roughly 15 seconds later than the manufacturer's own report shows
    them, on this patient's most frequent event type.

    These constants shift the flag back by the manufacturer's published mean
    durations for the validated card so most hypopneas land near their true
    start. This is a **display-placement heuristic, not measured data**: real
    durations vary, so individual events are approximate in either direction.
    The stored event duration remains zero, which is why no duration appears in
    the tooltip.

    Remove these once the per-event extent is recovered — Y17 is the suspected
    source. See Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md, open problem 1.
    @{ */
constexpr qint64 kObstructiveHypopneaPlacementMs = 16000;   //!< Vendor mean, 16 s
constexpr qint64 kCentralHypopneaPlacementMs     = 15000;   //!< Vendor mean, 15 s
/*! @} */

static bool sefam_initialised = false;

SefamLoader::SefamLoader()  { m_type = MT_CPAP; }
SefamLoader::~SefamLoader() = default;

void SefamLoader::Register()
{
    if (sefam_initialised) { return; }
    qDebug() << "Registering SefamLoader";
    RegisterLoader(new SefamLoader());
    sefam_initialised = true;
}

QString SefamLoader::findSerialDir(const QString &path)
{
    // Card layout: <root>/<digits><letter>/<digits>/DATA_nnn/
    // Accept the caller pointing at the root, the model directory, or the
    // serial directory itself, so import works from any of the three.
    static const QRegularExpression modelRe("\\A\\d+[A-Za-z]\\z");
    static const QRegularExpression serialRe("\\A\\d+\\z");

    QDir dir(path);
    if (!dir.exists()) { return QString(); }

    // Already the serial directory?
    if (!dir.entryList(QStringList("DATA_*"), QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        return dir.absolutePath();
    }

    const QStringList level1 = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &a : level1) {
        QDir sub(dir.absoluteFilePath(a));

        // <root>/<model>/<serial>/DATA_nnn
        if (modelRe.match(a).hasMatch()) {
            const QStringList level2 = sub.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &b : level2) {
                if (!serialRe.match(b).hasMatch()) { continue; }
                QDir leaf(sub.absoluteFilePath(b));
                if (!leaf.entryList(QStringList("DATA_*"),
                                    QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
                    return leaf.absolutePath();
                }
            }
        }

        // <model>/<serial>/DATA_nnn — caller pointed at the model directory
        if (serialRe.match(a).hasMatch()
            && !sub.entryList(QStringList("DATA_*"),
                              QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            return sub.absolutePath();
        }
    }
    return QString();
}

bool SefamLoader::Detect(const QString &path)
{
    const QString serialDir = findSerialDir(path);
    if (serialDir.isEmpty()) { return false; }

    QDir dir(serialDir);
    const QStringList sessions =
        dir.entryList(QStringList("DATA_*"), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    // Confirm the "#03/" tag in at least one channel header. A directory tree of
    // the right shape from another vendor must not produce a false positive.
    for (const QString &s : sessions) {
        QDir sess(dir.absoluteFilePath(s));
        if (!QFile::exists(sess.absoluteFilePath(s + ".INI"))) { continue; }

        QFile f(sess.absoluteFilePath(s + ".FLW"));
        if (!f.open(QIODevice::ReadOnly)) { continue; }
        QByteArray head = f.read(SefamParsing::kChannelHeaderLength);
        f.close();

        SefamParsing::descramble(head);
        SefamParsing::FileHeader hdr;
        if (SefamParsing::parseHeader(head, hdr)) {
            qDebug() << "SefamLoader::Detect matched" << serialDir
                     << "serial" << hdr.serial;
            return true;
        }
    }
    return false;
}

/*! \brief Turn an .INI "Created By" value into a display model name.
    \return e.g. "REVE_AUTO " -> "Rêve Auto"; empty if the input was empty.

    Only names actually observed on a card are translated. Anything else falls
    back to the raw trimmed value so an unknown model still shows something
    meaningful rather than a blank. */
static QString sefamModelName(const QString &createdBy)
{
    const QString key = createdBy.trimmed().toUpper();
    if (key.isEmpty())      { return QString(); }
    if (key == "REVE_AUTO") { return QString::fromUtf8("Rêve Auto"); }
    if (key == "SBOX_AUTO") { return QStringLiteral("S.Box Auto"); }
    return createdBy.trimmed();
}

MachineInfo SefamLoader::PeekInfo(const QString &path)
{
    MachineInfo info = newInfo();
    const QString serialDir = findSerialDir(path);
    if (serialDir.isEmpty()) { return info; }

    QDir dir(serialDir);

    // The hardware model code (e.g. "1279R") is the parent directory name. It is
    // kept as a property rather than in `series`, because `series` drives the
    // device-pixmap lookup in MachineLoader::getPixmap() and must stay "Sefam".
    const QString modelCode = QFileInfo(dir.absolutePath()).dir().dirName();
    info.properties[QStringLiteral("ModelCode")] = modelCode;

    const QStringList sessions =
        dir.entryList(QStringList("DATA_*"), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &s : sessions) {
        QDir sess(dir.absoluteFilePath(s));
        QFile f(sess.absoluteFilePath(s + ".FLW"));
        if (!f.open(QIODevice::ReadOnly)) { continue; }
        QByteArray head = f.read(SefamParsing::kChannelHeaderLength);
        f.close();

        SefamParsing::descramble(head);
        SefamParsing::FileHeader hdr;
        if (!SefamParsing::parseHeader(head, hdr)) { continue; }
        info.serial = hdr.serial;

        // Model name comes from the .INI, which is plain text and not obfuscated.
        QSettings ini(sess.absoluteFilePath(s + ".INI"), QSettings::IniFormat);
        const QString model = sefamModelName(ini.value("Create Info/Created By").toString());
        info.modelnumber = model.isEmpty() ? modelCode : model;
        info.model       = info.modelnumber;

        // Firmware version, for diagnostics on untested models.
        const QString fw = ini.value("Create Info/Version").toString().trimmed();
        if (!fw.isEmpty()) { info.properties[QStringLiteral("Firmware")] = fw; }
        break;
    }
    if (info.modelnumber.isEmpty()) {
        info.modelnumber = modelCode;
        info.model       = modelCode;
    }
    return info;
}

void SefamLoader::importWaveform(Session *session, const QVector<qint16> &values,
                                 const QVector<bool> &valid, ChannelID chan,
                                 EventDataType gain, int rateMs, qint64 startMs)
{
    const int n = qMin(values.size(), valid.size());
    int i = 0;
    while (i < n) {
        while (i < n && !valid.at(i)) { ++i; }      // skip a run of invalid samples
        if (i >= n) { break; }

        const int runStart = i;
        while (i < n && valid.at(i)) { ++i; }
        const int runLen = i - runStart;

        EventList *el = session->AddEventList(chan, EVL_Waveform, gain, 0.0,
                                              0.0, 0.0, rateMs);
        // AddWaveform takes a mutable pointer, so copy the run out.
        QVector<qint16> run = values.mid(runStart, runLen);
        el->AddWaveform(startMs + static_cast<qint64>(runStart) * rateMs,
                        run.data(), runLen,
                        static_cast<qint64>(runLen) * rateMs);
    }
}

int SefamLoader::Open(const QString &path)
{
    Q_ASSERT(m_ctx);
    if (!Detect(path)) { return -1; }

    const MachineInfo info = PeekInfo(path);
    qDebug() << "SefamLoader::Open — brand" << info.brand
             << "model" << info.model
             << "serial" << info.serial
             << "modelcode" << info.properties.value(QStringLiteral("ModelCode"))
             << "firmware" << info.properties.value(QStringLiteral("Firmware"));

    m_ctx->CreateMachineFromInfo(info);
    Machine *mach = p_profile->CreateMachine(info);

    // Only one model has been validated end-to-end against a manufacturer report.
    // Others import on a best-effort basis but must announce themselves.
    if (info.properties.value(QStringLiteral("ModelCode")) != sefam_validated_model) {
        MachineInfo untested = info;      // the signal takes a non-const reference
        emit deviceIsUntested(untested);
    }

    backupData(mach, path);

    const QString serialDir = findSerialDir(path);
    QDir dir(serialDir);
    const QStringList dirs =
        dir.entryList(QStringList("DATA_*"), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    emit updateMessage(QObject::tr("Reading SEFAM card..."));
    emit setProgressMax(dirs.size());
    emit setProgressValue(0);

    int imported = 0, skipped = 0, progress = 0;

    // The device writes a settings record only occasionally — 4 of the 31
    // sessions on the validated card carry one. Settings are therefore carried
    // forward from the most recent record at or before each session, which is
    // what the manufacturer's own software does (its report lists identical
    // settings on every session row). Directory names sort chronologically, so
    // iterating in order makes this safe even if settings change mid-card:
    // a session never inherits settings recorded after it.
    SefamParsing::Settings lastKnown;

    for (const QString &d : dirs) {
        if (isAborted()) { break; }
        emit setProgressValue(++progress);
        QCoreApplication::processEvents();

        SefamParsing::SessionData data;
        QString error;
        if (!SefamParsing::readSession(dir.absoluteFilePath(d), data, error)) {
            qWarning() << "Sefam:" << d << "skipped —" << error;
            ++skipped;
            continue;
        }

        const SessionID sid = data.header.utcEpoch != 0
                            ? static_cast<SessionID>(data.header.utcEpoch)
                            : static_cast<SessionID>(data.header.localStart.toSecsSinceEpoch());
        if (mach->SessionExists(sid)) { continue; }

        const qint64 startMs = data.header.localStart.toMSecsSinceEpoch();
        const qint64 endMs   = startMs
                             + static_cast<qint64>(data.recordCount)
                               * SefamParsing::kRecordSeconds * 1000LL;

        Session *session = new Session(mach, sid);
        session->SetChanged(true);
        session->really_set_first(startMs);
        session->really_set_last(endMs);

        if (data.settings.valid) { lastKnown = data.settings; }

        if (lastKnown.valid) {
            // A-PAP is the only mode observed on any SEFAM card examined.
            session->settings[CPAP_Mode]        = MODE_APAP;
            session->settings[CPAP_PressureMin] = lastKnown.minPressure;
            session->settings[CPAP_PressureMax] = lastKnown.maxPressure;
            if (lastKnown.rampMinutes > 0) {
                session->settings[CPAP_RampTime]     = lastKnown.rampMinutes;
                session->settings[CPAP_RampPressure] = lastKnown.rampPressure;
            }
        }
        // Sessions before the first settings record on a card carry no settings
        // rather than inheriting from the future.

        const ChannelSpec flwSpec = data.channels.value("FLW");
        const ChannelSpec preSpec = data.channels.value("PRE");
        const ChannelSpec lkSpec  = data.channels.value("LK");
        const QVector<quint8> &flw = data.samples.value("FLW");
        const QVector<quint8> &pre = data.samples.value("PRE");
        const QVector<quint8> &lk  = data.samples.value("LK");

        // Pressure: raw byte is tenths of a cmH2O, so store it as-is with gain 0.1.
        if (!pre.isEmpty() && preSpec.freq > 0) {
            QVector<qint16> v(pre.size());
            QVector<bool>   ok(pre.size());
            for (int k = 0; k < pre.size(); ++k) {
                ok[k] = (pre.at(k) != SefamParsing::kInvalidSample);
                v[k]  = static_cast<qint16>(pre.at(k));
            }
            importWaveform(session, v, ok, CPAP_Pressure, 0.1f,
                           1000 / preSpec.freq, startMs);
        }

        // Total leak: the device reports leak including the intentional mask vent.
        if (!lk.isEmpty() && lkSpec.freq > 0) {
            QVector<qint16> v(lk.size());
            QVector<bool>   ok(lk.size());
            for (int k = 0; k < lk.size(); ++k) {
                ok[k] = (lk.at(k) != SefamParsing::kInvalidSample);
                v[k]  = static_cast<qint16>(lk.at(k));
            }
            importWaveform(session, v, ok, CPAP_LeakTotal, 0.6f,
                           1000 / lkSpec.freq, startMs);
        }

        // Flow: the card reports TOTAL flow, which sits about 22 L/min above zero
        // because it includes the mask vent. OSCAR's breath detector
        // (FlowParser::calcPeaks) hard-codes a zero crossing line, so it must be
        // given patient flow. Subtracting the device's own leak estimate centres
        // the signal on zero and yields a plausible breath rate; leaving it as
        // total flow produces no detected breaths at all, and hence no
        // respiratory rate, tidal volume, minute ventilation or Ti/Te.
        // Stored in tenths of a L/min so gain is an exact 0.1.
        if (!flw.isEmpty() && flwSpec.freq > 0 && !lk.isEmpty() && lkSpec.freq > 0) {
            const float flwGain = (flwSpec.max - flwSpec.min) / 255.0f;   // 460/255
            const float flwZero = -flwSpec.min / flwGain;                 // raw value meaning 0 L/min
            QVector<qint16> v(flw.size());
            QVector<bool>   ok(flw.size());
            for (int k = 0; k < flw.size(); ++k) {
                const int lkIndex = static_cast<int>(
                    static_cast<qint64>(k) * lkSpec.freq / flwSpec.freq);
                const bool haveLeak = lkIndex < lk.size()
                                   && lk.at(lkIndex) != SefamParsing::kInvalidSample;
                ok[k] = (flw.at(k) != SefamParsing::kInvalidSample) && haveLeak;
                if (!ok[k]) { v[k] = 0; continue; }

                const float totalFlow  = (flw.at(k) - flwZero) * flwGain;
                const float leakFlow   = lk.at(lkIndex) * 0.6f;
                v[k] = static_cast<qint16>(qRound((totalFlow - leakFlow) * 10.0f));
            }
            importWaveform(session, v, ok, CPAP_FlowRate, 0.1f,
                           1000 / flwSpec.freq, startMs);
        }
        // DET, NSD and Y17 are deliberately not imported — see the design spec.

        // Events are placed relative to the session start so no timezone
        // arithmetic is needed. This requires the UTC epoch, which the short
        // header variant does not carry.
        if (!data.log.isEmpty() && data.header.utcEpoch != 0) {
            EventList *oa    = session->AddEventList(CPAP_Obstructive, EVL_Event);
            EventList *ca    = session->AddEventList(CPAP_ClearAirway, EVL_Event);
            EventList *hyp   = session->AddEventList(CPAP_Hypopnea,    EVL_Event);
            EventList *snore = session->AddEventList(CPAP_VSnore,      EVL_Event);
            EventList *fl    = session->AddEventList(CPAP_FlowLimit,   EVL_Event);

            for (const SefamParsing::LogRecord &rec : data.log) {
                const qint64 offsetMs = (rec.utcSeconds - data.header.utcEpoch) * 1000LL;
                if (offsetMs < 0 || offsetMs > (endMs - startMs)) { continue; }
                const qint64 when = startMs + offsetMs;

                // The log timestamp is the event END: reading it as a start
                // produces impossible overlaps between consecutive events, and
                // apnea flags placed there render visibly late against the
                // manufacturer's own waveform report.
                //
                // OSCAR flags are drawn as a bare vertical line at the event
                // timestamp — gFlagsLine's FLAG branch ignores the duration
                // entirely (it surfaces only in the tooltip). ResMed, the
                // reference loader, passes the EDF+ annotation *onset*, so the
                // convention is flag-at-start. Subtract the duration to match.
                const qint64 durMs = static_cast<qint64>(rec.arg) * 100;   // arg is 0.1 s

                switch (rec.code) {
                case SefamParsing::kLogObstructiveApnea:
                    oa->AddEvent(when - durMs, rec.arg / 10.0f); break;
                case SefamParsing::kLogCentralApnea:
                    ca->AddEvent(when - durMs, rec.arg / 10.0f); break;
                // OSCAR has no central hypopnea channel; bmcG3xDataParsing and
                // prisma_loader both fold both kinds into CPAP_Hypopnea. The
                // stored duration stays zero because the card does not report
                // one; only the flag placement is corrected, using the vendor's
                // published mean durations (see the constants above).
                case SefamParsing::kLogObstructiveHypop:
                    hyp->AddEvent(when - kObstructiveHypopneaPlacementMs, 0); break;
                case SefamParsing::kLogCentralHypopnea:
                    hyp->AddEvent(when - kCentralHypopneaPlacementMs, 0); break;
                case SefamParsing::kLogSnore:
                    snore->AddEvent(when, 0); break;
                case SefamParsing::kLogFlowLimitation:
                    fl->AddEvent(when, 0); break;
                default:
                    break;      // administrative codes are parsed but not imported
                }
            }
        } else if (!data.log.isEmpty()) {
            qWarning() << "Sefam:" << d
                       << "has no UTC epoch in its header — events skipped";
        }

        if (session->eventlist.isEmpty()) {
            qWarning() << "Sefam:" << d << "skipped — no valid samples";
            delete session;
            ++skipped;
            continue;
        }

        session->UpdateSummaries();
        mach->AddSession(session);
        ++imported;
    }

    mach->Save();
    finishAddingSessions();

    qDebug() << "Sefam TOTAL sessions imported" << imported << "skipped" << skipped;
    return imported;
}

bool SefamLoader::backupData(Machine *mach, const QString &path)
{
    Q_UNUSED(mach); Q_UNUSED(path);
    return true;
}
