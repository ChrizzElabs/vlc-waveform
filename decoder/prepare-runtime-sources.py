"""Collect exact runtime/SDK source archives and notices for redistribution.

No compiler executable or installed third-party program is redistributed.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ['LOCALAPPDATA']) / 'Temp/opencode/vlc-native-tools'
DEST = ROOT / 'third_party/runtime-sources'
DEST.mkdir(parents=True, exist_ok=True)
base = 'https://deb.debian.org/debian/pool/main/m/mingw-w64/'
files = {
    'mingw-w64_12.0.0.orig.tar.bz2': 'cc41898aac4b6e8dd5cffd7331b9d9515b912df4420a3a612b5ea2955bbeed2f',
    'mingw-w64_12.0.0-5.debian.tar.xz': '27513ca9c5730bc58eadda089022979b40fa1a5c05893ca3e339345d8c832dba',
}
records = []
for name, expected in files.items():
    path = DEST / name
    if not path.exists():
        urllib.request.urlretrieve(base + name, path)
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    assert actual == expected, name
    records.append({'file': name, 'url': base + name, 'sha256': actual})
name = 'mingw-w64_12.0.0-5.dsc'
if not (DEST / name).exists():
    urllib.request.urlretrieve(base + name, DEST / name)

# The existing plugin uses the pinned official VLC headers and Zig 0.13 runtime.
# Supply the exact VLC source archive, full public headers/import definition and
# the runtime source files used by this compiler, not private logs/test profiles.
vlc = TOOLS / 'vlc-3.0.24.tar.xz'
expected = urllib.request.urlopen('https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz.sha256').read().decode().split()[0]
assert hashlib.sha256(vlc.read_bytes()).hexdigest() == expected
shutil.copy2(vlc, DEST / vlc.name)
records.append({'file': vlc.name, 'url': 'https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz', 'sha256': expected})

zig = TOOLS / 'zig.zip'
index = json.load(urllib.request.urlopen('https://ziglang.org/download/index.json'))
expected = index['0.13.0']['x86_64-windows']['shasum']
assert hashlib.sha256(zig.read_bytes()).hexdigest() == expected
prefix = 'zig-windows-x86_64-0.13.0/'
with zipfile.ZipFile(zig) as source, zipfile.ZipFile(DEST / 'zig-0.13-runtime-sources.zip', 'w', zipfile.ZIP_DEFLATED) as output:
    for name in source.namelist():
        rel = name.removeprefix(prefix)
        if not name.endswith('/') and (rel.startswith(('lib/libc/mingw/', 'lib/compiler_rt/', 'lib/libc/include/', 'lib/std/')) or
                                       rel in ('LICENSE', 'lib/compiler_rt.zig', 'lib/zig.h')):
            output.writestr(rel, source.read(name))
records.append({'file': 'zig-0.13-runtime-sources.zip', 'upstream_archive_sha256': expected,
                'upstream_url': 'https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip',
                'sha256': hashlib.sha256((DEST / 'zig-0.13-runtime-sources.zip').read_bytes()).hexdigest()})

notices = DEST / 'notices'
notices.mkdir(exist_ok=True)
with tarfile.open(DEST / 'mingw-w64_12.0.0.orig.tar.bz2') as source:
    for member in source:
        if member.isfile() and member.name.endswith(('/COPYING', '/COPYING.MinGW-w64', '/COPYING.MinGW-w64-runtime')):
            name = member.name.replace('/', '_') + '.txt'
            (notices / name).write_bytes(source.extractfile(member).read())
shutil.copy2(ROOT / 'native/decoder/GCC-Runtime-Library-Exception-3.1.txt', notices)
shutil.copy2(ROOT / 'native/FFMPEG-GPLv3.txt', notices / 'GPL-3.0.txt')
(DEST / 'upstream.json').write_text(json.dumps({'files': records}, indent=2) + '\n')
print(json.dumps(records, indent=2), flush=True)
