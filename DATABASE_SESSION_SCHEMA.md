# OSCAR Session Data - Database Schema Design

## Analysis of BMC Loader Data Flow

### Current Architecture
```
SD Card Binary Files → BMC Loader → Session Objects → Binary Storage Files
                                                     ↓
                                            (Summary.sle + Events.001)
```

### Data Types in BMC Loader

**1. Session Metadata:**
- Start/end timestamps
- Duration
- Machine settings (mode, pressures, comfort settings)
- Summary statistics

**2. Respiratory Events (Discrete):**
- Hypopneas (HYP)
- Obstructive Sleep Apnea (OSA)
- Central Sleep Apnea (CSA)
- Each event: start time, end time, duration

**3. Waveform Data (High-Frequency):**
- 25 samples per packet
- Recorded every second
- Channels: Pressure, Flow, Leak, Tidal Volume, Minute Ventilation, SpO2, Pulse, etc.
- **~90,000 samples per hour** = **~3.6 million values per 8-hour night**

---

## Database Design Decision

### ✅ Store in Database:
1. **Session metadata** - Times, settings, flags
2. **Session settings** - Machine configuration per session
3. **Respiratory events** - Discrete events (typically 10-200 per night)
4. **Session summaries** - Cached statistics (AHI, averages, percentiles)
5. **Channel summaries** - Per-channel aggregates (min, max, avg, count)

### ❌ Keep in Files:
1. **Waveform data** - High-frequency time-series data
   - Reason: 3.6M values/night would bloat database
   - Files are efficient for sequential streaming
   - Binary format is compact
   - Current system works well for this use case

### 🔄 Hybrid Approach:
- Database: Session index + metadata + events + summaries
- Files: Waveform time-series data (pressure, flow, etc.)
- Database contains file references for loading waveforms on demand

---

## Proposed Database Tables

### 1. `sessions` Table
**Purpose:** Core session metadata and timing

```sql
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,  -- SessionID (computed from date)
    machine_id INTEGER NOT NULL,
    
    -- Timing
    start_time INTEGER NOT NULL,  -- ms since epoch
    end_time INTEGER NOT NULL,    -- ms since epoch
    duration INTEGER NOT NULL,    -- milliseconds
    
    -- Status
    enabled INTEGER DEFAULT 1,    -- 0=disabled, 1=enabled, 2=other
    summary_only INTEGER DEFAULT 0,  -- Boolean: has detail data or not
    no_settings INTEGER DEFAULT 0,   -- Boolean: has settings or not
    events_loaded INTEGER DEFAULT 0, -- Boolean: events currently in memory
    
    -- File references
    events_file VARCHAR(255),     -- Path to events file (relative)
    summary_file VARCHAR(255),    -- Path to summary file (relative)
    
    -- Metadata
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE CASCADE,
    UNIQUE(machine_id, session_id)
);

CREATE INDEX idx_sessions_machine ON sessions(machine_id);
CREATE INDEX idx_sessions_time ON sessions(start_time, end_time);
CREATE INDEX idx_sessions_enabled ON sessions(machine_id, enabled);
```

---

### 2. `session_settings` Table
**Purpose:** Machine settings for each session (CPAP mode, pressures, comfort settings)

```sql
CREATE TABLE session_settings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    
    -- Setting key-value pairs
    channel_id INTEGER NOT NULL,  -- ChannelID from schema (e.g., CPAP_Pressure)
    value REAL NOT NULL,          -- Setting value
    data_type VARCHAR(20),        -- 'int', 'float', 'bool', 'string'
    
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, channel_id)
);

CREATE INDEX idx_session_settings_session ON session_settings(session_id);
CREATE INDEX idx_session_settings_channel ON session_settings(session_id, channel_id);
```

**Example Data:**
```
session_id=1, channel_id=CPAP_Mode, value=1, data_type='int'  (MODE_CPAP)
session_id=1, channel_id=CPAP_Pressure, value=12.5, data_type='float'
session_id=1, channel_id=BMC_RAMPTIME, value=15, data_type='int'
```

---

### 3. `session_channels` Table
**Purpose:** Summary statistics for each channel in a session

```sql
CREATE TABLE session_channels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,  -- ChannelID (e.g., CPAP_Pressure, CPAP_Leak)
    
    -- Summary statistics
    count INTEGER DEFAULT 0,      -- Number of samples
    sum REAL DEFAULT 0,           -- Sum of all values
    avg REAL DEFAULT 0,           -- Average
    wavg REAL DEFAULT 0,          -- Weighted average
    min REAL DEFAULT 0,           -- Minimum value
    max REAL DEFAULT 0,           -- Maximum value
    median REAL DEFAULT 0,        -- 50th percentile
    p90 REAL DEFAULT 0,           -- 90th percentile
    p95 REAL DEFAULT 0,           -- 95th percentile
    
    -- Physical bounds for display
    phys_min REAL DEFAULT 0,
    phys_max REAL DEFAULT 0,
    
    -- Rate calculations
    cph REAL DEFAULT 0,           -- Count per hour (e.g., AHI)
    sph REAL DEFAULT 0,           -- Sum per hour
    
    -- Timing
    first_time INTEGER,           -- ms since epoch
    last_time INTEGER,            -- ms since epoch
    
    -- Gain for waveform data
    gain REAL DEFAULT 1.0,
    
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, channel_id)
);

CREATE INDEX idx_session_channels_session ON session_channels(session_id);
CREATE INDEX idx_session_channels_channel ON session_channels(channel_id);
CREATE INDEX idx_session_channels_lookup ON session_channels(session_id, channel_id);
```

**Example Data:**
```
session_id=1, channel_id=CPAP_Pressure, count=28800, avg=11.2, min=9.5, max=13.8
session_id=1, channel_id=CPAP_LeakTotal, count=28800, avg=15.3, min=0, max=42.5
session_id=1, channel_id=CPAP_Obstructive, count=15, cph=2.1  (15 OSA events, 2.1 per hour)
```

---

### 4. `respiratory_events` Table
**Purpose:** Individual respiratory events (apneas, hypopneas, etc.)

```sql
CREATE TABLE respiratory_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    
    event_type INTEGER NOT NULL,  -- Event type code (OSA, CSA, Hypopnea, RERA, etc.)
    start_time INTEGER NOT NULL,  -- ms since epoch
    end_time INTEGER NOT NULL,    -- ms since epoch
    duration INTEGER NOT NULL,    -- seconds
    
    -- Optional event-specific data
    desaturation REAL,            -- SpO2 drop percentage (for hypopneas)
    severity INTEGER,             -- Event severity (0-3)
    
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_respiratory_events_session ON respiratory_events(session_id);
CREATE INDEX idx_respiratory_events_type ON respiratory_events(session_id, event_type);
CREATE INDEX idx_respiratory_events_time ON respiratory_events(start_time, end_time);
```

**Example Data:**
```
session_id=1, event_type=CPAP_Obstructive, start_time=..., end_time=..., duration=15
session_id=1, event_type=CPAP_Hypopnea, start_time=..., duration=12, desaturation=4.2
session_id=1, event_type=CPAP_ClearAirway, start_time=..., duration=18
```

---

### 5. `session_summaries` Table
**Purpose:** Cached high-level session summaries

```sql
CREATE TABLE session_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL UNIQUE,
    
    -- Primary metrics
    ahi REAL DEFAULT 0,           -- Apnea-Hypopnea Index
    rdi REAL DEFAULT 0,           -- Respiratory Disturbance Index
    
    -- Event counts
    obstructive_count INTEGER DEFAULT 0,
    central_count INTEGER DEFAULT 0,
    hypopnea_count INTEGER DEFAULT 0,
    rera_count INTEGER DEFAULT 0,
    
    -- Pressure statistics (CPAP/APAP)
    pressure_avg REAL,
    pressure_min REAL,
    pressure_max REAL,
    pressure_95th REAL,
    
    -- Leak statistics
    leak_total_avg REAL,
    leak_total_95th REAL,
    leak_total_max REAL,
    
    -- Oximetry (if available)
    spo2_avg REAL,
    spo2_min REAL,
    pulse_avg REAL,
    
    -- Usage
    hours_used REAL DEFAULT 0,
    mask_on_hours REAL DEFAULT 0,
    
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_session_summaries_session ON session_summaries(session_id);
CREATE INDEX idx_session_summaries_ahi ON session_summaries(ahi);
```

---

### 6. `session_slices` Table
**Purpose:** Session slices for mask-on/mask-off periods

```sql
CREATE TABLE session_slices (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    
    start_time INTEGER NOT NULL,  -- ms since epoch
    end_time INTEGER NOT NULL,    -- ms since epoch
    status INTEGER NOT NULL,      -- 0=Unknown, 1=EquipmentOff, 2=MaskOn, 3=MaskOff
    
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_session_slices_session ON session_slices(session_id);
CREATE INDEX idx_session_slices_time ON session_slices(start_time, end_time);
```

---

## Table Creation Order

**Due to foreign key dependencies:**

```
1. machines              (already exists)
2. sessions              (FK → machines)
3. session_settings      (FK → sessions)
4. session_channels      (FK → sessions)
5. respiratory_events    (FK → sessions)
6. session_summaries     (FK → sessions)
7. session_slices        (FK → sessions)
```

---

## Data Size Estimates

**Per Session (8-hour night):**
- sessions: 1 row (~200 bytes)
- session_settings: ~20 rows (~1 KB)
- session_channels: ~15 rows (~2 KB)
- respiratory_events: ~50 rows (~3 KB)
- session_summaries: 1 row (~300 bytes)
- session_slices: ~4 rows (~200 bytes)

**Total per session: ~7 KB** (very manageable!)

**Per Profile (1 year = 365 sessions):**
- Database: ~2.5 MB
- Waveform files: ~500 MB (stays in files)

---

## Migration Strategy

### Phase 1: Create Tables
1. Add session tables to schema version 3
2. Create all tables with proper indexes
3. Enable CASCADE deletes

### Phase 2: Modify Loaders
1. Update BMC loader to write to database
2. Keep waveform file writing unchanged
3. Write session metadata + events + summaries to DB

### Phase 3: Update Session Class
1. Modify Session::Store() to write to DB
2. Modify Session::Load() to read from DB
3. Keep waveform loading from files

### Phase 4: Migration Tool
1. Read existing Summary.sle files
2. Parse and insert into database
3. Keep event files intact
4. Optional: can delete old .sle files after migration

---

## Benefits of This Design

✅ **Fast Queries** - Session listing, event counting, AHI calculations
✅ **Efficient Storage** - Only metadata in DB, large data in files
✅ **Scalability** - Can handle 1000+ sessions easily
✅ **Flexibility** - Easy to add new summary calculations
✅ **Integrity** - CASCADE deletes keep everything consistent
✅ **Backward Compatible** - Waveform files unchanged
✅ **Future Ready** - Foundation for cloud sync, analytics, etc.

---

## Next Steps

1. ✅ Design complete
2. Create schema migration (version 2 → 3)
3. Create repository classes for session tables
4. Modify Session class to use database
5. Update BMC loader to write to database
6. Test with sample data
7. Create migration tool for existing data
