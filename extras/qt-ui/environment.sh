# SPDX-License-Identifier: GPL-2.0-or-later
# See COPYING in the repository root.
# Source from scripts with VLC_SOURCE_ROOT set to the checkout root.
vlc_sdk="$VLC_SOURCE_ROOT/build-deps/root/usr"
if [ -d "$vlc_sdk" ]; then
    export PKG_CONFIG_PATH="$vlc_sdk/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PATH="$vlc_sdk/bin:$vlc_sdk/lib/qt6/bin:$vlc_sdk/lib/qt6/libexec:$PATH"
    export BISON_PKGDATADIR="$vlc_sdk/share/bison"
    export LD_LIBRARY_PATH="$vlc_sdk/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QML_IMPORT_PATH="$vlc_sdk/lib/x86_64-linux-gnu/qt6/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}"
    export QT_PLUGIN_PATH="$vlc_sdk/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi
