# CPAP Data Sharing Service Recommendation

Date: 2026-03-29

## Summary

For OSCAR, the practical sharing options appear to be:

1. Export to a file and let the user share it using a cloud service they already have, such as Dropbox, OneDrive, or Google Drive.
2. Offer an optional direct upload to `0x0.st` as a convenience feature for users who do not want to manage their own cloud storage.

At this time, these appear to be the only realistic approaches that satisfy the project's constraints:

- No OSCAR-hosted storage
- No ongoing storage/bandwidth obligation for the OSCAR team
- Low-friction sharing
- A simple returned URL
- Multiple downloads
- Retention on the order of weeks, not hours

## Recommendation

The recommended product direction is:

- Keep the current export-to-file workflow as the primary and safest sharing method.
- If desired, add `Upload to 0x0.st` as an explicit secondary option for convenience.
- Do not present `0x0.st` as equivalent to the user's own cloud storage.
- Make the third-party nature of the upload impossible to miss.

This keeps OSCAR's current low support burden while still giving users a one-step sharing option when convenience matters more than control.

## Why This Is The Best Fit

### User's existing cloud service

Pros:

- User already chose the provider and account
- Better aligns with normal user expectations around storage and sharing
- Less risk that OSCAR appears to endorse or depend on an anonymous file host
- No new service dependency in the OSCAR UI beyond exporting the file

Cons:

- More user steps
- Requires the user to already have a cloud service account

### 0x0.st

Pros:

- Very simple upload model
- Returns a shareable URL directly
- Supports expiry and file deletion via management token
- Retention is compatible with the "about a month" use case
- No user account required

Cons:

- Public third-party file host not controlled by OSCAR
- Privacy guarantees are limited
- Service terms/policies can change
- Availability and future reliability are outside OSCAR's control
- OSCAR must be careful not to imply medical-grade confidentiality or custody

## Suggested UX

If `0x0.st` upload is implemented:

- Put it behind an explicit command such as `Share via Temporary Upload Service...`
- Do not make it the default export action
- Show a confirmation dialog before upload
- Clearly identify the destination service by name
- State that the file will be uploaded to a third-party public host
- State that OSCAR does not control that service's privacy, retention, or availability
- Offer to copy the returned URL to the clipboard
- Store the delete token locally if available so OSCAR can offer `Delete Uploaded Copy`

## Draft Warning Dialog

### Title

Upload Exported Data to 0x0.st?

### Body

You are about to upload this OSCAR export to `0x0.st`, a third-party file hosting service not operated or controlled by the OSCAR team.

Anyone with the resulting link may be able to download the file. OSCAR cannot guarantee the privacy, retention period, or continued availability of files stored on this service.

Upload only if you are comfortable sharing this data through an external public host.

### Checkbox

I understand that this file will be uploaded to a third-party service outside OSCAR's control.

### Buttons

- Upload
- Cancel

## Draft Post-Upload Text

Upload complete. Share this link with the person who will review your OSCAR data.

If available:

- Copy Link
- Delete Uploaded Copy
- Close

## Implementation Notes

- Treat `0x0.st` as a provider-specific convenience feature, not as a general storage backend.
- Keep the provider implementation isolated so it can be disabled or replaced later.
- Use a distinct User-Agent string identifying OSCAR.
- Preserve the delete/management token if returned.
- Consider allowing the user to review the export details before upload.
- Consider defaulting to a shorter expiry if the service allows it.

## Bottom Line

OSCAR should continue to treat user-managed cloud storage as the primary sharing path.

If the team wants an easier built-in sharing option, `0x0.st` is the clearest candidate, but it should be presented as a convenience upload to an external third-party host, with prominent user consent and no implication that OSCAR controls the uploaded data.
