**Backup/Restore Review Findings**

1. Critical: `Replace` mode can irreversibly delete the existing profile before restore succeeds.
- `profile_restore.cpp:720` deletes the existing profile in `resolveUsernameConflict()`.
- New data is restored later in a separate transaction (`profile_restore.cpp:1103`).
- If restore fails after deletion, user data is lost.

2. High: backup export/import is not robust for multiline text values; rows can be silently dropped.
- Export writes raw text literals that may contain newlines (`sql_exporter.cpp:309`, `sql_exporter.cpp:312`).
- Restore reads line-by-line (`profile_restore.cpp:870`) and parses only single-line INSERTs (`profile_restore.cpp:196`).
- Parse failures are skipped silently (`profile_restore.cpp:874`), which can cause partial restore without explicit failure.

3. Medium: checksum validation can pass even when checksum computation fails due to I/O/read issues.
- Validation checks mismatch only when computed hash is non-empty (`profile_restore.cpp:428`).
- If hash computation fails and returns empty, validation does not fail.
- Only `database_export` checksum is validated; `manifest` and `package` checksums are not enforced.

4. Medium: duplicate signal connections on repeated restore attempts can cause repeated handlers/messages.
- Each restore click reconnects signals (`restoredialog.cpp:342`) without `Qt::UniqueConnection` or disconnect.
- Retrying after failure can trigger duplicate progress/failure/success handler execution.

**Open Questions / Assumptions**

1. Assumed multiline text values are possible in exported fields.
2. This was a static code review only; no runtime backup/restore tests were run.

**Summary**

Highest risks are destructive replace semantics before atomic restore and fragile SQL line parsing that can silently lose rows. Dialog signal wiring and checksum handling also have reliability/integrity gaps.
