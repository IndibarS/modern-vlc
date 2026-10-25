#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check embedded SRT/ASS rendering and subtitle hotkeys on a private X display.

Run with xvfb-run -a -s '-screen 0 1280x800x24'. Uses isolated VLC settings.
"""
import os
import argparse
from pathlib import Path
import subprocess
import tempfile
import time
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--theme', choices=['day', 'night'], default='day')
ARGS = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(tempfile.mkdtemp(prefix='subtitles-', dir=ROOT / 'build-qt/test-artifacts'))

def run(*args):
    return subprocess.check_output([str(x) for x in args], text=True)

def wait_for(predicate, message, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.2)
    raise AssertionError(message)

(OUT / 'sample.srt').write_text(''.join(f'{i+1}\n00:00:{i:02},000 --> 00:00:{i:02},900\nSRT subtitle rendering test\n\n' for i in range(60)))
(OUT / 'sample.ass').write_text('''[Script Info]
ScriptType: v4.00+
PlayResX: 640
PlayResY: 360
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,DejaVu Sans,30,&H000000FF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,20,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
''' + ''.join(f'Dialogue: 0,0:00:{i:02}.00,0:00:{i:02}.90,Default,,0,0,0,,ASS subtitle rendering test\n' for i in range(60)))
run('ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=c=0x303030:s=640x360:r=10',
    '-i', OUT / 'sample.srt', '-i', OUT / 'sample.ass', '-t', '60', '-map', '0', '-map', '1', '-map', '2',
    '-c:v', 'mpeg4', '-c:s', 'copy', '-disposition:s:0', 'default', OUT / 'sample.mkv')
env = dict(os.environ, QT_QPA_PLATFORM='xcb', XDG_CONFIG_HOME=str(OUT/'config'),
           XDG_DATA_HOME=str(OUT/'data'), XDG_CACHE_HOME=str(OUT/'cache'))
conf = OUT / 'config/vlc/vlc-qt-interface.conf'
conf.parent.mkdir(parents=True)
conf.write_text('[MainWindow]\ncolor-scheme-index=' + ('1' if ARGS.theme == 'day' else '2') + '\n')
log_path = OUT / 'playback.log'
with log_path.open('w') as log:
    process = subprocess.Popen([str(ROOT/'run-vlc'), '-vv', '--no-qt-privacy-ask',
        '--no-metadata-network-access', '--no-qt-titlebar', '--qt-pin-controls',
        '--no-qt-video-autoresize', '--no-video-title-show', '--no-sub-autodetect-file', '--sub-track=0', str(OUT/'sample.mkv')], env=env, stdout=log, stderr=log)
    try:
        wait_for(lambda: 'VLC_PLAYER_STATE_PLAYING' in log_path.read_text(errors='replace'), 'playback did not start')
        main = run('xdotool', 'search', '--onlyvisible', '--pid', process.pid).split()[0]
        run('xdotool', 'windowmove', main, '0', '0', 'windowsize', main, '1280', '800', 'windowfocus', main)
        time.sleep(2)
        def counts(name):
            path = OUT/(name+'.png')
            run('import', '-window', main, path)
            image = Image.open(path).convert('RGB').crop((100, 400, 1180, 745))
            pixels = list(image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata())
            return (sum(min(p)>180 for p in pixels), sum(p[0]>150 and p[1]<70 and p[2]<70 for p in pixels))
        wait_for(lambda: counts('srt')[0]>300, 'SRT text did not render')
        image = Image.open(OUT/'srt.png').convert('RGB').crop((100, 400, 1180, 745))
        pixels = list(image.get_flattened_data() if hasattr(image, 'get_flattened_data') else image.getdata())
        if ARGS.theme == 'day':
            assert sum(max(p)<25 for p in pixels)>1000, 'dark subtitle card missing'
        else:
            assert sum(min(p)>180 for p in pixels)>1000, 'light subtitle card missing'
            assert sum(max(p)<25 for p in pixels)>100, 'dark subtitle text missing'
        print(f'PASS: Embedded SRT renders in the {ARGS.theme} subtitle card', flush=True)
        for index in range(3):
            run('xdotool', 'key', 'v')
            time.sleep(.4)
            frames = []
            for frame in range(6):
                frames.append(counts(f'cycle-{index}-{frame}'))
                time.sleep(.17)
            if index == 0:
                assert any(red > 300 for white, red in frames), 'ASS styled text did not render'
            elif index == 1:
                assert all(white < 30 and red < 30 for white, red in frames), 'Disabling subtitles left text on screen'
            else:
                wait_for(lambda: counts('srt-restored')[0]>300, 'SRT did not render after switching back', timeout=12)
        print('PASS: Subtitle hotkey switches to styled ASS and disables subtitles', flush=True)
        run('xdotool', 'key', 'ctrl+q')
        assert process.wait(timeout=20)==0, 'VLC did not exit cleanly'
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        print('Artifacts:', OUT, flush=True)
