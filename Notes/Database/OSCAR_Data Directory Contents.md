## OSCAR Data Directory Contents

##### OSCAR20_Data - Main database folder and the starting point for OSCAR 2.0 data

The OSCAR20_Data name is the default name for an OSCAR 2.0 data folder, but the user may give it any name they wish.

| File or Folder | Use                                                          |
| -------------- | ------------------------------------------------------------ |
| Logs           | Debug and connection logs. Used for debugging.               |
| Profiles       | Contains one folder per profile.                             |
| oscar.db       | The OSCAR database. All data for all profiles are in this database. |
| oscar.db-wal   | SQLite work area to support write-ahead operation, transient. |
| oscar.db-shm   | SQLite work area, transient.                                 |



##### **Profile Folder** - Each folder has the name of the profile

| File or Folder     | Use                                                          |
| ------------------ | ------------------------------------------------------------ |
| Journal_000...     | One journal folder per profile. Journal data itself is in the database. This folder and its Summaries sub-folder are usually empty. |
| ResMed_21323213... | One of these for each machine used. There can be any number of these machine folders. Name usually contains manufacturer and some numbers. |
| rxchanges.cache    | Performance enhancement file. Recreated if missing, so need not be saved. |



##### Machine Folder - One folder per machine in a profile folder

| File or Folder | Use                                                          |
| -------------- | ------------------------------------------------------------ |
| Backup         | Contains a backup of the SD cards. Details and completeness depend on the loader for the machine type. For ResMed and PRS1 machines, contains backup files to recreate the database contents. Not all loaders have complete data; some may have only a month or so. |
