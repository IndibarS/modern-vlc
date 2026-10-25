#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# See COPYING in the repository root.
"""Exercise the built VLC Qt UI on a private X display, never user settings.

Run: xvfb-run -a -s '-screen 0 1920x1080x24' python3 extras/qt-ui/smoke-test.py
Requires ffmpeg, xdotool, ImageMagick, Python Pillow and the local Qt build.
Screenshots and logs remain in build-qt/test-artifacts/smoke-* for inspection.
This is an interaction smoke test, not a claim of complete visual equivalence.
"""
import argparse
import ctypes
from contextlib import closing
import functools
import http.server
import json
import os
from pathlib import Path
import re
import sqlite3
import shutil
import subprocess
import tempfile
import threading
import time

from PIL import Image, ImageChops, ImageStat

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--theme', choices=('day', 'night'), default='night')
parser.add_argument('--csd', action='store_true', help='Exercise VLC client-side window decorations')
parser.add_argument('--window-manager', action='store_true', help='Start isolated Openbox; use with --csd on a 1280x800 test display')
parser.add_argument('--floating', action='store_true', help='Exercise the auto-hiding player controls')
parser.add_argument('--quit-during-network-open', action='store_true',
                    help='Stress resolver cancellation by quitting as the saved stream opens')
ARGS = parser.parse_args()
if ARGS.window_manager and not ARGS.csd:
    parser.error('--window-manager requires --csd for fixed-coordinate tests')
ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('VLC_BUILD_DIR', ROOT / 'build-qt'))
BASE = BUILD / 'test-artifacts'
BASE.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix='smoke-', dir=BASE))
RESULTS = []


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True)


def wait_for(condition, description, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = condition()
        if value:
            return value
        time.sleep(.2)
    raise AssertionError(description)


def record(description):
    RESULTS.append(description)
    print('PASS:', description, flush=True)


def windows(*criteria):
    result = subprocess.run(['xdotool', 'search', '--onlyvisible', *map(str, criteria)],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return result.stdout.split() if result.returncode == 0 else []


def press_f11():
    # Send the physical key without xdotool choosing an Alt-modified XKB level.
    x11 = ctypes.CDLL('libX11.so.6')
    xtst = ctypes.CDLL('libXtst.so.6')
    x11.XOpenDisplay.restype = ctypes.c_void_p
    display = x11.XOpenDisplay(None)
    assert display
    x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x11.XKeysymToKeycode.restype = ctypes.c_ubyte
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    code = x11.XKeysymToKeycode(display, 0xffc8)
    xtst.XTestFakeKeyEvent(display, code, 1, 0)
    xtst.XTestFakeKeyEvent(display, code, 0, 0)
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay(display)


def click(x, y):
    run('xdotool', 'mousemove', x, y)
    time.sleep(.5)  # Allow the auto-hidden controls to finish appearing.
    run('xdotool', 'click', 1)
    time.sleep(.4)


def capture(window, name):
    path = OUT / (name + '.png')
    run('import', '-window', window, path)
    return Image.open(path).convert('RGB')


def colored(image, box, predicate):
    crop = image.crop(box)
    pixels = crop.get_flattened_data() if hasattr(crop, 'get_flattened_data') else crop.getdata()
    return sum(predicate(*pixel) for pixel in pixels)


def network_dialog():
    click(65, 18)
    run('xdotool', 'key', '--clearmodifiers', 'm')
    time.sleep(.3)
    run('xdotool', 'key', '--clearmodifiers', 'n')
    dialog = wait_for(lambda: windows('--name', '^Open Network Stream$'), 'network dialog did not open')[-1]
    run('xdotool', 'windowmove', dialog, 0, 0)
    time.sleep(.4)
    return dialog


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError):
            pass  # VLC cancels HTTP requests when seeking or closing.

    def send_head(self):
        # VLC's HTTP demuxers seek. SimpleHTTPRequestHandler ignores Range,
        # which makes it unsuitable as a media-server fixture on its own.
        self.remaining = None
        requested = self.headers.get('Range')
        if not requested:
            return super().send_head()
        match = re.fullmatch(r'bytes=(\d+)-(\d*)', requested)
        if not match:
            self.send_error(416)
            return None
        stream = open(self.translate_path(self.path), 'rb')
        size = os.fstat(stream.fileno()).st_size
        start = int(match[1])
        end = min(int(match[2]) if match[2] else size - 1, size - 1)
        if start > end:
            stream.close()
            self.send_error(416)
            return None
        self.remaining = end - start + 1
        self.send_response(206)
        self.send_header('Content-Type', 'video/mp4')
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.send_header('Content-Length', str(self.remaining))
        self.end_headers()
        stream.seek(start)
        return stream

    def copyfile(self, source, outputfile):
        if self.remaining is None:
            return super().copyfile(source, outputfile)
        while self.remaining:
            data = source.read(min(self.remaining, 65536))
            if not data:
                break
            outputfile.write(data)
            self.remaining -= len(data)


# Chapters exercise the seekbar's model-based markers as well as ordinary seeking.
metadata = OUT / 'chapters.txt'
metadata.write_text(';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=45000\ntitle=First\n'
                    '[CHAPTER]\nTIMEBASE=1/1000\nSTART=45000\nEND=90000\ntitle=Second\n')
media = OUT / 'sample.mp4'
subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                'testsrc2=size=640x360:rate=12', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100',
                '-i', str(metadata), '-map', '0:v', '-map', '1:a', '-map_metadata', '2',
                '-map_chapters', '2', '-t', '90', '-c:v', 'mpeg4', '-q:v', '8', '-c:a', 'aac',
                '-movflags', '+faststart', str(media)], check=True)
server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(QuietHandler, directory=str(OUT)))
threading.Thread(target=server.serve_forever, daemon=True).start()
urls = [f'http://127.0.0.1:{server.server_port}/sample.mp4?entry={i}' for i in (1, 2)]
env = os.environ.copy()
env.update(QT_QPA_PLATFORM='xcb', VLC_BUILD_DIR=str(BUILD),
           XDG_CONFIG_HOME=str(OUT / 'config'), XDG_CACHE_HOME=str(OUT / 'cache'),
           XDG_DATA_HOME=str(OUT / 'data'))
conf = OUT / 'config/vlc/vlc-qt-interface.conf'
conf.parent.mkdir(parents=True)
conf.write_text('[MainWindow]\ncolor-scheme-index=' + ('1' if ARGS.theme == 'day' else '2') + '\n')
log_path = OUT / 'playback.log'
process = wm = None
try:
    if ARGS.window_manager:
        wm_root = ROOT / 'build-deps/wm/root/usr'
        wm_env = env.copy()
        wm_env['LD_LIBRARY_PATH'] = str(wm_root / 'lib/x86_64-linux-gnu')
        wm_env['XDG_DATA_DIRS'] = str(wm_root / 'share') + ':/usr/share'
        with (OUT / 'wm.log').open('w') as log:
            wm = subprocess.Popen([shutil.which('openbox') or str(wm_root / 'bin/openbox'),
                                   '--sm-disable'], env=wm_env, stdout=log, stderr=log)
        wait_for(lambda: '_NET_SUPPORTING_WM_CHECK(WINDOW)' in run('xprop', '-root', '_NET_SUPPORTING_WM_CHECK'),
                 'window manager did not start')
    with log_path.open('w') as log:
        process = subprocess.Popen([str(ROOT / 'run-vlc'), '-vv', '--no-qt-privacy-ask',
            '--no-metadata-network-access', '--no-qt-pin-controls' if ARGS.floating else '--qt-pin-controls', '--no-qt-video-autoresize',
            *(['--no-qt-titlebar'] if ARGS.csd else []), '--intf=qt', '--clock-master=monotonic', '--aout=alsa', '--alsa-audio-device=null', '--restore-playback-pos=0', str(media)],
            env=env, stdout=log, stderr=log)
        wait_for(lambda: 'VLC_PLAYER_STATE_PLAYING' in log_path.read_text(errors='replace'), 'playback did not start')
        time.sleep(3)
        app_windows = windows('--pid', process.pid)
        main = app_windows[0]
        qml_surface = app_windows[-1]
        run('xdotool', 'windowmove', main, 0, 0)
        run('xdotool', 'windowsize', main, 1280, 800)
        run('xdotool', 'mousemove', 65, 20)
        time.sleep(1)
        image = capture(main, 'player-playing')
        assert colored(image, (100, 100, 1100, 600), lambda r,g,b: max(r,g,b)-min(r,g,b)>100) > 10000, 'no embedded video'
        record('Embedded video renders through VLC')
        assert colored(image, (1010, 758, 1120, 788), lambda r,g,b: g>65 and g>b*2 and r>b*2)>30, 'initial volume is empty despite active audio'
        record('Initial audio output volume reaches the slider before user interaction')
        surface = image.getpixel((400, 20))
        expected = 213 if ARGS.theme == 'day' else 27
        assert all(abs(channel-expected)<3 for channel in surface), surface
        record(f'{ARGS.theme} theme colors reach the player header')
        surface = image.getpixel((10, 773))
        assert all(abs(channel-expected)<3 for channel in surface), surface
        record(f'{ARGS.theme} theme colors reach the player control bar')
        if ARGS.csd:
            if ARGS.floating:
                run('xdotool', 'mousemove', 640, 400)
                time.sleep(5)
                idle = capture(main, 'csd-idle')
                assert idle.getpixel((400, 20)) == (expected,) * 3
                record('Windowed CSD header remains visible when playback controls auto-hide')
            fullscreen_start = len(log_path.read_text(errors='replace'))
            click(1226, 20)
            wait_for(lambda: 'setFullscreen fullscreen' in log_path.read_text(errors='replace')[fullscreen_start:], 'fullscreen button did not reach VLC')
            capture(main, 'player-fullscreen')
            run('xdotool', 'mousemove', 600, 20)
            time.sleep(.5)
            click(1226, 20)
            wait_for(lambda: 'on_player_vout_fullscreen_changed windowed' in log_path.read_text(errors='replace')[fullscreen_start:], 'exit-fullscreen button did not restore the player')
            record('Fullscreen and exit-fullscreen buttons toggle the existing player')

            if ARGS.window_manager:
                click(85, 773)
                def window_fullscreen():
                    return '_NET_WM_STATE_FULLSCREEN' in run('xprop', '-id', main, '_NET_WM_STATE')
                for exit_key in ('Escape', 'f'):
                    run('xdotool', 'windowactivate', '--sync', main, 'mousemove', 600, 400)
                    press_f11()
                    wait_for(window_fullscreen, 'F11 did not enter interface fullscreen')
                    run('xdotool', 'key', '--clearmodifiers', exit_key)
                    wait_for(lambda: not window_fullscreen(), f'{exit_key} did not exit interface fullscreen')
                press_f11()
                wait_for(window_fullscreen, 'F11 did not enter interface fullscreen')
                run('xdotool', 'mousemove', 600, 20)
                time.sleep(.5)
                click(1226, 20)
                wait_for(lambda: not window_fullscreen(), 'button did not exit interface fullscreen')
                record('Escape, configured fullscreen key and button clear interface fullscreen')
                click(85, 773)

        if ARGS.floating:
            # Exercise locks acquired by the volume popup and recreated CSD buttons.
            run('xdotool', 'mousemove', 1060, 765)
            time.sleep(.6)
            run('xdotool', 'mousemove', 640, 400)
            time.sleep(6)
            idle = capture(main, 'controls-idle-after-fullscreen')
            assert colored(idle, (40, 755, 1100, 790), lambda r,g,b: r>150 and 65<g<160 and b<60)<10, 'controls remain locked after leaving volume/fullscreen buttons'
            record('Playing controls hide after volume hover and fullscreen transitions')
            run('xdotool', 'mousemove', 85, 773)
            time.sleep(.5)

        click(85, 773)
        wait_for(lambda: 'VLC_PLAYER_STATE_PAUSED' in log_path.read_text(errors='replace'), 'pause was not delivered')
        record('Play button pauses the VLC player')
        image = capture(main, 'pause-notification')
        assert colored(image, (800, 45, 1270, 160), lambda r,g,b: r>150 and 65<g<160 and b<60)>100, 'themed pause notification missing'
        record('Pause uses the themed Qt notification')
        # Hover just inside the volume control's upper edge, where an
        # overlapping native tooltip window used to steal hover repeatedly.
        run('xdotool', 'mousemove', 1064, 761)
        time.sleep(.5)
        hover_frames = []
        for frame in range(16):
            shot = capture('root', f'volume-hover-{frame:02}')
            hover_frames.append(shot.crop((1020, 714, 1110, 744)))
            time.sleep(.04 + (frame % 3) * .02)
        assert colored(hover_frames[-1], (0, 0, 90, 30),
                       lambda r,g,b: max(r,g,b)-min(r,g,b)<15)>150, 'volume tooltip did not remain visible'
        assert all(ImageChops.difference(hover_frames[0], frame).getbbox() is None
                   for frame in hover_frames[1:]), 'volume tooltip flickers under a stationary pointer'
        record('Volume tooltip stays stable at the upper slider edge')
        run('xdotool', 'mousemove', 650, 773)
        def thumbnail_visible():
            shot = capture('root', 'seek-preview')
            return colored(shot, (535, 592, 755, 710),
                           lambda r,g,b: max(r,g,b)-min(r,g,b)>100)>5000 and colored(
                           shot, (524, 575, 778, 585),
                           lambda r,g,b: r>150 and 65<g<160 and b<60)>100
        wait_for(thumbnail_visible, 'hover thumbnail did not render', timeout=15)
        # Compare the preview against the actual hovered frame, not merely
        # against a non-empty card (a first-frame thumbnail is a regression).
        reference = OUT / 'preview-reference.png'
        run('ffmpeg', '-v', 'error', '-ss', '55.26', '-i', OUT / 'sample.mp4',
            '-frames:v', '1', '-vf', 'scale=240:135', '-y', reference)
        expected = Image.open(reference).convert('RGB')
        def exact_preview_visible():
            shown = capture('root', 'seek-preview').convert('RGB').crop((530, 584, 770, 719))
            return sum(ImageStat.Stat(ImageChops.difference(shown, expected)).mean) / 3 < 35
        wait_for(exact_preview_visible, 'preview did not update to the hovered time', timeout=15)
        record('Seekbar hover renders the requested video time without seeking playback')
        run('xdotool', 'mousemove', 600, 400)
        time.sleep(.2)
        cached_start = time.monotonic()
        run('xdotool', 'mousemove', 650, 773)
        wait_for(exact_preview_visible, 'cached preview did not render promptly', timeout=1.5)
        record(f'Cached hover frame reappears in {time.monotonic() - cached_start:.2f}s')

        click(650, 773)
        run('xdotool', 'mousemove', 600, 400)
        time.sleep(4 if ARGS.floating else 1)
        image = capture(main, 'player-seek')
        assert colored(image, (640, 758, 665, 788), lambda r,g,b: r>150 and 65<g<160 and b<60)>40, 'seek thumb did not move'
        record('Seek updates the VLC position and timeline')
        click(440, 773)
        run('xdotool', 'mousemove', 600, 400, 'windowfocus', main)
        shot = capture(main, 'seek-keyboard-start')
        assert colored(shot, (432, 758, 449, 788), lambda r,g,b: r>150 and 65<g<160 and b<60)>40, 'timeline click missed after the hide timeout'
        states = re.findall(r'on_player_state_changed VLC_PLAYER_STATE_(\w+)', log_path.read_text(errors='replace'))
        assert states[-1] == 'PAUSED', 'timeline click unexpectedly resumed playback'
        record('Paused controls remain clickable after the auto-hide timeout')
        jump_start = len(log_path.read_text(errors='replace'))
        run('xdotool', 'key', 'Right')
        wait_for(lambda: 'jumpSeconds: 10' in log_path.read_text(errors='replace')[jump_start:], 'Right did not seek forward 10 seconds')
        time.sleep(.25)
        capture(main, 'seek-forward-feedback')
        run('xdotool', 'key', 'Left')
        wait_for(lambda: 'jumpSeconds: -10' in log_path.read_text(errors='replace')[jump_start:], 'Left did not seek backward 10 seconds')
        time.sleep(.25)
        capture(main, 'seek-backward-feedback')
        record('Left/Right use ten-second seeks through the player controller')

        # Keep the floating toolbar visible while sampling its thumb.
        run('xdotool', 'mousemove', 180, 773)
        time.sleep(.5)
        def thumb_position(name):
            shot = capture(main, name)
            xs = [x for x in range(258, 895)
                  if colored(shot, (x, 765, x + 1, 780),
                             lambda r,g,b: r>150 and 65<g<160 and b<60) > 8]
            assert xs, 'timeline thumb disappeared'
            fill = (216, 186, 154) if ARGS.theme == 'day' else (55, 31, 6)
            assert all(abs(a-b) < 4 for a,b in zip(shot.getpixel((280, 773)), fill)), 'buffer animation obscures the progress fill'
            return sum(xs) / len(xs)
        for key, sign in [('Right', 1), ('Left', -1)]:
            positions = [thumb_position(f'repeat-{key}-start')]
            for press in range(3):
                run('xdotool', 'key', key)
                for frame in range(3):
                    positions.append(thumb_position(f'repeat-{key}-{press}-{frame}'))
                    time.sleep(.05)
            assert all(sign * (b-a) >= -2 for a,b in zip(positions, positions[1:])), positions
            assert sign * (positions[-1] - positions[0]) > 150, positions
        record('Repeated arrow seeks move the progress thumb consistently in each direction')

        # Volume is immediately left of the four action buttons (at 1280 px).
        click(1068, 773)
        run('xdotool', 'mousemove', 600, 400)
        time.sleep(.5)
        image = capture(main, 'player-volume')
        assert colored(image, (1010, 758, 1120, 788), lambda r,g,b: g>65 and g>b*2 and r>b*2)>30, 'volume fill did not update'
        record('Volume control updates with a real volume-capable silent output')
        volume_start = len(log_path.read_text(errors='replace'))
        run('xdotool', 'mousemove', 1068, 773, 'mousedown', 1,
            'mousemove', 1140, 773, 'mouseup', 1)
        wait_for(lambda: 'on_player_aout_volume_changed: 2.000' in log_path.read_text(errors='replace')[volume_start:], 'slider did not set 200% in VLC')
        time.sleep(.2)
        run('xdotool', 'mousemove', 600, 400)
        image = capture(main, 'volume-maximum')
        assert colored(image, (1090, 760, 1108, 786), lambda r,g,b: r>120 and r>g*1.3 and r>b*2)>60, 'maximum volume does not fill the slider'
        assert colored(image, (800, 45, 1270, 160), lambda r,g,b: r>150 and 65<g<160 and b<60)>100, 'themed volume notification missing'
        record('Slider reaches its 200% end and volume notification uses the theme')
        # Fullscreen can recreate the compositor's child window. Focus the
        # visible video through a real click instead of a cached X window ID.
        volume_start = len(log_path.read_text(errors='replace'))
        run('xdotool', 'key', 'ctrl+Down')
        wait_for(lambda: re.search(r'on_player_aout_volume_changed: 1\.[0-9]+', log_path.read_text(errors='replace')[volume_start:]), 'Down key did not reduce volume')
        run('xdotool', 'key', 'ctrl+Up')
        wait_for(lambda: 'on_player_aout_volume_changed: 2.000' in log_path.read_text(errors='replace')[volume_start:], 'Up key did not restore 200%')
        record('Keyboard volume steps stay synchronized with the 200% slider')
        # The video click used to establish focus can resume playback. Restore
        # the paused fixture before exercising dialogs and opening a new stream.
        states = re.findall(r'on_player_state_changed VLC_PLAYER_STATE_(\w+)', log_path.read_text(errors='replace'))
        if states[-1] == 'PLAYING':
            pause_start = len(log_path.read_text(errors='replace'))
            click(85, 773)
            wait_for(lambda: 'VLC_PLAYER_STATE_PAUSED' in log_path.read_text(errors='replace')[pause_start:], 'could not restore paused fixture')
        click(1068, 773)
        time.sleep(1.3)
        run('xdotool', 'windowfocus', qml_surface)
        click(1131, 773)
        image = capture(main, 'audio-tracks')
        assert colored(image, (20, 470, 350, 700), lambda r,g,b: max(r,g,b)-min(r,g,b)<80)>10000, 'audio popup did not open'
        record('Audio button opens its track-selection popup')
        run('xdotool', 'key', 'Escape')
        time.sleep(.4)
        click(1169, 773)
        image = capture(main, 'subtitle-tracks')
        assert colored(image, (20, 470, 350, 700), lambda r,g,b: max(r,g,b)-min(r,g,b)<80)>10000, 'subtitle popup did not open'
        record('Subtitle button opens its track-selection popup')
        run('xdotool', 'key', 'Escape')
        time.sleep(.4)
        image = capture(main, 'popups-closed')
        assert colored(image, (20, 470, 350, 700), lambda r,g,b: max(r,g,b)-min(r,g,b)>100)>10000, 'Escape did not dismiss the popup'
        record('Escape dismisses the focused track popup')
        # Exercise the stacked timeline fallback and restore the test geometry.
        run('xdotool', 'windowsize', main, 640, 480)
        time.sleep(1)
        capture(main, 'player-narrow')
        run('xdotool', 'windowsize', main, 1280, 800)
        time.sleep(1)
        click(1207, 773)
        prefs = wait_for(lambda: windows('--name', '^(Simple |Advanced |Expert )?Preferences$'), 'preferences did not open')[-1]
        run('xdotool', 'windowmove', prefs, 0, 0)
        time.sleep(.4)
        image = capture(prefs, 'preferences')
        surface = image.getpixel((5, image.height - 5))
        expected = 213 if ARGS.theme == 'day' else 27
        assert all(abs(channel-expected)<5 for channel in surface), surface
        record(f'{ARGS.theme} theme colors reach native preferences')
        run('xdotool', 'windowfocus', prefs, 'key', 'Escape')
        wait_for(lambda: not windows('--name', '^(Simple |Advanced |Expert )?Preferences$'), 'preferences did not close')
        dialog = network_dialog()
        image = capture(dialog, 'network-empty')
        surface = image.getpixel((30, 70))
        expected = 222 if ARGS.theme == 'day' else 25
        assert all(abs(channel-expected)<3 for channel in surface), surface
        record(f'{ARGS.theme} theme colors reach the network dialog')
        click(200, 200)
        run('xdotool', 'type', '--clearmodifiers', 'not a URL')
        click(990, 601)
        assert windows('--name', '^Open Network Stream$'), 'invalid URL was accepted'
        capture(dialog, 'network-invalid')
        record('Invalid network input cannot be submitted')
        run('xdotool', 'key', 'Escape')
        wait_for(lambda: not windows('--name', '^Open Network Stream$'), 'cancel did not close dialog')
        record('Escape cancels the network dialog')
        dialog = network_dialog()
        click(200, 200)
        run('xdotool', 'type', '--clearmodifiers', urls[0])
        run('xdotool', 'key', 'Return')
        run('xdotool', 'type', '--clearmodifiers', urls[1])
        click(200, 490)
        run('xdotool', 'type', '--clearmodifiers', 'UI smoke test')
        capture(dialog, 'network-filled')
        network_start = len(log_path.read_text(errors='replace'))
        run('xdotool', 'mousemove', 990, 601, 'click', '--repeat', 2, '--delay', 20, 1)
        wait_for(lambda: not windows('--name', '^Open Network Stream$'), 'playlist save did not finish')
        if not ARGS.quit_during_network_open:
            wait_for(lambda: 'VLC_PLAYER_STATE_PLAYING' in log_path.read_text(errors='replace')[network_start:],
                     'saved network stream did not start playback', timeout=30)
            record('Saved network stream starts playback through VLC')
        errors = re.findall(r'^.*(?:ReferenceError|TypeError|Binding loop|is not a type|Cannot assign|text is empty).*$',
                            log_path.read_text(errors='replace'), re.MULTILINE)
        assert not errors, '\n'.join(errors)
        record('No QML runtime errors during the exercised flows')
        if ARGS.quit_during_network_open:
            process.terminate()
        else:
            fullscreen_start = len(log_path.read_text(errors='replace'))
            click(1226, 20)
            wait_for(lambda: 'on_player_vout_fullscreen_changed fullscreen' in log_path.read_text(errors='replace')[fullscreen_start:], 'could not enter fullscreen for close test')
            run('xdotool', 'mousemove', 600, 20)
            time.sleep(.5)
            fullscreen = capture(main, 'fullscreen-window-actions')
            click(fullscreen.width - 54, 20)
            wait_for(lambda: 'on_player_vout_fullscreen_changed windowed' in log_path.read_text(errors='replace')[fullscreen_start:], 'Exit fullscreen button failed')
            record('Exit fullscreen control returns to the window')
            click(1226, 20)
            time.sleep(.5)
            run('xdotool', 'mousemove', 600, 20)
            time.sleep(.5)
            fullscreen = capture(main, 'fullscreen-before-close')
            click(fullscreen.width - 18, 20)
        try:
            exit_code = process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            if os.environ.get('VLC_SMOKE_DEBUG'):
                with (OUT / 'shutdown-backtrace.txt').open('w') as trace:
                    subprocess.run(['gdb', '-batch', '-p', str(process.pid), '-ex',
                                    'thread apply all bt 16', '-ex', 'detach'],
                                   stdout=trace, stderr=trace, timeout=20)
            raise
        assert exit_code == 0, f'VLC shutdown failed: {exit_code}'
        record('VLC shuts down cleanly without SIGSEGV')
        # Inspect only after VLC closes its database; external readers can
        # contend with MediaLibrary's exclusive write transactions.
        database = OUT / 'data/vlc/ml/ml.db'
        with closing(sqlite3.connect(f'file:{database}?mode=ro', uri=True)) as db:
            rows = db.execute('SELECT name,count(*),min(position),max(position) FROM Playlist '
                'JOIN PlaylistMediaRelation ON id_playlist=playlist_id GROUP BY id_playlist').fetchall()
            saved_urls = [row[0] for row in db.execute(
                'SELECT File.mrl FROM PlaylistMediaRelation JOIN File USING(media_id) ORDER BY position')]
        assert saved_urls == urls, saved_urls
        assert rows == [('UI smoke test', 2, 0, 1)], rows
        record('Repeated Save persists one playlist with both URLs in order; cancel saved none')
finally:
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    if wm is not None and wm.poll() is None:
        wm.terminate()
        try:
            wm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            wm.kill()
            wm.wait()
    server.shutdown()
    (OUT / 'results.json').write_text(json.dumps({'passed': RESULTS}, indent=2) + '\n')
    print('Artifacts:', OUT, flush=True)
