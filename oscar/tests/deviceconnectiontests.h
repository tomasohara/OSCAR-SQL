/* Device Connection Unit Tests
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class DeviceConnectionTests : public QObject
{
    Q_OBJECT
private slots:
    void testSerialPortInfoSerialization();
    void testSerialPortScanning();
    void testOximeterConnection();
};
DECLARE_TEST(DeviceConnectionTests)
