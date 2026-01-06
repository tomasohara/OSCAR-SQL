-- ============================================================================
-- OSCAR Database Query: Most Recent Session Settings
-- ============================================================================
-- Description: Display session settings for the most recent session for a profile,
--              including channel names and session metadata.
--
-- Copyright (c) 2026 The OSCAR Team
--
-- Purpose: This query retrieves all settings from the most recent CPAP session
--          for a specified profile, joining with the channels table to provide
--          human-readable channel names and descriptions.
--
-- Parameters: Replace :profile_id with the actual profile ID
--
-- Returns:
--   - Session metadata (session ID, machine info, session timing)
--   - Each setting with its channel name, code, and value
--   - Units/dimensions and data types where available
--
-- Schema Version: 8+
-- Last Updated: 2026-01-05
-- ============================================================================

-- ============================================================================
-- Query 1: Most Recent Session Settings with Channel Names
-- ============================================================================
-- This query shows all settings for the most recent session, including
-- channel names from the channels table (profile-specific customizations)
-- and falls back to channel_code if no custom name is defined.

SELECT 
    -- Session Information
    s.id AS session_db_id,
    s.session_id,
    s.start_time,
    datetime(s.start_time, 'unixepoch', 'localtime') AS start_datetime,
    s.end_time,
    datetime(s.end_time, 'unixepoch', 'localtime') AS end_datetime,
    s.duration / 3600.0 AS duration_hours,
    
    -- Machine Information
    m.brand,
    m.model,
    m.serial_number,
    m.loader_name,
    
    -- Profile Information
    p.username,
    
    -- Setting Information
    ss.channel_id,
    COALESCE(c.fullname, c.label, c.channel_code, 'Channel_' || ss.channel_id) AS channel_name,
    c.channel_code,
    ss.value AS setting_value,
    ss.data_type,
    c.description AS channel_description,
    
    -- Additional metadata from session_settings
    ss.created_at AS setting_recorded_at

FROM 
    profiles p
    
    -- Get machines for this profile
    INNER JOIN machines m ON m.profile_id = p.id
    
    -- Get the most recent session for this profile
    INNER JOIN (
        SELECT 
            machine_id,
            MAX(start_time) AS max_start_time
        FROM 
            sessions
        WHERE 
            enabled = 1  -- Only consider enabled sessions
        GROUP BY 
            machine_id
    ) recent ON recent.machine_id = m.id
    
    -- Join to get the actual session record
    INNER JOIN sessions s ON s.machine_id = recent.machine_id 
                          AND s.start_time = recent.max_start_time
    
    -- Get all settings for this session
    INNER JOIN session_settings ss ON ss.session_id = s.id
    
    -- Left join to channels table for custom channel names (may not exist for all profiles)
    LEFT JOIN channels c ON c.profile_id = p.id 
                         AND c.channel_id = ss.channel_id

WHERE 
    p.id = :profile_id  -- Replace with actual profile ID (e.g., 1)
    
ORDER BY 
    c.channel_code,
    ss.channel_id;


-- ============================================================================
-- Query 2: Simplified Version - Most Recent Session Settings
-- ============================================================================
-- A simpler version that gets settings for the most recent session across
-- ALL machines for a profile, ordered by channel ID.

SELECT 
    s.session_id,
    datetime(s.start_time, 'unixepoch', 'localtime') AS session_date,
    m.brand || ' ' || m.model AS machine,
    ss.channel_id,
    COALESCE(c.fullname, c.channel_code, 'Channel_' || ss.channel_id) AS channel_name,
    ss.value,
    ss.data_type
    
FROM 
    profiles p
    INNER JOIN machines m ON m.profile_id = p.id
    INNER JOIN sessions s ON s.machine_id = m.id
    INNER JOIN session_settings ss ON ss.session_id = s.id
    LEFT JOIN channels c ON c.profile_id = p.id AND c.channel_id = ss.channel_id
    
WHERE 
    p.id = :profile_id
    AND s.enabled = 1
    AND s.id = (
        -- Subquery to get the most recent session ID for this profile
        SELECT s2.id
        FROM sessions s2
        INNER JOIN machines m2 ON s2.machine_id = m2.id
        WHERE m2.profile_id = p.id
          AND s2.enabled = 1
        ORDER BY s2.start_time DESC
        LIMIT 1
    )
    
ORDER BY 
    ss.channel_id;


-- ============================================================================
-- Query 3: Most Recent Session Settings by Username
-- ============================================================================
-- Same as Query 1 but allows filtering by username instead of profile ID.

SELECT 
    s.session_id,
    datetime(s.start_time, 'unixepoch', 'localtime') AS session_start,
    s.duration / 3600.0 AS hours,
    m.brand || ' ' || m.model AS machine,
    ss.channel_id,
    COALESCE(c.fullname, c.label, 'Channel_' || ss.channel_id) AS channel_name,
    c.channel_code,
    ss.value,
    ss.data_type,
    c.description
    
FROM 
    profiles p
    INNER JOIN machines m ON m.profile_id = p.id
    INNER JOIN sessions s ON s.machine_id = m.id
    INNER JOIN session_settings ss ON ss.session_id = s.id
    LEFT JOIN channels c ON c.profile_id = p.id AND c.channel_id = ss.channel_id
    
WHERE 
    p.username = :username  -- Replace with actual username (e.g., 'JohnDoe')
    AND s.enabled = 1
    AND s.id = (
        SELECT s2.id
        FROM sessions s2
        INNER JOIN machines m2 ON s2.machine_id = m2.id
        INNER JOIN profiles p2 ON m2.profile_id = p2.id
        WHERE p2.username = :username
          AND s2.enabled = 1
        ORDER BY s2.start_time DESC
        LIMIT 1
    )
    
ORDER BY 
    CASE 
        WHEN c.channel_code IS NOT NULL THEN c.channel_code 
        ELSE 'zzz_' || ss.channel_id 
    END;


-- ============================================================================
-- Query 4: Most Recent Session Settings with Machine-Specific Filtering
-- ============================================================================
-- Get settings for the most recent session from a SPECIFIC machine.

SELECT 
    s.session_id,
    datetime(s.start_time, 'unixepoch', 'localtime') AS session_start,
    m.brand,
    m.model,
    m.serial_number,
    ss.channel_id,
    COALESCE(c.fullname, c.channel_code) AS channel_name,
    ss.value,
    c.description
    
FROM 
    machines m
    INNER JOIN sessions s ON s.machine_id = m.id
    INNER JOIN session_settings ss ON ss.session_id = s.id
    LEFT JOIN channels c ON c.profile_id = m.profile_id AND c.channel_id = ss.channel_id
    
WHERE 
    m.id = :machine_id  -- Replace with actual machine ID
    AND s.enabled = 1
    AND s.start_time = (
        SELECT MAX(s2.start_time)
        FROM sessions s2
        WHERE s2.machine_id = m.id
          AND s2.enabled = 1
    )
    
ORDER BY 
    ss.channel_id;


-- ============================================================================
-- Query 5: Compare Settings Between Most Recent and Previous Session
-- ============================================================================
-- Shows changes in settings between the two most recent sessions.

WITH recent_sessions AS (
    SELECT 
        s.id,
        s.session_id,
        s.start_time,
        s.machine_id,
        ROW_NUMBER() OVER (PARTITION BY s.machine_id ORDER BY s.start_time DESC) AS rn
    FROM 
        sessions s
        INNER JOIN machines m ON s.machine_id = m.id
    WHERE 
        m.profile_id = :profile_id
        AND s.enabled = 1
)
SELECT 
    COALESCE(c.fullname, c.channel_code, 'Channel_' || COALESCE(ss1.channel_id, ss2.channel_id)) AS channel_name,
    c.channel_code,
    ss1.value AS current_value,
    ss2.value AS previous_value,
    CASE 
        WHEN ss1.value != ss2.value THEN 'CHANGED'
        WHEN ss1.value IS NULL AND ss2.value IS NOT NULL THEN 'REMOVED'
        WHEN ss1.value IS NOT NULL AND ss2.value IS NULL THEN 'ADDED'
        ELSE 'UNCHANGED'
    END AS status,
    datetime(rs1.start_time, 'unixepoch', 'localtime') AS current_session,
    datetime(rs2.start_time, 'unixepoch', 'localtime') AS previous_session

FROM 
    recent_sessions rs1
    LEFT JOIN recent_sessions rs2 ON rs2.machine_id = rs1.machine_id AND rs2.rn = 2
    LEFT JOIN session_settings ss1 ON ss1.session_id = rs1.id
    LEFT JOIN session_settings ss2 ON ss2.session_id = rs2.id AND ss2.channel_id = ss1.channel_id
    LEFT JOIN profiles p ON p.id = :profile_id
    LEFT JOIN channels c ON c.profile_id = p.id AND c.channel_id = COALESCE(ss1.channel_id, ss2.channel_id)

WHERE 
    rs1.rn = 1
    AND (ss1.channel_id IS NOT NULL OR ss2.channel_id IS NOT NULL)

ORDER BY 
    c.channel_code,
    COALESCE(ss1.channel_id, ss2.channel_id);


-- ============================================================================
-- Usage Examples
-- ============================================================================

/*
-- Example 1: Get settings for profile ID 1
SELECT ... WHERE p.id = 1;

-- Example 2: Get settings for username 'JohnDoe'
SELECT ... WHERE p.username = 'JohnDoe';

-- Example 3: Get settings for machine ID 5
SELECT ... WHERE m.id = 5;

-- Example 4: Get most recent session info for all profiles
SELECT 
    p.username,
    MAX(s.start_time) AS latest_session,
    datetime(MAX(s.start_time), 'unixepoch', 'localtime') AS latest_session_datetime,
    COUNT(ss.id) AS settings_count
FROM 
    profiles p
    INNER JOIN machines m ON m.profile_id = p.id
    INNER JOIN sessions s ON s.machine_id = m.id
    INNER JOIN session_settings ss ON ss.session_id = s.id
WHERE 
    s.enabled = 1
GROUP BY 
    p.id, p.username
ORDER BY 
    latest_session DESC;
*/


-- ============================================================================
-- Notes
-- ============================================================================

/*
IMPORTANT CONSIDERATIONS:

1. Channel Names:
   - The channels table contains profile-specific customizations
   - Not all profiles may have entries in the channels table
   - Query uses COALESCE to fall back to channel_code or a generated name

2. Most Recent Session Logic:
   - Filters by enabled = 1 to exclude disabled sessions
   - Uses start_time for ordering (most recent = highest timestamp)
   - If profile has multiple machines, returns settings from the machine 
     with the most recent session

3. Timestamps:
   - start_time and end_time are stored as Unix timestamps (seconds)
   - Use datetime(timestamp, 'unixepoch', 'localtime') to convert to readable format
   - Duration is stored in seconds, divide by 3600 for hours

4. Data Types:
   - session_settings.value is REAL (floating point)
   - Some settings may have data_type hints for interpretation
   - Channel dimensions/units are in channels.description or implied by channel_code

5. Performance:
   - Queries use indexes: idx_sessions_machine, idx_sessions_time, idx_sessions_enabled
   - For large databases, consider adding WHERE filters to limit date ranges
   - The channels LEFT JOIN is optional and can be omitted if names not needed

6. Channel IDs:
   - channel_id values are OSCAR internal constants (defined in C++ code)
   - Common channels: Pressure (0x1001), Leak (0x1002), etc.
   - See channel_loader code for complete channel ID definitions
*/
