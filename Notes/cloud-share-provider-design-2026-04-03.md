# Cloud Share Provider Design

Date: 2026-04-03

Purpose:
- Provide a concrete refactoring direction for cloud upload/download support
- Keep Dropbox, Google Drive, OneDrive, and 0x0.st easy to add and maintain
- Minimize provider-specific logic in dialogs

## Design Goals

- Add new cloud providers without rewriting dialogs
- Keep upload, download, auth, and delete behavior provider-owned
- Reuse shared OAuth code where appropriate without forcing all providers into one shape
- Preserve simple Qt signal/slot integration
- Make provider detection and URL handling testable without network access

## Proposed Structure

Suggested new area:
- `oscar/network/cloud/`

Suggested files:
- `cloud_provider.h`
- `cloud_provider_registry.h/.cpp`
- `cloud_share_types.h`
- `cloud_oauth_service.h/.cpp`
- `providers/dropbox_cloud_provider.h/.cpp`
- `providers/google_drive_cloud_provider.h/.cpp`
- `providers/onedrive_cloud_provider.h/.cpp`
- `providers/zerox0_cloud_provider.h/.cpp`

Existing classes that can be retired or absorbed over time:
- `CloudUploader`
- `DropboxUploader`
- parts of `CloudDownloader`

## Core Types

### `CloudProviderId`

Use a string id or enum for stable internal identity.

Examples:
- `dropbox`
- `google_drive`
- `onedrive`
- `zerox0`

### `CloudProviderDescriptor`

Describes a provider for UI and capability checks.

Suggested fields:
- `id`
- `displayName`
- `description`
- `warningText`
- `requiresAuth`
- `supportsUpload`
- `supportsDownload`
- `supportsDelete`
- `isTemporaryHosting`
- `isRecommended`

This is what dialogs should consume.

### `CloudShareArtifact`

Represents the result of a successful upload.

Suggested fields:
- `providerId`
- `shareUrl`
- `remoteItemId`
- `deleteToken`
- `displayLabel`
- `expiresAt`
- `metadata`

Notes:
- `remoteItemId` is useful for Google Drive and OneDrive
- `deleteToken` is useful for 0x0.st
- `metadata` can hold provider-specific extras if needed

### `CloudDownloadRequest`

Represents a provider-resolved download.

Suggested fields:
- `providerId`
- `originalUrl`
- `resolvedUrl`
- `headers`
- `suggestedFilename`

This lets providers control more than just URL rewriting.

## Main Interface

### `ICloudProvider`

Qt-friendly abstract base class:

```cpp
class ICloudProvider : public QObject
{
    Q_OBJECT
public:
    explicit ICloudProvider(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ICloudProvider() = default;

    virtual CloudProviderDescriptor descriptor() const = 0;

    virtual bool canHandleUrl(const QUrl& url) const = 0;

    virtual bool isAuthenticated() const = 0;
    virtual void authenticate() = 0;
    virtual void signOut() = 0;

    virtual void uploadFile(const QString& localPath) = 0;
    virtual void deleteArtifact(const CloudShareArtifact& artifact) = 0;
    virtual bool canDeleteArtifact(const CloudShareArtifact& artifact) const = 0;

    virtual void resolveDownload(const QUrl& shareUrl) = 0;
    virtual void abort() = 0;

signals:
    void authCompleted(bool success, const QString& errorMessage);

    void uploadProgress(qint64 sent, qint64 total);
    void uploadCompleted(const CloudShareArtifact& artifact);
    void uploadFailed(const QString& errorMessage);

    void deleteCompleted();
    void deleteFailed(const QString& errorMessage);

    void downloadResolved(const CloudDownloadRequest& request);
    void downloadResolveFailed(const QString& errorMessage);
};
```

Notes:
- Providers own auth behavior
- Providers own URL detection and download resolution
- Providers do not need to own file streaming for downloads if that can be shared

## Registry

### `CloudProviderRegistry`

Responsibilities:
- construct and own provider instances
- return descriptors for UI
- find provider by id
- find provider that can handle a pasted URL

Suggested API:

```cpp
class CloudProviderRegistry : public QObject
{
    Q_OBJECT
public:
    explicit CloudProviderRegistry(QObject* parent = nullptr);

    QList<CloudProviderDescriptor> uploadProviders() const;
    QList<CloudProviderDescriptor> downloadProviders() const;

    ICloudProvider* providerById(const QString& id) const;
    ICloudProvider* providerForUrl(const QUrl& url) const;
};
```

Benefits:
- `CloudUploadDialog` no longer knows concrete provider classes
- `RestoreDialog` no longer depends on `CloudDownloader::identifyProvider()`

## Auth Design

### `CloudOAuthService`

Keep a reusable OAuth helper, but move provider-specific behavior into config.

Suggested config:
- `providerKey`
- `authUrl`
- `tokenUrl`
- `clientId`
- `scope`
- `redirectPort`
- `extraAuthorizeParams`
- `extraTokenParams`
- `preserveRefreshTokenIfMissing`

Important behavior:
- if token refresh response omits `refresh_token`, preserve the existing one
- allow provider-specific auth query params without hard-coded Dropbox assumptions

This can evolve from the current `OAuth2Handler`.

## Download Flow

Split provider resolution from transport.

### `CloudDownloadManager`

Responsibilities:
- ask registry for the provider that handles a URL
- ask that provider to resolve a `CloudDownloadRequest`
- perform the actual `QNetworkAccessManager` GET using the resolved request
- stream to temp file

That keeps provider logic out of the generic downloader.

Suggested flow:
1. user pastes URL
2. registry finds provider
3. provider resolves URL into direct download request
4. download manager executes the request
5. restore dialog receives local temp path

## Upload Flow

Suggested flow:
1. `ShareDialog` creates the `.oscar` file
2. `CloudUploadDialog` asks registry for upload-capable providers
3. user selects provider
4. dialog triggers provider auth if needed
5. provider uploads the file and emits `CloudShareArtifact`
6. dialog displays artifact share URL and delete option if supported

The dialog should display text from `CloudProviderDescriptor`, not hard-coded provider strings.

## Provider Responsibilities

### Dropbox

Responsibilities:
- OAuth auth
- upload file
- create or reuse shared link
- optional future delete support if desired

Can migrate most code from current `DropboxUploader`.

### Google Drive

Responsibilities:
- OAuth auth
- upload normal Drive file
- create public or link-based permission
- return share URL and remote file id

Implementation note:
- use normal Drive file storage, not app-data storage

### OneDrive

Responsibilities:
- OAuth auth
- upload file
- create sharing link
- return share URL and remote item id

### 0x0.st

Responsibilities:
- anonymous upload
- optional secret URL handling
- delete via token
- resolve direct-download URL

Implementation note:
- this provider is simple and should not define the architecture

## UI Changes

### `CloudUploadDialog`

Current issue:
- it manually owns `DropboxUploader` and `CloudUploader`

Suggested change:
- own a `CloudProviderRegistry`
- keep a selected provider id
- populate buttons from descriptors
- connect only to the selected provider

Possible UI model:
- provider list or radio buttons generated from descriptors
- sign-in/out button shown only when `requiresAuth`
- provider description/warning loaded from descriptor
- delete button shown only when `supportsDelete` and artifact allows deletion

### `RestoreDialog`

Current issue:
- it depends on static provider detection and URL transformation inside `CloudDownloader`

Suggested change:
- ask registry to identify the provider from the pasted URL
- use provider descriptor for hint text
- use `CloudDownloadManager` for actual download

## Migration Plan

1. Add shared types and `ICloudProvider`.
2. Add `CloudProviderRegistry`.
3. Wrap the existing Dropbox code in `DropboxCloudProvider`.
4. Wrap the existing 0x0.st code in `ZeroX0CloudProvider`.
5. Refactor `CloudUploadDialog` to use the registry.
6. Refactor `RestoreDialog` to use provider resolution via registry.
7. Replace or shrink `CloudDownloader` into `CloudDownloadManager`.
8. Add Google Drive provider.
9. Add OneDrive provider.
10. Remove obsolete direct references to old uploader classes.

## Testing Plan

Add focused tests for:
- provider registry selection by URL
- provider descriptors and capabilities
- Dropbox refresh-token preservation
- Google Drive share-link construction logic
- OneDrive sharing-link creation request building
- 0x0.st response parsing and delete token capture
- download resolution for supported URL formats

Prefer testing provider logic without real network access by mocking token responses and HTTP reply payloads.

## Practical Notes

- Keep the interface asynchronous and signal-based to fit the existing Qt code style.
- Avoid a giant "universal cloud provider" class with switches for every service.
- Prefer provider-owned request building over central URL-rewrite logic.
- Keep UI text mostly in provider descriptors so new providers do not require dialog surgery.

## Summary

The key refactor is:

- move from "dialog knows each provider"
- to "dialog talks to a registry of providers through one interface"

That change should make Dropbox cleaner immediately and give OSCAR a solid path for Google Drive, OneDrive, and any future service.
