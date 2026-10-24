<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Licensing and asset provenance

## Software

This work preserves VLC's existing licensing structure and notices rather than
assigning one new license to the whole source tree:

| Scope | License |
| --- | --- |
| VLC application and Qt interface additions | GPL-2.0-or-later, as stated in the file headers and [COPYING](../../COPYING) |
| Local build helpers, launcher, GUI smoke tests and new documentation | GPL-2.0-or-later |
| Linux resolver and its new regression test | LGPL-2.1-or-later, following the original resolver header and [COPYING.LIB](../../COPYING.LIB) |
| Other upstream components and dependencies | Their existing file-level licenses and notices, unchanged |

`COPYING` and `COPYING.LIB` remain identical to the upstream checkout. New helper
scripts and the resolver test carry SPDX identifiers. Existing author notices in
derived Qt components have been retained. No third-party software or artwork has
been relicensed by this change. Dependency licenses still apply to distributed
builds; a development build is not a distribution-license audit.

## Imported UI artwork

The 34 SVG files in `modules/gui/qt/pixmaps/modern/` were exported from the supplied
VLC Redesign reference. They are tracked separately from software licensing and
have not been stamped with GPL identifiers. File contents were retained when the
resource directory was renamed; presentation changes are made in Qt/QML.

Design credit: **VLC Redesign by [Juxtopposed](https://www.juxtopposed.com/)**.
This Qt implementation adapts the concept to responsive controls, themes and VLC's
existing playback and library interfaces; it is not an endorsement by the designer
or VideoLAN.

### Licensing information checked on 2026-10-18

A [separate public implementation](https://github.com/dsojevic/juxtopposed-vlc#redesign-attribution)
credits this design to Juxtopposed under CC BY 4.0. This is corroborating information,
not a license grant from its creator to this checkout. The original publication
could not be inspected directly. The exact supplied copy and individual exported
assets therefore retain **unverified license provenance** here.

The available information is recorded rather than inventing a license grant or
relicensing someone else's work. If the exact asset provenance is later confirmed,
record its license and required attribution here. Until then, do not describe the
imported artwork as GPL-licensed or the entire bundle as fully license-verified.
The software's GPL/LGPL terms are unaffected.
