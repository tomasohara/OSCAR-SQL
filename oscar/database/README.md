# OSCAR Database Implementation

## Phase 1: Database Infrastructure (COMPLETED)

This directory contains the initial database infrastructure for OSCAR's migration from file-based storage to SQLite database.

### Files Created

#### Core Database Files
1. **database_manager.h / .cpp** - Singleton class managing SQLite database connections
2. **database_schema.h / .cpp** - Database schema definitions and creation

#### Documentation
3. **IMPLEMENTATION_PLAN.md** - Detailed implementation plan for all phases
4. **README.md** - This file

### Current Status

✅ **COMPLETED - Ready for compilation testing**

The following has been implemented:
- DatabaseManager singleton with thread-safe connection handling
- Complete database schema for profiles and machines tables
- Automatic schema creation on first run
- Transaction support
- Error handling and logging
- Integration with oscar.pro build system

### Database Schema (Version 1)

#### Tables Created

**profiles**
- `id` - Primary key
- `username` - Unique username
- `data_folder` - Path to profile data
- `created_at` - Timestamp
- `updated_at` - Timestamp

**machines**
- `id` - Primary key  
- `profile_id` - Foreign key to profiles
- `machine_id` - Machine identifier
- `loader_name` - Loader plugin name
- `machine_type` - Type of machine
- `brand`, `model`, `series` - Device information
- `serial_number`, `model_number` - Device identifiers
- `last_imported` - Last import timestamp
- `purge_date` - Data retention date
- `data_version` - Version tracking
- `created_at` - Timestamp

**schema_version**
- `version` - Schema version number
- `applied_at` - When version was applied

### Next Steps for Testing

#### 1. Compile OSCAR
Open the project in QtCreator and compile:
- Open `C:\OSCAR\OSCAR-code\oscar\oscar.pro`
- Build the project
- Check for any compilation errors

#### 2. Expected Behavior (not yet implemented)
The database infrastructure is ready but NOT YET INTEGRATED into OSCAR's startup.

To test the database:
1. You need to add initialization code to main.cpp or mainwindow.cpp
2. Call `DatabaseManager::instance().initialize(GetAppData() + "/oscar.db")`
3. The database file will be created automatically in your OSCAR data folder

#### 3. Common Compilation Issues

**Issue: "cannot find -lsql"**
- **Solution**: Make sure Qt SQL module is installed with your Qt installation

**Issue: "QSqlDatabase: No such file or directory"**
- **Solution**: Verify `QT += sql` is in oscar.pro (already added)

**Issue: Path separator issues**
- **Solution**: Qt handles path separators automatically with QDir

### Testing the Database (Manual)

Once OSCAR compiles, you can manually test the database by adding this code temporarily to main.cpp:

```cpp
#include "database/database_manager.h"

// In main() function, after GetAppData() is set up:
QString dbPath = GetAppData() + "/oscar_test.db";
if (DatabaseManager::instance().initialize(dbPath)) {
    qDebug() << "Database initialized successfully!";
    qDebug() << "Database file:" << dbPath;
} else {
    qWarning() << "Database initialization failed!";
}
```

This will:
1. Create a new database file `oscar_test.db` in your OSCAR data folder
2. Create all tables and indexes
3. Set schema version to 1

You can examine the database with any SQLite viewer (DB Browser for SQLite, etc.)

### What's NOT Yet Implemented

The following are planned for future phases:

- ❌ Repository classes (ProfileRepository, MachineRepository)
- ❌ Migration from XML files to database
- ❌ Integration with Profile and Machine loading code
- ❌ Automatic migration at startup
- ❌ User interface for migration

### Phase 2: Next Steps

Once compilation is successful, the next phase will implement:

1. **ProfileRepository** - CRUD operations for profiles
2. **MachineRepository** - CRUD operations for machines  
3. **MigrationManager** - Convert existing Profile.xml and machines.xml to database
4. **Integration** - Modify OSCAR startup to use database when available

See **IMPLEMENTATION_PLAN.md** for complete details.

### Development Guidelines

#### Adding New Tables

To add a new table:

1. Add CREATE TABLE statement in `database_schema.cpp`
2. Add method to create the table (like `createProfilesTable`)
3. Call the method from `createSchema()`
4. Add indexes if needed in `createIndexes()`
5. Increment `CURRENT_SCHEMA_VERSION` in `database_schema.h`

#### Database Location

The database will be stored in the OSCAR data folder:
- **Windows**: `C:\Users\<username>\Documents\OSCAR_Data\oscar.db`
- **macOS**: `~/Documents/OSCAR_Data/oscar.db`
- **Linux**: `~/Documents/OSCAR_Data/oscar.db`

(Exact path depends on user's Documents folder location)

### Database Features

#### Enabled Features
- **Foreign Keys** - Referential integrity enforced
- **WAL Mode** - Write-Ahead Logging for better concurrency
- **64MB Cache** - Optimized for performance
- **Transactions** - ACID compliance

#### Schema Versioning
The schema version is tracked in the `schema_version` table.
Future versions can implement upgrade paths.

### Support

For questions or issues:
1. Check compilation errors in QtCreator
2. Review debug output (qDebug messages)
3. Verify Qt SQL module is installed
4. Check file permissions on OSCAR data folder

### Copyright

Copyright (c) 2026 The OSCAR Team

This database implementation is subject to the terms and conditions 
of the GNU General Public License. See the file COPYING in the main 
directory of the source code for more details.
