# Channels Database Design

## Overview

This document describes the database tables for storing channel definitions and their lookup options. Currently, OSCAR stores this data in `channels.dat` (binary format) and `channels.xml` files per profile.

## Current File Structure

### channels.dat
Binary file storing per-profile channel preferences:
- Channel enabled/disabled state
- Custom display colors
- Custom names, labels, descriptions
- Threshold values for flagging
- Overview display preferences

### channels.xml  
XML file storing channel definitions (not currently read by desktop version).

## Database Schema

### Table: channels

Stores channel configuration per profile. Each profile can customize channel display properties.

```sql
CREATE TABLE IF NOT EXISTS channels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,              -- ChannelID (quint32)
    channel_code TEXT NOT NULL,               -- Unique channel code (e.g., "CPAP_Pressure")
    enabled BOOLEAN NOT NULL DEFAULT 1,       -- Is channel enabled?
    
    -- Display Properties
    default_color TEXT,                       -- QColor as #RRGGBB
    fullname TEXT,                            -- Full translatable name
    label TEXT,                               -- Short label for graphs
    description TEXT,                         -- Tooltip description
    
    -- Threshold Settings
    lower_threshold REAL,                     -- Lower threshold value
    lower_threshold_color TEXT,               -- QColor as #RRGGBB
    upper_threshold REAL,                     -- Upper threshold value
    upper_threshold_color TEXT,               -- QColor as #RRGGBB
    
    -- UI Preferences
    show_in_overview BOOLEAN DEFAULT 0,       -- Show in Overview tab?
    
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    UNIQUE(profile_id, channel_id)
);

CREATE INDEX idx_channels_profile ON channels(profile_id);
CREATE INDEX idx_channels_code ON channels(channel_code);
```

### Table: channel_options

Stores lookup values for LOOKUP-type channels (e.g., CPAP Mode values, EPR levels).

```sql
CREATE TABLE IF NOT EXISTS channel_options (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    channel_id INTEGER NOT NULL,              -- ChannelID (matches schema.cpp definitions)
    option_key INTEGER NOT NULL,              -- Numeric key (index)
    option_value TEXT NOT NULL,               -- Display value (e.g., "CPAP", "APAP", "Bi-Level")
    
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE(channel_id, option_key)
);

CREATE INDEX idx_channel_options_channel ON channel_options(channel_id);
```

## Channel Types with Options

Channels with m_options (lookup fields) that need channel_options entries:

1. **CPAP_Mode** - CPAP modes (CPAP, APAP, Bi-Level, etc.)
2. **PRS1_FlexMode** - Flex modes (0=None, 1=CFlex, 2=CFlex+, 3=BiFlex)
3. **BMC_RESLEX_MODE** - Reslex modes
4. **INTP_SmartFlexMode** - SmartFlex modes
5. **EPR Level channels** - EPR levels (0, 1, 2, 3)
6. **Ramp settings** - On/Off values
7. **Other device-specific settings**

## Data Flow

### Saving to Database (Profile::saveChannels)

```cpp
1. Get or create profile record
2. For each channel in schema::channel.channels:
   a. Save channel preferences to `channels` table
   b. If channel has m_options, save to `channel_options` table
```

### Loading from Database (Profile::loadChannels)

```cpp
1. Find profile record
2. Load all channels for profile from `channels` table
3. Apply preferences to schema::channel objects
4. Load channel_options and populate m_options hash
```

### Migration Strategy

#### Phase 1: Add Database Support (Non-Breaking)
- Create tables
- Create repository classes
- Save to database in addition to files
- Load from database if available, fall back to files

#### Phase 2: Database-First (Backwards Compatible)
- Load from database first
- Fall back to files if database empty
- Continue saving to both

#### Phase 3: Database-Only (Future)
- Remove file I/O for channels.dat
- Keep only database storage

## Repository Classes

### ChannelRepository
```cpp
class ChannelRepository {
public:
    // CRUD operations
    qint64 create(const ChannelData& data);
    bool update(const ChannelData& data);
    bool remove(qint64 id);
    
    // Queries
    ChannelData findById(qint64 id);
    QList<ChannelData> findByProfile(qint64 profileId);
    ChannelData findByProfileAndChannelId(qint64 profileId, ChannelID channelId);
    
    // Batch operations
    bool saveBatch(qint64 profileId, const QList<ChannelData>& channels);
    
    // Integration with Profile
    bool saveFromProfile(qint64 profileId);
    bool loadIntoProfile(qint64 profileId);
};
```

### ChannelOptionsRepository
```cpp
class ChannelOptionsRepository {
public:
    // CRUD operations
    qint64 create(const ChannelOptionData& data);
    bool update(const ChannelOptionData& data);
    bool remove(qint64 id);
    
    // Queries
    QList<ChannelOptionData> findByChannel(ChannelID channelId);
    ChannelOptionData findByChannelAndKey(ChannelID channelId, int key);
    
    // Batch operations
    bool saveBatch(ChannelID channelId, const QHash<int, QString>& options);
    
    // Populate from schema
    bool populateFromSchema();
};
```

## Data Structures

```cpp
struct ChannelData {
    qint64 id = 0;
    qint64 profileId = 0;
    ChannelID channelId = 0;
    QString channelCode;
    bool enabled = true;
    
    // Display
    QColor defaultColor;
    QString fullname;
    QString label;
    QString description;
    
    // Thresholds
    double lowerThreshold = 0.0;
    QColor lowerThresholdColor;
    double upperThreshold = 0.0;
    QColor upperThresholdColor;
    
    // UI
    bool showInOverview = false;
};

struct ChannelOptionData {
    qint64 id = 0;
    ChannelID channelId = 0;
    int optionKey = 0;
    QString optionValue;
};
```

## Benefits

1. **Centralized Storage**: All profile data in one database
2. **Faster Queries**: Index-based lookups vs file parsing
3. **Atomic Updates**: Transaction support for consistency
4. **Easier Backup**: Single database file
5. **Better Migration**: SQL makes data migration easier
6. **Query Capabilities**: Can query channels across profiles
7. **Referential Integrity**: Foreign keys ensure consistency

## Migration Script

```sql
-- Migrate existing channels.dat files to database
-- Run once per profile during first load

1. Read channels.dat binary file
2. Parse channel data
3. INSERT INTO channels table
4. Read m_options from schema for lookup channels
5. INSERT INTO channel_options table
6. Verify data integrity
7. Keep channels.dat as backup
```

## Testing Plan

1. **Unit Tests**
   - Repository CRUD operations
   - Data conversion (QColor <-> TEXT)
   - Batch operations

2. **Integration Tests**
   - Save channels to database
   - Load channels from database
   - Fall back to file if database fails
   - Verify m_options population

3. **Migration Tests**
   - Import existing channels.dat
   - Verify all fields preserved
   - Verify lookup options correct

4. **Performance Tests**
   - Load time comparison (file vs database)
   - Query performance
   - Batch save performance
