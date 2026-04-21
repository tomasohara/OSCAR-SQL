---
  Code Review: Schema v14 — Graph Layouts + App Preferences to DB

  Commits covered: b74869ab (primary), 7bd16d15, 86241286

  Overview

  The schema v14 commit is a substantial, well-structured migration that moves the last two file-based persistence
  mechanisms (.shg layout files and Preferences.xml) into SQLite. The architecture is clean: a new
  GraphLayoutsRepository handles both per-profile current layouts and shared named slots via a single unified table, and
   gGraphView serialization is properly extracted into serializeSettings()/deserializeSettings() methods. The DB-or-file
   fallback pattern is consistent. The one-shot legacy importers at startup are idiomatic for this kind of migration.

---
  Issues

  1. migrateV13ToV14 has no transaction wrapper (database_schema.cpp)

  The migration does four distinct DDL/DML steps. If setSchemaVersion fails after the tables are created, the DB is left
   with schema version 13 but the new tables already present. In practice the migration is idempotent (all steps are IF
  NOT EXISTS or handle "duplicate column"), so a retry on next launch would succeed — but this is relying on a happy
  coincidence rather than deliberate atomicity. Wrap the body in db.transaction() / db.commit() / db.rollback().

  2. peekVersion lambda duplicated in main.cpp (main.cpp)

  The identical 6-line lambda is copy-pasted into both importLegacyNamedLayouts() and importLegacyProfileLayouts(). Make
   it a static file-scope helper:

  static int peekShgVersion(const QByteArray& data) {
      if (data.size() < 6) return 0;
      quint16 ver;
      memcpy(&ver, data.constData() + 4, 2);
      return static_cast<int>(ver);
  }

  3. importLegacyNamedLayouts / importLegacyProfileLayouts lack static (main.cpp)

  Both functions are defined at file scope without static, giving them external linkage. If any other TU ever defines a
  function with the same name (unlikely but possible), it's a linker clash. Mark them static or put them in an anonymous
   namespace.

  4. loadNamedLayout selects is_current but ignores it (graph_layouts_repository.cpp:135)

  The SELECT includes is_current (column index 3), but the mapping code jumps to description at index 4, skipping 3
  entirely and hardcoding out.isCurrent = false. Either remove is_current from the SELECT, or map it explicitly. The
  current code will silently mismap if column ordering ever changes.

  -- current:
  SELECT id, view_name, slot_index, is_current, description, format_version, data ...
  -- simpler:
  SELECT id, view_name, slot_index, description, format_version, data ...

  5. openOk global not set on DB-path save/load (gGraphView.cpp)

  The legacy openOk = f.open(...) global is set in the file-fallback branch of both SaveSettings and LoadSettings, but
  not in the DB branch. If any caller checks openOk after a DB-routed call, it will see stale state from the previous
  file operation. This is an existing code smell but the new DB path makes it inconsistent. Worth auditing callers of
  openOk.

  6. extern const quint16 gVversion = 5; in gGraphView.cpp — correct but non-idiomatic

  Using extern on a definition is valid C++ (needed here because const at file scope has internal linkage by default),
  but it reads oddly. The conventional idiom is extern const quint16 gVversion; in the header (as done) and just const
  quint16 gVversion = 5; in the .cpp — but that would give it internal linkage and break the extern declaration. The
  current form is the right solution; just add a comment so the next reader doesn't remove the extern:

  extern const quint16 gVversion = 5;  // extern needed: const at file scope defaults to internal linkage

---
  Positives

  - Serialization extraction is clean. serializeSettings() / deserializeSettings() are well-factored, testable
    independently of I/O, and the fallback to file is transparent to callers.
  - Upsert SQL is correct. Both saveCurrentLayout and saveNamedLayout use ON CONFLICT ... DO UPDATE targeting the exact
    partial unique indexes — the predicate in the ON CONFLICT clause matches the index WHERE clause.
  - Description-preservation design in saveNamedLayout is well-commented: the DO UPDATE clause intentionally omits
    description so updates don't clobber user-edited names. Good.
  - Legacy importers are defensive: check file existence, handle partial success, clean up only after confirmed DB
    write, and attempt to remove the now-empty directory.
  - Dead code removed: The #if 0 version-upgrade blocks in deserializeSettings and the file-folder management in
    saveGraphLayoutSettings.cpp are cleanly gone.
  - Schema migration is additive-only — no existing data is touched. The blob_value ADD COLUMN is handled with the
    "ignore duplicate column" idiom, making it safely re-runnable.

---
  Minor / Style

  - saveCurrentLayout comment says "Upsert: on conflict update data, format_version, and timestamp; preserve
    description." — but there's no description column in the INSERT column list for current layouts, so "preserve
    description" isn't relevant there. Slightly misleading comment.
  - The createSaveFolder() body in saveGraphLayoutSettings.cpp is now a no-op with just a comment. Consider removing the
      method entirely and deleting the call in the constructor, rather than leaving a vestigial stub.
  - graph_layouts_repository.cpp opens DatabaseManager::instance().database() at the top of every method. This is the
    project-wide convention, so it's fine — but it means a failed DB open triggers a silent return false in the load path
    with no qWarning. The save paths log a warning; the load paths should too for consistency.

---
  Summary

  Solid implementation of a meaningful architectural improvement. The four issues above are worth addressing before
  release — the transaction wrapper and the loadNamedLayout column mismatch are the most important. The peekVersion
  duplication and missing static are low risk but easy to fix.