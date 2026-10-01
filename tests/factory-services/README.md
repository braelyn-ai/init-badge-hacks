# Native services host checks

Run `python3 tests/factory-services/run.py` on macOS. The script uses Clang,
CommonCrypto, Node, and the built-in image converter. Output stays in `.build/`.
It never opens a serial port or contacts the badge.

The fixture compiles the actual validators and record helpers extracted from
`firmware/factory_badge/main/services.cpp`, with host filesystem/crypto adapters.
It writes a synthetic profile/image using the retained Arduino store, reads it
with the native implementation, edits with native code, and reads the result
back. Native writes are now the single-account version 3 record (name, network
key, handle, company); versions 1 and 2 (three URL slots) are still read, and
their first filled slot becomes the account, so the Arduino store can no longer
read current records. Tests reject truncated/extra/invalid metadata, unknown
network keys and altered hashes/headers, inject failed sync/rename operations,
and check every network's handle parsing, Other-link provider recognition and
the setup page's network list. Reads never rewrite records.

The actual native form validator and page renderer cover optional company,
older requests retaining the saved value, explicit clearing, duplicate/unknown
fields, type checks, 60-codepoint/120-byte UTF-8 bounds, HTML escaping and template
token isolation. cJSON tree access is adapted; this does not exercise its parser.
The explicit initialization helper is checked against
all six partition boundaries, an extra partition, active setup, an already
ready store, a recovered mount, and a blank store. Only the last case may format.
Address/undefined-behavior sanitizers are enabled.

The actual native portal browser script is also executed through the existing
clock fixture: automatic sync, fresh timestamps on retry/reopen, UTC offset
direction, nonce, visible failure state, and independent profile saving. A second
browser fixture checks the one-account payload, Other-link wording, company
submission/clearing and retained edits after a failed save, through the actual form submit handler.

Photos are no longer chosen or uploaded on the phone (October 1, 2026): the badge
downloads one from the saved account after setup when it has none or the account
changed. The phone-side photo scripts, their fixtures and the `/image` endpoint
were removed; see Git history for the former upload checks.

These checks do not emulate ESP-IDF LittleFS mounting, Wi-Fi association, HTTP
socket lifecycle, or physical RTC writes. Integrated builds and device checks
are separate requirements.

The native reset fixture executes the production queue and worker operation against
synthetic profile/image records. Acceptance does not change the profile; only the
verified empty-record commit reports success. It checks setup/service/store
exclusion, duplicate requests, execution-time rechecking, failed open/sync/rename
retaining every prior field/photo, explicit retry, durable empty-record readback,
revision changes, and no automatic re-execution after terminal results. A separate
legacy-file sentinel and the format-call counter remain unchanged. These checks
never request a reset from a connected badge.
