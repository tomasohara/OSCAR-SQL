import sqlite3

conn = sqlite3.connect("oscar.db")
tables = conn.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").fetchall()

for (table,) in tables:
    count = conn.execute(f"SELECT COUNT(*) FROM [{table}]").fetchone()[0]
    print(f"{table}: {count}")

conn.close()