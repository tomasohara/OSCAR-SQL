# Data Migration Process Review

Based on a review of commit `b74869ab00229e5d0a1da41300d36d7770ad55e6`, the overarching data migration implementation is structurally sound, leveraging proper SQLite `ON CONFLICT` patterns and keeping data access siloed in the `Repositories`.

However, addressing specific paths that bypass data migration completely or permanently halt it upon interruption:

## 1. Manual Profile Restoration (Entirely Unsupported)
**Scenario**: A user bypasses the traditional UI/Import tools by dropping a profile backup (`Profiles/UserName` containing `.shg` files) directly into the app data directory.
**The Problem**: In OSCAR 2.0, this manual folder drop is no longer supported at all. `ProfileSelector` now exclusively populates the UI list by reading directly from the `profiles` SQL table (`profileRepo.findAll()`). **It no longer scans the `Profiles/` directory on the filesystem.** 
**Impact**: The profile will simply never appear in the UI, requiring the user to use the official "Import OSCAR 1.x Data" tool or create a new profile. My previous assumption that the ProfileSelector lazily auto-registered rogue folders was completely incorrect.

## 2. Interrupting The Process / Crash During Import (Stuck Migration)
**Scenario**: OSCAR begins exporting XML to SQLite during `Preferences::Open()` on startup and is abruptly aborted by a power-failure or crash.
**The Problem**: In `Preferences::Open()`, settings are loaded individually into the database. If it is stopped in the middle of this loop, `QFile::remove` doesn't execute. Upon launching the application again, it checks if `rows.isEmpty()`. Because there are now *some* recovered keys in the database, this equals `false`.
**Impact**: The system loads the half-finished attributes and skips looking at `Preferences.xml` ever again. Any keys still sitting in the previous XML log are ignored permanently. 

## 3. OSCAR 1.7 Import (Migration Safe)
**Scenario**: Utilizing the *Import OSCAR 1.x* feature from the application menu itself. 
**Result**: This works properly. Directly inside `MainWindow::on_action_Import_OSCAR_Data_triggered()`, after the importer handles copying over the logic and committing the profile to the database structure, both `importLegacyNamedLayouts()` and `importLegacyProfileLayouts()` are manually forced. Because the databases are aware of the imported profiles, this bridges everything cleanly. 

---

## Code Clarity and Design Remarks

1. **Zombie / Undeletable Preferences**: `Preferences::Save()` was ported to purely invoke an `INSERT / UPDATE` statement inside a loop. However, if code elsewhere deletes an attribute explicitly via `p_pref->Erase()`, it is stripped from application memory but left in the SQLite table. **There are no `DELETE` clauses generated.** On the next UI startup, the `Open()` method pulls it out of the database and resurrects the deleted setting natively back into working memory.
2. **Lack of Performance Transactions**: `importLegacyNamedLayouts`, `importLegacyProfileLayouts()`, and `Preferences::Save()` all push loops that instantiate queries independently. Surrounding each of these with a single `db.transaction()` / `db.commit()` block would massively increase performance and securely solve the partial-import/crashes issue detailed in Point #2. 
3. **Small Layout Appends**: Within `SaveGraphLayoutSettings::itemChanged(QListWidgetItem *item)`, if a user types a label longer than `maxDescriptionLen`, `desc.append("...")` runs. Instead of sub-stringing or shortening the string properly, it directly appends `...` to the end of a label over the allowed length.
