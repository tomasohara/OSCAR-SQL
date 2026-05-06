-- OSCAR Database — Pulse Rate 95th Percentile Queries
-- Channel: OXI_Pulse, channel_id = 6144 (0x1800)
--
-- These queries use the standard report macros substituted at runtime:
--   #PROFILE_ID  → integer profile ID (no quotes)
--   #START_DATE  → 'YYYY-MM-DD' (already quoted)
--   #END_DATE    → 'YYYY-MM-DD' (already quoted)
--
-- OSCAR DAY CONVENTION:
--   An OSCAR day runs from noon on the given date to noon the following
--   calendar day.  The expression
--       date(s.start_time/1000 - 43200, 'unixepoch', 'localtime')
--   subtracts 12 h (43 200 s) before extracting the date, which maps
--   every session to the correct OSCAR calendar date.
--
-- For a single-night query set #START_DATE = #END_DATE.
-- Requires schema v12+ (session_channels.profile_id denormalized).
--
-- TWO QUERIES:
--   Query A — fast:    reads pre-calculated p95 from session_channels.
--             Returns one row per session; on multi-session nights each
--             session's own p95 is shown, not a combined value.
--   Query B — accurate: aggregates the raw value-count histogram from
--             session_channel_values across all sessions per OSCAR day
--             and computes the true combined 95th percentile.
--             Requires schema v7+ (session_channel_values populated).

-- ============================================================
-- QUERY A — Per-session p95 (fast, pre-calculated)
-- ============================================================

SELECT
    date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') AS oscar_date,
    datetime(s.start_time/1000, 'unixepoch', 'localtime')      AS session_start,
    ROUND(sc.p95,    1)                                         AS pulse_p95_bpm,
    ROUND(sc.avg,    1)                                         AS pulse_avg_bpm,
    ROUND(sc.min,    1)                                         AS pulse_min_bpm,
    ROUND(sc.max,    1)                                         AS pulse_max_bpm,
    ROUND(sc.median, 1)                                         AS pulse_median_bpm,
    sc.count                                                    AS sample_count
FROM session_channels sc
JOIN sessions s ON s.id = sc.session_id
WHERE sc.profile_id = #PROFILE_ID
  AND sc.channel_id = 6144  -- OXI_Pulse = 0x1800
  AND s.enabled = 1
  AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime')
          BETWEEN #START_DATE AND #END_DATE
ORDER BY s.start_time;


-- ============================================================
-- QUERY B — True daily p95 from raw value distribution
-- ============================================================
-- Aggregates the value-count histogram stored in session_channel_values
-- across all sessions for each OSCAR day, then finds the first value at
-- which the cumulative count reaches 95 % of the total.
-- Requires schema v7+ (session_channel_values populated).

WITH day_sessions AS (
    -- Collect all OXI_Pulse session_channel rows within the date range,
    -- tagged with their OSCAR date.
    SELECT
        sc.id  AS sc_id,
        date(s.start_time/1000 - 43200, 'unixepoch', 'localtime') AS oscar_date
    FROM session_channels sc
    JOIN sessions s ON s.id = sc.session_id
    WHERE sc.profile_id = #PROFILE_ID
      AND sc.channel_id = 6144  -- OXI_Pulse = 0x1800
      AND s.enabled = 1
      AND date(s.start_time/1000 - 43200, 'unixepoch', 'localtime')
              BETWEEN #START_DATE AND #END_DATE
),
value_dist AS (
    -- Merge value histograms across all sessions within each OSCAR day.
    SELECT
        ds.oscar_date,
        scv.value,
        SUM(scv.count) AS bucket_count
    FROM day_sessions ds
    JOIN session_channel_values scv ON scv.session_channel_id = ds.sc_id
    GROUP BY ds.oscar_date, scv.value
),
day_totals AS (
    SELECT oscar_date, SUM(bucket_count) AS total
    FROM value_dist
    GROUP BY oscar_date
),
cumulative AS (
    SELECT
        vd.oscar_date,
        vd.value,
        SUM(vd.bucket_count) OVER (
            PARTITION BY vd.oscar_date
            ORDER BY vd.value
            ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
        ) AS cum_count
    FROM value_dist vd
)
SELECT
    c.oscar_date,
    MIN(c.value)   AS pulse_p95_bpm,
    dt.total       AS total_samples
FROM cumulative c
JOIN day_totals dt ON dt.oscar_date = c.oscar_date
WHERE c.cum_count >= dt.total * 0.95
GROUP BY c.oscar_date, dt.total
ORDER BY c.oscar_date;
