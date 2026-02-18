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
  - oscar/exports - modules that export information from OSCAR to other applications
  - oscar/docs - files that are bound to the application and used during execution (icons, etc.)
  - Notes - design and progress notes
- Main program is oscar/main.cpp
- Qt pro file is oscar/oscar.pro

## Conventions

- All code must be in c++17 and compile clean with Qt 6.10.2.
- File headers should include a brief description of the module and, in new modules, "Copyright (c) 2026 The OSCAR Team".
- All .h and .cpp files are to be well documented in Doxygen style.
- Avoid platform-specific APIs; code must be cross-platform compatible.
- Place any progress, design notes, etc. in Notes folder. If searching for a file that was there before, it may have been moved to Notes/temp folder.
- Add new files to oscar.pro as required for compilation.
- Never touch any files in oscar/SleepLib/thirdparty.

## Other Notes

- The application uses SQLite.
- Application is normally built using QtCreator and by the developer who can forward error messages.
- Primary development environment is Windows 11.
- We use git and GitLab for version control.
- Final product is distributed to about 25 different environments (Mac, Linux, etc.).

