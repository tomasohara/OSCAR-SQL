# OSCAR - Open Source CPAP Analysis Reporter

## Overview

OSCAR reads data from SD cards produced by CPAP machines. OSCAR's goal is to present the data to the user in an understandable fashion and keep historical records.

"OSCAR" means OSCAR 2.0 (this codebase) unless explicitly stated otherwise. OSCAR 1.7.1 source lives at c:/oscar17/oscar-code — same overall structure but no database layer. Backports to 1.7.1 will be described explicitly when needed.

You are a helpful coding assistant specializing in c++ 17 and Qt.

## Project Structure

- c:/OSCAR/OSCAR-code is the root directory for all OSCAR files
- Key subdirectories of c:/OSCAR/OSCAR-code are:
  - oscar - main program and most user-facing modules
  - oscar/SleepLib - modules that manipulate internal OSCAR data
  - oscar/SleepLib/loader_plugins - modules that read data from CPAP machine's SD cards
  - oscar/database - modules that talk to the database
  - oscar/network - modules that talk to the network
  - oscar/exports - modules that export information from OSCAR to other applications
  - oscar/docs - files that are bound to the application and used during execution (icons, etc.)
  - Notes - design, progress, bug fix, and other notes
  - Building - instructions and aids for building on different platforms
- Main program is oscar/main.cpp
- Qt pro file is oscar/oscar.pro

## Conventions

- All code must be in c++17 and compile clean with Qt 6.10.2.
- Include a brief description of the module in the file header and, in new modules, "Copyright (c) 2026 The OSCAR Team".
- Document all .h and .cpp files in Doxygen style. Document code for understanding by a programmer reading the code.
- Avoid platform-specific APIs; code must be cross-platform compatible.
- Place any progress, design notes, etc. in Notes folder. If searching for a file that was there before, it may have been moved to Notes/temp folder.
- Add new files to oscar.pro as required for compilation.
- Never touch any files in oscar/SleepLib/thirdparty.
- Log all bug fixes in Notes/Developer Notes/BUG_FIXES.md.
- An "OSCAR day" starts at noon and runs until noon of the following calendar day.
- When modifying a UI component, make sure the UX is as user would expect. E.g., when a button is clicked on, show that it has been clicked on.
- If user clicks on Cancel button, cancellation should take effect within a few seconds if not immediately.

## Database

- The application uses SQLite.
- Schema reference: Notes/DATABASE_SCHEMA_REFERENCE.md (all tables, fields, relationships — schema v15)
- ER diagram: Notes/Database-ER-Diagram.png
- Useful queries: Notes/USEFUL_QUERIES.sql, Notes/HOW_TO_USE_QUERIES.md

## Other Notes

- Developer builds incrementally in QtCreator.
- Primary development environment is Windows 11.
- We use git and GitLab for version control: https://gitlab.com/Seeker4/OSCAR-code
- Final product is distributed to about 25 different environments (Mac, Linux, etc.).
- After completing a task that involves tool use, provide a quick summary of the work you've done.
- Never speculate about code you have not opened. Make sure to investigate and read relevant files BEFORE answering questions about the codebase. Never make any claims about code before investigating unless you are certain of the correct answer
- give grounded and hallucination-free answers.

## Debugging

When fixing bugs, always search for ALL root causes before applying a fix. Multiple sessions showed first fixes missed secondary causes (e.g., scrollTo() auto-expand, empty AllMachineSettings list, QLocale::setDefault needed separately).

## Qt Framework Notes

This project uses Qt6 (migrated from Qt5). Be aware of Qt5→Qt6 behavior changes, especially: QDate::toString no longer uses system locale (use QLocale), stylesheet dimming on disabled widgets behaves differently (prefer hide/show).

## Loader

When modifying loader code, confirm scope first: changes must not affect other loaders unless explicitly requested.

## Bug Tracking

Bugs are tracked in GitLab Issues: https://gitlab.com/Seeker4/OSCAR-code/-/issues

When fixing a bug during a session:
1. Create a GitLab issue first (use the Python snippet below) and note the returned IID.
2. Fix the bug.
3. Log the fix in Notes/BUG_FIXES.md.
4. When committing, include `Closes #IID` in the commit message body — GitLab will
   auto-close the issue when pushed to master.

To create an issue from Python (token from env var GITLAB_TOKEN):
```python
import json, os, urllib.request
tok = os.environ["GITLAB_TOKEN"]
data = json.dumps({"title": "...", "description": "...", "labels": "bug"}).encode()
req = urllib.request.Request(
    "https://gitlab.com/api/v4/projects/Seeker4%2FOSCAR-code/issues",
    data=data, headers={"PRIVATE-TOKEN": tok, "Content-Type": "application/json"})
with urllib.request.urlopen(req) as r:
    iid = json.loads(r.read())["iid"]
print(f"Created issue #{iid}")
```

To close an issue (replace IID):
```python
req = urllib.request.Request(
    f"https://gitlab.com/api/v4/projects/Seeker4%2FOSCAR-code/issues/{iid}",
    data=json.dumps({"state_event": "close"}).encode(),
    headers={"PRIVATE-TOKEN": tok, "Content-Type": "application/json"},
    method="PUT")
urllib.request.urlopen(req)
```

If GITLAB_TOKEN is not set, ask the user to provide the token or set the environment variable.

## Workflow

 - After making fixes, log them to Notes/Developer Notes/BUG_FIXES.md.
 - When committing: keep messages concise (one-line subject, 2-3 sentence summary; no change lists). Write summary in lines not exceeding 90 characters. Omit the "co-authored by" line. Include Htmldocs/release_notes.html if changed; if it hasn't been changed, ask whether I want to to update it before committing.
  - To push: git push origin master
  - Ask user before committing or pushing or updating GitLab issues