/* SleepLib Apple Health Loader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef APPLEHEALTHLOADER_H
#define APPLEHEALTHLOADER_H

#include "SleepLib/machine_loader.h"

const QString applehealth_class_name = "AppleHealth";
const int applehealth_data_version = 1;


class AppleHealthLoader : public MachineLoader
{
  public:
    AppleHealthLoader();
    virtual ~AppleHealthLoader();

    virtual bool Detect(const QString & path);

    virtual int Open(const QString & path) { Q_UNUSED(path); return 0; }
    virtual int OpenFile(const QString & path);
    virtual QStringList getNameFilter() { return QStringList("Apple Health Export (export.xml)"); }
    static void Register();

    virtual int Version() { return applehealth_data_version; }
    virtual const QString &loaderName() { return applehealth_class_name; }

    virtual MachineInfo newInfo() {
        return MachineInfo(MT_OXIMETER, 0, applehealth_class_name, QObject::tr("Apple"), QObject::tr("Apple Watch"), QString(), QString(), QObject::tr("Apple Health"), QDateTime::currentDateTime(), applehealth_data_version);
    }

    MachineInfo newInfoSleep() {
        return MachineInfo(MT_SLEEPSTAGE, 0, applehealth_class_name, QObject::tr("Apple"), QObject::tr("Apple Watch Sleep"), QString(), QString(), QObject::tr("Apple Health"), QDateTime::currentDateTime(), applehealth_data_version);
    }
};

#endif // APPLEHEALTHLOADER_H
