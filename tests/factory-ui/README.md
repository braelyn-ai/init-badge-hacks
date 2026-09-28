# Native UI host checks

Run `bash tests/factory-ui/run.sh` after fetching the pinned factory dependencies.
This compiles the production page files with real LVGL, Smooth container ownership
and Mooncake lifecycle, using a memory framebuffer and a simulated pointer input.
Address/undefined-behavior sanitizers are enabled by default.

Checks cover all six pages and centered indicators while the invitation is locked
or unlocked; the exact `tap the code to reveal a secret invitation` prompt; whole-page native touch-Morse taps/holds,
complete-word callbacks, immediate code retry after an explicit relock, and cancellation on drags, press loss, page/modal changes
and rotation; noninterrupting timed reveal during agenda scrolling/modals;
the supplied artwork hidden while locked/during the words, upward slide, centered
rounded poster, completed artwork/caption taps to toggle the official event QR
and return, drag rejection, and QR view retained during updates;
the ordered three-word reveal, one-shot native GIF and cleanup on completion or
early navigation, with no replay on already-unlocked entry or timed reveal;
supplied-brand chrome and square page indicators after partial redraws; no redraws on idle
static pages; release-only buttons; drag-out-and-back, within-button drag and
long-hold cancellation for ordinary buttons (the code surface accepts holds);
real agenda text and wrapped titles/details; current/past/upcoming
styles, release-only bookmark toggles, invalid time, daily reset and preserved schedule scroll;
agenda entry and native swipe snapping center each row between the arrows within one pixel,
including the first/last rows and entries without a current session or valid clock;
settled agenda frames match a full redraw byte-for-byte after entry and endpoint swipes;
native schedule/social scrolling; dynamic profile and
avatar/company updates, social-only profiles and configured faces without chrome; QR expansion; setup return paths; and Touch test lifecycle.
They do not validate the physical sensor, display transport, or orientation HAL.
The touch-code view uses standard nominal 200 ms dots/internal gaps, 600 ms
dashes/letter gaps and 1400 ms word gaps. Production `MorseUnlock` accepts dots
50–399 ms, dashes 400–1400 ms, internal gaps 50–399 ms and letter pauses
400–1399 ms; a word boundary makes an incomplete `init` incorrect. It has a 2.5-second
released-idle reset and a 15-second attempt bound. Correct input completes on the
final dash release. Native dot/dash groups, letter spacing, shake/fade feedback,
immediate retry during that animation and lifecycle cleanup are checked too.
Haptic callbacks are checked for held-contact edges, bounded long holds, and
shutdown on release, cancellation, view destruction, rotation and timed reveal;
this fixture does not operate or measure a motor.
Main owns clock policy and NVS persistence;
the UI fixture observes its callback without writing a device unlock.

Optional review images (PPM, outside Git):

```
.build/tests/factory-ui/factory_ui_test .build/factory-ui-captures
```

`FACTORY_COMPONENTS` may point to an alternate directory containing the same
pinned `lvgl`, `mooncake`, and `smooth_ui_toolkit` component sources.
