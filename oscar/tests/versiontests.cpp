/* Version Unit Tests
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "versiontests.h"
#include "version.h"

void VersionTests::testCurrentVersion()
{
    qDebug() << getVersion();

    // If this fails, it means that the defined VERSION isn't valid and needs fixing!
    Q_ASSERT(getVersion().IsValid());
}

void VersionTests::testPrecedence()
{
    // This is the list of precedence examples from the Semantic Version documentation:
    Q_ASSERT(Version("1.0.0-alpha") < Version("1.0.0-alpha.1"));
    Q_ASSERT(Version("1.0.0-alpha.1") < Version("1.0.0-alpha.beta"));
    Q_ASSERT(Version("1.0.0-alpha.beta") < Version("1.0.0-beta"));
    Q_ASSERT(Version("1.0.0-beta") < Version("1.0.0-beta.2"));
    Q_ASSERT(Version("1.0.0-beta.2") < Version("1.0.0-beta.11"));
    Q_ASSERT(Version("1.0.0-beta.11") < Version("1.0.0-rc.1"));
    Q_ASSERT(Version("1.0.0-rc.1") < Version("1.0.0"));
    
    Q_ASSERT(Version("1.0.0-alpha+001") == Version("1.0.0-alpha+002"));
    Q_ASSERT(Version("1.0.0+20130313144700") == Version("1.0.0+20200313144700"));
    Q_ASSERT(Version("1.0.0-beta+exp.sha.5114f85") == Version("1.0.0-beta+exp.sha.00000000"));

    // This is the list of precedence that we expect to work correctly as of 1.1.0:
    Q_ASSERT(Version("1.0.1-r1") < Version("1.1.0-testing-1"));
    Q_ASSERT(Version("1.1.0-testing-1") < Version("1.1.0-testing-4"));
    Q_ASSERT(Version("1.1.0-testing-4") < Version("1.1.0-beta-1"));
    Q_ASSERT(Version("1.1.0-beta-1") < Version("1.1.0-beta-2"));
    Q_ASSERT(Version("1.1.0-beta-2") < Version("1.1.0-rc.1"));
    Q_ASSERT(Version("1.1.0-rc.1") < Version("1.1.0-rc.2"));
    Q_ASSERT(Version("1.1.0-rc.2") < Version("1.1.0"));
    Q_ASSERT(Version("1.1.0-rc.2") < Version("1.2.0"));
}
