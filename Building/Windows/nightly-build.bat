@echo off
:::
::: nightly-build.bat
:::
::: Checks whether GitLab master has new commits since the last nightly build.
::: If so, builds OSCAR, renames the installer with a "-nightly" suffix, and
::: copies it to the Dropbox folder.  Replaces any previous nightly build.
:::
::: Usage: run from any directory.  No arguments needed.
:::
setlocal enabledelayedexpansion

set DROPBOX_DIR=C:\Users\Guy\Dropbox\OSCAR Files
set REPO_DIR=C:\OSCAR\OSCAR-code
set BUILD_DIR=C:\OSCAR\OSCAR-code\Building\Windows
set BUILD_SCRIPT=buildall-qt6.bat
set INSTALLER_DIR=C:\OSCAR\OSCAR-code\build-oscar-win_64_bit\Installer

:: -----------------------------------------------------------------------
:: Step 1: Fetch latest from GitLab and record the remote master HEAD hash.
:: -----------------------------------------------------------------------
cd /d "%REPO_DIR%"
echo Fetching latest from origin...
git fetch origin master
if %errorlevel% neq 0 (
    echo ERROR: git fetch failed.
    exit /b 1
)

for /f %%h in ('git rev-parse origin/master') do set REMOTE_FULL_HASH=%%h
echo Remote HEAD: %REMOTE_FULL_HASH%

:: -----------------------------------------------------------------------
:: Step 2: Find the current nightly build and extract its commit hash.
::
::   Filename pattern:  OSCAR-<version>-Win64-<hash>-nightly.exe
::   The hash is the field immediately before "-nightly".
::   We write the base name to a temp file and use awk to split on "-".
:: -----------------------------------------------------------------------
set NIGHTLY_FILE=
set NIGHTLY_NAME=
for %%f in ("%DROPBOX_DIR%\*Win64*nightly*.exe") do (
    set NIGHTLY_FILE=%%f
    set NIGHTLY_NAME=%%~nf
)

set NIGHTLY_HASH=
if defined NIGHTLY_NAME (
    echo !NIGHTLY_NAME! > "%TEMP%\oscar_nightly_name.txt"
    for /f %%h in ('awk -F- "{print $(NF-1)}" "%TEMP%\oscar_nightly_name.txt"') do set NIGHTLY_HASH=%%h
    del /q "%TEMP%\oscar_nightly_name.txt" 2>nul
    echo Current nightly hash: !NIGHTLY_HASH!
) else (
    echo No existing nightly build found - will build fresh.
)

:: -----------------------------------------------------------------------
:: Step 3: Compare hashes.
::
::   The nightly filename carries an abbreviated hash.  Check whether the
::   remote full hash begins with that abbreviation.  If it matches, the
::   remote has not advanced and there is nothing to do.
:: -----------------------------------------------------------------------
if defined NIGHTLY_HASH (
    echo %REMOTE_FULL_HASH% | findstr /B /I "!NIGHTLY_HASH!" >nul
    if !errorlevel! == 0 (
        echo Remote HEAD matches current nightly build. No action needed.
        exit /b 0
    )
)

echo New commits detected. Building nightly for %REMOTE_FULL_HASH%...

:: -----------------------------------------------------------------------
:: Step 4: Check out the specific commit so the build reflects it exactly.
:: -----------------------------------------------------------------------
cd /d "%REPO_DIR%"
git checkout %REMOTE_FULL_HASH%
if %errorlevel% neq 0 (
    echo ERROR: git checkout %REMOTE_FULL_HASH% failed.
    exit /b 1
)

:: -----------------------------------------------------------------------
:: Step 5: Run the full build from the required working directory.
:: -----------------------------------------------------------------------
cd /d "%BUILD_DIR%"
git stash
call %BUILD_SCRIPT%
git stash pop
set BUILD_RESULT=%errorlevel%

:: -----------------------------------------------------------------------
:: Step 6: Return to master branch regardless of build outcome.
:: -----------------------------------------------------------------------
cd /d "%REPO_DIR%"
git checkout master
if %errorlevel% neq 0 (
    echo WARNING: git checkout master failed - you may be in detached HEAD state.
)

if %BUILD_RESULT% neq 0 (
    echo ERROR: Build failed with error %BUILD_RESULT%.
    exit /b %BUILD_RESULT%
)

:: -----------------------------------------------------------------------
:: Step 7: Locate the newly-built installer.
:: -----------------------------------------------------------------------
set NEW_INSTALLER=
set NEW_BASE_NAME=
for %%f in ("%INSTALLER_DIR%\OSCAR*Win64*.exe") do (
    set NEW_INSTALLER=%%f
    set NEW_BASE_NAME=%%~nf
)

if not defined NEW_INSTALLER (
    echo ERROR: No installer found in %INSTALLER_DIR%.
    exit /b 1
)

echo Found installer: !NEW_INSTALLER!

:: -----------------------------------------------------------------------
:: Step 8: Remove the old nightly build from Dropbox, then copy the new
::         installer with "-nightly" appended before ".exe".
:: -----------------------------------------------------------------------
del /q "%DROPBOX_DIR%\*Win64*nightly*.exe" 2>nul

set NIGHTLY_NEW_NAME=!NEW_BASE_NAME!-nightly.exe
copy "!NEW_INSTALLER!" "%DROPBOX_DIR%\!NIGHTLY_NEW_NAME!"
if %errorlevel% neq 0 (
    echo ERROR: Failed to copy installer to Dropbox.
    exit /b 1
)

echo.
echo Nightly build complete: %DROPBOX_DIR%\!NIGHTLY_NEW_NAME!
endlocal
exit /b 0
