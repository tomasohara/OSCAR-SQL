#ifndef YUWELL_LOADER_H
#define YUWELL_LOADER_H

#include "SleepLib/machine.h"
#include "SleepLib/machine_loader.h"

const int yuwell_data_version = 1;
const QString yuwell_class_name = "YuwellLoader";
const unsigned char YUWELL_CPAP = 0x00;
const unsigned char YUWELL_APAP = 0x01;

class YuwellLoader;

class YuwellLoader : public CPAPLoader
{
  public:
    YuwellLoader();
    virtual ~YuwellLoader();
    static void Register();
    virtual bool Detect(const QString & path);
    virtual bool backupData (Machine * mach, const QString & path);
    virtual int Version() { return yuwell_data_version; };
    virtual int Open(const QString &);
    virtual const QString &loaderName() { return yuwell_class_name; };
    virtual QStringList getYuwellMachines (QString givenPath);
    virtual int OpenMachine(Machine *mach, const QString & path, const QString & serialPath);
    virtual bool OpenSession(Machine *mach, const QString & filename);

    virtual MachineInfo newInfo() {
        return MachineInfo(MT_CPAP, 0, yuwell_class_name, QObject::tr("Yuwell"),
                           QString(), QString(), QString(), QObject::tr("Yuwell"),
                           QDateTime::currentDateTime(), yuwell_data_version);
    }


  protected:
    QMap<SessionID, Session *> Sessions;
    bool calc_leaks = true;
    float lpm4, lpm20;  // Leak per minute at 4 and 20 cmH20
    bool rebuild_from_backups = false;
    bool create_backups = true;

};

#endif // YUWELL_LOADER_H
