/* SEFAM SD Card Data Parsing Header
 *
 * Decodes the SEFAM CPAP SD card container: whole-file XOR-0xBF obfuscation,
 * fixed-length text headers, 10-second sample records carrying a checksum and
 * sequence number, and 49-byte .LOG event records.
 *
 * This module deliberately contains no OSCAR types. The card format is fully
 * specified in Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md and validated against
 * the manufacturer's analyzer output; keeping the decode layer free of SleepLib
 * dependencies lets it be reasoned about on its own.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SEFAM_DATA_PARSING_H
#define SEFAM_DATA_PARSING_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

/*! \namespace SefamParsing
    \brief Pure decoding of the SEFAM SD card format, free of OSCAR types. */
namespace SefamParsing {

//! Every .LOG and channel data file on the card is XORed with this constant.
constexpr quint8 kObfuscationKey = 0xBF;

//! Header length for channel data files: "#03/" + 20 serial + "/" + 12 date + "/" + 32 hex + "/".
constexpr int kChannelHeaderLength = 71;

//! Header length for .LOG files: as above but without the 32-hex field.
constexpr int kLogHeaderLength = 38;

//! Each sample record holds exactly this many seconds of data.
constexpr int kRecordSeconds = 10;

//! Bytes following the samples in each record: 1 checksum + 2 big-endian sequence.
constexpr int kRecordTrailerBytes = 3;

//! Sample value meaning "no valid data" on FLW, PRE and LK.
constexpr quint8 kInvalidSample = 0xFF;

/*! \struct FileHeader
    \brief The decoded fixed-length text header at the start of every card file. */
struct FileHeader
{
    QString   serial;                  //!< Full serial, pad spaces trimmed.
    QDateTime localStart;              //!< Device local wall-clock start time.
    qint64    utcEpoch = 0;            //!< UTC epoch seconds; 0 when the short header is used.
    int       length   = 0;            //!< 71 or 38, whichever variant was detected.
};

/*! \brief XOR every byte with kObfuscationKey, in place. Self-inverse. */
void descramble(QByteArray &data);

/*! \brief Parse a descrambled file header.
    \param decoded Descrambled bytes; at least kChannelHeaderLength are examined.
    \param out     Populated on success.
    \return true if the header carries the "#03/" tag and a parseable timestamp.

    The header length is detected, never assumed: after the 12-character
    timestamp and its delimiter, 32 hex digits followed by '/' indicate the
    71-byte channel variant; anything else means the 38-byte .LOG variant.
    Do not locate the end of the header by scanning for the fourth '/' — a
    payload byte can descramble to '/' and shift the parse. */
bool parseHeader(const QByteArray &decoded, FileHeader &out);

}  // namespace SefamParsing

#endif  // SEFAM_DATA_PARSING_H
