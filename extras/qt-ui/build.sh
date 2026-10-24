#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# See COPYING in the repository root.
set -eu
VLC_SOURCE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$VLC_SOURCE_ROOT/extras/qt-ui/environment.sh"
vlc_build=${VLC_BUILD_DIR:-"$VLC_SOURCE_ROOT/build-qt"}
vlc_reconfigure=
if [ -f "$vlc_build/build.ninja" ]; then
    vlc_reconfigure="--reconfigure --clearcache"
fi
meson setup $vlc_reconfigure "$vlc_build" "$VLC_SOURCE_ROOT" -Dauto_features=disabled \
    -Dqt=enabled -Dmedialibrary=enabled -Dxcb=enabled -Dx11=enabled -Dpulse=enabled \
    -Dalsa=enabled -Dlua=enabled -Dgnutls=enabled -Djpeg=enabled -Dmatroska=enabled \
    -Dfreetype=enabled -Dfontconfig=enabled -Dfribidi=enabled -Dharfbuzz=enabled -Dlibass=enabled
meson compile -C "$vlc_build" -j "${VLC_BUILD_JOBS:-4}"
