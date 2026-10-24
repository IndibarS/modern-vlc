<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# VLC redesign implementation tracker

Design reference: VLC Redesign by [Juxtopposed](https://www.juxtopposed.com/).

Target: native VLC 4 Qt/QML interface.

## Acceptance criteria

Score each screen out of 10: geometry (2), typography/colors (2), assets (2), working interactions (2), and rendered/runtime verification (2). An unimplemented screen scores 0. A screen cannot score 10 without a matching rendered comparison and working interactions. Overall completion requires every screen to pass, not an average that hides missing screens.

## Screens

| Design node | Screen | Status |
| --- | --- | --- |
| 187:328 | Player | Pending |
| 970:3308 | Player | Pending |
| 970:3792 | Player | Pending |
| 970:2703 | Player | Pending |
| 970:4028 | Player | Pending |
| 6261:15078 | Player | Pending |
| 247:445 | Player | Partial: header, transport, timeline, volume and track buttons implemented; 7/10 |
| 4767:23726 | Player - Music | Pending |
| 4778:25123 | Player - Music | Pending |
| 3328:11554 | Player - hide UI | Pending |
| 2425:13624 | Player - Light | Partial: palette and light icons verified; library/sidebar layout pending; 6/10 |
| 2107:5604 | Player - customize | Pending |
| 2160:6137 | Player - customize | Pending |
| 2160:6564 | Player - customize | Pending |
| 2160:7005 | Player - customize | Pending |
| 2160:7437 | Player - customize | Pending |
| 2160:7869 | Player - customize | Pending |
| 6587:19923 | Player - customize | Pending |
| 2160:8301 | Player - customize | Pending |
| 2160:8732 | Player - customize | Pending |
| 999:5047 | Settings - global | Pending |
| 3046:9282 | Settings - global | Pending |
| 3046:10057 | Settings - interface | Pending |
| 3062:11379 | Settings - interface | Pending |
| 3062:11911 | Settings - privacy | Pending |
| 3062:12552 | Settings - playback | Pending |
| 3072:13408 | Settings - playback | Pending |
| 3073:14187 | Settings - video | Pending |
| 3073:15375 | Settings - video | Pending |
| 3073:15879 | Settings - audio | Pending |
| 3180:17398 | Settings - audio | Pending |
| 3180:17989 | Settings - subtitles | Pending |
| 6536:13908 | Settings - subtitles | Pending |
| 6536:14482 | Settings - controls | Pending |
| 6542:15251 | Settings - library | Pending |
| 6543:15649 | Settings - network | Pending |
| 6571:15958 | Settings - performance | Pending |
| 6571:16429 | Settings - recording | Pending |
| 6575:17137 | Settings - extensions | Pending |
| 1911:7545 | Add Subtitles to Playlist/Folder | Pending |
| 3336:11929 | Add Subtitles to Playlist/Folder | Pending |
| 1919:8707 | Add Subtitles to Playlist/Folder | Pending |
| 1919:9278 | Add Subtitles to Playlist/Folder | Pending |
| 2437:14832 | Keyboard Shortcuts | Pending |
| 6216:13614 | Open network stream | Implemented; input, cancel and ordered save verified; 8/10 |
| 6259:14954 | new playlist | Pending |
| 1899:3055 | Onboarding | Pending |
| 1902:3670 | Onboarding | Pending |
| 1902:6770 | Onboarding | Pending |
| 1902:4020 | Onboarding | Pending |
| 1902:4325 | Onboarding | Pending |
| 1902:4634 | Onboarding | Pending |
| 1902:4940 | Onboarding | Pending |
| 1902:5245 | Onboarding | Pending |
| 1902:5550 | Onboarding | Pending |
| 1902:6160 | Onboarding | Pending |
| 1902:6465 | Onboarding | Pending |

## Build and run

```sh
python3 extras/qt-ui/prepare-debian-sdk.py  # Once, when local SDK dependencies are missing.
extras/qt-ui/build.sh
./run-vlc /absolute/path/to/video.mp4
```

The local Debian SDK is extracted into ignored `build-deps/root`; system packages
are not installed. The Meson build lives in ignored `build-qt`. The launcher selects
its plugins and Qt runtime. Enabled development modules include Qt, MediaLibrary,
X11/XCB, ALSA, PulseAudio, GnuTLS, Lua and JPEG (for seek previews). The SDK pairs the Lua compiler and runtime
at 5.4; reconfiguration clears stale dependency detection.

This is a development build, not a complete distribution package. Existing toolbar
profiles are retained; redesigned defaults apply to new/default profiles. Use
`--qt-pin-controls` for pinned playback controls or `--no-qt-titlebar` for VLC's
client-side window controls.

## Source organization

- Qt interface and controls: `modules/gui/qt/`, using the existing QML/C++ structure.
- UI icons: `modules/gui/qt/pixmaps/modern/`, registered in `assets.qrc` and `Makefile.am`.
- Build/runtime helpers and GUI smoke tests: `extras/qt-ui/`.
- Linux resolver regression: `test/src/network/getaddrinfo.c`, registered for Meson and Automake.
- Documentation: this directory. Temporary logs, screenshots and SDK files stay in ignored build directories.

## Fixes and verification (2026-10-18)

The full Qt build succeeds. The native-window smoke test verifies live Day/Night
preview, Save persistence, Cancel rollback, both titlebar transitions, corner-drag
resizing from 1000×700 to 1100×800, and clean exit from the in-app Close button.
Windowed playback keeps its client-side header visible when playback controls hide.

Explicit themes populate both the QML semantic palette and the native Widgets
palette. System mode restores the platform palette. Dialog surfaces and disabled
text follow palette changes. Preferences retain the selected scheme when reloading
other options on Save instead of restoring a stale startup value.

The shutdown SIGSEGV was traced with GDB to MediaLibrary's static logger destructor
after its plugin unloaded. The plugin now remains mapped until process exit so its
logger deleter remains valid. The existing thumbnail API adapter supports both
MediaLibrary 0.13 and 0.14.

A separate shutdown hang was traced to Linux asynchronous DNS cancellation.
`src/linux/getaddrinfo.c` now returns directly for a successfully cancelled queued
request, since glibc does not deliver its notification. For running/completed
requests it waits for the pending callback before releasing stack storage. This
also fixes the callback lifetime race. The behavior was checked against
[glibc's request cancellation implementation](https://github.com/bminor/glibc/blob/master/resolv/gai_misc.c).

The resolver regression covers normal completion, queued cancellation, running
lookup cancellation, delayed notification after completion, enqueue failure, and
128 real local-address interrupted lookups per run. The original source hangs in
the queued-cancellation case; the patched version passed ten runs (1,280 real
lookups). Qt's FSM test also passes.

```sh
# Playback, track selectors, themes, dialogs, HTTP playback and clean shutdown:
xvfb-run -a -s '-screen 0 1280x800x24' python3 extras/qt-ui/smoke-test.py --theme night --csd --window-manager
xvfb-run -a -s '-screen 0 1280x800x24' python3 extras/qt-ui/smoke-test.py --theme day --csd --floating --window-manager
# Quit while a network stream is still opening:
xvfb-run -a -s '-screen 0 1920x1080x24' python3 extras/qt-ui/smoke-test.py --quit-during-network-open
# Requires Openbox (system or local extraction in build-deps/wm/root):
xvfb-run -a -s '-screen 0 1600x1000x24' python3 extras/qt-ui/window-smoke-test.py
# Enable/build the resolver test:
VLC_SOURCE_ROOT="$PWD" QT_QPA_PLATFORM=offscreen bash -ec '. extras/qt-ui/environment.sh; meson setup --reconfigure build-qt -Dtests=enabled; meson compile -C build-qt qml_test test_src_network_getaddrinfo -j4; meson test -C build-qt --no-rebuild --print-errorlogs tst_FSM test_src_network_getaddrinfo'
```

Tests use temporary settings, generated MPEG-4/AAC media and local HTTP streams,
with ALSA's silent null output and a monotonic clock for real-time pacing.
The HTTP fixture supports byte ranges. Playlist
contents are inspected after shutdown so the test does not compete with
MediaLibrary's exclusive SQLite transactions. Results and screenshots are retained
under `build-qt/test-artifacts/`. Set `VLC_SMOKE_DEBUG=1` to capture a debugger
backtrace if a playback smoke test times out at shutdown.

These checks do not certify every codec, audible output, GPU or desktop platform.
The acceptance playback runs and the dedicated window test use Openbox.
Use the window-manager option for realistic fullscreen/focus transitions.
Earlier failed runs are retained in test artifacts, not counted as acceptance
evidence. The floating toolbar now remains visible while playback is paused,
preventing timeline clicks from reaching the video after the hide timeout. The Qt static timer-cleanup, portal registration and discovery-socket
warnings may still appear and are not claimed fixed. Older smoke runs that did not
assert process exit status are not shutdown evidence.

## Volume and playback notifications

The volume slider has a 200% minimum range (including with old 125% settings),
while larger configured ranges remain available. The 100% marker stays at the
correct position; right-click stepping also reaches the full range. The first
valid backend volume now notifies interfaces without displaying a startup OSD.
Playback text, pause/play, mute, seek and volume notifications use the current
Qt palette. External video windows and other interfaces retain native OSD, and
the existing OSD enable/disable option remains respected. Fullscreen has explicit
Exit fullscreen and Quit VLC buttons at the top right, independent of native
titlebar settings. With auto-hiding controls, moving the mouse reveals them.
Ctrl+Q remains available to quit.

`test_src_player_outputs` covers the initial volume notification, duplicate and
invalid reports, repeated OSD text, icons, 200% slider messages and disabled OSD.
The GUI smoke test checks startup fill, the actual 200% backend volume, keyboard
volume steps, themed pause/volume notifications, fullscreen exit/quit and clean
shutdown. Dark-theme and light-theme runs passed; the light run also covered
client-side decorations and auto-hiding controls. The player-output regression
passed. These GUI runs use silent ALSA output, not an audible-output test.

## Seek previews and control stability

Hovering the video timeline displays a 240×135 thumbnail and timestamp in an
in-window, themed, passive card that cannot grab clicks or keyboard focus. `player/seek_preview.cpp` uses VLC's asynchronous
thumbnail API, debounces hover requests for 60 ms and permits one request at a
time. Local videos precompute 32 spread-out samples with a 1.5-second pause
between background requests. The nearest cached image appears immediately with
its own timestamp, then the exact hovered frame replaces it. Up to 96 images per
video persist in the application cache; other-media files beyond a 128 MiB budget
are pruned when opening a source. File size and modification time invalidate stale
images. Changing media cancels pending work; leaving hover lets useful work finish.
Each request has a three-second timeout and uses an external helper when available.
Unsupported sources display “Preview unavailable”. Hovering never seeks the active
player. The development build enables VLC's existing JPEG encoder and native MKV
reader; no video codec implementation is changed. The build also explicitly enables
FreeType/fontconfig, FriBidi/HarfBuzz, and libass for text and ASS/SSA subtitles.
`extras/qt-ui/subtitle-smoke-test.py` exercises embedded SRT rendering, styled ASS
rendering, subtitle track cycling, disabling subtitles, and shutdown on a private
display with isolated settings.

Plain Left/Right in the playback view seek by ten seconds and display −10s/+10s.
The notification retains priority over position/frame-step messages from paused
seeks. Qt discards queued position updates belonging to earlier seeks, preventing
backward flashes in the progress fill. Buffering uses a delayed thin strip in
the compact timeline instead of covering its position fill. The volume tooltip stays inside the Qt
window so it cannot steal hover from its slider. Fullscreen uses VLC's exit-
fullscreen glyph and an explicit Quit button.

Floating controls remain visible while paused and auto-hide during playback.
The GUI smoke test waits past the hide timeout and verifies a timeline click
seeks without resuming playback. It also checks thumbnail pixels against the requested video frame,
stationary volume-hover stability over 16 captures, and progress movement during
repeated arrow seeks, including checking that buffering cannot cover the fill.
The final dark pinned and light floating runs with Openbox passed.
`test_src_preparser_thumbnail` checks thumbnail timestamps
(including position/time seeks, delayed tracks and cancellation).

## Playback boundary

Core changes are limited to the Linux resolver lifetime/cancellation fix, the
first valid audio-volume notification, an optional informational OSD handoff,
and applying the initial thumbnail seek before its first frame is demuxed. The
latter runs only for thumbnail inputs; ordinary playback is unchanged.
The OSD event contract is declared in `include/vlc_vout_osd.h`; interfaces copy
messages synchronously and leave native rendering as the fallback when inactive.
Qt displays these messages in a non-interactive, theme-aware notification while
its embedded player is visible. Subtitles are unaffected. There are no changes in
`modules/codec`, `modules/demux`, `modules/packetizer`, `modules/video_output`,
`modules/video_filter` or `modules/access`. UI actions continue through VLC's
existing player and playlist controllers. Media decoding and video compatibility
logic are unchanged.

## Remaining design work

The screen scores above remain provisional. Full design acceptance still requires
settings layouts, onboarding, music screens, sidebar/library composition,
customization states, the new-playlist dialog, subtitle import, shortcuts, all
light variants and per-screen rendered comparisons. The preferences layout and
track selectors still reuse upstream VLC layouts. Player spacing and the rounded
video frame also need further design work. Shared colors alone do not complete a
screen, and this is not a 10/10 completion claim.

## Maintenance and licensing

The obsolete build tree for the former nested source checkout has been removed.
Unused redesign palette roles and their unused context have been removed. The
clipboard URL dialog, custom toolbar controls, fullscreen/resume layouts and
non-compact fallbacks remain reachable VLC functionality; they are not dead code.

The application retains VLC's GPL-2.0-or-later licensing; library components keep
their existing file-level licenses. New helper scripts use GPL-2.0-or-later, and
the resolver test follows LGPL-2.1-or-later. See [licensing and asset provenance](LICENSING.md)
for the imported artwork's separate, unverified licensing status.

## Playback stability checks (October 2026)

The development build explicitly enables the native Matroska reader. With the
previous fallback reader, two tested MKVs produced an unidentified subtitle codec
warning although their video played. Short playback checks of H.264, HEVC and AV1
MKVs now use the native reader without that warning. This changes module selection,
not codec implementations.

The fullscreen exit action releases both interface and video fullscreen holds.
Escape and the configured fullscreen hotkey use that action while fullscreen;
F11 also works through the player's hotkey route. GUI regression checks cover the
button and keyboard exits, stable seeking, 200% volume, themed track popups, and
clean exit. Four core tests cover the toolbar state machine, thumbnail timestamps,
player seeks and output notifications.

Embedded SRT and styled ASS rendering, track cycling and disabling subtitles pass
the dedicated subtitle test. An AV1 MKV was also checked visually with English
subtitles enabled and disabled. Three real MKVs generated library thumbnails and
the Home and Video grids displayed them. Cached hover reuse measured about half a
second in the GUI smoke test. All media checks use isolated settings and library
storage and leave the original files unchanged.

A shutdown warning, `QObject::~QObject: Timers cannot be stopped from another
thread`, remains under investigation. A debugger traces it to Qt Quick static
cleanup at process exit, after the GUI thread stops. Qt’s [pixmap cache implementation](https://github.com/qt/qtdeclarative/blob/6.10/src/quick/util/qquickpixmapcache.cpp)
uses a static object and a delayed cleanup timer. Tested exits returned zero;
this does not establish that every reported crash is fixed. No warning suppression
or unsafe global-object deletion is used as a workaround.
