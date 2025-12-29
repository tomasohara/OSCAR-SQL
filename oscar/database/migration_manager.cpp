/* Migration Manager Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the MigrationManager class for migrating
 * legacy XML data to the SQLite database.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "migration_manager.h"
#include "profile_repository.h"
#include "machine_repository.h"
#include "user_info_repository.h"
#include "doctor_info_repository.h"
#include "preferences_repository.h"
#include "../SleepLib/profiles.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

/*
 * Constructor
 *
 * Parameters:
 *   parent - Parent QObject (optional)
 */
MigrationManager::MigrationManager(QObject* parent)
    : QObject(parent)
    , m_profileRepo(new ProfileRepository())
    , m_machineRepo(new MachineRepository())
{
}

/*
 * Destructor
 */
MigrationManager::~MigrationManager()
{
    delete m_profileRepo;
    delete m_machineRepo;
}

/*
 * Check if migration is needed for a profile
 *
 * Parameters:
 *   profilePath - Path to profile directory
 *
 * Returns: true if machines.xml exists but database has no machines
 *
 * This method checks if the legacy machines.xml file exists
 * and if the database needs to be populated.
 */
bool MigrationManager::isMigrationNeeded(const QString& profilePath)
{
    // Check if machines.xml exists
    QString machinesXmlPath = profilePath + "/machines.xml";
    if (!QFile::exists(machinesXmlPath)) {
        qDebug() << "MigrationManager: No machines.xml found";
        return false;
    }

    // Extract username from path
    QFileInfo pathInfo(profilePath);
    QString username = pathInfo.fileName();

    // Find profile in database
    ProfileData profile = m_profileRepo->findByUsername(username);

    if (profile.id == 0) {
        // Profile doesn't exist in DB yet - migration needed
        qDebug() << "MigrationManager: Profile not in database, migration needed";
        return true;
    }

    // Check if this specific profile has machines in the database
    int machineCount = m_machineRepo->count(profile.id);

    if (machineCount > 0) {
        qDebug() << "MigrationManager: Profile already has" << machineCount << "machines in database";
        return false;
    }

    qDebug() << "MigrationManager: Migration needed for profile" << username;
    return true;
}

/*
 * Migrate a profile's data from XML to database
 *
 * Parameters:
 *   profilePath - Path to profile directory
 *
 * Returns: true if successful, false otherwise
 *
 * This performs the complete migration by reading machines.xml
 * and importing all machine records into the database.
 */
bool MigrationManager::migrateProfile(const QString& profilePath)
{
    m_lastError.clear();

    emit progressChanged(0, 100, "Starting migration...");

    // Extract username from path
    QFileInfo pathInfo(profilePath);
    QString username = pathInfo.fileName();
    
    if (username.isEmpty()) {
        m_lastError = "Could not determine username from path";
        qWarning() << "MigrationManager:" << m_lastError;
        emit migrationComplete(false);
        return false;
    }

    emit progressChanged(10, 100, "Creating profile record...");

    // Get or create profile record
    qint64 profileId = getOrCreateProfile(profilePath, username);
    if (profileId < 0) {
        m_lastError = "Failed to create profile record";
        qWarning() << "MigrationManager:" << m_lastError;
        emit migrationComplete(false);
        return false;
    }

    emit progressChanged(30, 100, "Reading machines.xml...");

    // Parse machines.xml
    QString machinesXmlPath = profilePath + "/machines.xml";
    if (!parseMachinesXml(machinesXmlPath, profileId)) {
        // Error message already set by parseMachinesXml
        emit migrationComplete(false);
        return false;
    }

    emit progressChanged(80, 100, "Loading profile for extended data...");

    // Load profile to migrate extended data (user_info, doctor_info, preferences)
    Profile* tempProfile = new Profile(profilePath, true);
    if (tempProfile && tempProfile->isOpen()) {
        emit progressChanged(90, 100, "Migrating extended profile data...");
        
        // Migrate extended data
        bool extendedSuccess = migrateExtendedData(tempProfile, profileId);
        if (!extendedSuccess) {
            qWarning() << "MigrationManager: Extended data migration had errors (continuing)";
        }
    } else {
        qWarning() << "MigrationManager: Could not load profile for extended data migration";
    }
    
    delete tempProfile;

    emit progressChanged(100, 100, "Migration complete!");
    emit migrationComplete(true);

    qDebug() << "MigrationManager: Successfully migrated profile" << username;
    return true;
}

/*
 * Migrate a single profile and its machines
 *
 * Parameters:
 *   profile - Pointer to Profile object to migrate
 *
 * Returns: true if successful, false otherwise
 *
 * Alternative migration method that works with an existing Profile object.
 */
bool MigrationManager::migrateProfile(Profile* profile)
{
    if (!profile) {
        m_lastError = "Invalid profile pointer";
        qWarning() << "MigrationManager:" << m_lastError;
        return false;
    }

    // First migrate basic profile and machines
    bool basicSuccess = migrateProfile(profile->path());
    
    if (!basicSuccess) {
        return false;
    }
    
    // Get profile ID for extended data migration
    QString username = profile->user->userName();
    ProfileData profileData = m_profileRepo->findByUsername(username);
    
    if (profileData.id == 0) {
        m_lastError = "Profile not found in database after migration";
        qWarning() << "MigrationManager:" << m_lastError;
        return false;
    }
    
    // Migrate extended data (user info, doctor info, preferences)
    emit progressChanged(90, 100, "Migrating extended profile data...");
    bool extendedSuccess = migrateExtendedData(profile, profileData.id);
    
    if (!extendedSuccess) {
        qWarning() << "MigrationManager: Extended data migration failed:" << m_lastError;
        // Don't fail the entire migration if extended data fails
    }
    
    emit progressChanged(100, 100, "Migration complete!");
    return true;
}

/*
 * Migrate all profiles in the OSCAR data directory
 *
 * Parameters:
 *   profilesPath - Path to Profiles directory
 *
 * Returns: Number of profiles successfully migrated
 *
 * This scans the Profiles directory and migrates all subdirectories
 * that contain a machines.xml file. Each subdirectory is treated as
 * a separate profile.
 */
int MigrationManager::migrateAllProfiles(const QString& profilesPath)
{
    qDebug() << "MigrationManager: Scanning for profiles in" << profilesPath;

    QDir profilesDir(profilesPath);
    if (!profilesDir.exists()) {
        m_lastError = QString("Profiles directory does not exist: %1").arg(profilesPath);
        qWarning() << "MigrationManager:" << m_lastError;
        return 0;
    }

    // Get all subdirectories (each is a profile)
    profilesDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    QFileInfoList profileFolders = profilesDir.entryInfoList();

    if (profileFolders.isEmpty()) {
        qDebug() << "MigrationManager: No profile folders found";
        return 0;
    }

    int successCount = 0;
    int totalProfiles = profileFolders.size();

    qDebug() << "MigrationManager: Found" << totalProfiles << "profile folders";

    for (int i = 0; i < profileFolders.size(); i++) {
        const QFileInfo& profileInfo = profileFolders.at(i);
        QString profilePath = profileInfo.absoluteFilePath();
        QString profileName = profileInfo.fileName();

        qDebug() << "MigrationManager: Checking profile" << (i+1) << "of" << totalProfiles << ":" << profileName;

        // Check if migration is needed for this profile
        if (!isMigrationNeeded(profilePath)) {
            qDebug() << "MigrationManager: Skipping profile" << profileName << "(no migration needed)";
            continue;
        }

        // Emit overall progress
        int overallProgress = (i * 100) / totalProfiles;
        emit progressChanged(overallProgress, 100, 
            QString("Migrating profile %1 of %2: %3").arg(i+1).arg(totalProfiles).arg(profileName));

        // Migrate this profile
        if (migrateProfile(profilePath)) {
            successCount++;
            qDebug() << "MigrationManager: Successfully migrated profile" << profileName;
        } else {
            qWarning() << "MigrationManager: Failed to migrate profile" << profileName << ":" << m_lastError;
        }
    }

    // Final progress update
    emit progressChanged(100, 100, 
        QString("Migration complete: %1 of %2 profiles migrated").arg(successCount).arg(totalProfiles));

    qDebug() << "MigrationManager: Migrated" << successCount << "of" << totalProfiles << "profiles";
    return successCount;
}

/*
 * Get or create profile database record
 *
 * Parameters:
 *   profilePath - Path to profile directory
 *   username - Username for the profile
 *
 * Returns: Profile database ID, or -1 on error
 *
 * This checks if the profile exists in the database and creates
 * it if needed.
 *
 * Note: Stores the path as "%PROFDIR%/username" for portability.
 * %PROFDIR% should be replaced with GetAppData()+"/Profiles" when reading.
 */
qint64 MigrationManager::getOrCreateProfile(const QString& profilePath, const QString& username)
{
    Q_UNUSED(profilePath)
    // Check if profile already exists
    ProfileData existing = m_profileRepo->findByUsername(username);
    if (existing.id > 0) {
        qDebug() << "MigrationManager: Profile" << username << "already exists with id" << existing.id;
        return existing.id;
    }

    // Create new profile record with portable path
    // Store as "%PROFDIR%/username" instead of full path
    // This allows the database to be portable across different machines
    ProfileData newProfile;
    newProfile.username = username;
    newProfile.dataFolder = QString("%PROFDIR%/") + username;

    qint64 profileId = m_profileRepo->create(newProfile);
    if (profileId < 0) {
        m_lastError = "Failed to create profile in database";
        return -1;
    }

    qDebug() << "MigrationManager: Created profile" << username << "with id" << profileId 
             << "path:" << newProfile.dataFolder;
    return profileId;
}

/*
 * Read and parse machines.xml file
 *
 * Parameters:
 *   machinesXmlPath - Full path to machines.xml
 *   profileId - Database ID of the profile
 *
 * Returns: true if successful, false otherwise
 *
 * This reads the XML file and populates machine records into the database.
 * The XML format is:
 * <machines>
 *   <machine id="..." type="..." class="...">
 *     <brand>...</brand>
 *     <model>...</model>
 *     <serial>...</serial>
 *     ...
 *   </machine>
 * </machines>
 */
bool MigrationManager::parseMachinesXml(const QString& machinesXmlPath, qint64 profileId)
{
    QFile file(machinesXmlPath);
    if (!file.open(QFile::ReadOnly)) {
        m_lastError = QString("Could not open %1: %2").arg(machinesXmlPath).arg(file.errorString());
        qWarning() << "MigrationManager:" << m_lastError;
        return false;
    }

    QDomDocument doc("machines.xml");
    if (!doc.setContent(&file)) {
        m_lastError = "Invalid XML content in machines.xml";
        qWarning() << "MigrationManager:" << m_lastError;
        file.close();
        return false;
    }
    file.close();

    QDomElement root = doc.firstChild().toElement();
    if (root.tagName().toLower() != "machines") {
        m_lastError = "No machines tag in machines.xml";
        qWarning() << "MigrationManager:" << m_lastError;
        return false;
    }

    int machineCount = 0;
    int errorCount = 0;
    QDomElement machineElem = root.firstChildElement();

    while (!machineElem.isNull()) {
        if (machineElem.tagName().toLower() == "machine") {
            // Parse machine attributes
            bool ok;
            qint64 machineId = machineElem.attribute("id", "0").toLongLong(&ok);
            if (!ok) machineId = 0;

            int machineType = machineElem.attribute("type", "0").toInt(&ok);
            if (!ok) machineType = 0;

            QString loaderName = machineElem.attribute("class", "");

            // Parse machine properties
            MachineData machine;
            machine.profileId = profileId;
            machine.machineId = machineId;
            machine.machineType = machineType;
            machine.loaderName = loaderName;

            // JSON object to store nested properties
            QJsonObject propertiesJson;

            QDomElement propElem = machineElem.firstChildElement();
            while (!propElem.isNull()) {
                QString key = propElem.tagName().toLower();
                QString value = propElem.text();

                if (key == "brand") {
                    machine.brand = value;
                } else if (key == "model") {
                    machine.model = value;
                } else if (key == "series") {
                    machine.series = value;
                } else if (key == "serial") {
                    machine.serialNumber = value;
                } else if (key == "modelnumber") {
                    machine.modelNumber = value;
                } else if (key == "dataversion") {
                    machine.dataVersion = value.toInt();
                } else if (key == "lastimported") {
                    machine.lastImported = value;
                } else if (key == "purgedate") {
                    machine.purgeDate = value;
                } else if (key == "properties") {
                    // Parse nested properties element
                    QDomElement nestedProp = propElem.firstChildElement();
                    while (!nestedProp.isNull()) {
                        QString propKey = nestedProp.tagName();
                        QString propValue = nestedProp.text();
                        propertiesJson[propKey] = propValue;
                        nestedProp = nestedProp.nextSiblingElement();
                    }
                }

                propElem = propElem.nextSiblingElement();
            }

            // Convert properties JSON to string for storage
            if (!propertiesJson.isEmpty()) {
                QJsonDocument doc(propertiesJson);
                machine.properties = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
            }

            // Insert into database
            qint64 dbId = m_machineRepo->create(machine);
            if (dbId > 0) {
                machineCount++;
                qDebug() << "MigrationManager: Migrated machine" << machine.brand << machine.model 
                         << "serial" << machine.serialNumber;
            } else {
                errorCount++;
                qWarning() << "MigrationManager: Failed to migrate machine" << machine.brand << machine.model;
            }

            // Update progress (40-90% range for machine migration)
            int progress = 40 + (machineCount * 50 / qMax(1, machineCount + errorCount));
            emit progressChanged(progress, 100, 
                QString("Migrated %1 machines...").arg(machineCount));
        }

        machineElem = machineElem.nextSiblingElement();
    }

    if (errorCount > 0) {
        m_lastError = QString("Migrated %1 machines with %2 errors").arg(machineCount).arg(errorCount);
        qWarning() << "MigrationManager:" << m_lastError;
    }

    qDebug() << "MigrationManager: Successfully migrated" << machineCount << "machines";
    return (machineCount > 0 || errorCount == 0);
}

/*
 * Migrate extended profile data
 *
 * Parameters:
 *   profile - Pointer to Profile object with loaded data
 *   profileId - Database ID of the profile
 *
 * Returns: true if successful, false otherwise
 *
 * This migrates user info, doctor info, and all preferences from
 * the Profile object into the extended database tables.
 */
bool MigrationManager::migrateExtendedData(Profile* profile, qint64 profileId)
{
    if (!profile) {
        m_lastError = "Invalid profile pointer for extended data migration";
        return false;
    }
    
    bool success = true;
    
    // Migrate user info
    if (profile->user) {
        UserInfoRepository userRepo;
        if (!userRepo.saveFromUserInfo(profileId, profile->user)) {
            qWarning() << "MigrationManager: Failed to migrate user info";
            m_lastError = "Failed to migrate user info";
            success = false;
        } else {
            qDebug() << "MigrationManager: Migrated user info";
        }
    }
    
    // Migrate doctor info
    if (profile->doctor) {
        DoctorInfoRepository doctorRepo;
        if (!doctorRepo.saveFromDoctorInfo(profileId, profile->doctor)) {
            qWarning() << "MigrationManager: Failed to migrate doctor info";
            m_lastError = "Failed to migrate doctor info";
            success = false;
        } else {
            qDebug() << "MigrationManager: Migrated doctor info";
        }
    }
    
    // Migrate all preferences
    PreferencesRepository prefRepo;
    if (!prefRepo.saveAllPreferences(profileId, 
                                     profile->cpap,
                                     profile->oxi,
                                     profile->session,
                                     profile->appearance,
                                     profile->general)) {
        qWarning() << "MigrationManager: Failed to migrate preferences";
        m_lastError = "Failed to migrate preferences";
        success = false;
    } else {
        qDebug() << "MigrationManager: Migrated all preferences";
    }
    
    if (success) {
        qDebug() << "MigrationManager: Successfully migrated extended data";
    }
    
    return success;
}
