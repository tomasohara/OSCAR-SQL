# How to Use the OSCAR Database Queries

## Database Location

The OSCAR database file is typically located at:
```
{User Data Directory}/OSCAR_Data/oscar.db
```

Common locations:
- **Windows**: `C:\Users\{Username}\Documents\OSCAR_Data\oscar.db`
- **macOS**: `~/Library/Application Support/OSCAR_Data/oscar.db`
- **Linux**: `~/.local/share/OSCAR_Data/oscar.db`

## Tools to Run the Queries

### Option 1: DB Browser for SQLite (Recommended)
1. Download from: https://sqlitebrowser.org/
2. Open the `oscar.db` file
3. Go to "Execute SQL" tab
4. Copy and paste any query from `USEFUL_QUERIES.sql`
5. Click "Execute" (Play button)

### Option 2: SQLite Command Line
```bash
# Open the database
sqlite3 /path/to/oscar.db

# Enable column headers and better formatting
.headers on
.mode column

# Run a query (copy from USEFUL_QUERIES.sql)
SELECT p.name, COUNT(m.id) as machines 
FROM profiles p 
LEFT JOIN machines m ON p.id = m.profile_id 
GROUP BY p.name;

# Exit
.quit
```

### Option 3: Qt SQL Browser (if you have Qt installed)
Use the Qt SQL Browser tool that comes with Qt Creator.

## Most Useful Queries

### 1. View Profiles and Their Machines
```sql
SELECT 
    p.name as profile_name,
    m.serial_number,
    m.model_name,
    m.brand
FROM profiles p
LEFT JOIN machines m ON p.id = m.profile_id
ORDER BY p.name;
```

### 2. Recent Sessions with Summary Data
```sql
SELECT 
    datetime(s.start_time/1000, 'unixepoch', 'localtime') as date,
    ROUND(ss.ahi, 2) as AHI,
    ROUND(ss.hours_used, 2) as Hours,
    ss.obstructive_count + ss.central_count + ss.hypopnea_count as Events,
    p.name as profile
FROM session_summaries ss
JOIN sessions s ON ss.session_id = s.id
JOIN machines m ON s.machine_id = m.id
JOIN profiles p ON m.profile_id = p.id
ORDER BY s.start_time DESC
LIMIT 30;
```

### 3. Monthly Statistics
```sql
SELECT 
    strftime('%Y-%m', s.start_time/1000, 'unixepoch', 'localtime') as month,
    COUNT(*) as nights,
    ROUND(AVG(ss.ahi), 2) as avg_ahi,
    ROUND(AVG(ss.hours_used), 2) as avg_hours
FROM session_summaries ss
JOIN sessions s ON ss.session_id = s.id
GROUP BY month
ORDER BY month DESC;
```

## Understanding Time Fields

OSCAR stores timestamps as milliseconds since Unix epoch. To convert:

- **To readable date**: `datetime(time_field/1000, 'unixepoch', 'localtime')`
- **To date only**: `date(time_field/1000, 'unixepoch', 'localtime')`
- **To hours**: `duration / 3600000.0`

## Common Filters

### Filter by Date Range
```sql
WHERE s.start_time >= strftime('%s', '2024-01-01') * 1000
  AND s.start_time < strftime('%s', '2025-01-01') * 1000
```

### Filter by Profile
```sql
WHERE p.name = 'ProfileName'
```

### Filter by Machine
```sql
WHERE m.serial_number = 'SerialNumber'
```

### Only Enabled Sessions
```sql
WHERE s.enabled = 1
```

## Exporting Results

### From DB Browser for SQLite
1. Run your query
2. Click "Save results" button
3. Choose format (CSV, JSON, SQL, etc.)

### From SQLite Command Line
```bash
sqlite3 oscar.db << EOF
.headers on
.mode csv
.output results.csv
SELECT * FROM profiles;
.quit
EOF
```

## Tips

1. **Start Small**: Test queries on recent data first using `LIMIT`
2. **Use Comments**: The `--` starts a comment in SQL
3. **Check Counts**: Use `COUNT(*)` to verify data before detailed queries
4. **Round Numbers**: Use `ROUND(value, decimals)` for cleaner output
5. **Save Favorites**: Keep commonly used queries in a text file

## Safety Notes

⚠️ **Read-Only Operations**: All queries in `USEFUL_QUERIES.sql` are SELECT statements and will NOT modify your data.

⚠️ **Backup First**: Always backup `oscar.db` before experimenting with new queries.

⚠️ **Don't Run While OSCAR is Open**: Close OSCAR before running queries to avoid database locks.

## Troubleshooting

### "Database is locked"
- Close OSCAR application
- Make sure no other programs have the database open

### "No such table"
- Verify you're using the correct database file
- Check that the database has been initialized by OSCAR

### Times are wrong
- Check the timezone in the datetime() function
- Use 'localtime' for local time or 'utc' for UTC

## Need More Help?

See the full collection of queries in `USEFUL_QUERIES.sql` which includes:
- Profile and machine management
- Session analysis
- Compliance tracking
- Data quality checks
- Statistical summaries
