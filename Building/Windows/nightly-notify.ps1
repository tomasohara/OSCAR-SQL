# nightly-notify.ps1
# Shows a modal error dialog. Called by nightly-build.bat on failure.
# Usage: nightly-notify.ps1 "message text"
param([string]$Message)

Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.MessageBox]::Show(
    $Message,
    "OSCAR Nightly Build",
    [System.Windows.Forms.MessageBoxButtons]::OK,
    [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
