/* Cryptographic Abstraction Unit Tests
 *
 * Copyright (c) 2021-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class CryptoTests : public QObject
{
    Q_OBJECT
private slots:
    void testAES256();
    void testAES256GCM();
    void testAES256GCMencrypt();
    void testPBKDF2_SHA256();
    void testPRS1Benchmarks();
};
DECLARE_TEST(CryptoTests)

