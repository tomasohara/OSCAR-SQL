# OSCAR - Open Source CPAP Analysis Reporter

## Overview

OSCAR reads data from SD cards produced by CPAP machines. OSCAR's goal is to present the data to the user in an understandable fashion and keep historical records.

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
  - Notes - design and progress notes
- Main program is oscar/main.cpp
- Qt pro file is oscar/oscar.pro

## Conventions

- All code must be in c++17 and compile clean with Qt 6.10.2.
- Include a brief description of the module and in the file header and, in new modules, "Copyright (c) 2026 The OSCAR Team".
- Document all .h and .cpp files in Doxygen style. Document code for understanding by a programmer reading the code.
- Avoid platform-specific APIs; code must be cross-platform compatible.
- Place any progress, design notes, etc. in Notes folder. If searching for a file that was there before, it may have been moved to Notes/temp folder.
- Add new files to oscar.pro as required for compilation.
- Never touch any files in oscar/SleepLib/thirdparty.
- Log all bug fixes in Notes/BUG_FIXES.md.
- An "OSCAR day" starts at noon and runs until noon of the following calendar day.
- When I ask you to commit changes, if Htmldocs/release_notes.html have not been changed, ask if I want to update them first.
- Keep commit messages concise: a one-line subject and a 2-3 sentence summary. Avoid listing individual changes — anyone interested can read the diff.
- When committing changes, include Htmldocs/release_notes.html if changed. Omit the "co-authored by" line.
- If I ask you to push the changes, the command to use is usually "git push origin master"

## Other Notes

- The application uses SQLite.
- Developer builds application using QtCreator and will forward error messages.
- Primary development environment is Windows 11.
- We use git and GitLab for version control.
- Final product is distributed to about 25 different environments (Mac, Linux, etc.).

## Debugging

When fixing bugs, always search for ALL root causes before applying a fix. Multiple sessions showed first fixes missed secondary causes (e.g., scrollTo() auto-expand, empty AllMachineSettings list, QLocale::setDefault needed separately).

## Qt Framework Notes

This project uses Qt6 (migrated from Qt5). Be aware of Qt5→Qt6 behavior changes, especially: QDate::toString no longer uses system locale (use QLocale), stylesheet dimming on disabled widgets behaves differently (prefer hide/show).

## Loader

When modifying loader code, confirm scope first: changes must not affect other loaders unless explicitly requested.

## Workflow

After making fixes, log them to Notes/BUG_FIXES.md per project convention.
