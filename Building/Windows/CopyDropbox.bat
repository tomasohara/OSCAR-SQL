@echo off
setlocal enabledelayedexpansion

::: Current Directory is the shadowBuildDir
set shadowBuildDir=%cd%

git fetch --tags
for /f "tokens=*" %%i in ('git describe --exact-match --tags 2^>nul') do set TAG=%%i

if defined TAG (
    set COPIED=0
    for %%f in (%shadowBuildDir%\Installer\*.exe) do (
        echo %%~nf | findstr /i "\-plus" >nul && (
            echo Installer file contains -plus so not copied to Dropbox
        ) || (
            copy "%%f" "C:\Users\Guy\Dropbox\OSCAR Files"
            set COPIED=1
        )
    )
    if !COPIED!==1 echo Copied installer for: %TAG%
) else (
    echo Current commit is not tagged
)