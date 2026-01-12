/* Profile Importer Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profileimporter.h"
#include "SleepLib/profiles.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"
#include "SleepLib/journal.h"
#include "SleepLib/progressdialog.h"
#include "database/migration_manager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QApplication>

ProfileImporter::ProfileImporter(QObject *parent)
    : QObject(parent)
    , m_progress(nullptr)
    , m_totalSessions(0)
    , m_loadedSessions(0)
{
}

ProfileImporter::~ProfileImporter()
{
}

bool ProfileImporter::importProfile(const QString& sourcePath,
                                    const QString& newProfileName,
                                    ProgressDialog* progress)
{
    m_progress = progress;
    m_lastError.clear();
    m_totalSessions = 0;
    m_loadedSessions = 0;
    
    reportProgress(0, 100, tr("Validating source profile..."));
    
    if (!validateSourceProfile(sourcePath)) {
        m_lastError = tr("Invalid source profile: %1").arg(m_lastError);
        return false;
    }
    
    // Create new profile folder
    QString newPath = GetAppData() + "/Profiles/" + newProfileName;
    QDir().mkpath(newPath);
    
    reportProgress(10, 100, tr("Copying profile structure..."));
    
    if (!copyProfileStructure(sourcePath, newPath)) {
        rollbackImport(newPath);
        return false;
    }
    
    // Copy machines.xml BEFORE creating Profile so MigrationManager can find it
    QString sourceMachinesXml = sourcePath + "/machines.xml";
    QString destMachinesXml = newPath + "/machines.xml";
    if (!QFile::copy(sourceMachinesXml, destMachinesXml)) {
        m_lastError = tr("Failed to copy machines.xml");
        rollbackImport(newPath);
        return false;
    }
    
    reportProgress(20, 100, tr("Creating profile in database..."));
    
    // Create profile object - this initializes the database
    Profile* profile = new Profile(newPath, false);
    
    // Set username first, before migration
    profile->user->setUserName(newProfileName);
    
    reportProgress(25, 100, tr("Migrating profile metadata..."));
    
    if (!migrateMetadata(profile, sourcePath)) {
        rollbackImport(newPath);
        delete profile;
        return false;
    }
    
    reportProgress(30, 100, tr("Copying user information..."));
    
    // IMPORTANT: Copy user data AFTER migration to prevent MigrationManager from overwriting it
    // Load source profile to copy user data
    Profile* sourceProfile = new Profile(sourcePath, true);
    if (sourceProfile && sourceProfile->isOpen()) {
        qDebug() << "ProfileImporter: Source profile opened successfully";
        qDebug() << "ProfileImporter: Source user firstname:" << sourceProfile->user->firstName();
        qDebug() << "ProfileImporter: Source user lastname:" << sourceProfile->user->lastName();
        qDebug() << "ProfileImporter: Source doctor name:" << sourceProfile->doctor->name();
        
        // Copy user info (keep the new username we already set)
        profile->user->setDOB(sourceProfile->user->DOB());
        profile->user->setHeight(sourceProfile->user->height());
        profile->user->setFirstName(sourceProfile->user->firstName());
        profile->user->setLastName(sourceProfile->user->lastName());
        profile->user->setAddress(sourceProfile->user->address());
        profile->user->setPhone(sourceProfile->user->phone());
        profile->user->setEmail(sourceProfile->user->email());
        profile->user->setGender(sourceProfile->user->gender());
        profile->user->setCountry(sourceProfile->user->country());
        // Copy doctor info
        profile->doctor->setName(sourceProfile->doctor->name());
        profile->doctor->setPracticeName(sourceProfile->doctor->practiceName());
        profile->doctor->setAddress(sourceProfile->doctor->address());
        profile->doctor->setPhone(sourceProfile->doctor->phone());
        profile->doctor->setEmail(sourceProfile->doctor->email());
        profile->doctor->setPatientID(sourceProfile->doctor->patientID());
        // Note: Preferences are copied during extended data migration
        
        qDebug() << "ProfileImporter: After copy - user firstname:" << profile->user->firstName();
        qDebug() << "ProfileImporter: After copy - user lastname:" << profile->user->lastName();
        qDebug() << "ProfileImporter: After copy - doctor name:" << profile->doctor->name();
        
        // Note: We'll save user/doctor info in Profile::Save() below
        // Don't save here because profile might not be in database yet
    } else {
        qWarning() << "ProfileImporter: Failed to open source profile or profile not open";
    }
    delete sourceProfile;
    
    reportProgress(40, 100, tr("Loading session data from files..."));
    
    // IMPORTANT: Set p_profile before loading sessions - Session objects need it
    extern Profile* p_profile;
    Profile* savedProfile = p_profile;
    p_profile = profile;
    
    bool sessionsLoaded = loadSessionsFromFiles(profile, sourcePath);
    
    if (!sessionsLoaded) {
        p_profile = savedProfile;  // Restore before failing
        rollbackImport(newPath);
        delete profile;
        return false;
    }
    
    reportProgress(90, 100, tr("Calculating daily summaries..."));
    
    if (!calculateSummaries(profile)) {
        // Don't fail for this - summaries can be regenerated
        qWarning() << "Failed to calculate summaries, but import succeeded";
    }
    
    // IMPORTANT: Set opened state to true so Profile::Save() doesn't return early
    profile->setOpened(true);
    
    // Save profile (still needs p_profile set)
    profile->Save();
    
    reportProgress(100, 100, tr("Import complete!"));
    
    // Delete profile (still needs p_profile set - destructor may call Session methods)
    delete profile;
    
    // Restore p_profile AFTER profile is deleted
    p_profile = savedProfile;
    
    return true;
}

bool ProfileImporter::validateSourceProfile(const QString& path)
{
    QDir dir(path);
    
    if (!dir.exists()) {
        m_lastError = tr("Source path does not exist");
        return false;
    }
    
    if (!QFile::exists(path + "/machines.xml")) {
        m_lastError = tr("Source is not a valid OSCAR profile (missing machines.xml)");
        return false;
    }
    
    return true;
}

bool ProfileImporter::copyProfileStructure(const QString& oldPath, 
                                          const QString& newPath)
{
    QDir oldDir(oldPath);
    QDir newDir(newPath);
    
    // 1. Copy journal folders (structure only, empty)
    if (!copyJournalFolders(oldPath, newPath)) {
        return false;
    }
    
    // 2. Copy machine folders with their Backup subfolder structure
    QStringList entries = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& entry : entries) {
        // Skip journal folders (already handled)
        if (entry.startsWith("Journal_")) continue;
        
        // Skip generated files/folders
        if (entry == "Summaries" || entry == "Events") continue;
        
        // This is a machine folder - create it and its Backup subfolder
        QString oldMachinePath = oldPath + "/" + entry;
        QString newMachinePath = newPath + "/" + entry;
        
        // Create machine folder
        if (!QDir().mkpath(newMachinePath)) {
            m_lastError = tr("Failed to create machine folder: %1").arg(newMachinePath);
            return false;
        }
        
        // Copy Backup subfolder recursively if it exists in source
        QString oldBackupPath = oldMachinePath + "/Backup";
        if (QDir(oldBackupPath).exists()) {
            QString newBackupPath = newMachinePath + "/Backup";
            qDebug() << "Copying Backup folder recursively:" << oldBackupPath << "to" << newBackupPath;
            if (!copyDirectoryRecursively(oldBackupPath, newBackupPath)) {
                qWarning() << "Failed to copy Backup folder recursively:" << oldBackupPath;
                // Don't fail - Backup is optional
            }
        }
    }
    
    return true;
}

bool ProfileImporter::copyDirectoryRecursively(const QString& sourcePath, const QString& destPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }
    
    QDir destDir(destPath);
    if (!destDir.exists()) {
        if (!QDir().mkpath(destPath)) {
            return false;
        }
    }
    
    // Copy all files in this directory
    QStringList files = sourceDir.entryList(QDir::Files);
    for (const QString& fileName : files) {
        QString srcFilePath = sourcePath + "/" + fileName;
        QString dstFilePath = destPath + "/" + fileName;
        
        if (!QFile::copy(srcFilePath, dstFilePath)) {
            qWarning() << "Failed to copy file:" << srcFilePath << "to" << dstFilePath;
        }
        // Don't log every file - too verbose for large backup folders
    }
    
    // Recursively copy subdirectories
    QStringList subdirs = sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& subdir : subdirs) {
        QString srcSubdirPath = sourcePath + "/" + subdir;
        QString dstSubdirPath = destPath + "/" + subdir;
        
        if (!copyDirectoryRecursively(srcSubdirPath, dstSubdirPath)) {
            qWarning() << "Failed to copy subdirectory:" << srcSubdirPath;
        }
    }
    
    return true;
}

bool ProfileImporter::copyJournalFolders(const QString& oldPath, 
                                        const QString& newPath)
{
    QDir oldDir(oldPath);
    QStringList entries = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& entry : entries) {
        if (entry.startsWith("Journal_")) {
            QDir().mkpath(newPath + "/" + entry);
        }
    }
    
    return true;
}

bool ProfileImporter::migrateMetadata(Profile* profile, const QString& oldPath)
{
    Q_UNUSED(oldPath);
    
    // machines.xml was already copied to profile folder before Profile creation
    // Now use MigrationManager to migrate it to the database
    MigrationManager migrator;
    
    // Use the Profile* overload which will read machines.xml from profile->path()
    if (!migrator.migrateProfile(profile)) {
        m_lastError = migrator.lastError();
        return false;
    }
    
    // Migration successful - remove machines.xml since all data is now in database
    QString machinesXml = profile->Get("{DataFolder}") + "/machines.xml";
    QFile::remove(machinesXml);
    
    // Reload machines from database now that they're migrated
    if (!profile->OpenMachines()) {
        m_lastError = tr("Failed to load machines from database");
        return false;
    }
    
    // Migrate journal if needed
    // Note: Journal migration is handled separately and may not be implemented yet
    // This is a non-critical feature for basic import
    
    return true;
}

bool ProfileImporter::loadSessionsFromFiles(Profile* profile, 
                                           const QString& oldPath)
{
    // NOTE: p_profile is already set by caller (importProfile)
    // We don't set or restore it here
    
    // Scan for machine folders in old profile
    QDir oldDir(oldPath);
    QStringList entries = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    int machineCount = 0;
    for (const QString& entry : entries) {
        // Skip non-machine folders
        if (entry.startsWith("Journal_") || 
            entry == "Summaries" || 
            entry == "Events") {
            continue;
        }
        machineCount++;
    }
    
    int currentMachine = 0;
    
    for (const QString& entry : entries) {
        // Skip non-machine folders
        if (entry.startsWith("Journal_") || 
            entry == "Summaries" || 
            entry == "Events") {
            continue;
        }
        
        currentMachine++;
        QString oldMachinePath = oldPath + "/" + entry;
        
        reportProgress(40 + (currentMachine * 45 / machineCount), 100, 
            tr("Loading machine %1 of %2...").arg(currentMachine).arg(machineCount));
        
        // Find corresponding machine in profile
        Machine* machine = findMachineByFolderName(profile, entry);
        if (!machine) {
            qWarning() << "Could not find machine for folder" << entry;
            qWarning() << "Available machines:" << profile->m_machlist.size();
            for (Machine* m : profile->m_machlist) {
                qWarning() << "  - Machine hexid:" << m->hexid();
            }
            continue;
        }
        
        qDebug() << "Loading sessions for machine:" << machine->hexid() << "from folder:" << entry;
        
        if (!loadMachineSessions(machine, oldMachinePath)) {
            return false;
        }
        
        qDebug() << "Machine now has" << machine->sessionlist.size() << "sessions";
        qDebug() << "Machine day list size:" << machine->day.size();
    }
    
    // Debug: Check profile's overall daylist
    qDebug() << "ProfileImporter: Total sessions loaded:" << m_loadedSessions;
    qDebug() << "ProfileImporter: Profile daylist size:" << profile->daylist.size();
    qDebug() << "ProfileImporter: Profile first day:" << profile->FirstDay();
    qDebug() << "ProfileImporter: Profile last day:" << profile->LastDay();
    
    return true;
}

bool ProfileImporter::loadMachineSessions(Machine* machine, 
                                         const QString& oldMachinePath)
{
    // Scan for .000 files in Summaries subdirectory
    QString summariesPath = oldMachinePath + "/Summaries";
    QDir summariesDir(summariesPath);
    
    if (!summariesDir.exists()) {
        qDebug() << "No Summaries folder for" << oldMachinePath;
        return true;  // Not an error - machine may have no sessions
    }
    
    QStringList filters;
    filters << "*.000";
    QFileInfoList files = summariesDir.entryInfoList(filters, QDir::Files);
    
    if (files.isEmpty()) {
        qDebug() << "No session files found in" << summariesPath;
        return true;  // Not an error
    }
    
    m_totalSessions += files.size();
    
    for (const QFileInfo& fileInfo : files) {
        // Parse session ID from filename (hex)
        QString baseName = fileInfo.baseName();
        bool ok;
        SessionID sessionId = baseName.toLongLong(&ok, 16);
        
        if (!ok) {
            qWarning() << "Invalid session filename:" << fileInfo.fileName();
            continue;
        }
        
        // Create session
        Session* session = new Session(machine, sessionId);
        
        // Load summary from .000 file
        if (!session->LoadSummaryFromFile(fileInfo.absoluteFilePath())) {
            qWarning() << "Failed to load summary:" << fileInfo.fileName();
            delete session;
            continue;
        }
        
        // Load events from .001 file (if exists)
        QString eventsPath = oldMachinePath + "/Events/" + baseName + ".001";
        if (QFile::exists(eventsPath)) {
            if (!session->LoadEventsFromFile(eventsPath)) {
                qWarning() << "Failed to load events:" << eventsPath;
                // Continue anyway - summary data is still valid
            }
        }
        
        // Save session metadata to database first
        if (!session->StoreToDatabase()) {
            qWarning() << "Failed to store session to database:" << fileInfo.fileName();
            delete session;
            continue;
        }
        
        // Now store events if they were loaded
        if (session->eventlist.size() > 0) {
            if (!session->StoreEventsToDatabase()) {
                qWarning() << "Failed to store events for session:" << fileInfo.fileName();
                // Continue anyway - session metadata is saved
            }
        }
        
        // Add to machine
        qDebug() << "ProfileImporter: Adding session" << sessionId << "to machine";
        machine->AddSession(session);
        qDebug() << "ProfileImporter: Machine now has" << machine->sessionlist.size() << "sessions";
        qDebug() << "ProfileImporter: Machine day list size:" << machine->day.size();
        
        m_loadedSessions++;
        
        // Update progress every 10 sessions or so to avoid too many updates
        if (m_loadedSessions % 10 == 0 || m_loadedSessions == m_totalSessions) {
            int progress = 40 + (m_loadedSessions * 45 / qMax(1, m_totalSessions));
            reportProgress(progress, 100, 
                tr("Loaded %1 of %2 sessions...").arg(m_loadedSessions).arg(m_totalSessions));
        }
    }
    
    return true;
}

bool ProfileImporter::calculateSummaries(Profile* profile)
{
    try {
        profile->calculateDailySummaries();
        return true;
    } catch (...) {
        qWarning() << "Exception while calculating summaries";
        return false;
    }
}

bool ProfileImporter::rollbackImport(const QString& profilePath)
{
    // Delete the partially created profile
    QDir dir(profilePath);
    if (dir.exists()) {
        bool success = dir.removeRecursively();
        if (!success) {
            qWarning() << "Failed to rollback import - could not remove" << profilePath;
        }
        return success;
    }
    return true;
}

Machine* ProfileImporter::findMachineByFolderName(Profile* profile, const QString& folderName)
{
    // Folder name format is typically "Brand_Serial" (e.g., "ResMed_22222439696")
    // Extract the serial number from the folder name
    QString serial = folderName;
    if (folderName.contains('_')) {
        serial = folderName.section('_', -1);  // Get last part after underscore
    }
    
    qDebug() << "Matching folder:" << folderName << "extracted serial:" << serial;
    
    // Try matching by serial number
    for (Machine* machine : profile->m_machlist) {
        QString machineSerial = machine->serial();
        qDebug() << "  Checking machine serial:" << machineSerial << "hexid:" << machine->hexid();
        
        if (machineSerial == serial) {
            qDebug() << "  MATCH! Found machine by serial";
            return machine;
        }
    }
    
    // Try matching by hexid (for older formats)
    for (Machine* machine : profile->m_machlist) {
        QString machineFolder = machine->hexid();
        if (machineFolder == folderName) {
            qDebug() << "  MATCH! Found machine by hexid";
            return machine;
        }
    }
    
    // Try partial match
    for (Machine* machine : profile->m_machlist) {
        QString machineSerial = machine->serial();
        QString machineFolder = machine->hexid();
        if (folderName.contains(machineSerial) || folderName.contains(machineFolder)) {
            qDebug() << "  MATCH! Found machine by partial match";
            return machine;
        }
    }
    
    // Last resort: if there's only one CPAP machine, use it
    QList<Machine*> cpapMachines;
    for (Machine* machine : profile->m_machlist) {
        if (machine->type() == MT_CPAP) {
            cpapMachines.append(machine);
        }
    }
    
    if (cpapMachines.size() == 1) {
        qDebug() << "  MATCH! Using only CPAP machine";
        return cpapMachines.first();
    }
    
    qWarning() << "Could not find machine for folder:" << folderName;
    return nullptr;
}

void ProfileImporter::reportProgress(int current, int total, const QString& message)
{
    emit progressChanged(current, total, message);
    
    if (m_progress) {
        m_progress->setMessage(message);
        m_progress->setProgressMax(total);
        m_progress->setProgressValue(current);
        
        // Process events to keep UI responsive
        QApplication::processEvents();
    }
}
