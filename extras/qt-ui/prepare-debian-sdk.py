#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# See COPYING in the repository root.
"""Download and extract build dependencies locally; does not install packages."""
from pathlib import Path
import subprocess

project = Path(__file__).resolve().parents[2]
deps = project / 'build-deps'
archive = deps / 'debs'
root = deps / 'root'
archive.mkdir(parents=True, exist_ok=True)
root.mkdir(parents=True, exist_ok=True)
packages = '''flex bison libfl2 libgnutls28-dev libjpeg62-turbo-dev libebml-dev libmatroska-dev libfreetype-dev libfontconfig-dev libfribidi-dev libharfbuzz-dev libass-dev liblua5.4-dev lua5.4
qt6-base-dev qt6-base-private-dev qt6-declarative-dev
qt6-declarative-private-dev qt6-shadertools-dev qt6-svg-dev
qml6-module-qtquick-controls qml6-module-qtquick-layouts
qml6-module-qtquick-templates qml6-module-qtquick-window
qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript
libmedialibrary-dev libmedialibrary0 libxcb1-dev libxcb-shape0-dev
libxcb-xfixes0-dev libxcb-composite0-dev libxcb-xkb-dev
libxkbcommon-x11-dev libxcb-randr0-dev libxcb-render0-dev
libxcb-shm0-dev libxcb-keysyms1-dev libxcb-image0-dev
libxcb-icccm4-dev libxcb-util-dev libxcb-render-util0-dev
libxcb-damage0-dev'''.split()
plan = subprocess.check_output(
    ['apt-get', '-s', '--no-install-recommends', 'install', *packages], text=True)
packages = sorted(set(packages + [line.split()[1] for line in plan.splitlines()
                                  if line.startswith('Inst ')]))
subprocess.run(['apt-get', 'download', *packages], cwd=archive, check=True)
for package in archive.glob('*.deb'):
    subprocess.run(['dpkg-deb', '-x', str(package), str(root)], check=True)
for metadata in root.rglob('*.pc'):
    metadata.write_text(metadata.read_text().replace('prefix=/usr', f'prefix={root}/usr'))
# Development packages can link against runtime libraries already on the host.
for library in (root / 'usr/lib/x86_64-linux-gnu').glob('*.so'):
    if library.is_symlink() and not library.exists():
        installed = Path('/usr/lib/x86_64-linux-gnu') / library.readlink().name
        if installed.exists():
            library.unlink()
            library.symlink_to(installed)
luac = root / 'usr/bin/luac'
if not luac.exists():
    luac.symlink_to('luac5.4')
lua_pc = root / 'usr/lib/x86_64-linux-gnu/pkgconfig/lua.pc'
if not lua_pc.exists():
    lua_pc.symlink_to('lua5.4.pc')
lua_multiarch = root / 'usr/include/lua5.4/lua5.4-deb-multiarch.h'
if not lua_multiarch.exists():
    lua_multiarch.symlink_to('../x86_64-linux-gnu/lua5.4-deb-multiarch.h')
print(f'Qt SDK ready in {root}')
