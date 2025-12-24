# OSCAR Database Integration Guide

## Overview

This guide explains how to integrate the new SQLite database into OSCAR's existing codebase. The approach is designed to be **incremental and safe** - the database can coexist with XML files during the transition period.

---

## Integration Strategy: Hybrid Approach

### Phase A: Parallel Operation (Safest)
- XML files remain primary data source
- Database is populated but not used for reading
- Allows extensive testing without risk
- Easy rollback if issues occur

### Phase B: Database-First with XML Fallback
- Read from database first
- Fall back to XML if database empty
- Provides safety net during transition
- **RECOMMENDED STARTING POINT**

### Phase C: Database-Only
- All reads/writes use database
- XML files kept for backup/export only
- Full database-driven system

---

## Integration Points

### 1. Profile Machine Loading

**Current Code** (`SleepLib/profiles.cpp`):
```cpp
bool Profile::OpenMachines()
{
    QString filename = p_path+"machines.xml";
    // ... parse XML and create Machine objects
}
```

**Integrated Version** (Database-first with XML fallback):
```cpp
#include "database/machine_repository.h"
#include "database/profile_repository.h"

bool Profile::OpenMachines()
{
    // Try database first
    if (loadMachinesFromDatabase()) {
        qDebug() << "Profile: Loaded machines from database";
        return true;
    }
    
    // Fall back to XML
    qDebug() << "Profile: Falling back to XML for machines";
    return loadMachinesFromXML();  // Existing code
}

bool Profile::loadMachinesFromDatabase()
{
    ProfileRepository profileRepo;
    MachineRepository machineRepo;
    
    // Find this profile in database
    ProfileData profileData = profileRepo.findByUsername(user->userName());
    if (profileData.id == 0) {
        return false;  // Profile not in database yet
    }
    
    // Get all machines for this profile
    QList<MachineData> machines = machineRepo.findByProfile(profileData.id);
    if (machines.isEmpty()) {
        return false;  // No machines in database
    }
    
    // Create Machine objects from database
    for (const MachineData& data : machines) {
        MachineInfo info;
        info.type = (MachineType)data.machineType;
        info.loadername = data.loaderName;
        info.brand = data.brand;
        info.model = data.model;
        info.series = data.series;
        info.serial = data.serialNumber;
        info.modelnumber = data.modelNumber;
        info.lastimported = QDateTime::fromString(data.lastImported, Qt::ISODate);
        info.purgeDate = QDate::fromString(data.purgeDate, Qt::ISODate);
        info.version = data.dataVersion;
        
        // Parse properties from JSON
        if (!data.properties.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(data.properties.toUtf8());
            if (doc.isObject()) {
                QJsonObject props = doc.object();
                for (auto it = props.begin(); it != props.end(); ++it) {
                    info.properties[it.key()] = it.value().toString();
                }
            }
        }
        
        // Create machine
        Machine* m = CreateMachine(info, data.machineId);
        if (m) {
            AddMachine(m);
        }
    }
    
    return !m_machlist.isEmpty();
}

bool Profile::loadMachinesFromXML()
{
    // Existing OpenMachines() code goes here
    QString filename = p_path+"machines.xml";
    // ... rest of existing code
}
```

---

### 2. Profile Machine Saving

**Current Code**:
```cpp
bool Profile::StoreMachines()
{
    // ... write machines.xml
}
```

**Integrated Version** (Write to both database and XML):
```cpp
bool Profile::StoreMachines()
{
    bool xmlSuccess = storeMachinesToXML();    // Keep XML for backup
    bool dbSuccess = storeMachinesToDatabase(); // Also save to database
    
    return xmlSuccess || dbSuccess;  // Success if either works
}

bool Profile::storeMachinesToDatabase()
{
    ProfileRepository profileRepo;
    MachineRepository machineRepo;
    
    // Get or create profile record
    ProfileData profileData = profileRepo.findByUsername(user->userName());
    qint64 profileId = profileData.id;
    
    if (profileId == 0) {
        // Create profile record
        ProfileData newProfile;
        newProfile.username = user->userName();
        newProfile.dataFolder = QString("%PROFDIR%/") + user->userName();
        profileId = profileRepo.create(newProfile);
        
        if (profileId < 0) {
            qWarning() << "Profile: Failed to create database profile";
            return false;
        }
    }
    
    // Update/insert each machine
    for (Machine* m : m_machlist) {
        // Check if machine exists in database
        MachineData existing = machineRepo.findByProfileAndMachineId(profileId, m->id());
        
        MachineData machineData;
        machineData.profileId = profileId;
        machineData.machineId = m->id();
        machineData.loaderName = m->loaderName();
        machineData.machineType = m->type();
        machineData.brand = m->brand();
        machineData.model = m->model();
        machineData.series = m->series();
        machineData.serialNumber = m->serial();
        machineData.modelNumber = m->modelnumber();
        machineData.lastImported = m->lastImported().toString(Qt::ISODate);
        machineData.purgeDate = m->purgeDate().toString(Qt::ISODate);
        machineData.dataVersion = m->version();
        
        // Convert properties to JSON
        if (!m->info.properties.isEmpty()) {
            QJsonObject propsJson;
            for (auto it = m->info.properties.begin(); it != m->info.properties.end(); ++it) {
                propsJson[it.key()] = it.value();
            }
            QJsonDocument doc(propsJson);
            machineData.properties = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        }
        
        if (existing.id > 0) {
            // Update existing
            machineData.id = existing.id;
            machineRepo.update(machineData);
        } else {
            // Insert new
            machineRepo.create(machineData);
        }
    }
    
    return true;
}

bool Profile::storeMachinesToXML()
{
    // Existing StoreMachines() code goes here
}
```

---

### 3. Automatic Migration on Startup

**Where**: `main.cpp` after database initialization

```cpp
#include "database/migration_manager.h"

int main(int argc, char *argv[])
{
    // ... existing initialization ...
    
    // Initialize database
    QString dbPath = GetAppData() + "/oscar.db";
    if (DatabaseManager::instance().initialize(dbPath)) {
        qDebug() << "Database initialized";
        
        // Auto-migrate all profiles on first run
        MigrationManager migrator;
        QString profilesPath = GetAppData() + "/Profiles";
        
        if (QDir(profilesPath).exists()) {
            int count = migrator.migrateAllProfiles(profilesPath);
            if (count > 0) {
                qDebug() << "Auto-migrated" << count << "profiles to database";
            }
        }
    }
    
    // ... rest of existing code ...
}
```

---

### 4. Profile Listing

**Current Code** (`Profiles::Scan()`):
```cpp
void Profiles::Scan()
{
    QString path = p_pref->Get("{home}/Profiles");
    // ... scan directories ...
}
```

**Integrated Version** (Hybrid - use database if available):
```cpp
void Profiles::Scan()
{
    // Try database first
    ProfileRepository profileRepo;
    QList<ProfileData> dbProfiles = profileRepo.findAll();
    
    if (!dbProfiles.isEmpty()) {
        qDebug() << "Loading profiles from database";
        QString profilesBase = p_pref->Get("{home}/Profiles");
        
        for (const ProfileData& data : dbProfiles) {
            QString profilePath = ProfileRepository::resolvePath(
                data.dataFolder, profilesBase);
            
            Profile* prof = new Profile(profilePath);
            profiles[data.username] = prof;
        }
        return;
    }
    
    // Fall back to directory scanning
    qDebug() << "Falling back to directory scanning for profiles";
    QString path = p_pref->Get("{home}/Profiles");
    // ... existing scanning code ...
}
```

---

## Step-by-Step Integration Plan

### Step 1: Add Database Includes

**File**: `oscar/SleepLib/profiles.h`

Add near the top:
```cpp
// Forward declarations for database
class ProfileRepository;
class MachineRepository;
```

**File**: `oscar/SleepLib/profiles.cpp`

Add to includes:
```cpp
#include "../database/profile_repository.h"
#include "../database/machine_repository.h"
#include <QJsonDocument>
#include <QJsonObject>
```

### Step 2: Add Helper Methods to Profile Class

**File**: `oscar/SleepLib/profiles.h`

Add to `Profile` class private section:
```cpp
private:
    bool loadMachinesFromDatabase();
    bool loadMachinesFromXML();
    bool storeMachinesToDatabase();
    bool storeMachinesToXML();
```

### Step 3: Implement Database Methods

Create implementations in `profiles.cpp` as shown above.

### Step 4: Modify OpenMachines() and StoreMachines()

Update existing methods to use database-first approach.

### Step 5: Add Auto-Migration to main.cpp

Add migration code after database initialization.

### Step 6: Test Incrementally

1. Compile and test database loading
2. Verify fallback to XML works
3. Test database writing
4. Verify both XML and database updated

---

## Testing Checklist

### ✅ Phase 1: Database Reading
- [ ] OSCAR starts successfully
- [ ] Profiles load from database
- [ ] Machines load from database
- [ ] Machine properties parsed correctly
- [ ] Fallback to XML works when database empty

### ✅ Phase 2: Database Writing
- [ ] Machine changes save to database
- [ ] Machine changes also save to XML (backup)
- [ ] New machines added to database
- [ ] Properties stored correctly

### ✅ Phase 3: Migration
- [ ] Auto-migration runs on first start
- [ ] All profiles migrated successfully
- [ ] All machines migrated correctly
- [ ] Properties migrated from XML

### ✅ Phase 4: Integration
- [ ] Profile selector shows correct profiles
- [ ] Machine data displays correctly
- [ ] Import works as before
- [ ] Data integrity maintained

---

## Rollback Plan

If issues occur, rollback is simple:

1. **Remove database initialization** from `main.cpp`
2. **Revert OpenMachines()** to original version
3. **Revert StoreMachines()** to original version
4. **Delete oscar.db** file
5. OSCAR returns to XML-only operation

Original XML files are never deleted, so no data loss possible.

---

## Performance Considerations

### Database is Faster:
- ✅ No XML parsing overhead
- ✅ Indexed queries for quick lookups
- ✅ Single file vs many XML files
- ✅ Prepared statements

### Migration is One-Time:
- Migration only runs when database is empty
- Subsequent starts use existing database
- No performance impact after migration

---

## Future Enhancements

Once basic integration is stable:

1. **Profile.xml Migration** - Store profile preferences in database
2. **Session Index** - Index session data for faster queries
3. **Statistics Cache** - Pre-compute statistics in database
4. **Search Functionality** - Fast searching across all data
5. **Data Analysis** - SQL queries for advanced analytics

---

## Summary

**Recommended First Integration:**

1. Add auto-migration to `main.cpp`
2. Modify `Profile::OpenMachines()` for database-first reading
3. Keep `Profile::StoreMachines()` writing to XML
4. Test thoroughly
5. Once stable, add database writing

**This approach:**
- ✅ Minimally invasive
- ✅ Safe (XML files preserved)
- ✅ Testable
- ✅ Reversible
- ✅ Provides immediate benefits

---

## Questions?

The database infrastructure is complete and tested. The integration code above shows exactly where and how to modify OSCAR's existing code.

**Ready to begin integration whenever you are!**
