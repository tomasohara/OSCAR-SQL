/* Dreem Unit Tests
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DREEMTESTS_H
#define DREEMTESTS_H

#include "AutoTest.h"
#include "../SleepLib/loader_plugins/dreem_loader.h"

class DreemTests : public QObject
{
    Q_OBJECT
 
private slots:
    void initTestCase();
    void testSessionsToYaml();
    void cleanupTestCase();
};

DECLARE_TEST(DreemTests)

#endif // DREEMTESTS_H

