/* Profile Importer Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

//#define DBDEBUG

#include "profileimporter.h"
#include "SleepLib/profiles.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"
#include "SleepLib/journal.h"
#include "SleepLib/progressdialog.h"
#include "database/migration_manager.h"
#include "database/database_manager.h"
#include "database/preferences_repository.h"
#include "database/profile_repository.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
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
    
    // ===== BEGIN SINGLE TRANSACTION FOR ENTIRE IMPORT =====
    // Wrap the entire import process in one transaction for maximum performance
    // If anything fails, everything rolls back cleanly
    DatabaseManager& dbMgr = DatabaseManager::instance();
    if (!dbMgr.transaction()) {
        m_lastError = tr("Failed to begin database transaction: %1").arg(dbMgr.lastError().text());
        return false;
    }
    
    qDebug() << "ProfileImporter: Started database transaction for entire import";
    
    // Create new profile folder
    QString newPath = GetAppData() + "/Profiles/" + newProfileName;
    QDir().mkpath(newPath);
    
    reportProgress(10, 100, tr("Copying profile structure..."));
    
    if (!copyProfileStructure(sourcePath, newPath)) {
        dbMgr.rollback();
        qWarning() << "ProfileImporter: Rolled back transaction due to copyProfileStructure failure";
        rollbackImport(newPath);
        return false;
    }
    
    // Copy machines.xml BEFORE creating Profile so MigrationManager can find it
    QString sourceMachinesXml = sourcePath + "/machines.xml";
    QString destMachinesXml = newPath + "/machines.xml";
    if (!QFile::copy(sourceMachinesXml, destMachinesXml)) {
        m_lastError = tr("Failed to copy machines.xml");
        dbMgr.rollback();
        qWarning() << "ProfileImporter: Rolled back transaction due to machines.xml copy failure";
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
        dbMgr.rollback();
        qWarning() << "ProfileImporter: Rolled back transaction due to migrateMetadata failure";
        rollbackImport(newPath);
        delete profile;
        return false;
    }
    
    reportProgress(30, 100, tr("Copying user information..."));
    
    // IMPORTANT: Copy user data AFTER migration to prevent MigrationManager from overwriting it
    // Load source profile to copy user data
    Profile* sourceProfile = new Profile(sourcePath, true);
    if (sourceProfile && sourceProfile->isOpen()) {
//        qDebug() << "ProfileImporter: Source profile opened successfully";
//        qDebug() << "ProfileImporter: Source user firstname:" << sourceProfile->user->firstName();
//        qDebug() << "ProfileImporter: Source user lastname:" << sourceProfile->user->lastName();
//        qDebug() << "ProfileImporter: Source doctor name:" << sourceProfile->doctor->name();
        
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
        // Copy preferences (cpap, oxi, session, appearance, general) from source profile.
        // migrateExtendedData() runs on the freshly-created destination profile (which holds
        // only defaults), so we must explicitly overwrite with the source values here.
        ProfileRepository profileRepo;
        ProfileData profileData = profileRepo.findByUsername(newProfileName);
        if (profileData.id > 0) {
            PreferencesRepository prefRepo;
            if (!prefRepo.saveAllPreferences(profileData.id,
                                             sourceProfile->cpap,
                                             sourceProfile->oxi,
                                             sourceProfile->session,
                                             sourceProfile->appearance,
                                             sourceProfile->general)) {
                qWarning() << "ProfileImporter: Failed to save source preferences to database";
            }
        } else {
            qWarning() << "ProfileImporter: Could not find destination profile in database to save preferences";
        }
        
//        qDebug() << "ProfileImporter: After copy - user firstname:" << profile->user->firstName();
//        qDebug() << "ProfileImporter: After copy - user lastname:" << profile->user->lastName();
//        qDebug() << "ProfileImporter: After copy - doctor name:" << profile->doctor->name();
        
        // Note: We'll save user/doctor info in Profile::Save() below
        // Don't save here because profile might not be in database yet
    } else {
        qWarning() << "ProfileImporter: Failed to open source profile or profile not open";
    }
    delete sourceProfile;
    
    reportProgress(40, 100, tr("Loading session data from files..."));

    // CRITICAL: Initialize schema before loading sessions
    // This populates schema::channel[] with channel types so extractRespiratoryEvents() works
    schema::init();

    // IMPORTANT: Set p_profile before loading sessions - Session objects need it.
    // NOTE: p_profile is a global that is temporarily replaced here. reportProgress()
    // calls QApplication::processEvents(), which can dispatch Qt events while the swap
    // is active. This is safe in practice because the import dialog is modal (blocking
    // UI interaction with the main window) and no known timer touches p_profile during
    // import. If future timers or background tasks are added that read p_profile, this
    // swap should be eliminated and progress updates driven via queued signals instead.
    extern Profile* p_profile;
    Profile* savedProfile = p_profile;
    p_profile = profile;

    bool sessionsLoaded = loadSessionsFromFiles(profile, sourcePath);
    
    if (!sessionsLoaded) {
        p_profile = savedProfile;  // Restore before failing
        dbMgr.rollback();
        qWarning() << "ProfileImporter: Rolled back transaction due to loadSessionsFromFiles failure";
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
    if (!profile->Save()) {
        m_lastError = tr("Failed to save profile to database");
        qWarning() << "ProfileImporter: Rolled back transaction due to profile Save() failure";
        dbMgr.rollback();
        p_profile = savedProfile;
        rollbackImport(newPath);
        delete profile;
        return false;
    }

    // ===== COMMIT THE ENTIRE TRANSACTION =====
    // All database operations succeeded - commit everything at once
    if (!dbMgr.commit()) {
        m_lastError = tr("Failed to commit database transaction: %1").arg(dbMgr.lastError().text());
        qWarning() << "ProfileImporter: Failed to commit transaction - rolling back";
        dbMgr.rollback();
        p_profile = savedProfile;
        rollbackImport(newPath);
        delete profile;
        return false;
    }
    
//    qDebug() << "ProfileImporter: Successfully committed entire import transaction";
//    qDebug() << "ProfileImporter: Import completed - all data saved to database";

    // Initialize channels for this profile immediately after import.
    // Without this, channels are only populated when the profile is first opened
    // via LoadMachineData(), which means bulk-imported profiles show empty
    // channels/channel_options tables until the user manually opens each one.
    if (!profile->initializeChannelsFromSchema()) {
        qWarning() << "ProfileImporter: Failed to initialize channels for" << newProfileName
                   << "- they will be initialized when the profile is first opened";
    }

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

    // 2. Copy graph layout files from profile root (daily.shg, overview.shg, etc.)
    QStringList shgFiles = oldDir.entryList(QStringList() << "*.shg", QDir::Files);
    for (const QString& shgFile : shgFiles) {
        if (!QFile::copy(oldPath + "/" + shgFile, newPath + "/" + shgFile)) {
            qWarning() << "ProfileImporter::copyProfileStructure: Failed to copy" << shgFile;
            // Not fatal - graph layout will revert to defaults
        }
    }
    
    // 3. Copy machine folders with their Backup subfolder structure
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
//            qDebug() << "Copying Backup folder recursively:" << oldBackupPath << "to" << newBackupPath;
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
            // Create empty journal folder structure (files will be read from source and migrated to database)
            QString newJournalPath = newPath + "/" + entry;
            QDir().mkpath(newJournalPath);
            
            // Create Summaries subdirectory (empty - journal data goes to database)
            QString newSummariesPath = newJournalPath + "/Summaries";
            QDir().mkpath(newSummariesPath);
        }
    }
    
    return true;
}

bool ProfileImporter::migrateMetadata(Profile* profile, const QString& oldPath)
{
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
    
    // Migrate journal data from source directory (read directly, don't copy files)
    if (!migrateJournalFromSource(profile, oldPath)) {
        qWarning() << "ProfileImporter::migrateMetadata() - Journal migration failed, but continuing";
        // Don't fail the import - journal is not critical
    }
    
    return true;
}

bool ProfileImporter::migrateJournalFromSource(Profile* profile, const QString& sourcePath)
{
//    qDebug() << "ProfileImporter::migrateJournalFromSource() - Starting";
    
    // Get journal machine
    Machine* journalMachine = profile->GetMachine(MT_JOURNAL);
    if (!journalMachine) {
        qDebug() << "ProfileImporter::migrateJournalFromSource() - No journal machine";
        return true;  // Not an error - profile may not have journal
    }
    
    // Verify machine is in database
    qint64 machineDbId = journalMachine->getDatabaseId();
    if (machineDbId == 0) {
        qWarning() << "ProfileImporter::migrateJournalFromSource() - Journal machine not in database";
        return false;
    }
    
//    qDebug() << "ProfileImporter::migrateJournalFromSource() - Journal machine database ID:" << machineDbId;
    
    // Find journal folder in source directory
    QDir sourceDir(sourcePath);
    QStringList entries = sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    QString journalFolderName;
    for (const QString& entry : entries) {
        if (entry.startsWith("Journal_")) {
            journalFolderName = entry;
            break;
        }
    }
    
    if (journalFolderName.isEmpty()) {
        qDebug() << "ProfileImporter::migrateJournalFromSource() - No journal folder in source";
        return true;  // Not an error - no journal to migrate
    }
    
    // Read journal .000 files from SOURCE directory
    QString sourceSummariesPath = sourcePath + "/" + journalFolderName + "/Summaries";
    QDir dir(sourceSummariesPath);
    
    if (!dir.exists()) {
        qDebug() << "ProfileImporter::migrateJournalFromSource() - No summaries directory in source";
        return true;  // Not an error
    }
    
    QStringList filters;
    filters << "*.000";
    dir.setNameFilters(filters);
    QStringList files = dir.entryList(QDir::Files);
    
    if (files.isEmpty()) {
//        qDebug() << "ProfileImporter::migrateJournalFromSource() - No .000 files found in source";
        return true;  // Not an error
    }
    
//    qDebug() << "ProfileImporter::migrateJournalFromSource() - Found" << files.size() << ".000 files to migrate";
    
    // Migrate each file
    int migratedCount = 0;
    int errorCount = 0;
    
    for (const QString& filename : files) {
        // Parse date from filename
        QString baseName = filename.section(".", 0, -2);  // Remove .000 extension
        bool ok;
        SessionID sessionId = baseName.toLongLong(&ok, 16);
        
        if (!ok) {
            qWarning() << "ProfileImporter::migrateJournalFromSource() - Invalid filename format:" << filename;
            errorCount++;
            continue;
        }
        
        QDateTime dateTime = QDateTime::fromSecsSinceEpoch(sessionId);
        QDate date = dateTime.date();
        if (!date.isValid()) {
            qWarning() << "ProfileImporter::migrateJournalFromSource() - Invalid date for session:" << sessionId;
            errorCount++;
            continue;
        }
        
        // Load the .000 file from SOURCE directory
        QString fullPath = sourceSummariesPath + "/" + filename;
        
        // Create session object
        Session* sess = new Session(journalMachine, sessionId);
        
        // Load the session data from the file (this populates settings, first, last, etc.)
        if (!sess->LoadSummaryFromFile(fullPath)) {
            qWarning() << "ProfileImporter::migrateJournalFromSource() - Failed to load file" << fullPath;
            delete sess;
            errorCount++;
            continue;
        }
        
        // IMPORTANT: Store session to database with the machine's database ID
        // The session must be associated with the correct machine_id for foreign keys to work
        if (!sess->StoreToDatabase()) {
            qWarning() << "ProfileImporter::migrateJournalFromSource() - Failed to store to database for" << date.toString();
            delete sess;
            errorCount++;
            continue;
        }
        
//        qDebug() << "ProfileImporter::migrateJournalFromSource() - Migrated" << date.toString() << "to database";
        migratedCount++;
        
        delete sess;
    }
    
//    qDebug() << "ProfileImporter::migrateJournalFromSource() - Migration complete:"
//             << migratedCount << "sessions migrated,"
//             << errorCount << "errors";
    
    return (errorCount == 0);
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

    // Prevent possible divide by zero error if this should-be-impossible condition occurs
    if (machineCount == 0) {
        m_lastError = tr("No machine folders found in source profile");
        return false;
    }
    
    int currentMachine = 0;
    QStringList skippedFolders;

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
                qWarning() << "  - Machine hexid:" << m->hexid() << "serial:" << m->serial()
                           << "loader:" << m->loaderName() << "type:" << m->type();
            }
            skippedFolders << entry;
            continue;
        }

//        qDebug() << "Loading sessions for machine:" << machine->hexid() << "from folder:" << entry;

        if (!loadMachineSessions(machine, oldMachinePath)) {
            return false;
        }

//        qDebug() << "Machine now has" << machine->sessionlist.size() << "sessions";
//        qDebug() << "Machine day list size:" << machine->day.size();
    }

    if (!skippedFolders.isEmpty()) {
        // Second pass: process-of-elimination matching.
        // Collect machines that were not matched by any folder in the first pass.
        QSet<Machine*> matchedMachines;
        for (const QString& entry : entries) {
            if (entry.startsWith("Journal_") || entry == "Summaries" || entry == "Events") {
                continue;
            }
            if (skippedFolders.contains(entry)) {
                continue;  // This folder failed to match
            }
            Machine* m = findMachineByFolderName(profile, entry);
            if (m) {
                matchedMachines.insert(m);
            }
        }

        QList<Machine*> unmatchedMachines;
        for (Machine* m : profile->m_machlist) {
            if (!matchedMachines.contains(m)) {
                unmatchedMachines.append(m);
            }
        }

        // If the number of unmatched machines equals the number of skipped folders,
        // try to assign them by machine type.
        if (unmatchedMachines.size() == skippedFolders.size()) {
            QStringList stillSkipped;
            for (const QString& skipped : skippedFolders) {
                QString oldMachinePath = oldPath + "/" + skipped;
                // Find an unmatched machine of type MT_CPAP (ResMed folders are always CPAP)
                Machine* assigned = nullptr;
                for (Machine* candidate : unmatchedMachines) {
                    if (candidate->type() == MT_CPAP) {
                        assigned = candidate;
                        break;
                    }
                }
                if (!assigned && !unmatchedMachines.isEmpty()) {
                    assigned = unmatchedMachines.first();
                }
                if (assigned) {
                    qWarning() << "ProfileImporter: Using process-of-elimination to assign folder"
                               << skipped << "to machine serial:" << assigned->serial()
                               << "hexid:" << assigned->hexid();
                    unmatchedMachines.removeOne(assigned);
                    if (!loadMachineSessions(assigned, oldMachinePath)) {
                        return false;
                    }
                } else {
                    stillSkipped << skipped;
                }
            }
            if (!stillSkipped.isEmpty()) {
                m_lastError = tr("Could not match machine folder(s) to imported profile: %1")
                                  .arg(stillSkipped.join(", "));
                return false;
            }
        } else {
            m_lastError = tr("Could not match machine folder(s) to imported profile: %1")
                              .arg(skippedFolders.join(", "));
            return false;
        }
    }

    // Debug: Check profile's overall daylist
//    qDebug() << "ProfileImporter: Total sessions loaded:" << m_loadedSessions;
//    qDebug() << "ProfileImporter: Profile daylist size:" << profile->daylist.size();
//    qDebug() << "ProfileImporter: Profile first day:" << profile->FirstDay();
//    qDebug() << "ProfileImporter: Profile last day:" << profile->LastDay();

    return true;
}

bool ProfileImporter::loadMachineSessions(Machine* machine, 
                                         const QString& oldMachinePath)
{
    // Scan for .000 files in Summaries subdirectory
    QString summariesPath = oldMachinePath + "/Summaries";
    QDir summariesDir(summariesPath);
    
    if (!summariesDir.exists()) {
        qDebug() << "ProfileImporter::loadMachineSessions: No Summaries folder for" << oldMachinePath;
        return true;  // Not an error - machine may have no sessions
    }
    
    QStringList filters;
    filters << "*.000";
    QFileInfoList files = summariesDir.entryInfoList(filters, QDir::Files);
    
    if (files.isEmpty()) {
        qDebug() << "ProfileImporter::loadMachineSessions: No session files found in" << summariesPath;
        return true;  // Not an error
    }
    
    m_totalSessions += files.size();

    int sessionFailures = 0;
    int eventFailures = 0;

    for (const QFileInfo& fileInfo : files) {
        // Parse session ID from filename (hex)
        QString baseName = fileInfo.baseName();
        bool ok;
        SessionID sessionId = baseName.toLongLong(&ok, 16);

        if (!ok) {
            qWarning() << "ProfileImporter::loadMachineSessions: Invalid session filename:" << fileInfo.fileName();
            continue;
        }

        // Create session
        Session* session = new Session(machine, sessionId);

        // Load summary from .000 file
        if (!session->LoadSummaryFromFile(fileInfo.absoluteFilePath())) {
            qWarning() << "ProfileImporter::loadMachineSessions: Failed to load summary:" << fileInfo.fileName();
            delete session;
            continue;
        }

        // Load events from .001 file (if exists)
        QString eventsPath = oldMachinePath + "/Events/" + baseName + ".001";
        if (QFile::exists(eventsPath)) {
            if (!session->LoadEventsFromFile(eventsPath)) {
                qWarning() << "ProfileImporter::loadMachineSessions: Failed to load events:" << eventsPath;
                // Continue anyway - summary data is still valid
            }
        }

        // Save session metadata to database
        if (!session->StoreToDatabase()) {
            qWarning() << "ProfileImporter::loadMachineSessions: Failed to store session to database:" << fileInfo.fileName();
            delete session;
            sessionFailures++;
            continue;
        }

        // Store events if they were loaded
        if (session->eventlist.size() > 0) {
            if (!session->StoreEventsToDatabase()) {
                qWarning() << "ProfileImporter::loadMachineSessions: Failed to store events for session:" << fileInfo.fileName();
                eventFailures++;
            }
        }
        
        // Add to machine
//        qDebug() << "ProfileImporter: Adding session" << sessionId << "to machine";
        machine->AddSession(session);
//        qDebug() << "ProfileImporter: Machine now has" << machine->sessionlist.size() << "sessions";
//        qDebug() << "ProfileImporter: Machine day list size:" << machine->day.size();
        
        m_loadedSessions++;
        
        // Update progress every 10 sessions or so to avoid too many updates
        if (m_loadedSessions % 10 == 0 || m_loadedSessions == m_totalSessions) {
            int progress = 40 + (m_loadedSessions * 45 / qMax(1, m_totalSessions));
            reportProgress(progress, 100,
                tr("Loaded %1 of %2 sessions...").arg(m_loadedSessions).arg(m_totalSessions));
        }
    }

    if (sessionFailures > 0 || eventFailures > 0) {
        m_lastError = tr("Session persistence failures: %1 session(s) and %2 event set(s) failed to store")
                          .arg(sessionFailures).arg(eventFailures);
        return false;
    }

    return true;
}

bool ProfileImporter::calculateSummaries(Profile* profile)
{
    try {
        profile->calculateDailySummaries();
        return true;
    } catch (...) {
        qWarning() << "ProfileImporter::calculateSummaries: Exception while calculating summaries";
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
    
#ifdef DBDEBUG
    qDebug() << "Matching folder:" << folderName << "extracted serial:" << serial;
#endif

    // Try matching by serial number
    for (Machine* machine : profile->m_machlist) {
        QString machineSerial = machine->serial();
#ifdef DBDEBUG
        qDebug() << "  Checking machine serial:" << machineSerial << "hexid:" << machine->hexid();
#endif
        if (machineSerial == serial) {
#ifdef DBDEBUG
            qDebug() << "  MATCH! Found machine by serial";
#endif
            return machine;
        }
    }

    // Try matching by hexid (for older formats)
    for (Machine* machine : profile->m_machlist) {
        QString machineFolder = machine->hexid();
        if (machineFolder == folderName) {
#ifdef DBDEBUG
            qDebug() << "  MATCH! Found machine by hexid";
#endif
            return machine;
        }
    }

    // Try partial match
    for (Machine* machine : profile->m_machlist) {
        QString machineSerial = machine->serial();
        QString machineFolder = machine->hexid();
        if (folderName.contains(machineSerial) || folderName.contains(machineFolder)) {
#ifdef DBDEBUG
            qDebug() << "  MATCH! Found machine by partial match";
#endif
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
#ifdef DBDEBUG
        qDebug() << "  MATCH! Using only CPAP machine";
#endif
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
