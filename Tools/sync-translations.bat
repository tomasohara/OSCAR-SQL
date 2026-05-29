@echo off
set "_SF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$f=Get-Content -LiteralPath $env:_SF; $n=[Array]::IndexOf($f,'## PS_START'); iex ($f[($n+1)..($f.Count-1)] -join [char]10)"
exit /b
## PS_START

# sync-translations.bat  (batch/PowerShell polyglot)
# Syncs updated translation files from Dropbox to the OSCAR repo.
#
# In TEST_MODE the script reports what would be copied but makes no changes.
# Set $TEST_MODE = $false to enable actual copying and git commit/push.

$TEST_MODE = $true

$SOURCE = "C:\Users\Guy\Dropbox\Translations"
$DEST   = "C:\OSCAR\OSCAR-code\Translations"
$REPO   = "C:\OSCAR\OSCAR-code"

$copied  = [System.Collections.Generic.List[string]]::new()
$reasons = @{}

Write-Host ""
Write-Host "Syncing translation files..."
Write-Host "  Source : $SOURCE"
Write-Host "  Dest   : $DEST"
if ($TEST_MODE) { Write-Host "  ** TEST MODE - no files will be copied **" }
Write-Host ""

$srcFiles = Get-ChildItem -Path $SOURCE -File | Sort-Object Name
foreach ($srcFile in $srcFiles) {
    $destPath = Join-Path $DEST $srcFile.Name
    $doCopy   = $false
    $reason   = ""

    if (-not (Test-Path $destPath)) {
        $doCopy = $true
        $reason = "new file"
    } else {
        $destFile = Get-Item $destPath
        if ($srcFile.LastWriteTime -gt $destFile.LastWriteTime) {
            $doCopy = $true
            $reason = "newer"
        } elseif ($srcFile.Length -ne $destFile.Length) {
            $doCopy = $true
            $reason = "different size"
        } else {
            $srcHash  = (Get-FileHash $srcFile.FullName -Algorithm MD5).Hash
            $destHash = (Get-FileHash $destPath          -Algorithm MD5).Hash
            if ($srcHash -ne $destHash) {
                $doCopy = $true
                $reason = "different content"
            }
        }
    }

    if ($doCopy) {
        $copied.Add($srcFile.Name)
        $reasons[$srcFile.Name] = $reason
        if (-not $TEST_MODE) {
            Copy-Item $srcFile.FullName $destPath -Force
        }
    }
}

# ── Report ────────────────────────────────────────────────────────────────────
if ($copied.Count -eq 0) {
    Write-Host "All files are up to date. Nothing to copy."
} else {
    $verb = if ($TEST_MODE) { "Would copy" } else { "Copied" }
    foreach ($name in $copied) {
        Write-Host ("  {0,-40}  ({1})" -f "$verb $name", $reasons[$name])
    }
    Write-Host ""
    $n      = $copied.Count
    $plural = if ($n -eq 1) { "file" } else { "files" }
    Write-Host "$n $plural $($verb.ToLower())."

    if (-not $TEST_MODE) {
        $msg = "$n translation $plural updated"
        Push-Location $REPO
        git add "Translations/"
        git commit -m $msg
        git push origin master
        Pop-Location
        Write-Host ""
        Write-Host "Committed and pushed: $msg"
    }
}

Write-Host ""
