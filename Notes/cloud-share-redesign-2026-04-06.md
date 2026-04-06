# Share Profile Redesign — Design & Implementation Plan

Date: 2026-04-06
Status: Design — for implementation by Sonnet 4.6

## Problem

The current Share Profile flow requires too many clicks and dialogs:
1. ShareDialog: configure profile, date range, output directory, filename, click "Create File"
2. Sharing warning confirmation dialog
3. Success message box with "Upload to Cloud..." button
4. CloudUploadDialog: pick provider, authenticate, click "Upload", wait, copy link
5. Close CloudUploadDialog, close ShareDialog

That is 5 dialogs for what should be a single operation. The user wants to select
the destination *inside* the Share Dialog itself, then click one button to do
everything.

## Goals

- Single dialog for the entire share operation: configure + create + deliver
- Destination selector (combo box) with: File, Dropbox, Google Drive, OneDrive
- 0x0.st support commented out (site currently down, no ETA)
- Cloud destinations produce a temporary file that is deleted after upload
- File destination produces a permanent file (current behavior)
- Last-used destination remembered via QSettings
- Architecture supports adding new providers later without rewriting the dialog
- Fix the OAuth2 refresh-token preservation bug identified in the review

## Non-Goals (this iteration)

- Full provider abstraction/registry as described in cloud-share-provider-design
  (that is a larger refactor; this iteration focuses on the UX improvement using
  the existing uploader classes, wrapped lightly for extensibility)
- Download-side refactoring (RestoreDialog + CloudDownloader stay as-is)
- Unit tests for provider logic (deferred to a future iteration)
- Google Drive and OneDrive upload implementation (placeholder entries in the
  combo box that show "coming soon" until implemented)

---

## UI Design

### Revised ShareDialog Layout

```
+----------------------------------------------------------+
|  Share Profile Data                               [X]    |
|                                                          |
|  +----------------------------------------------------+  |
|  | Prepare a copy of your OSCAR data to share with    |  |
|  | another user for review.                           |  |
|  +----------------------------------------------------+  |
|                                                          |
|  Profile:  [Current Profile     v]                       |
|                                                          |
|  +- Date Range ---------------------------------------+  |
|  |  Range:  [Last Week           v]                   |  |
|  |  From:   [04/01/2026]     To: [04/06/2026]        |  |
|  +----------------------------------------------------+  |
|                                                          |
|  [x] Replace personal information    [x] Simplify name  |
|                                                          |
|  +- Destination --------------------------------------+  |
|  |  Share via:  [Dropbox             v]               |  |
|  |                                                    |  |
|  |  (provider-specific info/controls area)            |  |
|  |                                                    |  |
|  |  -- For File: --                                   |  |
|  |  Directory: [________________________] [Browse...] |  |
|  |  File:      [share_p1_20260401_7.oscar]            |  |
|  |                                                    |  |
|  |  -- For Dropbox (authenticated): --                |  |
|  |  Signed in to Dropbox.           [Sign Out]        |  |
|  |                                                    |  |
|  |  -- For Dropbox (not authenticated): --            |  |
|  |  Sign in to Dropbox to upload.   [Sign In...]      |  |
|  |                                                    |  |
|  |  -- For Google Drive / OneDrive: --                |  |
|  |  (Same pattern as Dropbox when implemented.)       |  |
|  |  Coming soon.                                      |  |
|  +----------------------------------------------------+  |
|                                                          |
|  [========progress bar========]                          |
|  Status: Ready                                           |
|                                                          |
|  Share URL: [https://dropbox.com/s/abc123]  [Copy Link]  |
|                                                          |
|                             [Share]  [Close]             |
+----------------------------------------------------------+
```

### Key UI Behaviors

**Destination combo box** contains: "File", "Dropbox", "Google Drive", "OneDrive".
The combo's `currentData()` carries a provider ID string ("file", "dropbox",
"google_drive", "onedrive"). The last-used selection is persisted in QSettings
under `ShareDialog/lastDestination`.

**Provider-specific area** — a QStackedWidget inside the Destination group box,
with one page per provider. This keeps the layout clean and avoids
show/hide spaghetti. Pages:

- **Page 0 — File**: directory row (outputDirEdit + browseButton) and filename
  row (filenameEdit). Same as current.
- **Page 1 — Dropbox**: auth status label + Sign In/Sign Out button.
- **Page 2 — Google Drive**: "Coming soon" label (placeholder).
- **Page 3 — OneDrive**: "Coming soon" label (placeholder).

**Share URL row** (urlEdit + copyButton): hidden initially, shown after a
successful cloud upload. Hidden when destination is "File" (the user already
knows where the file is).

**Share button text** changes based on destination:
- File: "Create File"
- Cloud providers: "Share" (creates temp file, uploads, returns URL)

**Share button enabled** logic:
- File: enabled when output dir is set and filename is non-empty
- Dropbox: enabled when authenticated
- Google Drive / OneDrive: disabled (coming soon)

**After successful cloud upload**: the dialog does NOT close. Instead it shows
the share URL in the URL row and changes the button row to show
[Copy Link] [Close]. The Share button is hidden. This lets the user copy the
link without a separate dialog. The sharing warning is shown once at the start,
before any file creation or upload begins.

**After successful file creation** (File destination): show a brief success
message in the status label, offer "Open Containing Folder" as a button or
link. Don't pop up a separate message box. The dialog stays open so the user
can close it themselves.

**Progress bar**: hidden until operation starts. For cloud uploads, shows
upload progress after file creation completes. File creation progress is
typically fast, so the bar may jump from "Creating file..." to upload progress.

### Sharing Warning

The sharing warning dialog is still shown (it's a legal/ethical safeguard), but
only once per Share button click, before any work begins. It is NOT shown again
if the user changes destination and clicks Share again in the same dialog
session — the flag `m_warningAcknowledged` persists for the dialog's lifetime.

---

## Architecture

### What Changes

| File | Change |
|------|--------|
| `sharedialog.ui` | Replace "Save To" group with "Destination" group containing combo + QStackedWidget. Add URL row. Remove nextStepsLabel. |
| `sharedialog.h` | Add destination handling, cloud upload members, URL row, stacked widget page management. Remove CloudUploadDialog include. |
| `sharedialog.cpp` | Integrate upload logic directly. Use temp file for cloud. Show URL on completion. |
| `cloud_upload_dialog.h/.cpp` | **Delete** — functionality absorbed into ShareDialog. |
| `oauth2_handler.cpp` | Fix refresh-token preservation bug. |
| `oscar.pro` | Remove cloud_upload_dialog from SOURCES/HEADERS. |
| `mainwindow.cpp` | No change (already launches ShareDialog). |

### What Stays

| File | Status |
|------|--------|
| `dropbox_uploader.h/.cpp` | Keep as-is (used directly by ShareDialog) |
| `cloud_uploader.h/.cpp` | Keep but usage commented out (0x0.st disabled) |
| `oauth2_handler.h/.cpp` | Keep with refresh-token fix |
| `cloud_downloader.h/.cpp` | No changes (download/restore path unaffected) |

### Provider Management in ShareDialog

Rather than building a full registry/interface system now, use a lightweight
approach that supports future extension:

```cpp
// In sharedialog.h

/// Identifiers for share destinations. Stored as combo box userData.
enum class ShareDestination {
    File,
    Dropbox,
    // GoogleDrive,   // Uncomment when implemented
    // OneDrive,      // Uncomment when implemented
    // ZeroX0,        // Commented out: site currently unavailable
};

// In sharedialog.cpp, constructor:

ui->destinationCombo->addItem(tr("File (save to disk)"),
    static_cast<int>(ShareDestination::File));
ui->destinationCombo->addItem(tr("Dropbox"),
    static_cast<int>(ShareDestination::Dropbox));
ui->destinationCombo->addItem(tr("Google Drive (coming soon)"),
    static_cast<int>(ShareDestination::GoogleDrive));
ui->destinationCombo->addItem(tr("OneDrive (coming soon)"),
    static_cast<int>(ShareDestination::OneDrive));
```

When Google Drive and OneDrive uploaders are implemented, remove "(coming soon)"
from the label, add the stacked widget page, and implement the upload path in
`doCloudUpload()` with a switch on the destination enum. The combo box approach
means adding a provider is: add enum value, add combo item, add stacked page,
add upload case.

### DropboxUploader Ownership

ShareDialog creates a `DropboxUploader*` in its constructor (same as
CloudUploadDialog does now). This allows checking auth state for the Sign In /
Sign Out button and the Share button enabled state. The uploader is a member
of ShareDialog, not created per-upload.

---

## Implementation Plan

### Step 1: Fix OAuth2 refresh-token bug

**File**: `oscar/network/oauth2_handler.cpp`

In `onTokenReplyFinished()`, the refresh token is unconditionally overwritten:

```cpp
m_refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
```

Dropbox (and some other providers) do not return a new refresh token on refresh
responses. This erases the stored refresh token, eventually forcing
re-authentication.

**Fix**: Only update refresh token if the response contains one:

```cpp
QString newRefresh = obj.value(QStringLiteral("refresh_token")).toString();
if (!newRefresh.isEmpty()) {
    m_refreshToken = newRefresh;
}
```

Also add `token_access_type=offline` to the OAuth2Handler Config as an
`extraAuthParams` map, rather than hard-coding it in `startAuth()`. This makes
the handler genuinely reusable for Google/OneDrive which use different parameter
names (`access_type=offline` for Google, `offline_access` scope for Microsoft).

**Changes to Config struct**:
```cpp
struct Config {
    QUrl    authUrl;
    QUrl    tokenUrl;
    QString clientId;
    QString scope;
    quint16 redirectPort = 17178;
    QMap<QString, QString> extraAuthParams;  // Added: provider-specific auth query params
};
```

In `startAuth()`, replace the hard-coded `token_access_type` line with a loop
over `extraAuthParams`:
```cpp
for (auto it = m_config.extraAuthParams.cbegin();
     it != m_config.extraAuthParams.cend(); ++it) {
    query.addQueryItem(it.key(), it.value());
}
```

In `dropbox_uploader.cpp` constructor, add:
```cpp
config.extraAuthParams[QStringLiteral("token_access_type")] = QStringLiteral("offline");
```

### Step 2: Revise sharedialog.ui

Replace the current "Save To" group box and "next steps" label with a
"Destination" group box containing:

1. A combo box row: `QLabel("Share via:")` + `QComboBox(destinationCombo)`
2. A `QStackedWidget(destinationStack)` with pages:
   - Page 0 (file): directory row + filename row (moved from current outputGroup)
   - Page 1 (Dropbox): auth status label + sign in/out button, horizontally laid out
   - Page 2 (Google Drive): "Coming soon" label
   - Page 3 (OneDrive): "Coming soon" label

Add below the progress bar:
- URL display row: `QLineEdit(urlEdit)` read-only + `QPushButton(copyLinkButton)`,
  both initially hidden

Change button row:
- Rename `shareButton` text dynamically based on destination
- Add `openFolderButton` (hidden, shown after File creation)

Remove: `nextStepsLabel`, `outputGroup` (contents moved into stacked page 0)

### Step 3: Revise sharedialog.h

```cpp
#ifndef SHAREDIALOG_H
#define SHAREDIALOG_H

#include <QDate>
#include <QDialog>

class DropboxUploader;

namespace Ui {
class ShareDialog;
}

/// Identifiers for share destinations.
enum class ShareDestination {
    File,
    Dropbox,
    GoogleDrive,
    OneDrive,
};

class ShareDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShareDialog(QWidget* parent = nullptr);
    ~ShareDialog() override;

private slots:
    void on_profileCombo_currentIndexChanged(int index);
    void on_rangeCombo_currentTextChanged(const QString& text);
    void on_browseButton_clicked();
    void on_shareButton_clicked();
    void on_destinationCombo_currentIndexChanged(int index);
    void onSignInOutClicked();
    void onDropboxAuthComplete(bool success);
    void onProgressChanged(int percent, const QString& message);
    void onBackupCompleted(const QString& path);
    void onBackupFailed(const QString& error);
    void onUploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void onUploadFinished(const QString& shareUrl);
    void onUploadFailed(const QString& error);
    void onCopyLinkClicked();
    void onOpenFolderClicked();

private:
    void populateProfiles();
    void populateDestinations();
    void applyDateRange(const QString& rangeText);
    void updateFilenamePreview();
    void updateDestinationUi();
    void updateShareButtonState();
    void setupCalendarFormatting();
    void saveSettings();
    void restoreSettings();
    QDate getFirstDataDate() const;
    QDate getLastDataDate() const;
    bool showSharingWarning();
    ShareDestination currentDestination() const;
    void setUiLocked(bool locked);
    void startCloudUpload(const QString& tempFilePath);
    void cleanupTempFile();

    Ui::ShareDialog*  ui;
    QList<qint64>     m_profileIds;
    DropboxUploader*  m_dropboxUploader   = nullptr;
    QString           m_tempFilePath;          // Temp file for cloud uploads
    QString           m_lastFilePath;          // Last created file (for open folder)
    bool              m_warningAcknowledged = false;
    bool              m_uploadInProgress    = false;
};

#endif // SHAREDIALOG_H
```

### Step 4: Revise sharedialog.cpp

Key changes from current implementation:

**Constructor**:
- Create `m_dropboxUploader = new DropboxUploader(this)` and connect its
  `authComplete` signal.
- Call `populateDestinations()` to fill the combo box.
- Call `restoreSettings()` which now also restores `lastDestination`.
- Connect `destinationCombo` signal to `on_destinationCombo_currentIndexChanged`.
- Connect `copyLinkButton`, sign in/out button, `openFolderButton`.

**populateDestinations()**:
```cpp
void ShareDialog::populateDestinations()
{
    ui->destinationCombo->addItem(tr("File (save to disk)"),
        static_cast<int>(ShareDestination::File));
    ui->destinationCombo->addItem(tr("Dropbox"),
        static_cast<int>(ShareDestination::Dropbox));
    ui->destinationCombo->addItem(tr("Google Drive (coming soon)"),
        static_cast<int>(ShareDestination::GoogleDrive));
    ui->destinationCombo->addItem(tr("OneDrive (coming soon)"),
        static_cast<int>(ShareDestination::OneDrive));
}
```

**on_destinationCombo_currentIndexChanged()**:
- Switch the stacked widget page.
- Call `updateDestinationUi()` and `updateShareButtonState()`.

**updateDestinationUi()**:
- For File: show directory/filename controls (stacked page 0).
- For Dropbox: show auth status and sign in/out button (stacked page 1).
  Update button text to "Sign In..." or "Sign Out" based on
  `m_dropboxUploader->isAuthenticated()`.
- For Google Drive / OneDrive: show "coming soon" (stacked pages 2/3).
- Hide or show the URL row and open-folder button as appropriate.

**updateShareButtonState()**:
```cpp
void ShareDialog::updateShareButtonState()
{
    ShareDestination dest = currentDestination();
    switch (dest) {
    case ShareDestination::File:
        ui->shareButton->setText(tr("Create File"));
        ui->shareButton->setEnabled(
            !ui->outputDirEdit->text().isEmpty() &&
            !ui->filenameEdit->text().trimmed().isEmpty());
        break;
    case ShareDestination::Dropbox:
        ui->shareButton->setText(tr("Share"));
        ui->shareButton->setEnabled(m_dropboxUploader->isAuthenticated());
        break;
    case ShareDestination::GoogleDrive:
    case ShareDestination::OneDrive:
        ui->shareButton->setText(tr("Share"));
        ui->shareButton->setEnabled(false);  // Coming soon
        break;
    }
}
```

**on_shareButton_clicked()** — revised flow:
```
1. Show sharing warning (if not already acknowledged this session)
2. Lock UI (disable all inputs)
3. Determine destination
4. If File:
     a. Create ProfileBackup with output dir + filename
     b. Connect signals, run createBackup()
     c. On success: show status, enable Open Folder button
   If Cloud (Dropbox, etc.):
     a. Create ProfileBackup with QDir::tempPath() as output
     b. Use a temp filename like "oscar_share_tmp_XXXXX.oscar"
     c. Connect signals, run createBackup()
     d. On success (onBackupCompleted): call startCloudUpload(path)
```

**onBackupCompleted(path)** — revised:
```cpp
void ShareDialog::onBackupCompleted(const QString& path)
{
    ShareDestination dest = currentDestination();
    if (dest == ShareDestination::File) {
        // File destination: done.
        m_lastFilePath = path;
        ui->progressBar->setValue(100);
        ui->statusLabel->setText(tr("File created successfully."));
        ui->openFolderButton->setVisible(true);
        ui->shareButton->setVisible(false);
        ui->closeButton->setEnabled(true);
    } else {
        // Cloud destination: now upload the temp file.
        m_tempFilePath = path;
        ui->statusLabel->setText(tr("Uploading..."));
        startCloudUpload(path);
    }
}
```

**startCloudUpload()**:
```cpp
void ShareDialog::startCloudUpload(const QString& filePath)
{
    m_uploadInProgress = true;
    ShareDestination dest = currentDestination();

    switch (dest) {
    case ShareDestination::Dropbox:
        m_dropboxUploader->setFilePath(filePath);
        connect(m_dropboxUploader, &DropboxUploader::uploadProgress,
                this, &ShareDialog::onUploadProgress, Qt::UniqueConnection);
        connect(m_dropboxUploader, &DropboxUploader::uploadFinished,
                this, &ShareDialog::onUploadFinished, Qt::UniqueConnection);
        connect(m_dropboxUploader, &DropboxUploader::uploadFailed,
                this, &ShareDialog::onUploadFailed, Qt::UniqueConnection);
        m_dropboxUploader->startUpload();
        break;
    // case ShareDestination::GoogleDrive:
    // case ShareDestination::OneDrive:
    //     (future implementation)
    default:
        break;
    }
}
```

**onUploadFinished(shareUrl)**:
```cpp
void ShareDialog::onUploadFinished(const QString& shareUrl)
{
    m_uploadInProgress = false;
    cleanupTempFile();  // Delete the temp .oscar file

    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(100);
    ui->statusLabel->setText(tr("Upload complete. Share this link:"));

    ui->urlEdit->setText(shareUrl);
    ui->urlEdit->setVisible(true);
    ui->urlEdit->selectAll();
    ui->copyLinkButton->setVisible(true);
    ui->shareButton->setVisible(false);
    ui->closeButton->setEnabled(true);
}
```

**onUploadFailed(error)**:
```cpp
void ShareDialog::onUploadFailed(const QString& error)
{
    m_uploadInProgress = false;
    cleanupTempFile();

    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Upload failed: %1").arg(error));
    setUiLocked(false);
}
```

**cleanupTempFile()**:
```cpp
void ShareDialog::cleanupTempFile()
{
    if (!m_tempFilePath.isEmpty()) {
        QFile::remove(m_tempFilePath);
        m_tempFilePath.clear();
    }
}
```

**saveSettings() / restoreSettings()** — add destination persistence:
```cpp
void ShareDialog::saveSettings()
{
    QSettings s;
    s.beginGroup("ShareDialog");
    s.setValue("lastOutputDir", ui->outputDirEdit->text());
    s.setValue("lastDestination",
        ui->destinationCombo->currentData().toInt());
    s.endGroup();
}

void ShareDialog::restoreSettings()
{
    QSettings s;
    s.beginGroup("ShareDialog");
    const QString lastDir = s.value("lastOutputDir").toString();
    int lastDest = s.value("lastDestination",
        static_cast<int>(ShareDestination::File)).toInt();
    s.endGroup();

    if (!lastDir.isEmpty() && QDir(lastDir).exists()) {
        ui->outputDirEdit->setText(lastDir);
    }

    // Find the combo index with matching data.
    for (int i = 0; i < ui->destinationCombo->count(); ++i) {
        if (ui->destinationCombo->itemData(i).toInt() == lastDest) {
            ui->destinationCombo->setCurrentIndex(i);
            break;
        }
    }

    updateFilenamePreview();
}
```

**setUiLocked()** — helper to enable/disable all input controls:
```cpp
void ShareDialog::setUiLocked(bool locked)
{
    ui->profileCombo->setEnabled(!locked);
    ui->rangeCombo->setEnabled(!locked);
    ui->destinationCombo->setEnabled(!locked);
    ui->shareButton->setEnabled(!locked);
    ui->browseButton->setEnabled(!locked);
    ui->filenameEdit->setEnabled(!locked && currentDestination() == ShareDestination::File);
    ui->closeButton->setEnabled(!locked);
}
```

### Step 5: Delete CloudUploadDialog

Remove `oscar/network/cloud_upload_dialog.h` and `oscar/network/cloud_upload_dialog.cpp`.
Remove from `oscar.pro` SOURCES and HEADERS.
Remove `#include "network/cloud_upload_dialog.h"` from `sharedialog.cpp`.

### Step 6: Update oscar.pro

Remove:
```
SOURCES += network/cloud_upload_dialog.cpp
HEADERS += network/cloud_upload_dialog.h
```

No new files are added — the destination logic goes into the existing
ShareDialog files.

---

## Temp File Handling for Cloud Uploads

When the destination is a cloud provider:

1. `ProfileBackup` is configured with `setOutputPath(QDir::tempPath())`
2. Filename is set to a unique temp name: `oscar_upload_XXXXXX.oscar`
   (use `QTemporaryFile` to generate a unique name, then close it and pass
   the path to ProfileBackup, which will write to it)
3. After upload completes (success or failure), `cleanupTempFile()` deletes it
4. If the dialog is closed or destroyed while `m_tempFilePath` is non-empty,
   the destructor also calls `cleanupTempFile()`

The destructor should be:
```cpp
ShareDialog::~ShareDialog()
{
    cleanupTempFile();
    delete ui;
}
```

---

## Edge Cases

### User changes destination after creating a file
The URL row and open-folder button are hidden when the destination changes.
The user can click Share again with a different destination. The warning flag
persists so they won't be prompted again.

### User closes dialog during upload
The destructor cleans up the temp file. The DropboxUploader (owned by the
dialog) is destroyed, which should abort any in-progress reply. Add an
`abort()` call in the destructor if upload is in progress:
```cpp
ShareDialog::~ShareDialog()
{
    if (m_uploadInProgress && m_dropboxUploader) {
        m_dropboxUploader->abort();
    }
    cleanupTempFile();
    delete ui;
}
```

### Port conflict on OAuth redirect
If port 17178 is in use, the OAuth handler already reports the error via
`authFailed`. The dialog should surface this in the status label rather
than a separate error dialog.

### Dropbox not in production mode
Other users may get "app has reached its user limit". This error comes back
as an auth failure — surface it in the status label. Consider adding a
note in the Dropbox info area if we can detect this condition.

---

## OAuth2Handler Extra Auth Params

Currently `startAuth()` hard-codes `token_access_type=offline` (Dropbox-specific).
Google uses `access_type=offline`. Microsoft uses the `offline_access` scope.

The `extraAuthParams` map in Config handles Dropbox and Google cleanly. For
Microsoft, the offline_access is just added to the scope string, so no special
handling needed.

Provider configs would look like:

**Dropbox**:
```cpp
config.extraAuthParams["token_access_type"] = "offline";
config.scope = "files.content.write sharing.write";
```

**Google Drive** (future):
```cpp
config.extraAuthParams["access_type"] = "offline";
config.extraAuthParams["prompt"] = "consent";
config.scope = "https://www.googleapis.com/auth/drive.file";
```

**OneDrive** (future):
```cpp
config.scope = "Files.ReadWrite offline_access";
// No extra auth params needed
```

---

## Summary of File Changes

| File | Action |
|------|--------|
| `oscar/network/oauth2_handler.h` | Add `extraAuthParams` to Config |
| `oscar/network/oauth2_handler.cpp` | Fix refresh-token bug; use extraAuthParams instead of hard-coded `token_access_type` |
| `oscar/network/dropbox_uploader.cpp` | Add `token_access_type` to config.extraAuthParams |
| `oscar/sharedialog.ui` | Redesign: destination combo + stacked widget, URL row, remove nextStepsLabel |
| `oscar/sharedialog.h` | Add ShareDestination enum, upload members, new slots |
| `oscar/sharedialog.cpp` | Integrate destination selection, cloud upload, temp file handling |
| `oscar/network/cloud_upload_dialog.h` | **Delete** |
| `oscar/network/cloud_upload_dialog.cpp` | **Delete** |
| `oscar/oscar.pro` | Remove cloud_upload_dialog.{h,cpp} |

## Implementation Order

1. Fix OAuth2 refresh-token bug + add extraAuthParams (Step 1)
2. Update DropboxUploader to use extraAuthParams (part of Step 1)
3. Redesign sharedialog.ui (Step 2)
4. Rewrite sharedialog.h (Step 3)
5. Rewrite sharedialog.cpp (Step 4)
6. Delete CloudUploadDialog (Step 5)
7. Update oscar.pro (Step 6)
8. Build and test all destinations (File, Dropbox auth + upload)
