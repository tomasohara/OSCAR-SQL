#include <QTimeZone>
#include <QString>
#include <QDir>
#include <QDebug>
#include <QMessageBox>
#include <QCoreApplication>

#include "SleepLib/loader_plugins/yuwell_loader.h"

ChannelID Yuwell_Ramp;
ChannelID Yuwell_Humidity;

YuwellLoader::YuwellLoader() {
    m_type = MT_CPAP;

    const QString YUWELL_ICON = ":/icons/yuwell.png";

    QString s = newInfo().series;
    m_pixmap_paths[s] = YUWELL_ICON;
    m_pixmaps[s] = QPixmap(YUWELL_ICON);
}

YuwellLoader::~YuwellLoader() {}

bool yuwell_initialized = false;
void YuwellLoader::Register() {
    if (yuwell_initialized)
        return;

    RegisterLoader(new YuwellLoader());

    yuwell_initialized = true;
}

bool YuwellLoader::Detect(const QString & givenPath) {
    /*
     * The YH550A has the structure:
     * root - RunLog.bys
     *      \ MODEL-SERIAL
     */
    QDir dir(givenPath);

    if (!dir.exists()) {
        return false;
    }

    // Should contain a file named "RunLog.bys".
    if (!dir.exists("RunLog.bys")) {
        return false;
    }

    QStringList machines = getYuwellMachines(givenPath);
    if (machines.length() <= 0)
        // Did not find any Yuwell device directories
        return false;

    return true;
}

bool YuwellLoader::backupData (Machine * mach, const QString & path) {
    QDir ipath(path);
    QDir bpath(mach->getBackupPath());

    // Compare QDirs rather than QStrings because separators may be different, especially on Windows.
    if (ipath == bpath) {
        // Don't create backups if importing from backup folder
        rebuild_from_backups = true;
        create_backups = false;
    } else {
        rebuild_from_backups = false;
        create_backups = p_profile->session->backupCardData();
    }

    if (rebuild_from_backups || !create_backups)
        return true;

    // Copy input data to backup location
    copyPath(ipath.absolutePath(), bpath.absolutePath(), true);

    return true;
}


QStringList YuwellLoader::getYuwellMachines (QString givenPath) {
    /*
     * Return a list of machines, model-serial
     */
    QStringList yuwellMachines;
    QDir cardDir (givenPath);

    cardDir.setFilter(QDir::NoDotAndDotDot | QDir::Dirs | QDir::NoSymLinks);
    cardDir.setSorting(QDir::Name);

    QFileInfoList flist = cardDir.entryInfoList();   // List of machine subdirectories

    for (int i = 0; i < flist.size(); i++) {
        QFileInfo fi = flist.at(i);
        QString filename = fi.fileName();

        /*
         * Only supporting the YH550A at the moment because that's all I have!
         * At this stage I don't know which machines have SD-Cards, some may not
         * and if all machines provide full logs rather than just summaries.
         * This is left in the simple state that it is until I receive data from
         * other models. Other models include:
         *
         * YH350 (BreathCare I Auto-CPAP)
         * YH360 (BreathCare I Auto-CPAP)
         * YH450 (BreathCare II Auto-CPAP)
         * YH480 (BreathCare II Auto-CPAP)
         * YH550 (BreathCare ECO Auto-CPAP, A/B Models)
         * YH580 (BreathCare I Auto-CPAP)
         * YH680 (BreathCare III Auto-CPAP, A/B Models)
         * YH690 (BreathCare III Auto-CPAP, A/B Models)
         * YH720 (BreathCare I Bi-PAP)
         * YH725 (BreathCare I Bi-PAP)
         * YH730 (BreathCare I Bi-PAP)
         * YH820 (BreathCare II Bi-PAP)
         * YH825 (BreathCare II Bi-PAP)
         * YH830 (BreathCare II Bi-PAP)
         * So This will probably change around here to detect each model and if the file
         * format is different, the Open() will have to change as well
         *
         * Fingers crossed the other models with CD-Cards have the same file format.
         */
        if (!filename.toUpper().startsWith("YH550A-")) {
            continue;
        }

        // Data files have a .BYS extension
        QDir machineDir (givenPath + "/" + filename);
        machineDir.setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::NoSymLinks);
        machineDir.setSorting(QDir::Name);

        QStringList filters;
        filters << "*.BYS";
        machineDir.setNameFilters(filters);
        QFileInfoList flist = machineDir.entryInfoList();
        if (flist.size() <= 0) { // No data files
            continue;
        }
        yuwellMachines.push_back(filename);
    }

    return yuwellMachines;
}

int YuwellLoader::Open(const QString & dirPath) {
    QStringList serialNumbers = getYuwellMachines(dirPath);
    if (serialNumbers.length() <= 0)
        return -1;

    Machine *m;

    int c = 0;
    for (int i = 0; i < serialNumbers.size(); i++) {
        MachineInfo info = newInfo();
        info.serial = serialNumbers[i];
        m = p_profile->CreateMachine(info);
        
        // IMPORTANT: Save machine to database immediately so sessions can reference it
        if (m && m->getDatabaseId() == 0) {
            if (!m->SaveToDatabase()) {
                qWarning() << "YuwellLoader::Open() - Failed to save machine to database";
            } else {
                qDebug() << "YuwellLoader::Open() - Saved machine to database with ID" << m->getDatabaseId();
            }
        }
        
        QString serialPath = dirPath + "/" + info.serial;
        try {
            if (m) {
                c += OpenMachine(m, dirPath, serialPath);
                m->Save();
            }
        } catch (OneTypePerDay& e) {
            Q_UNUSED(e)
            p_profile->DelMachine(m);
            QMessageBox::warning(nullptr, tr("Import Error"),
                                 tr("This device Record cannot be imported in this profile.")+"\n\n"+tr("The Day records overlap with already existing content."),
                                 QMessageBox::Ok);
            delete m;
        }
    }

    return c;
}

int YuwellLoader::OpenMachine(Machine *mach, const QString & path, const QString & serialPath)
{
    emit updateMessage(QObject::tr("Getting Ready..."));
    emit setProgressValue(0);
    QCoreApplication::processEvents();

    QDir dir(serialPath);

    if (!dir.exists() || (!dir.isReadable())) {
        return -1;
    }

    backupData(mach, path);

    calc_leaks = p_profile->cpap->calculateUnintentionalLeaks();
    lpm4 = p_profile->cpap->custom4cmH2OLeaks();
    lpm20 = p_profile->cpap->custom20cmH2OLeaks();

    dir.setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::NoSymLinks);
    dir.setSorting(QDir::Name);
    QFileInfoList flist = dir.entryInfoList();


    emit updateMessage(QObject::tr("Reading data files..."));
    QCoreApplication::processEvents();

    Sessions.clear();

    QString filename, fpath;
    emit setProgressMax(flist.size());

    // There's only one file type with an extension .BYS containing a header and line data
    for (int i = 0; i < flist.size(); i++) {
        emit setProgressValue(i);
        QFileInfo fi = flist.at(i);
        filename = fi.fileName();
        fpath = serialPath + "/" + filename;

        OpenSession(mach, fpath);
    }

    int c = Sessions.size();
    qDebug() << "Yuwell Loader found" << c << "sessions";

    emit updateMessage(QObject::tr("Finishing up..."));
    QCoreApplication::processEvents();

    finishAddingSessions();

    return c;
}

bool YuwellLoader::OpenSession(Machine *mach, const QString & filename)
{
    QFile file(filename);

    if (!file.open(QFile::ReadOnly)) {
        qWarning() << "Yuwell Session Couldn't open" << filename;
        return false;
    }

    // Read header of summary file, 51 bytes ending in 0xF9
    QByteArray header;
    header = file.read(0x33);

    if (header.size() != 0x33) {
        qWarning() << "Yuwell Session Short file" << filename;
        file.close();
        return false;
    }

    char terminator = 0xf9;
    if (terminator != header.at(0x32)) {
        qWarning() << "Yuwell Session Header missing 0xf9 terminator" << filename;
    }

    QDataStream in(header);
    in.setVersion(QDataStream::Qt_4_8);
    in.setByteOrder(QDataStream::LittleEndian);

    unsigned char start_year, start_month, start_day, start_hour, start_minute, start_second;
    unsigned char finish_year, finish_month, finish_day, finish_hour, finish_minute, finish_second;

    in >> start_year;
    in >> start_month;
    in >> start_day;
    in >> start_hour;
    in >> start_minute;
    in >> start_second;

    in >> finish_year;
    in >> finish_month;
    in >> finish_day;
    in >> finish_hour;
    in >> finish_minute;
    in >> finish_second;

    QDateTime start = QDateTime(QDate((int)start_year + 2000, start_month, start_day), QTime(start_hour, start_minute, start_second), QTimeZone::systemTimeZone());
    QDateTime finish = QDateTime(QDate((int)finish_year + 2000, finish_month, finish_day), QTime(finish_hour, finish_minute, finish_second), QTimeZone::systemTimeZone());

    unsigned char mode, ramp_up_time, initial_pressure, minimum_pressure, maximum_pressure;
    unsigned char humidity_settings;
    unsigned char avg_leak_volume, avg_pressure;
    QByteArray raw_model_serial(16, Qt::Uninitialized);
    short int record_count;

    in >> mode;  // 0x00=CPAP, 0x01=APAP
    in >> ramp_up_time;  // Minutes
    in >> initial_pressure;
    in >> minimum_pressure;
    in >> maximum_pressure;
    in.skipRawData(1);
    in >> humidity_settings;
    in.skipRawData(7);
    in >> avg_leak_volume;
    in.skipRawData(1);
    in >> avg_pressure;
    in.skipRawData(1);
    in.readRawData(raw_model_serial.data(), 16);
    in >> record_count;

    QString model_serial(raw_model_serial);
    mach->setModel(model_serial);

    quint32 ts;
    ts = start.toSecsSinceEpoch();  // Create timestamp from session start
    if (!mach->SessionExists(ts)) {
        Session *sess = new Session(mach, ts);
        sess->really_set_first(qint64(ts) * 1000L);
        sess->really_set_last(qint64(finish.toSecsSinceEpoch()) * 1000L);

        if (mode == YUWELL_CPAP) {
            sess->settings[CPAP_Mode] = (int)MODE_CPAP;
            sess->settings[CPAP_Pressure] = initial_pressure / 10.0;
        } else {
            sess->settings[CPAP_Mode] = (int)MODE_APAP;
            sess->settings[CPAP_PressureMin] = minimum_pressure / 10.0;
            sess->settings[CPAP_PressureMax] = maximum_pressure / 10.0;
        }

        sess->settings[Yuwell_Humidity] = humidity_settings;
        sess->settings[Yuwell_Ramp] = ramp_up_time;

        EventList *LK = sess->AddEventList(CPAP_LeakTotal, EVL_Event, 1);
        EventList *PR = sess->AddEventList(CPAP_Pressure, EVL_Event, 0.1F);
        EventList *OA = sess->AddEventList(CPAP_Obstructive, EVL_Event);
        EventList *CA = sess->AddEventList(CPAP_ClearAirway, EVL_Event);
        EventList *H =  sess->AddEventList(CPAP_Hypopnea, EVL_Event);

        qint64 ti;
        ti = qint64(ts) * 1000L;

        for (int i = 0; i < record_count; i++) {
            QCoreApplication::processEvents();
            QByteArray record;

            record = file.read(0x0a);

            if (record.size() != 0x0a) {
                qWarning() << "Yuwell Session Record Short" << filename;
                file.close();
                return false;
            }

            QDataStream in(record);
            in.setVersion(QDataStream::Qt_4_8);
            in.setByteOrder(QDataStream::LittleEndian);
            
            unsigned char pressure, oai, hi, cai, leak_volume;
            in >> pressure;
            in.skipRawData(2);
            in >> oai;
            in >> hi;
            in >> cai;
            in.skipRawData(3);
            in >> leak_volume;

            PR->AddEvent(ti + (i * 60000), pressure); // Samples are every 60 seconds

            if (oai > 0) {
                OA->AddEvent(ti + (i * 60000), 0);
            }
            if (hi > 0) {
                H->AddEvent(ti + (i * 60000), 0);
            }
            if (cai > 0) {
                CA->AddEvent(ti + (i * 60000), 0);
            }
            if (leak_volume > 0) {
                LK->AddEvent(ti + (i * 60000), leak_volume);
            }
        }
        sess->SetChanged(true);
        Sessions[ts] = sess;
        sess->UpdateSummaries();
        sess->Store(mach->getDataPath());
        mach->AddSession(sess);
    }

    return true;
}
