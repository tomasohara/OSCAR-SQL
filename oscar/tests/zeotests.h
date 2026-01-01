/* ZEO Unit Tests
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ZEOTESTS_H
#define ZEOTESTS_H

#include "AutoTest.h"
#include "../SleepLib/loader_plugins/zeo_loader.h"

class ZeoTests : public QObject
{
    Q_OBJECT
 
private slots:
    void initTestCase();
    void testSessionsToYaml();
    void cleanupTestCase();
};

DECLARE_TEST(ZeoTests)

#endif // ZEOTESTS_H

