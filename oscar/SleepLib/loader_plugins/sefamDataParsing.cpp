/* SEFAM SD Card Data Parsing Implementation
 *
 * Decodes the SEFAM card container. See sefamDataParsing.h for the interface
 * and Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md for the format itself.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sefamDataParsing.h"

#include <QRegularExpression>

namespace SefamParsing {

void descramble(QByteArray &data)
{
    for (int i = 0; i < data.size(); ++i) {
        data[i] = static_cast<char>(static_cast<quint8>(data.at(i)) ^ kObfuscationKey);
    }
}

bool parseHeader(const QByteArray &decoded, FileHeader &out)
{
    // Minimum viable header: "#03/" + 20 serial + "/" + 12 timestamp + "/"
    if (decoded.size() < kLogHeaderLength) { return false; }
    if (!decoded.startsWith("#03/")) { return false; }

    const int serialStart = 4;
    const int serialLen   = 20;
    const int stampStart  = serialStart + serialLen + 1;   // skip the '/' after the serial
    const int stampLen    = 12;

    if (decoded.at(serialStart + serialLen) != '/') { return false; }
    if (decoded.size() < stampStart + stampLen + 1) { return false; }
    if (decoded.at(stampStart + stampLen) != '/') { return false; }

    out.serial = QString::fromLatin1(decoded.mid(serialStart, serialLen)).trimmed();

    const QString stamp = QString::fromLatin1(decoded.mid(stampStart, stampLen));
    out.localStart = QDateTime::fromString(stamp, "yyMMddHHmmss");
    if (!out.localStart.isValid()) { return false; }
    // QDateTime maps a two-digit year to 1900+yy; the card is post-2000.
    if (out.localStart.date().year() < 2000) {
        out.localStart = out.localStart.addYears(100);
    }

    // Detect the variant: 32 hex digits then '/' means the long channel header.
    const int hexStart = stampStart + stampLen + 1;
    const int hexLen   = 32;
    out.utcEpoch = 0;
    out.length   = kLogHeaderLength;

    if (decoded.size() >= hexStart + hexLen + 1 && decoded.at(hexStart + hexLen) == '/') {
        const QByteArray hex = decoded.mid(hexStart, hexLen);
        static const QRegularExpression hexOnly("\\A[0-9A-Fa-f]{32}\\z");
        if (hexOnly.match(QString::fromLatin1(hex)).hasMatch()) {
            bool ok = false;
            const qint64 epoch = hex.left(16).toLongLong(&ok, 16);
            if (ok) {
                out.utcEpoch = epoch;
                out.length   = kChannelHeaderLength;
            }
        }
    }
    return true;
}

}  // namespace SefamParsing
