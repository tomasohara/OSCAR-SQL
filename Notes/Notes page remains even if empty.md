I have a complete picture now. Here's the assessment:

---

## How Difficult Is This Fix?

**Short answer: Low-to-medium complexity. The infrastructure already exists; it's mostly plumbing.**

---

### Root Cause

In `Daily::Unload()` (`daily.cpp:2352`), when a journal session has `IsChanged() == true`, it unconditionally calls `session->StoreToDatabase()`. That write always creates/updates the `sessions` row, even if all meaningful fields have been cleared. On next app launch, that row is loaded back, creates a `Day` object for `MT_JOURNAL`, and `UpdateCalendarDay()` at line 1018–1039 sees a non-null `journalDay` → bold calendar text.

The "meaningful fields" that could exist in a journal session are:
- `Journal_Notes` (text)
- `Journal_Weight` / `Journal_BMI`
- `Journal_ZombieMeter`
- `Bookmark_Start` / `Bookmark_End` / `Bookmark_Notes`
- `LastUpdated` — always written by `Unload()`, **should not count**

The code already correctly removes `Journal_Notes`, `Journal_Weight`, and `Journal_ZombieMeter` from `session->settings` when those fields are zeroed/cleared. Bookmarks are not removed when the list goes empty — that's a small edge case to handle.

---

### What the Fix Would Require

**1. An `isJournalSessionEmpty()` helper** — check that `settings` contains nothing meaningful beyond `LastUpdated`. Straightforward.

**2. A delete path in `Unload()`** — instead of saving the session, delete it. Two things need to happen:
- **Database**: Call `SessionRepository::remove(session->id())` — the method already exists at `session_repository.cpp:354`. Need to verify whether the schema has `ON DELETE CASCADE` for `session_settings`, or whether a separate settings delete is needed.
- **Memory**: Call `day->removeSession(session)` — already exists at `day.cpp:1423`. This also removes `MT_JOURNAL` from the Day's machine map if no sessions remain, so `FindDay(date, MT_JOURNAL)` will return `nullptr` and `UpdateCalendarDay()` will no longer bold the date.

**3. Session DB primary key** — `SessionRepository::remove()` takes a DB `id`, not the OSCAR session ID. Need to confirm `Session` stores its DB primary key (likely `session->id()` — worth a quick check).

**4. Already-persisted orphan sessions** — Sessions already in the DB from before this fix won't be cleaned up automatically unless the user visits that date and navigates away (triggering `Unload()`). A one-time migration pass could be added, but it's optional.

---

### Risk Profile

- **Low risk of regression**: The delete path only fires when `journal->IsChanged()` is true AND all meaningful fields are absent. A session with weight, bookmarks, or notes would not be touched.
- **One edge case**: Bookmarks — the existing code sets `Bookmark_Start` even when the list is empty. The helper needs to check the list length, not just key presence.
- **Future dates specifically**: No special handling needed; the logic is date-agnostic.

---

### Estimate

The actual code change is probably **30–60 lines** spread across `daily.cpp` and possibly a small schema verification. The research (confirming session DB key access and cascade behavior) is the main uncertainty. Call it **2–4 hours** of careful work including testing.