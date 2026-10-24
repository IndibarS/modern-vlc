#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# See COPYING in the repository root.
"""Test live themes, decorations, resizing and clean close with a window manager.

Run under xvfb-run -a -s '-screen 0 1600x1000x24'. Requires xdotool,
ImageMagick, Pillow, and Openbox (system or build-deps/wm/root/usr/bin).
Uses isolated settings and fixed coordinates for the default 100% UI scale.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(tempfile.mkdtemp(prefix='window-', dir=ROOT / 'build-qt/test-artifacts'))
print('Artifacts:', OUT, flush=True)
env = os.environ.copy()
env.update(QT_QPA_PLATFORM='xcb', XDG_CONFIG_HOME=str(OUT / 'config'),
           XDG_DATA_HOME=str(OUT / 'data'), XDG_CACHE_HOME=str(OUT / 'cache'))
config = OUT / 'config/vlc'
config.mkdir(parents=True)
(config / 'vlcrc').write_text('qt-titlebar=0\n')
(config / 'vlc-qt-interface.conf').write_text('[MainWindow]\ncolor-scheme-index=2\n')
wm_env = env.copy()
wm_root = ROOT / 'build-deps/wm/root/usr'
openbox = shutil.which('openbox') or str(wm_root / 'bin/openbox')
wm_env['LD_LIBRARY_PATH'] = str(wm_root / 'lib/x86_64-linux-gnu')
wm_env['XDG_DATA_DIRS'] = str(wm_root / 'share') + ':/usr/share'


def xd(*args):
    return subprocess.check_output(['xdotool', *map(str, args)], text=True).strip()


def click(x, y):
    xd('mousemove', x, y)
    xd('click', 1)
    time.sleep(.3)


def capture(name):
    path = OUT / (name + '.png')
    subprocess.run(['import', '-window', 'root', str(path)], check=True)
    return Image.open(path).convert('RGB')


def theme(name, expected):
    image = capture(name)
    assert image.getpixel((110, 200)) == (expected,) * 3, (name, image.getpixel((110, 200)))
    # Native preferences and QML must change together.
    if name.endswith('preview'):
        assert image.getpixel((400, 250)) == (expected,) * 3


def scroll_preferences():
    xd('mousemove', 1100, 710)
    xd('click', '--repeat', 4, '--delay', 100, 5)
    time.sleep(.5)


def scheme(*keys):
    click(900, 582)
    xd('key', *keys, 'Return')
    time.sleep(1)


process = wm = None
try:
    with (OUT / 'wm.log').open('w') as log:
        wm = subprocess.Popen([openbox, '--sm-disable'], env=wm_env, stdout=log, stderr=log)
    time.sleep(2)
    with (OUT / 'vlc.log').open('w') as log:
        process = subprocess.Popen([str(ROOT / 'run-vlc'), '--no-qt-privacy-ask',
            '--no-metadata-network-access', '--intf=qt'], env=env, stdout=log, stderr=log)
    time.sleep(8)
    main = xd('search', '--onlyvisible', '--pid', process.pid).split()[0]
    xd('windowmove', main, 100, 100)
    xd('windowsize', main, 1000, 700)
    xd('windowactivate', '--sync', main)
    time.sleep(1)
    theme('initial-night', 27)
    click(170, 770)
    time.sleep(1)
    scroll_preferences()
    scheme('Home', 'Down')
    theme('day-preview', 213)
    click(785, 455)  # Enable native decorations.
    click(1175, 828)  # Save.
    time.sleep(2)
    theme('day-saved', 213)
    print('PASS: live Day preview and Save persist without restarting', flush=True)

    xd('windowmove', main, 100, 100)
    time.sleep(1)
    click(170, 770)
    time.sleep(1)
    scroll_preferences()
    scheme('End')
    theme('night-preview', 27)
    xd('key', 'Escape')  # Cancel the live preview.
    time.sleep(1)
    theme('cancel-restores-day', 213)
    print('PASS: Cancel restores the previous theme', flush=True)

    click(170, 770)
    time.sleep(1)
    click(785, 695)  # Disable native decorations again.
    click(1175, 828)
    time.sleep(2)
    xd('windowmove', main, 100, 100)
    time.sleep(1)
    theme('csd-restored', 213)
    xd('mousemove', 1097, 797)
    xd('mousedown', 1)
    time.sleep(.3)
    xd('mousemove', 1197, 897)
    time.sleep(.5)
    xd('mouseup', 1)
    time.sleep(1)
    geometry = xd('getwindowgeometry', '--shell', main)
    assert 'WIDTH=1100\n' in geometry and 'HEIGHT=800\n' in geometry, geometry
    capture('resized')
    print('PASS: live titlebar toggle and corner drag resize', flush=True)
    click(1180, 120)  # CSD Close, not a termination signal.
    assert process.wait(timeout=15) == 0, process.returncode
    assert 'color-scheme-index=1' in (config / 'vlc-qt-interface.conf').read_text()
    print('PASS: CSD Close exits cleanly and retains the saved theme', flush=True)
finally:
    for child in (process, wm):
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
