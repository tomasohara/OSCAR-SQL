# OSCAR QSettings / Registry Keys

OSCAR uses Qt's `QSettings` class to persist lightweight, cross-session state that must
survive database switches or be available before the database is open. On each platform
the backing store is:

| Platform | Location |
|----------|----------|
| Windows  | `HKEY_CURRENT_USER\SOFTWARE\OSCAR_Team\OSCAR[-x] 2.0` |
| Linux    | `~/.config/OSCAR_Team/OSCAR[-x] 2.0.conf` |
| macOS    | `~/Library/Preferences/com.oscar_team.oscar_[-x]_2_0.plist` |

`[-x]` is the pre-release suffix (e.g., `-beta`, `-rc`); absent for release builds.
The suffix comes from `getPrereleaseSuffix()` and is appended to the application name
set by `QCoreApplication::setApplicationName()` in `main.cpp`.

---

## Key Hierarchy

```
OSCAR[-x] 2.0
  ├── GFXEngine
  ├── OpenGLCompatibilityCheck        (Windows only)
  ├── Fingerprint                     (Windows only)
  ├── RecentDatabases
  ├── ShowDatabaseMenu
  ├── Settings\
  │     ├── AppData
  │     └── Language
  ├── <DataFolderName>\               (one per database folder)
  │     └── MainWindow\
  │           └── geometry
  └── CloudAuth\
        └── <providerKey>\           (one per OAuth2 provider)
              ├── refreshToken
              └── accessToken
```

---

## Top-Level Keys

### GFXEngine
- **Type:** `uint`
- **Values:** 0 = OpenGL (default), 1 = ANGLE (Windows only), 2 = Software
- **Source:** `oscar/SleepLib/common.h` (`GFXEngineSetting = "GFXEngine"`),
  `oscar/SleepLib/common.cpp`, `oscar/main.cpp`
- **Notes:** Read before `QApplication` is created so the correct rendering backend
  can be selected. Written by the shift-key-at-launch shortcut, the `--legacy`
  command-line switch, and the crash-recovery path.

### OpenGLCompatibilityCheck *(Windows only)*
- **Type:** `bool`
- **Source:** `oscar/mainwindow.cpp`
- **Notes:** Set to `true` immediately before OpenGL initialisation. If this key is
  present at the *next* startup, OSCAR crashed during the previous OpenGL test.
  The engine is then forced to Software and the key is removed. Absent on a clean run.

### Fingerprint *(Windows only)*
- **Type:** `string` (SHA-256 hex)
- **Source:** `oscar/mainwindow.cpp`
- **Notes:** Hash derived from the Windows `MachineGuid` (see below). If it differs
  from the stored value at startup, `TestWindowsOpenGL()` is called to re-validate
  the graphics hardware. Written on a clean exit.

### RecentDatabases
- **Type:** `QStringList`
- **Source:** `oscar/database/recent_databases.cpp`, `oscar/database/database_delete_dialog.cpp`
- **Notes:** Canonical, deduplicated list of recently opened database folder paths
  (maximum 10). Entries whose `oscar.db` no longer exists are pruned automatically
  on each read. Maintained by `RecentDatabases::add()`, `remove()`, and `entries()`.

### ShowDatabaseMenu
- **Type:** `bool` (default `false`)
- **Source:** `oscar/SleepLib/appsettings.h`
- **Notes:** Controls visibility of the Database submenu. Stored in `QSettings` rather
  than the database so the setting survives switching between database folders.

---

## Settings\ Group

Keys written without `beginGroup()`, so the full key name includes the prefix
(e.g., `Settings/AppData`).

### Settings/AppData
- **Type:** `string` (absolute path)
- **Source:** `oscar/SleepLib/preferences.cpp` (`GetAppData()` / `SetAppData()`),
  `oscar/database/recent_databases.cpp` (`RecentDatabases::setActive()`)
- **Notes:** Full path to the last database folder opened. Read before `QApplication`
  is created. On first run, set to `<Documents>/OSCAR[-x]_Data`.

### Settings/Language
- **Type:** `string` (BCP-47 language code, e.g., `en_US`, `fr`, `zh_CN`)
- **Source:** `oscar/translation.h` (`LangSetting = "Settings/Language"`),
  `oscar/translation.cpp`, `oscar/main.cpp`, `oscar/SleepLib/profiles.cpp`
- **Notes:** Selected UI language. An empty string (or an unrecognised code) causes
  the language-selection dialog to appear at the next startup. The `--language`
  command-line switch clears this key to force re-selection.

---

## \<DataFolderName\>\ Group

One group per database folder. The group name is the *basename* of the data folder
(e.g., `OSCAR20_Data` or `OSCAR20-beta_Data`). Used via `beginGroup(QFileInfo(GetAppData()).fileName())`.

### \<DataFolderName\>/MainWindow/geometry
- **Type:** `QByteArray` (from `QWidget::saveGeometry()`)
- **Source:** `oscar/mainwindow.cpp`
- **Notes:** Main window position and size. Saved in `closeEvent()`, restored in the
  `MainWindow` constructor. Scoped per data folder so each database can have its own
  window layout.

---

## CloudAuth\ Group

One sub-group per OAuth2 provider, keyed by a provider identifier string
(e.g., `"oscar-cloud"`). Used via `beginGroup("CloudAuth/<providerKey>")`.

### CloudAuth/\<providerKey\>/refreshToken
- **Type:** `string`
- **Source:** `oscar/network/oauth2_handler.cpp`
- **Notes:** OAuth2 refresh token for the named provider. Cleared by `clearTokens()`.

### CloudAuth/\<providerKey\>/accessToken
- **Type:** `string`
- **Source:** `oscar/network/oauth2_handler.cpp`
- **Notes:** OAuth2 access token for the named provider. Cleared by `clearTokens()`.

---

## External System Registry Read *(Windows only)*

This is a **read-only** access to a different registry hive, not part of OSCAR's own
`QSettings` namespace.

| Hive | Key | Value | Type |
|------|-----|-------|------|
| `HKEY_LOCAL_MACHINE` | `SOFTWARE\Microsoft\Cryptography` | `MachineGuid` | string |

- **Source:** `oscar/mainwindow.cpp`
- **Notes:** The Windows machine GUID is read to generate the `Fingerprint` value.
  OSCAR opens this with `QSettings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography", QSettings::NativeFormat)`.

---

## What Is NOT in QSettings

Most user-visible preferences (AHI display mode, graph colours, machine settings, etc.)
are stored in the **SQLite database** (`oscar.db`) via `AppPreferencesRepository`,
`ProfileRepository`, and related classes. Dialog state for Backup, Restore,
Journal Notes Export, and Report Exporter is also persisted in the database.
See `Notes/DATABASE_SCHEMA_REFERENCE.md` for the full schema.
