# OSCAR Database Structure

## Overview

OSCAR uses SQLite for storing profiles, machines, sessions, and related metadata. The database provides fast access to summary data while detailed waveform data remains in separate files for performance.

**Current Schema Version:** 4  
**Database File:** `oscar.db` (located in user data directory)

---

## Table Relationships

```
profiles (1) ─────┬──── (N) machines
                  │
                  ├──── (1) user_info
                  │
                  ├──── (1) doctor_info
                  │
                  └──── (N) profile_preferences

machines (1) ───── (N) sessions

sessions (1) ─────┬──── (N) session_settings
                  │
                  ├──── (N) session_channels
                  │
                  ├──── (N) respiratory_events
                  │
                  ├──── (1) session_summaries
                  │
                  └──── (N) session_slices
```

---

## Core Tables

### schema_version
Tracks the database schema version for migrations.

| Column | Type | Description |
|--------|------|-------------|
| **version** | INTEGER | Schema version number (PRIMARY KEY) |
| applied_at | TEXT | Timestamp when version was applied |

---

### profiles
Stores user profiles.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| username | TEXT | Unique username (UNIQUE, NOT NULL) |
| data_folder | TEXT | Path to profile data directory (NOT NULL) |
| status | TEXT | Profile status: 'active', 'missing', 'archived' |
| status_changed_at | TEXT | When status last changed |
| created_at | TEXT | Profile creation timestamp |
| updated_at | TEXT | Last update timestamp |

**Indexes:**
- `idx_profiles_username` on username

---

### machines
Stores CPAP machines and other devices.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| profile_id | INTEGER | Foreign key to profiles (NOT NULL) |
| machine_id | INTEGER | Machine ID from files (NOT NULL) |
| loader_name | TEXT | Loader plugin name (NOT NULL) |
| machine_type | INTEGER | Type of machine (NOT NULL) |
| brand | TEXT | Manufacturer name |
| model | TEXT | Model name |
| series | TEXT | Series name |
| serial_number | TEXT | Serial number |
| model_number | TEXT | Model number |
| last_imported | TEXT | Last import timestamp |
| purge_date | TEXT | Data purge date |
| data_version | INTEGER | Data format version |
| properties | TEXT | Additional properties (JSON) |
| created_at | TEXT | Creation timestamp |

**Constraints:**
- UNIQUE(profile_id, machine_id)
- FOREIGN KEY (profile_id) → profiles(id) ON DELETE CASCADE

**Indexes:**
- `idx_machines_profile` on profile_id
- `idx_machines_serial` on serial_number
- `idx_machines_loader` on loader_name

---

### user_info
Stores detailed user personal information.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| profile_id | INTEGER | Foreign key to profiles (UNIQUE, NOT NULL) |
| dob | TEXT | Date of birth |
| first_name | TEXT | First name |
| last_name | TEXT | Last name |
| address | TEXT | Address |
| phone | TEXT | Phone number |
| email | TEXT | Email address |
| country | TEXT | Country |
| height | REAL | Height |
| gender | INTEGER | Gender code |
| timezone | TEXT | Timezone |
| dst_enabled | INTEGER | Daylight saving time flag |
| password_hash | TEXT | Password hash (if protected) |
| created_at | TEXT | Creation timestamp |
| updated_at | TEXT | Last update timestamp |

**Constraints:**
- FOREIGN KEY (profile_id) → profiles(id) ON DELETE CASCADE

**Indexes:**
- `idx_user_info_profile` on profile_id

---

### doctor_info
Stores doctor/medical provider information.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| profile_id | INTEGER | Foreign key to profiles (UNIQUE, NOT NULL) |
| name | TEXT | Doctor name |
| phone | TEXT | Doctor phone |
| email | TEXT | Doctor email |
| practice_name | TEXT | Practice/clinic name |
| address | TEXT | Practice address |
| patient_id | TEXT | Patient ID at practice |
| created_at | TEXT | Creation timestamp |
| updated_at | TEXT | Last update timestamp |

**Constraints:**
- FOREIGN KEY (profile_id) → profiles(id) ON DELETE CASCADE

**Indexes:**
- `idx_doctor_info_profile` on profile_id

---

### profile_preferences
Stores all profile settings as key-value pairs.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| profile_id | INTEGER | Foreign key to profiles (NOT NULL) |
| category | TEXT | Setting category (NOT NULL) |
| key | TEXT | Setting key (NOT NULL) |
| value | TEXT | Setting value |
| data_type | TEXT | Data type hint |
| created_at | TEXT | Creation timestamp |
| updated_at | TEXT | Last update timestamp |

**Categories:** cpap, oximetry, session, appearance, general, etc.

**Constraints:**
- UNIQUE(profile_id, category, key)
- FOREIGN KEY (profile_id) → profiles(id) ON DELETE CASCADE

**Indexes:**
- `idx_preferences_profile` on profile_id
- `idx_preferences_category` on (profile_id, category)
- `idx_preferences_key` on (profile_id, category, key)

---

## Session Tables

### sessions
Core session metadata and timing.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| session_id | INTEGER | Session ID from files (NOT NULL) |
| machine_id | INTEGER | Foreign key to machines (NOT NULL) |
| start_time | INTEGER | Start time (ms since epoch, NOT NULL) |
| end_time | INTEGER | End time (ms since epoch, NOT NULL) |
| duration | INTEGER | Duration in milliseconds (NOT NULL) |
| enabled | INTEGER | Is session enabled (1=yes, 0=no) |
| summary_only | INTEGER | Contains summary data only |
| no_settings | INTEGER | Has no settings data |
| events_loaded | INTEGER | Events are loaded |
| events_file | TEXT | Events file name |
| summary_file | TEXT | Summary file name |
| created_at | TEXT | Creation timestamp |
| updated_at | TEXT | Last update timestamp |

**Constraints:**
- UNIQUE(machine_id, session_id)
- FOREIGN KEY (machine_id) → machines(id) ON DELETE CASCADE

**Indexes:**
- `idx_sessions_machine` on machine_id
- `idx_sessions_time` on (start_time, end_time)
- `idx_sessions_enabled` on (machine_id, enabled)

---

### session_settings
Machine configuration for each session.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| session_id | INTEGER | Foreign key to sessions (NOT NULL) |
| channel_id | INTEGER | Channel ID code (NOT NULL) |
| value | REAL | Setting value (NOT NULL) |
| data_type | TEXT | Data type hint |
| created_at | TEXT | Creation timestamp |

**Examples:** CPAP mode, pressure settings, ramp time, comfort settings

**Constraints:**
- UNIQUE(session_id, channel_id)
- FOREIGN KEY (session_id) → sessions(id) ON DELETE CASCADE

**Indexes:**
- `idx_session_settings_session` on session_id
- `idx_session_settings_channel` on (session_id, channel_id)

---

### session_channels
Summary statistics for each data channel in a session.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| session_id | INTEGER | Foreign key to sessions (NOT NULL) |
| channel_id | INTEGER | Channel ID code (NOT NULL) |
| count | INTEGER | Number of data points |
| sum | REAL | Sum of values |
| avg | REAL | Average value |
| wavg | REAL | Weighted average |
| min | REAL | Minimum value |
| max | REAL | Maximum value |
| median | REAL | Median value |
| p90 | REAL | 90th percentile |
| p95 | REAL | 95th percentile |
| phys_min | REAL | Physical minimum for graphing |
| phys_max | REAL | Physical maximum for graphing |
| cph | REAL | Count per hour |
| sph | REAL | Sum per hour |
| first_time | INTEGER | First timestamp |
| last_time | INTEGER | Last timestamp |
| gain | REAL | Channel gain/scaling factor |
| created_at | TEXT | Creation timestamp |

**Common Channels:** Pressure, Flow, Leak, SpO2, Pulse, Resp Rate, etc.

**Constraints:**
- UNIQUE(session_id, channel_id)
- FOREIGN KEY (session_id) → sessions(id) ON DELETE CASCADE

**Indexes:**
- `idx_session_channels_session` on session_id
- `idx_session_channels_channel` on channel_id
- `idx_session_channels_lookup` on (session_id, channel_id)

---

### respiratory_events
Individual respiratory events (apneas, hypopneas, etc.).

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| session_id | INTEGER | Foreign key to sessions (NOT NULL) |
| event_type | INTEGER | Event type code (NOT NULL) |
| start_time | INTEGER | Event start (ms since epoch, NOT NULL) |
| end_time | INTEGER | Event end (ms since epoch, NOT NULL) |
| duration | INTEGER | Duration in milliseconds (NOT NULL) |
| desaturation | REAL | Oxygen desaturation amount |
| severity | INTEGER | Event severity |
| created_at | TEXT | Creation timestamp |

**Event Types:**
- Obstructive Apnea
- Central Apnea
- Hypopnea
- RERA (Respiratory Effort Related Arousal)
- Clear Airway

**Constraints:**
- FOREIGN KEY (session_id) → sessions(id) ON DELETE CASCADE

**Indexes:**
- `idx_respiratory_events_session` on session_id
- `idx_respiratory_events_type` on (session_id, event_type)
- `idx_respiratory_events_time` on (start_time, end_time)

---

### session_summaries ⭐
**Cached high-level session summaries for fast queries.**

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| session_id | INTEGER | Foreign key to sessions (UNIQUE, NOT NULL) |
| **ahi** | REAL | Apnea-Hypopnea Index |
| **rdi** | REAL | Respiratory Disturbance Index |
| obstructive_count | INTEGER | Number of obstructive events |
| central_count | INTEGER | Number of central events |
| hypopnea_count | INTEGER | Number of hypopneas |
| rera_count | INTEGER | Number of RERAs |
| pressure_avg | REAL | Average pressure |
| pressure_min | REAL | Minimum pressure |
| pressure_max | REAL | Maximum pressure |
| pressure_95th | REAL | 95th percentile pressure |
| leak_total_avg | REAL | Average total leak |
| leak_total_95th | REAL | 95th percentile leak |
| leak_total_max | REAL | Maximum leak |
| spo2_avg | REAL | Average SpO2 |
| spo2_min | REAL | Minimum SpO2 |
| pulse_avg | REAL | Average pulse |
| **hours_used** | REAL | Total hours of therapy |
| mask_on_hours | REAL | Hours with mask on |
| created_at | TEXT | Creation timestamp |
| updated_at | TEXT | Last update timestamp |

**Auto-Populated:** This table is automatically populated when sessions are saved to the database.

**Constraints:**
- UNIQUE(session_id)
- FOREIGN KEY (session_id) → sessions(id) ON DELETE CASCADE

**Indexes:**
- `idx_session_summaries_session` on session_id
- `idx_session_summaries_ahi` on ahi

---

### session_slices
Mask-on/mask-off periods within sessions.

| Column | Type | Description |
|--------|------|-------------|
| **id** | INTEGER | Auto-increment primary key |
| session_id | INTEGER | Foreign key to sessions (NOT NULL) |
| start_time | INTEGER | Slice start (ms since epoch, NOT NULL) |
| end_time | INTEGER | Slice end (ms since epoch, NOT NULL) |
| status | INTEGER | Slice status code (NOT NULL) |

**Status Codes:**
- 0 = Unknown
- 1 = Equipment Off
- 2 = Mask On
- 3 = Mask Off

**Constraints:**
- FOREIGN KEY (session_id) → sessions(id) ON DELETE CASCADE

**Indexes:**
- `idx_session_slices_session` on session_id
- `idx_session_slices_time` on (start_time, end_time)

---

## Data Types Reference

### Time Values
All timestamps are stored as:
- **Milliseconds since Unix epoch** (INTEGER)
- Convert to readable: `datetime(value/1000, 'unixepoch', 'localtime')`

### Boolean Values
Stored as INTEGER:
- `1` = true
- `0` = false

### Channel IDs
Integer codes representing different data channels (pressure, flow, SpO2, etc.)
Defined in OSCAR channel schema.

### Machine Types
Integer codes for different machine types:
- CPAP, Auto CPAP, BiPAP, ASV, etc.

---

## Schema Versions

### Version 1-2
Initial schema with profiles, machines, user info, doctor info, preferences.

### Version 3 (Current)
Added complete session tables:
- sessions
- session_settings
- session_channels
- respiratory_events
- session_summaries
- session_slices

### Version 4
Added profile status tracking:
- status field (active/missing/archived)
- status_changed_at timestamp

---

## Performance Notes

1. **Indexes:** All foreign keys and common query columns are indexed
2. **Cascading Deletes:** Deleting a profile removes all related data
3. **UNIQUE Constraints:** Prevent duplicate data
4. **Summary Data:** `session_summaries` provides fast access without loading full session files
5. **File References:** Waveform data stays in files for performance; database stores references

---

## Common Queries

See `USEFUL_QUERIES.sql` for complete query collection.

### Quick Stats
```sql
SELECT 
    (SELECT COUNT(*) FROM profiles) as profiles,
    (SELECT COUNT(*) FROM machines) as machines,
    (SELECT COUNT(*) FROM sessions) as sessions,
    (SELECT COUNT(*) FROM session_summaries) as summaries;
```

### Recent AHI Values
```sql
SELECT 
    datetime(s.start_time/1000, 'unixepoch', 'localtime') as date,
    ss.ahi,
    ss.hours_used
FROM session_summaries ss
JOIN sessions s ON ss.session_id = s.id
ORDER BY s.start_time DESC
LIMIT 30;
```
