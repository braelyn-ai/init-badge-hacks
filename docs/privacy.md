# Privacy and local storage

> **Current application: offline conference badge (September 17, 2026).**
> Read [conference-badge.md](conference-badge.md) for the current six-page UI,
> manual setup, radio policy and storage model, and
> [conference-clock.md](conference-clock.md) for repeatable flashing.
> The connected badge/voice behavior below is the retained historical application.
> Hardware geometry, pin assignments, calibrated IMU mapping, dependency pins,
> and partition-preservation constraints remain applicable.

## Current conference data

The active native conference firmware stores only manually entered name, optional
company, three social URLs and a photo in its conference profile record. Bookmarked
agenda items occupy a separate versioned NVS mask. These fields stay on the badge;
the temporary local phone portal is AP-only and does not require a cloud login.
Source branding and the embedded background clip are static conference artwork,
not attendee data. Normal operation keeps Wi-Fi and Bluetooth off.

Photo selection and its preview are processed in the local browser; the image
is sent to the badge only on Save. Settings → Reset badge explicitly confirms
replacement of the current manual profile with an empty record and restores
conference UI defaults/bookmarks. It retains the clock, permanent event unlock
and historical connected records. This is logical removal, not a secure flash
erase. Ordinary updates and cancellation of reset preserve the profile.

Profile fields/photos, setup credentials and framebuffer captures remain private.
The diagnostic status reports company presence and bookmark bits, not company
text. The stock verifier refuses to capture any personalized profile, including
a company-only profile. See [conference-badge.md](conference-badge.md) for the
current record versions and local setup. The following sections document the
retained connected prototype and data that normal updates intentionally preserve.

This firmware is configured for chan.dev's Production Devices application. Each signed-in user sees their own connected profiles. The repository contains source and public configuration; device credentials and personal test captures are kept outside it.

## Where data lives

| Data | Location |
| --- | --- |
| Public AuthKit client ID, service URLs, certificate roots | Firmware source and compiled application |
| Wi-Fi network name and password | The open event network is compiled into firmware; any other network is saved only in device NVS through the local setup portal |
| WorkOS refresh token, user ID, organization ID, and account email | Device NVS session record |
| WorkOS access token | Device RAM |
| LinkedIn, X, and GitHub OAuth/provider tokens | Existing chan.dev/WorkOS services; not sent to this firmware |
| Public profile name, handle, IDs, URLs, and avatar pixels | Device RAM and LittleFS cache |
| Selected account and styles | Device NVS |
| Recorded PCM/WAV and transcript | Volatile device memory; bounded HTTPS transcription through the Devices gateway |
| Unresolved reply key and owner/workspace/target/sender/connection IDs | Device NVS, retained until a definitive matching receipt |
| Reply receipt and sender/target duplicate guard | Private gateway Durable Object storage |

The public-profile cache excludes email, Wi-Fi credentials, access/refresh tokens, and raw authentication responses. Its owner/workspace identifiers are needed to keep users' records separate. The cached portrait comes from the user's profile and is processed on the device; it is not embedded as a personal image in the source.

## Wi-Fi setup and network requests

**Profile photo download.** If the attendee chooses "From my social network" during
phone setup, the badge later sends that one handle (and the
network name) to `avatar.chan.dev`, which looks it up through unavatar.io and
returns a 160×160 JPEG. No account, token or other profile field is sent. The
relay caches photos for 7 days and does not log handles. The downloaded photo is
stored on the badge like an uploaded one and is cleared by Reset badge.

Wi-Fi setup creates a temporary hotspot protected by a newly generated password shown in its QR code. The local portal uses HTTP at `192.168.4.1` over that hotspot. It accepts setup requests from the hotspot interface, checks a per-session nonce, and saves credentials only after joining the selected network. The hotspot closes after success, when leaving setup, or after ten minutes.

WorkOS, backend, and avatar traffic use certificate-validated HTTPS. The firmware sends the WorkOS bearer token only to the exact configured profile/workspace endpoints. Avatar requests use restricted provider CDN URLs, omit the bearer token, and do not follow redirects. Downloads and decoding have size limits.

The voice app also sends its Devices bearer to exact approved routes on
`devices.chan.dev`. The gateway obtains user-bound X/xAI/Deepgram credentials
through a private binding to the separate shared Auth service. Those provider
credentials never enter device responses. The gateway does not save raw audio or
transcripts and does not log request bodies or tokens. Transcription necessarily
sends the recorded speech to the explicitly selected provider. The board records
only after a deliberate hold or an explicitly requested bounded diagnostic.

The board wipes the recording after upload, failure, or cancellation and retains
transcripts only in RAM for review. Pending receipt metadata survives restart so
an uncertain network result cannot cause an automatic duplicate reply. Receipt
storage is not an offline authorization source. See [voice replies](voice-replies.md)
for send recovery and the distinction between cancellation and retraction.

The device's session checks validate context on a trusted HTTPS response; they are not a general-purpose offline JWT verifier. Authorization for provider data is enforced by the separately maintained backend.

## Offline behavior

A saved badge represents the last successfully saved public data. It can be shown without Wi-Fi or a current access token; Settings labels this state accordingly. The cache never creates an authenticated session.

A detected user/workspace change or explicit authentication rejection invalidates cached profiles. Provider disconnection invalidates the affected provider. If the linked remote account changes, its former identity is invalidated before its replacement is saved. Temporary network failures retain matching saved data for display. A sign-out or disconnection made elsewhere cannot be discovered while the device is offline.

Cache hashes and context checks protect against damaged files and accidental account mixups. They do not make local storage confidential or prevent a person with physical flash access from changing it.

## Physical device and publishing

**Device storage is currently unencrypted.** A physical flash read or full-device backup can contain Wi-Fi credentials, the refresh token, account email, identifiers, and cached profiles. This personal prototype does not enable secure boot, flash encryption, or change security eFuses.

Keep full-device backups private. Distribute reviewed source or source-built application components, never a dump taken from a provisioned board. The normal flash script replaces application components while preserving existing NVS and file storage; it is not a credential-erasure tool.

USB diagnostics do not expose passwords or tokens, but some report public pairing information, client/user/session identifiers, connection state, and device metrics. Display captures can contain names, portraits, and public profile QR codes. Review diagnostic output before publishing it. `BADGE_STORE` counters contain storage/activity metrics rather than cached profile contents.
