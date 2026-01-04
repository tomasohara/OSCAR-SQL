

```sql
CREATE TABLE daily_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    date TEXT NOT NULL,
    machine_id INTEGER,
    
    session_count INTEGER DEFAULT 0,
    enabled_session_count INTEGER DEFAULT 0,
    
    machine settings (embedded in this table)
      PAP mode text (may be n/a = Unknown)
      Min real (may be n/a)
      Max real (may be n/a)
      PS real (may be n/a)
    
    total_hours REAL DEFAULT 0,
    mask_on_hours REAL DEFAULT 0,
    
    ahi REAL DEFAULT 0,
    rdi REAL DEFAULT 0,
    
    -- indices subtable (include only non-zero counts)
       channel id integer
       count integer
       index (count / mask_on_hours)
    
    -- percent subtable (include only non-zero times)
       channel id integer
       time real 
       percent real (time / mask_on_hours)
    
    -- statistics subtable (include only channels with non-zero time)
       channel id integer
	   average real
       min real
       median real
       90th percentile real
       95th percentile real
       99.5th percentile real
    
    is_compliant INTEGER DEFAULT 0,
    has_oximetry INTEGER DEFAULT 0,
    
    calculated_at TEXT DEFAULT CURRENT_TIMESTAMP,
    sessions_hash TEXT,
    
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    FOREIGN KEY (machine_id) REFERENCES machines(id) ON DELETE SET NULL,
    UNIQUE(profile_id, date, machine_id)
)
```

