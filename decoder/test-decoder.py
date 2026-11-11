"""Headless decoder + actual VLC plugin integration, without Qt or screenshots."""
import base64
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
DECODER = ROOT / 'third_party/ffmpeg/build/ffmpeg.exe'
VLC = Path(os.environ['ProgramFiles']) / 'VideoLAN/VLC/vlc.exe'
DLL = ROOT / 'native/bin-release/libwaveform_plugin.dll'
ap = argparse.ArgumentParser()
ap.add_argument('--version', default='1.0.2')
VERSION = ap.parse_args().version
OUT = ROOT / ('native/test-results/release-' + VERSION)
OUT.mkdir(parents=True, exist_ok=True)
REPORT = {'decoder_sha256': hashlib.sha256(DECODER.read_bytes()).hexdigest(), 'tests': []}
REPORT['plugin_sha256'] = hashlib.sha256(DLL.read_bytes()).hexdigest()
cache = Path(os.environ['LOCALAPPDATA']) / 'Temp/opencode'
sandbox = Path(tempfile.mkdtemp(prefix='waveform-decoder-test-', dir=cache))
REPORT['sandbox'] = str(sandbox)
real_config = Path(os.environ['APPDATA']) / 'vlc/vlcrc'
before = hashlib.sha256(real_config.read_bytes()).hexdigest()

def api(command=None, **params):
    if command:
        params['command'] = command
    req = urllib.request.Request('http://127.0.0.1:8099/requests/status.json?' + urllib.parse.urlencode(params),
                                 headers={'Authorization': 'Basic ' + base64.b64encode(b':native-test-only').decode()})
    with urllib.request.urlopen(req, timeout=3) as response:
        return json.load(response)

def wait_for(predicate, timeout=25):
    end = time.monotonic() + timeout
    last = None
    while time.monotonic() < end:
        try:
            last = api()
            if predicate(last):
                return last
        except (OSError, ValueError) as error:
            last = repr(error)
        time.sleep(.1)
    raise AssertionError(f'VLC status timeout: {last}')

def cache_file(path):
    st = path.stat(); h = 14695981039346656037
    data = str(path).encode('utf-16le') + struct.pack('<QII', st.st_mtime_ns//100+116444736000000000, st.st_size>>32, st.st_size&0xffffffff)
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    return Path(os.environ['LOCALAPPDATA']) / f'VLCWaveform/cache/{h:016x}.wfp'

try:
    for port in (8099, 8100):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', port))
    config = subprocess.check_output([str(DECODER), '-version'], stderr=subprocess.STDOUT, text=True)
    license_text = subprocess.check_output([str(DECODER), '-L'], stderr=subprocess.STDOUT, text=True)
    assert '9.0.2' in config and '--disable-network' in config
    assert 'Lesser General Public License' in license_text
    assert '--enable-gpl' not in config and '--enable-nonfree' not in config
    REPORT['tests'].append('FFmpeg 9.0.2 LGPL configuration, no GPL/nonfree/external codecs/network')
    references = []
    old = Path(os.environ['ProgramFiles']) / 'PySceneDetect/ffmpeg.exe'
    for extension in ('wav', 'flac', 'mp3', 'm4a'):
        source = ROOT / ('test/clap-test.wav' if extension == 'wav' else 'test/take.' + extension)
        local = sandbox / ('Audio äöü mit Leerzeichen.' + extension)
        shutil.copy2(source, local)
        args = ['-v', 'error', '-nostdin', '-i', str(local), '-map', '0:a:0', '-vn',
                '-ar', '48000', '-ac', '2', '-f', 's16le', '-acodec', 'pcm_s16le', '-']
        result = subprocess.run([str(DECODER)] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert result.returncode == 0, result.stderr.decode(errors='replace')
        assert len(result.stdout) > 100000 and len(result.stdout) % 4 == 0
        samples = struct.unpack('<' + 'h' * (len(result.stdout)//2), result.stdout)
        assert max(abs(x) for x in samples) > 1000
        reference = subprocess.run([str(old)] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert reference.returncode == 0
        assert abs(len(result.stdout) - len(reference.stdout)) <= 4096
        n = min(len(result.stdout), len(reference.stdout))//2
        other = struct.unpack('<' + 'h' * n, reference.stdout[:n*2])
        rms = math.sqrt(sum((samples[i]-other[i])**2 for i in range(n))/n)
        assert rms < 100, (extension, rms)
        references.append(local)
        REPORT['tests'].append(f'{extension.upper()} Unicode/space path, expected PCM output, reference RMS error {rms:.4f}')

    # Use installed VLC binaries with isolated profile/env and no graphical interface.
    plugins = sandbox / 'plugins'
    plugins.mkdir()
    shutil.copy2(DLL, plugins / DLL.name)
    env = os.environ.copy()
    env['VLC_PLUGIN_PATH'] = str(plugins)
    env['LOCALAPPDATA'] = str(sandbox / 'Local')
    env['APPDATA'] = str(sandbox / 'Roaming')
    env['VLC_WAVEFORM_TEST'] = 'headless-decoder-integration'
    log = sandbox / 'vlc.log'
    args = [str(VLC), '--no-crashdump', '--ignore-config', '--no-one-instance', '--no-media-library',
            '--config=' + str(sandbox/'vlcrc'), '--intf=dummy', '--loop', '--extraintf=waveformintf:http:oldrc',
            '--rc-host=127.0.0.1:8100', '--rc-quiet',
            '--audio-visual=waveformvis', '--aout=dummy', '--vout=dummy', '--no-plugins-cache',
            '--waveform-ffmpeg=' + str(DECODER), '--http-host=127.0.0.1', '--http-port=8099',
            '--http-password=native-test-only', '--file-logging', '--logfile=' + str(log), '--verbose=2', str(references[0])]
    stream = (sandbox / 'process.log').open('wb')
    proc = subprocess.Popen(args, env=env, cwd=VLC.parent, stdin=subprocess.PIPE, stdout=stream, stderr=stream,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        wait_for(lambda s: s.get('state') == 'playing')
        for i, source in enumerate(references):
            if i:
                api('in_play', input=source.as_uri())
            wait_for(lambda s: s.get('state') == 'playing' and s.get('length', 0) > 0)
            end = time.monotonic() + 15
            while time.monotonic() < end:
                if cache_file(source).is_file():
                    break
                time.sleep(.1)
            assert cache_file(source).is_file(), 'No complete waveform cache: ' + source.suffix
            api('pl_forcepause')
            wait_for(lambda s: s.get('state') == 'paused')
            api('seek', val='60%')
            wait_for(lambda s: s.get('state') == 'paused' and abs(s.get('position', s.get('pos', 0))-.6) < .03)
            api('pl_forceresume')
            wait_for(lambda s: s.get('state') == 'playing')
            REPORT['tests'].append(source.suffix + ': actual native VLC plugin analysis succeeds; pause/seek/resume works headlessly')
        REPORT['tests'].append('No Qt/browser/overlay/screenshot used; real VLC visualization with dummy video output')
    finally:
        if proc.poll() is None:
            try:
                # Explicit native RC quit avoids EOF/loop races in short test
                # media and requests ordinary libvlc teardown, not termination.
                with socket.create_connection(('127.0.0.1', 8100), timeout=3) as control:
                    control.sendall(b'quit\n')
                proc.wait(timeout=20)
            except (OSError, subprocess.TimeoutExpired):
                proc.terminate()
                proc.wait(timeout=5)
        stream.close()
        REPORT['vlc_exit_code'] = proc.returncode
        for path in (log, sandbox/'process.log'):
            if path.exists():
                shutil.copy2(path, OUT / path.name)
        assert proc.returncode == 0, proc.returncode
    assert 'Native waveform video output ready' in log.read_text(errors='replace')
    assert hashlib.sha256(real_config.read_bytes()).hexdigest() == before
    REPORT['tests'].append('Real VLC configuration unchanged; normal VLC teardown exit 0')
    REPORT['ok'] = True
finally:
    (OUT / 'decoder-test.json').write_text(json.dumps(REPORT, indent=2), encoding='utf-8')
    print(json.dumps(REPORT, indent=2), flush=True)
