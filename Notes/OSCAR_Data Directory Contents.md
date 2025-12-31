OSCAR_Data Directory Contents

| File or Folder  | Use                                                          |
| --------------- | ------------------------------------------------------------ |
| Logs            | debug and connection logs                                    |
| Profiles        | Contains one folder per profile                              |
| preferences.xml | Global preferences. This is the only XML file left that OSCAR uses. It could probably be moved into the database, but currently it has to be opened before the database is initialized. Will be recreated if missing. |
| oscar.db        | The OSCAR database                                           |
| oscar.db-wal    | SQLite work area to support write-ahead operation            |
| oscar.db-shm    | SQLite work area                                             |

Profile Folder - each folder has the name of the profile

| File or Folder                           | Use                                                          |
| ---------------------------------------- | ------------------------------------------------------------ |
| Journal_0000000                          | Contains journal data. One journal folder per profile. May be converted to a database table later. |
| ResMed_21323213...                       | One of these for each machine used. There can be any number of these machine folders. |
| daily.shg, overview.shg, rxchanges.cache | Performance enhancement files. All can be recreated as needed, so need not be saved. |

