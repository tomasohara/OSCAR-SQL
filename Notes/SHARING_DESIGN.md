# OSCAR Profile Sharing — Design, Specifications, and Implementation Plan

Date: 2026-04-01

## 1. Overview

OSCAR 2.0 adds the ability for users to share portions of their CPAP profile data
with others for review. This document covers three phases:

| Phase | Scope | Status |
|-------|-------|--------|
| **Reading / Short-Term** | Accept a URL in the import dialog; download .oscar file directly from a cloud share link | Implement first |
| **Sharing / Short-Term** | New "Share Profile" menu item with a dialog optimized for preparing data to share | Implement second |
| **Sharing / Long-Term** | Direct upload to cloud services from within OSCAR; return a shareable URL | Defer decision until short-term phases complete |

The .oscar backup file format is unchanged — sharing produces and consumes the same
ZIP package that Backup/Restore already uses.

> **Open question:** The requirements mention "Pluton" as a cloud service. This
> document assumes **Proton Drive** was intended. Please confirm.

---

## 2. Existing Architecture Summary

| Component | File(s) | Role |
|-----------|---------|------|
| BackupDialog | `oscar/backupdialog.{h,cpp,ui}` | UI for creating .oscar backup packages |
| RestoreDialog | `oscar/restoredialog.{h,cpp,ui}` | UI for importing .oscar packages from local disk |
| ProfileBackup | `oscar/database/backup/profile_backup.{h,cpp}` | Core backup logic (date range, privacy, ZIP creation) |
| ProfileRestore | `oscar/database/backup/profile_restore.{h,cpp}` | Core restore logic (validate, remap IDs, SQL import) |
| BackupManifest | `oscar/database/backup/backup_manifest.{h,cpp}` | JSON manifest inside .oscar packages |
| SqlExporter | `oscar/database/backup/sql_exporter.{h,cpp}` | Portable SQL INSERT generation |
| ZipFile / UnzipFile | `oscar/zip.{h,cpp}` | miniz-based ZIP read/write |
| MainWindow menu | `oscar/mainwindow.{h,cpp,ui}` | File → Profiles → {Backup, Restore} actions |
| CheckUpdates | `oscar/checkupdates.{h,cpp}` | Existing QNetworkAccessManager usage (precedent) |

**Qt network module** is already linked (`QT += network` in oscar.pro line 28).

---

## 3. Phase 1 — Reading / Short-Term

### 3.1 Goal

Allow the restore dialog to accept a cloud share URL in addition to a local file
path. OSCAR downloads the .oscar file, saves it to a temp location, and feeds it
into the existing ProfileRestore pipeline.

### 3.2 Supported Cloud Services (Initial)

Each service uses a different share-link format. OSCAR must recognize the URL
pattern and transform it into a direct-download URL.

| Service | Share Link Pattern | Direct Download Transform |
|---------|-------------------|--------------------------|
| **Dropbox** | `https://www.dropbox.com/s{l}/HASH/file.oscar?...` | Replace `dl=0` with `dl=1` (or append `?dl=1`) |
| **Google Drive** | `https://drive.google.com/file/d/FILE_ID/...` | `https://drive.google.com/uc?export=download&id=FILE_ID` |
| **OneDrive** | `https://1drv.ms/...` or `https://*.sharepoint.com/...` | Base64-decode the sharing URL per Microsoft's algorithm, construct `https://api.onedrive.com/v1.0/shares/u!{encoded}/root/content` |
| **Proton Drive** | `https://drive.proton.me/urls/...` | **Not supported.** Files are end-to-end encrypted; download requires JS execution and client-side decryption. No public API exists. Users must download in their browser and use the local file option. |
| **0x0.st** | `https://0x0.st/XXXX.oscar` | URL is already a direct download link |
| **Box** | `https://app.box.com/s/HASH` | Append `/download` or use `https://dl.boxcloud.com/...` redirect |

**Fallback:** If the URL domain is not recognized, display a message telling the
user to download the file manually and use the Browse button.

### 3.3 New Class: CloudDownloader

**File:** `oscar/network/cloud_downloader.{h,cpp}`

A utility class that handles URL recognition, transformation, and HTTP download
with progress reporting.

```cpp
/// Identifies which cloud service (if any) a URL belongs to.
enum class CloudProvider {
    Unknown,
    Dropbox,
    GoogleDrive,
    OneDrive,
    ProtonDrive,
    ZeroX0,        // 0x0.st
    Box,
    DirectLink     // Any URL ending in .oscar (assumed direct download)
};

/// Downloads an .oscar file from a cloud share URL.
///
/// Recognizes share-link patterns for supported cloud services,
/// transforms them into direct-download URLs, and streams the file
/// to a temporary location on disk. Emits progress signals suitable
/// for driving a QProgressBar.
///
/// Usage:
///   CloudDownloader dl;
///   dl.setUrl(url);
///   connect(&dl, &CloudDownloader::downloadProgress, ...);
///   connect(&dl, &CloudDownloader::downloadFinished, ...);
///   connect(&dl, &CloudDownloader::downloadFailed, ...);
///   dl.start();
///   // On success: dl.localFilePath() returns path to temp .oscar file
class CloudDownloader : public QObject {
    Q_OBJECT
public:
    explicit CloudDownloader(QObject* parent = nullptr);
    ~CloudDownloader() override;

    /// Set the share URL to download from.
    void setUrl(const QUrl& url);

    /// Identify the cloud provider from the URL (static, for UI feedback).
    static CloudProvider identifyProvider(const QUrl& url);

    /// Human-readable name for a provider.
    static QString providerName(CloudProvider provider);

    /// Returns true if this provider's URLs can be direct-downloaded.
    static bool isProviderSupported(CloudProvider provider);

    /// Begin the asynchronous download.
    void start();

    /// Abort an in-progress download.
    void abort();

    /// Path to the downloaded .oscar file (valid after downloadFinished).
    QString localFilePath() const;

signals:
    /// Emitted periodically during download.
    /// bytesReceived/bytesTotal may be -1 if total is unknown.
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

    /// Emitted when the download completes successfully.
    void downloadFinished(const QString& localPath);

    /// Emitted on any failure (network error, unsupported provider, etc.).
    void downloadFailed(const QString& errorMessage);

private:
    QUrl transformUrl(const QUrl& shareUrl, CloudProvider provider);
    void handleRedirect(const QUrl& redirectUrl);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply*         m_reply = nullptr;
    QTemporaryFile*        m_tempFile = nullptr;
    QUrl                   m_originalUrl;
    CloudProvider          m_provider = CloudProvider::Unknown;
    int                    m_redirectCount = 0;
    static constexpr int   MAX_REDIRECTS = 5;
};
```

**Key design decisions:**

- **Asynchronous:** Uses Qt's event loop via QNetworkReply signals, not blocking.
- **Streams to disk:** Writes chunks to a QTemporaryFile via `readyRead` signal,
  avoiding loading entire file into memory (backups can be hundreds of MB).
- **Redirect handling:** Cloud services frequently redirect (e.g., Dropbox `dl=1`
  → CDN). Follow up to 5 redirects. Qt 6 can auto-follow redirects via
  `QNetworkRequest::RedirectPolicyAttribute` — use `NoLessSafeRedirectPolicy`.
- **Temp file lifetime:** QTemporaryFile is set to `setAutoRemove(true)`.
  CloudDownloader retains ownership; the file persists until the downloader is
  deleted (which happens when RestoreDialog finishes with it).
- **SSL:** Required. Reject non-HTTPS URLs with an error.
- **User-Agent:** Set to `OSCAR/<version>` so services don't block the request.

### 3.4 Changes to RestoreDialog

The restore dialog gains a URL input area alongside the existing file browse.

**UI changes to `restoredialog.ui`:**

```
┌─────────────────────────────────────────────────────────────┐
│  Restore Profile                                       [X]  │
│                                                             │
│  Select a backup package to restore. You can browse for a   │
│  local file or paste a cloud share link.                    │
│                                                             │
│  ┌─ Source ───────────────────────────────────────────────┐  │
│  │  ○ Local file:                                        │  │
│  │    [_________________________________] [Browse...]     │  │
│  │                                                       │  │
│  │  ○ Cloud share link:                                  │  │
│  │    [_________________________________] [Download]     │  │
│  │    (Supports Dropbox, Google Drive, OneDrive, Box,    │  │
│  │     0x0.st, and direct .oscar links)                  │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─ Package Information ──────────── (shown after validate) │
│  │  ...existing info fields...                              │
│  └──────────────────────────────────────────────────────────│
│  ...rest of existing dialog unchanged...                    │
└─────────────────────────────────────────────────────────────┘
```

**Behavior:**

1. **Radio buttons** select between local file and URL modes.
   - "Local file" mode: existing Browse + Validate flow unchanged.
   - "Cloud share link" mode: user pastes URL, clicks Download.

2. **Download button clicked:**
   - Validate that it's an HTTPS URL.
   - Call `CloudDownloader::identifyProvider()` to show the detected service name.
   - If provider is unsupported, show message: *"OSCAR cannot download directly
     from [provider]. Please download the file in your browser and use the
     'Local file' option."*
   - If supported, create CloudDownloader and start download.
   - Progress bar shows download progress.
   - Status label shows: "Downloading from Dropbox..." (or whichever provider).

3. **Download completes:**
   - `packagePathEdit` is set to the temp file path.
   - Automatic validation triggers (same as clicking Validate).
   - Flow continues identically to a locally-browsed file.

4. **Download fails:**
   - Show error in status label and a QMessageBox.
   - Re-enable controls so user can retry or switch to local file.

**New members in RestoreDialog:**

```cpp
private:
    CloudDownloader* m_downloader = nullptr;  // Owned; deleted in destructor.

private slots:
    void on_downloadButton_clicked();
    void on_sourceLocalRadio_toggled(bool checked);
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished(const QString& localPath);
    void onDownloadFailed(const QString& error);
```

### 3.5 Testing Strategy

Since we can test this phase using existing capabilities:

1. **Manual preparation:** Create a .oscar backup using the existing Backup dialog.
2. **Upload to each service:** Manually upload the .oscar file to Dropbox, Google
   Drive, OneDrive, 0x0.st, etc. and obtain share links.
3. **Test URL recognition:** Paste each share link into the restore dialog and
   verify the provider is correctly identified.
4. **Test download:** Verify the file downloads successfully and the progress bar
   updates.
5. **Test restore:** Verify the downloaded file passes validation and restores
   correctly (end-to-end with existing restore pipeline).
6. **Test error cases:** Invalid URLs, non-HTTPS, unsupported providers,
   network failures, interrupted downloads.
7. **Test large files:** Ensure streaming works for multi-hundred-MB backups.

**Unit tests for CloudDownloader:**
- `identifyProvider()` with various URL patterns (parameterized test).
- `transformUrl()` produces correct direct-download URLs.
- Redirect-following logic (mock QNetworkAccessManager if feasible).

---

## 4. Phase 2 — Sharing / Short-Term

### 4.1 Goal

Add a "Share Profile" option to File → Profiles that provides a dialog optimized
for the sharing use case. It produces the same .oscar file as Backup, but the UI
is tuned for sharing rather than archiving.

### 4.2 Menu Changes

**mainwindow.ui:** Add `actionShare_Profile` to `menuProfiles`:

```xml
<widget class="QMenu" name="menuProfiles">
  <addaction name="action_Import_OSCAR_Data"/>
  <addaction name="actionBackup_Profile"/>
  <addaction name="actionRestore_Profile"/>
  <addaction name="separator"/>
  <addaction name="actionShare_Profile"/>
</widget>

<action name="actionShare_Profile">
  <property name="text">
    <string>Share Profile...</string>
  </property>
  <property name="statusTip">
    <string>Prepare profile data for sharing with another OSCAR user</string>
  </property>
</action>
```

**mainwindow.cpp:**

```cpp
void MainWindow::on_actionShare_Profile_triggered()
{
    ShareDialog *dialog = new ShareDialog(this);
    dialog->exec();
    delete dialog;
}
```

### 4.3 New Class: ShareDialog

**Files:** `oscar/sharedialog.{h,cpp,ui}`

The ShareDialog is visually distinct from BackupDialog but reuses the same
ProfileBackup engine underneath.

**Key differences from BackupDialog:**

| Aspect | BackupDialog | ShareDialog |
|--------|-------------|-------------|
| **Window title** | "Backup Profile" | "Share Profile Data" |
| **Introductory text** | None | Explains the sharing workflow |
| **Privacy default** | OFF | ON (sharing with strangers is the common case) |
| **Simplify default** | OFF | ON (recipients don't need timestamps in filenames) |
| **SD card data** | Included in "Everything" range | Never included (too large, not useful for review) |
| **Range presets** | Everything, Most Recent Day, Last Week, ... | Most Recent Day, Last Week, Last Fortnight, Last Month, Custom |
| **Default range** | Everything | Last Week |
| **Output dir label** | "Output Directory" | "Save To" |
| **Security warning** | Generic backup security | Sharing-specific: warns data will be sent to another person |
| **Success dialog** | Shows path and size | Shows path, size, and next-steps instructions |
| **Filename pattern** | `profile_<name>_<dates>_<timestamp>.oscar` | `share_<name>_<dates>.oscar` (simpler, no timestamp) |

### 4.4 ShareDialog UI Layout

```
┌──────────────────────────────────────────────────────────────┐
│  Share Profile Data                                     [X]  │
│                                                              │
│  Prepare a copy of your OSCAR data to share with another     │
│  user for review. The resulting .oscar file can be sent via   │
│  email, cloud storage link, or any file transfer method.     │
│                                                              │
│  Profile: [▾ Current Profile___]                             │
│                                                              │
│  ┌─ Date Range ────────────────────────────────────────────┐ │
│  │  [▾ Last Week________]                                  │ │
│  │  From: [03/25/2026]   To: [04/01/2026]                  │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
│  ☑ Remove personal information (recommended for sharing)     │
│                                                              │
│  ┌─ Output ────────────────────────────────────────────────┐ │
│  │  Save to: [________________________] [Browse...]        │ │
│  │  Filename: [share_JohnDoe_20260325_20260401.oscar___]   │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌─ Next Steps ────────────────────────────────────────────┐ │
│  │  After creating the file:                               │ │
│  │  1. Upload it to a cloud service (Dropbox, Google       │ │
│  │     Drive, OneDrive, etc.) or attach it to an email     │ │
│  │  2. Send the download link or file to the person who    │ │
│  │     will review your data                               │ │
│  │  3. They can import it using OSCAR's                    │ │
│  │     File → Profiles → Restore Profile                   │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
│  [================50%=================]                      │
│  Exporting sessions...                                       │
│                                                              │
│                              [Create File]  [Close]          │
└──────────────────────────────────────────────────────────────┘
```

### 4.5 ShareDialog Implementation

```cpp
class ShareDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShareDialog(QWidget* parent = nullptr);
    ~ShareDialog() override;

private slots:
    void on_profileCombo_currentIndexChanged(int index);
    void on_rangeCombo_currentTextChanged(const QString& text);
    void on_browseButton_clicked();
    void on_shareButton_clicked();      // "Create File" button
    void onProgressChanged(int percent, const QString& message);
    void onBackupCompleted(const QString& path);
    void onBackupFailed(const QString& error);

private:
    void populateProfiles();
    void applyDateRange(const QString& rangeText);
    void updateFilenamePreview();
    void setupCalendarFormatting();
    void saveSettings();
    void restoreSettings();
    QDate getFirstDataDate() const;
    QDate getLastDataDate() const;
    bool showSharingWarning();

    Ui::ShareDialog*  ui;
    QList<qint64>     m_profileIds;
};
```

**Sharing-specific security warning:**

```
Title: "Sharing Medical Data"

Body:
"You are about to create a file containing your sleep therapy data
that will be shared with another person.

• The file will contain session data, events, and machine settings
  for the selected date range
• Personal information (name, DOB, contact details) will be [removed /
  included] based on your privacy setting above

Make sure you trust the recipient before sharing this data."

Checkbox: "I understand this data will be shared with another person"
Buttons: [Continue] [Cancel]
```

**Success dialog (replaces the simple backup-complete message):**

```
Title: "File Created Successfully"

Body:
"Your sharing file has been created:

  File: share_JohnDoe_20260325_20260401.oscar
  Size: 12.3 MB
  Location: C:\Users\...\Documents\OSCAR Shares

To share this file:
  • Upload it to Dropbox, Google Drive, OneDrive, or another
    cloud service and share the link
  • Or send it directly via email or file transfer

The recipient can import it in OSCAR using
  File → Profiles → Restore Profile"

Buttons: [Open Containing Folder] [Close]
```

The **"Open Containing Folder"** button calls
`QDesktopServices::openUrl(QUrl::fromLocalFile(dir))` to open the output folder
in the system file manager — reduces friction for the upload step.

### 4.6 Code Reuse

ShareDialog reuses ProfileBackup entirely — no changes needed to the backup
engine. The dialog simply configures ProfileBackup with sharing-appropriate
defaults:

```cpp
void ShareDialog::on_shareButton_clicked()
{
    // ... validation ...
    if (!showSharingWarning()) return;

    ProfileBackup* backup = new ProfileBackup(profileId, this);
    backup->setOutputPath(outputDir);
    backup->setFilename(filename);
    backup->setPrivacyMode(ui->privacyCheck->isChecked());  // default ON
    backup->setDateRange(ui->fromDate->date(), ui->toDate->date());
    backup->setIncludeSDData(false);  // never include SD data for sharing

    // ... connect signals, run ...
    backup->createBackup();
}
```

### 4.7 Shared Code Extraction

BackupDialog and ShareDialog share significant logic (profile population,
date-range calculation, calendar formatting, settings persistence). Two options:

**Option A — Copy and customize:** Duplicate the relevant methods into
ShareDialog. Simple, no risk of breaking BackupDialog, but creates maintenance
burden.

**Option B — Extract common base class:** Create `BaseExportDialog` that both
BackupDialog and ShareDialog inherit from, containing shared logic.

**Recommendation:** Option A for initial implementation (faster, lower risk).
Refactor to Option B later if the two dialogs drift apart less than expected.

---

## 5. Phase 3 — Sharing / Long-Term

### 5.1 Goal

Upload the .oscar file directly to a cloud service from within OSCAR, returning a
shareable URL to the user without requiring them to use a browser.

### 5.2 Architecture: Cloud Provider Plugin System

```
┌─────────────────────────────────────────────────────────┐
│                      ShareDialog                         │
│  (creates .oscar file, then offers upload options)       │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│                   CloudUploadDialog                      │
│  (provider selection, auth status, upload progress,      │
│   displays resulting URL with Copy button)               │
└────────────────────────┬────────────────────────────────┘
                         │
           ┌─────────────┼─────────────────┐
           ▼             ▼                 ▼
    ┌─────────────┐ ┌──────────┐ ┌─────────────────┐
    │CloudProvider│ │CloudProv.│ │  CloudProvider   │
    │  Dropbox    │ │  GDrive  │ │    OneDrive      │  ...
    └─────────────┘ └──────────┘ └─────────────────┘
```

**Abstract base class:**

```cpp
/// Base class for cloud storage upload providers.
///
/// Each provider implements OAuth2 authentication, file upload,
/// and share-link generation for a specific cloud service.
class CloudProvider : public QObject {
    Q_OBJECT
public:
    virtual ~CloudProvider() = default;

    /// Human-readable service name (e.g., "Dropbox").
    virtual QString name() const = 0;

    /// Whether the user has valid stored credentials.
    virtual bool isAuthenticated() const = 0;

    /// Start the OAuth2 authentication flow (opens browser).
    virtual void authenticate() = 0;

    /// Upload a local file and create a share link.
    /// Emits uploadProgress, uploadFinished, or uploadFailed.
    virtual void upload(const QString& localFilePath) = 0;

    /// Cancel an in-progress upload.
    virtual void abort() = 0;

    /// Delete a previously uploaded file (if supported).
    virtual void deleteRemote(const QString& shareUrl) = 0;

signals:
    void authenticationComplete(bool success);
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void uploadFinished(const QString& shareUrl);
    void uploadFailed(const QString& error);
};
```

### 5.3 Cloud Service Implementation Notes

**Dropbox:**
- OAuth2 with PKCE (no client secret needed for desktop apps).
- Upload via `https://content.dropboxapi.com/2/files/upload`.
- Create share link via `https://api.dropboxapi.com/2/sharing/create_shared_link_with_settings`.
- Well-documented, straightforward API.

**Google Drive:**
- OAuth2 with PKCE. Requires Google Cloud Console project setup.
- Upload via `https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart`.
- Set file permission to "anyone with link" via Permissions API.
- More complex setup but very well documented.

**OneDrive:**
- OAuth2 with PKCE via Microsoft identity platform.
- Upload via `https://graph.microsoft.com/v1.0/me/drive/root:/{filename}:/content`.
- Create share link via `https://graph.microsoft.com/v1.0/me/drive/items/{id}/createLink`.
- Good documentation, reasonable complexity.

**0x0.st (convenience upload):**
- No authentication needed.
- Simple HTTP POST with multipart form data.
- Returns the URL directly in the response body.
- Delete via management token in response header.
- Simplest to implement; good first target for the long-term phase.

**Proton Drive:**
- No public API currently available for third-party applications.
- Would require browser-based upload flow.
- **Recommendation:** Defer until Proton publishes a public API.

### 5.4 OAuth2 Handler

All three major services use OAuth2 with PKCE. A shared OAuth2 handler avoids
three separate implementations:

```cpp
/// Handles OAuth2 Authorization Code flow with PKCE.
///
/// Opens the system browser for user consent, runs a temporary
/// local HTTP server to receive the callback, exchanges the
/// authorization code for tokens, and handles token refresh.
class OAuth2Handler : public QObject {
    Q_OBJECT
public:
    struct Config {
        QUrl    authUrl;
        QUrl    tokenUrl;
        QString clientId;
        QString scope;
        int     localPort = 0;  // 0 = auto-select
    };

    explicit OAuth2Handler(const Config& config, QObject* parent = nullptr);

    void startAuth();
    void refreshToken();
    bool hasValidToken() const;
    QString accessToken() const;

    // Persist/restore tokens via QSettings (encrypted if platform supports it).
    void saveTokens(const QString& providerKey);
    void loadTokens(const QString& providerKey);

signals:
    void authenticated(const QString& accessToken);
    void authFailed(const QString& error);
    void tokenRefreshed(const QString& accessToken);
};
```

**Local callback server:** Uses `QTcpServer` listening on `127.0.0.1` with an
ephemeral port. The redirect URI is `http://127.0.0.1:{port}/callback`. After
receiving the auth code, the server sends a "You may close this window" HTML page
and shuts down.

### 5.5 Token Storage

OAuth2 tokens are sensitive. Storage options by platform:

| Platform | Storage Method |
|----------|---------------|
| Windows | Windows Credential Manager via `CredWrite`/`CredRead` |
| macOS | Keychain via `SecItemAdd`/`SecItemCopyMatching` |
| Linux | libsecret (GNOME Keyring / KDE Wallet) or encrypted QSettings fallback |

**Cross-platform approach:** Use Qt's `QKeychain` (from the `qtkeychain` library)
if we add it as a dependency, or use encrypted QSettings with a machine-derived
key as a simpler fallback that works everywhere.

**Recommendation:** Start with encrypted QSettings for simplicity. Evaluate
`qtkeychain` if security review requires it.

### 5.6 CloudUploadDialog

After creating the .oscar file (Phase 2), the ShareDialog offers an "Upload Now"
button that opens CloudUploadDialog:

```
┌──────────────────────────────────────────────────────────┐
│  Upload to Cloud Service                            [X]  │
│                                                          │
│  Select a cloud service to upload your sharing file:     │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │  ● Dropbox              [Connected ✓]              │  │
│  │  ○ Google Drive         [Sign In...]               │  │
│  │  ○ OneDrive             [Sign In...]               │  │
│  │  ○ 0x0.st (anonymous)   [No sign-in needed]        │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ⚠ Note: 0x0.st is a third-party file hosting service   │
│  not operated by the OSCAR team.                         │
│                                                          │
│  [================75%=================]                  │
│  Uploading to Dropbox... (8.2 MB / 12.3 MB)             │
│                                                          │
│  ┌─ Share Link ───────────────────────────────────────┐  │
│  │  https://www.dropbox.com/s/abc123/share_...oscar   │  │
│  │                                     [Copy Link]    │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│                              [Upload]  [Close]           │
└──────────────────────────────────────────────────────────┘
```

**Post-upload:** The URL is displayed and a "Copy Link" button copies it to the
clipboard via `QApplication::clipboard()->setText(url)`.

### 5.7 Complexity Assessment

| Component | Estimated Effort | Risk |
|-----------|-----------------|------|
| 0x0.st provider | Small | Low — simple HTTP POST, no auth |
| OAuth2Handler | Medium | Medium — local callback server, token management |
| Dropbox provider | Medium | Low — well-documented API |
| Google Drive provider | Medium | Medium — requires Cloud Console project |
| OneDrive provider | Medium | Medium — Microsoft auth can be finicky |
| CloudUploadDialog | Medium | Low — straightforward UI |
| Token storage | Small-Medium | Low if using QSettings; higher for keychain |
| **Total** | **Large** | **Medium overall** |

**Recommendation:** If the long-term phase is attempted in this release, start
with **0x0.st only** as the built-in upload option. It requires no OAuth, no
token storage, and can be implemented in a day. Then evaluate whether adding
Dropbox/Google Drive/OneDrive is feasible within the release timeline.

---

## 6. File Organization

### New Files

```
oscar/
  network/
    cloud_downloader.h          Phase 1 — URL download
    cloud_downloader.cpp        Phase 1
  sharedialog.h                 Phase 2 — Share dialog
  sharedialog.cpp               Phase 2
  sharedialog.ui                Phase 2
  network/
    cloud_provider.h            Phase 3 — Abstract provider base
    cloud_upload_dialog.h       Phase 3 — Upload dialog
    cloud_upload_dialog.cpp     Phase 3
    cloud_upload_dialog.ui      Phase 3
    oauth2_handler.h            Phase 3 — OAuth2 PKCE flow
    oauth2_handler.cpp          Phase 3
    providers/
      dropbox_provider.h        Phase 3
      dropbox_provider.cpp      Phase 3
      gdrive_provider.h         Phase 3
      gdrive_provider.cpp       Phase 3
      onedrive_provider.h       Phase 3
      onedrive_provider.cpp     Phase 3
      zerox0_provider.h         Phase 3
      zerox0_provider.cpp       Phase 3
```

### Modified Files

| File | Phase | Change |
|------|-------|--------|
| `oscar/oscar.pro` | 1 | Add new source/header files |
| `oscar/restoredialog.h` | 1 | Add URL input, CloudDownloader member |
| `oscar/restoredialog.cpp` | 1 | Download logic, source radio buttons |
| `oscar/restoredialog.ui` | 1 | Add source group with radio buttons and URL field |
| `oscar/mainwindow.h` | 2 | Declare `on_actionShare_Profile_triggered()` |
| `oscar/mainwindow.cpp` | 2 | Implement share action slot |
| `oscar/mainwindow.ui` | 2 | Add Share Profile action and menu entry |

---

## 7. Implementation Plan

### Phase 1: Reading / Short-Term

This phase can be tested using existing backup capability to create .oscar files
and manually uploading them to cloud services.

| Step | Task | Dependencies |
|------|------|-------------|
| 1.1 | Create `oscar/network/` directory | — |
| 1.2 | Implement `CloudDownloader` class with URL recognition and transform logic | — |
| 1.3 | Write unit tests for `identifyProvider()` and `transformUrl()` | 1.2 |
| 1.4 | Implement async download with progress, redirect following, streaming to temp file | 1.2 |
| 1.5 | Modify `restoredialog.ui`: add source group box with Local/URL radio buttons | — |
| 1.6 | Modify `restoredialog.{h,cpp}`: wire up URL download flow | 1.4, 1.5 |
| 1.7 | Update `oscar.pro` with new files | 1.2 |
| 1.8 | Manual testing: create backup, upload to Dropbox/GDrive/OneDrive/0x0.st, import via URL | 1.6 |
| 1.9 | Test error cases: bad URLs, unsupported providers, network failures, large files | 1.6 |

### Phase 2: Sharing / Short-Term

| Step | Task | Dependencies |
|------|------|-------------|
| 2.1 | Create `sharedialog.ui` with layout per section 4.4 | — |
| 2.2 | Implement `ShareDialog` class (copy relevant logic from BackupDialog) | 2.1 |
| 2.3 | Add "Open Containing Folder" to success dialog | 2.2 |
| 2.4 | Add sharing-specific security warning dialog | 2.2 |
| 2.5 | Add `actionShare_Profile` to `mainwindow.ui` menu | — |
| 2.6 | Add `on_actionShare_Profile_triggered()` to `mainwindow.{h,cpp}` | 2.2, 2.5 |
| 2.7 | Update `oscar.pro` with new files | 2.2 |
| 2.8 | Manual testing: create share file, verify contents match backup, verify privacy default | 2.6 |
| 2.9 | End-to-end test: create share → upload manually → import via Phase 1 URL | Phase 1 |

### Phase 3: Sharing / Long-Term (if pursued)

| Step | Task | Dependencies |
|------|------|-------------|
| 3.1 | Implement `zerox0_provider.{h,cpp}` (simplest provider, no auth) | — |
| 3.2 | Create `CloudUploadDialog` with provider selection UI | 3.1 |
| 3.3 | Add "Upload Now" button to ShareDialog success dialog | 3.2, Phase 2 |
| 3.4 | Test 0x0.st upload end-to-end | 3.3 |
| 3.5 | *If continuing:* Implement `OAuth2Handler` | — |
| 3.6 | *If continuing:* Implement `dropbox_provider` | 3.5 |
| 3.7 | *If continuing:* Implement `gdrive_provider` | 3.5 |
| 3.8 | *If continuing:* Implement `onedrive_provider` | 3.5 |
| 3.9 | Token storage implementation | 3.5 |
| 3.10 | Integration testing with all providers | 3.6–3.9 |

---

## 8. Cross-Platform Considerations

- **All network code** uses Qt's QNetworkAccessManager — cross-platform by default.
- **OAuth2 local server** uses QTcpServer on 127.0.0.1 — works on all platforms.
- **Browser opening** for OAuth uses `QDesktopServices::openUrl()` — cross-platform.
- **File dialogs** use existing `nativeDialogOption()` helper already used in
  BackupDialog and RestoreDialog.
- **SSL/TLS:** Qt bundles OpenSSL on most platforms. Verify the Windows build
  includes SSL DLLs (it should, since CheckUpdates already uses HTTPS).
- **Token storage:** QSettings-based approach works everywhere; platform-specific
  keychains are optional enhancements.

## 9. Security Considerations

1. **HTTPS only** — reject HTTP URLs in CloudDownloader.
2. **Privacy mode defaults to ON** in ShareDialog — personal data stripped unless
   user explicitly opts in.
3. **Sharing warning** — user must acknowledge they're sharing medical data.
4. **0x0.st warning** — if implemented, user must acknowledge third-party service.
5. **OAuth tokens** — stored encrypted, not in plaintext QSettings.
6. **Temp files** — auto-removed when CloudDownloader is destroyed.
7. **No cloud database** — OSCAR's SQLite database never leaves the local machine;
   only exported .oscar packages are shared.

## 10. Summary of Deliverables by Phase

| Phase | Deliverables | Can Test With |
|-------|-------------|---------------|
| **1 (Reading/ST)** | CloudDownloader class, modified RestoreDialog with URL import | Existing Backup + manual cloud upload |
| **2 (Sharing/ST)** | ShareDialog class, new menu item | Phase 1 URL import for end-to-end |
| **3 (Sharing/LT)** | CloudProvider system, CloudUploadDialog, 0x0.st provider (+ optional OAuth providers) | Complete end-to-end within OSCAR |
