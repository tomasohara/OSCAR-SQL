# URI Scheme / Deep-Link for Restore Profile

## Goal

Allow a user to post a forum link pointing to a `.oscar` backup file (e.g. on Dropbox or
Google Drive) so that another user can click the link and have OSCAR launch and open the
Restore Profile dialog — pre-populated but not auto-run.

---

## Mechanism: Custom URI Scheme

Register `oscar://` (or `x-oscar://`) as a protocol handler with the OS.  A link of the
form:

```
oscar://restore?source=https://www.dropbox.com/s/abc123/mybackup.oscar?dl=1
```

causes the OS to launch OSCAR (or activate the running instance) with the URI passed as
an argument.  OSCAR downloads the file to a temp path and opens the Restore Profile dialog.

---

## Per-platform Registration

### Windows

The installer writes to the registry:

```
HKEY_CLASSES_ROOT\oscar
  (Default) = "URL:OSCAR Protocol"
  URL Protocol = ""
  \shell\open\command
    (Default) = "C:\...\oscar.exe" "%1"
```

OSCAR receives the URI as `argv[1]` on startup.

### macOS

Add to `Info.plist`:

```xml
<key>CFBundleURLTypes</key>
<array>
  <dict>
    <key>CFBundleURLSchemes</key>
    <array><string>oscar</string></array>
  </dict>
</array>
```

Qt receives this as a `QFileOpenEvent`; override `QApplication::event()` to catch it.
macOS may also pass it as a command-line argument depending on how the app is launched.

### Linux

Add to the `.desktop` file:

```ini
MimeType=x-scheme-handler/oscar
```

Then the installer runs:

```
xdg-mime default oscar.desktop x-scheme-handler/oscar
```

OSCAR receives the URI as `argv[1]`.

---

## Qt-side Handling

Two cases must be handled:

**Cold start:** Check `argv[1]` in `main.cpp`.  If it begins with `oscar://`, extract the
source URL and queue opening the Restore dialog after the main window is ready.

**Already running (single instance):** Without a single-instance guard a second OSCAR
window opens, which is undesirable.  Use `QLocalServer`/`QLocalSocket`: the second
instance sends the URI to the running instance and exits; the running instance opens the
dialog.

```cpp
// In QApplication subclass or main window
bool event(QEvent *e) override {
    if (e->type() == QEvent::FileOpen) {   // macOS URL-open event
        auto *foe = static_cast<QFileOpenEvent *>(e);
        handleOscarUrl(foe->url());
        return true;
    }
    return QMainWindow::event(e);
}
```

---

## Downloading the Remote File

Once OSCAR has the source URL it:

1. Uses `QNetworkAccessManager` to download to a `QTemporaryFile`
2. Shows a small progress indicator
3. On completion opens the Restore Profile dialog pointed at the temp file

Dropbox direct-download links work cleanly (`?dl=1`).  Google Drive is messier (redirect
dance, virus-warning interstitial for large files) and may need special handling.

---

## Simpler Fallback: File Association for `.oscar` Files

Register OSCAR to handle `.oscar` files.  Then double-clicking a downloaded backup opens
OSCAR directly to the Restore Profile dialog.  Much simpler to implement and a natural fit
since `.oscar` is already the backup extension.  The URI scheme adds the "click a forum
link and go" experience on top.

---

## Effort Summary

| Feature                                        | Effort |
|------------------------------------------------|--------|
| `.oscar` file association (double-click)       | Low — installer + `argv[1]` check |
| `oscar://` URI scheme, cold start              | Medium — same `argv[1]` + download |
| Single-instance forwarding via `QLocalSocket`  | Medium |
| macOS `QFileOpenEvent` handler                 | Low — a few lines in `QApplication::event()` |
| Google Drive URL normalisation                 | Medium — redirect/cookie handling |

The file association is the easiest win.  The URI scheme enables the "click a forum link"
workflow but adds the download step and single-instance complexity.
