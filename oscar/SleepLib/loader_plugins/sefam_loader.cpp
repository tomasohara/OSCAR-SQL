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
#include "SleepLib/session.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

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

int SefamLoader::Open(const QString &path)
{
    Q_ASSERT(m_ctx);
    if (!Detect(path)) { return -1; }

    // PeekInfo is only consulted by the import UI on the auto-scan path
    // (mainwindow.cpp), so log it here to make the identity visible when a
    // folder is browsed to manually.
    const MachineInfo info = PeekInfo(path);
    qDebug() << "SefamLoader::Open stub — brand" << info.brand
             << "model" << info.model
             << "serial" << info.serial
             << "modelcode" << info.properties.value(QStringLiteral("ModelCode"))
             << "firmware" << info.properties.value(QStringLiteral("Firmware"));

    const QString serialDir = findSerialDir(path);
    QDir dir(serialDir);
    const QStringList dirs =
        dir.entryList(QStringList("DATA_*"), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    int ok = 0, skipped = 0, records = 0;
    for (const QString &d : dirs) {
        SefamParsing::SessionData data;
        QString error;
        if (!SefamParsing::readSession(dir.absoluteFilePath(d), data, error)) {
            qWarning() << "Sefam:" << d << "skipped —" << error;
            ++skipped;
            continue;
        }
        ++ok;
        records += data.recordCount;

        QStringList populated = data.samples.keys();
        populated.sort();
        qDebug() << "Sefam" << data.dirName
                 << "records" << data.recordCount
                 << "channels" << populated
                 << "start" << data.header.localStart.toString("yyyy-MM-dd HH:mm:ss");
    }
    qDebug() << "Sefam TOTAL sessions" << ok << "skipped" << skipped
             << "hours" << (records * 10.0 / 3600.0);
    return ok;
}

bool SefamLoader::backupData(Machine *mach, const QString &path)
{
    Q_UNUSED(mach); Q_UNUSED(path);
    return true;
}
