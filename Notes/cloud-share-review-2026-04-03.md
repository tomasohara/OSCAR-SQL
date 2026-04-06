# Cloud Share Code Review

Date: 2026-04-03

Scope:
- Share Profile upload flow
- Cloud restore/download flow
- Current Dropbox and 0x0.st handling
- Design readiness for Google Drive, OneDrive, and future providers

## Findings

1. 0x0.st is technically viable for this use case, but carries product/policy risk.

For the intended OSCAR usage, 0x0.st appears technically workable: medium-sized files, short retention, and low per-user volume are a reasonable fit. The main concern is not bandwidth or file size, but that 0x0.st's current site policy explicitly says it is not for backups or automated mass uploads. Even if OSCAR's actual usage stays modest, support for this path still depends on the operator continuing to view that usage as acceptable.

Relevant code:
- `oscar/network/cloud_upload_dialog.cpp`
- `oscar/network/cloud_uploader.cpp`

Recommendation:
- 0x0.st can remain a supported temporary-hosting provider.
- Avoid making 0x0.st the only or primary long-term cloud strategy.
- If it is kept, use secret URLs and make the instance configurable.

2. OAuth refresh-token handling is incorrect and will break reuse across providers.

`OAuth2Handler` overwrites the stored refresh token with whatever is returned by the token endpoint. Dropbox refresh responses return a new access token but typically do not return a new refresh token. That means the existing refresh token can be erased after a refresh, which will eventually force re-authentication.

Relevant code:
- `oscar/network/oauth2_handler.cpp`
- `oscar/network/dropbox_uploader.cpp`

Recommendation:
- Preserve the existing refresh token when a refresh response omits `refresh_token`.
- Move provider-specific token parsing rules into provider config or provider adapters.

3. Upload design is tightly coupled to the current providers.

`CloudUploadDialog` directly knows about `DropboxUploader` and `CloudUploader` (0x0.st), builds provider radio buttons manually, and contains provider-specific text and state logic. Adding Google Drive and OneDrive in the same style will spread provider logic through the dialog and make maintenance harder.

Relevant code:
- `oscar/network/cloud_upload_dialog.h`
- `oscar/network/cloud_upload_dialog.cpp`

Recommendation:
- Replace hard-coded provider wiring with a provider registry and a common interface.
- Let the UI render available providers from metadata rather than hard-coded controls.

4. The current "generic" OAuth helper is still Dropbox-shaped.

`OAuth2Handler` is reusable in name, but it hard-codes auth request behavior such as `token_access_type=offline` and assumes a single token-response shape. Google Drive and OneDrive will need provider-specific auth parameters and token parsing behavior.

Relevant code:
- `oscar/network/oauth2_handler.h`
- `oscar/network/oauth2_handler.cpp`

Recommendation:
- Keep a shared OAuth base if useful, but make request parameters and token parsing provider-configurable.
- Avoid growing a single helper full of provider-specific conditionals.

5. Download support is also hard-coded and will be hard to extend safely.

`CloudDownloader` uses a central enum plus switch statements for provider detection, support checks, naming, and URL transformation. That works for a small number of simple cases, but it is not a strong foundation for long-term support of multiple cloud services.

Relevant code:
- `oscar/network/cloud_downloader.h`
- `oscar/network/cloud_downloader.cpp`

Recommendation:
- Move provider detection and URL resolution into provider-specific classes.
- Keep the downloader focused on network transfer and temp-file handling.

6. Google Drive and OneDrive should be implemented as real providers, not just link hacks.

For restore, simple link transformation may work for some public links, but upload support needs proper provider flows:
- Google Drive: upload file, then create public sharing permission
- OneDrive: upload file, then create a sharing link

Recommendation:
- Design each provider around its real upload/share model, not around one shared "URL rewrite" abstraction.

7. Google Drive is a strong provider candidate.

Google Drive fits the expected usage well and should be treated as a first-class provider. The main implementation caution is to avoid `appDataFolder`, because that storage is meant for app-private data rather than user-shareable files.

Recommendation:
- Implement Google Drive using normal file upload plus sharing permissions.
- Treat Google Drive as a primary supported provider alongside Dropbox and OneDrive.

8. There is no cloud-specific test coverage in `oscar/tests`.

I did not find tests covering:
- provider detection
- share-link transformation
- OAuth token persistence and refresh behavior
- 0x0.st upload/delete parsing

Recommendation:
- Add focused tests for provider parsing and auth edge cases before expanding provider support.

## Recommended Design Direction

Introduce a provider abstraction, for example:

- `ICloudShareProvider`
- `CloudProviderDescriptor`
- `CloudProviderRegistry`

Suggested provider responsibilities:
- provider id and display name
- whether auth is required
- whether upload/download/delete are supported
- authenticate/sign out
- upload file and return share artifact
- delete uploaded artifact if supported
- detect whether a URL belongs to the provider
- resolve a share URL into a direct download request

Suggested shared data object:
- `CloudShareArtifact`
  - provider id
  - share URL
  - provider-specific remote id
  - delete token or remote delete handle
  - expiry metadata if available

Suggested UI behavior:
- the dialog asks the registry for available upload providers
- the dialog shows provider description/warning/auth state from provider metadata
- the dialog never directly references `DropboxUploader`, `CloudUploader`, or future provider classes

Suggested layering:
- UI layer: dialogs and progress presentation
- provider layer: Dropbox, Google Drive, OneDrive, 0x0 adapters
- auth layer: reusable OAuth helper with provider-specific configuration hooks
- transfer layer: common network helpers if needed

## Near-Term Priorities

1. Fix refresh-token persistence in `OAuth2Handler`.
2. Stop defaulting to 0x0.st.
3. Extract a common upload-provider interface.
4. Convert Dropbox to the new provider interface first.
5. Add tests for token refresh handling and provider URL resolution.
6. Implement Google Drive and OneDrive against the new abstraction.

## Notes

- Google Drive app-data storage is not appropriate for this feature because files there are not meant for user sharing.
- OneDrive upload/share should be built around its official share-link APIs rather than relying only on URL rewriting.
- 0x0.st appears suitable for modest short-term sharing, but should still be treated as a provider with external policy risk rather than one fully under OSCAR's control.
