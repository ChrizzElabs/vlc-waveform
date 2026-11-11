"""Build SDK headers/import definitions from the matching official VLC source/runtime."""
import hashlib
import os
from pathlib import Path
import struct
import tarfile
import urllib.request

tools = Path(os.environ['LOCALAPPDATA']) / 'Temp/opencode/vlc-native-tools'
runtime = tools / 'vlc-3.0.24'
sdk = runtime / 'sdk'
archive = tools / 'vlc-3.0.24.tar.xz'
url = 'https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz'
expected = urllib.request.urlopen(url + '.sha256').read().decode().split()[0]
if not archive.exists() or hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() != expected:
    partial = archive.with_suffix('.download')
    urllib.request.urlretrieve(url, partial)
    assert hashlib.file_digest(partial.open('rb'), 'sha256').hexdigest() == expected
    partial.replace(archive)
assert hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() == expected
with tarfile.open(archive) as tar:
    for member in tar:
        prefix = 'vlc-3.0.24/include/'
        if member.name.startswith(prefix) and member.isfile():
            relative = Path(member.name[len(prefix):])
            assert '..' not in relative.parts
            dest = sdk / 'include/vlc/plugins' / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(tar.extractfile(member).read())

# GNU ld/lld accepts a DLL export definition as an import library input.
b = (runtime / 'libvlccore.dll').read_bytes()
u16 = lambda p: struct.unpack_from('<H', b, p)[0]
u32 = lambda p: struct.unpack_from('<I', b, p)[0]
pe = u32(0x3c)
opt = pe + 24
sections = opt + u16(pe + 20)
def offset(rva):
    for i in range(u16(pe + 6)):
        s = sections + i * 40
        va, size, raw = u32(s + 12), u32(s + 16), u32(s + 20)
        if va <= rva < va + max(size, u32(s + 8)):
            return raw + rva - va
    raise ValueError(rva)
exp = offset(u32(opt + 112))
names = offset(u32(exp + 32))
exports = []
for i in range(u32(exp + 24)):
    start = offset(u32(names + 4 * i))
    exports.append(b[start:b.index(b'\0', start)].decode())
(sdk / 'lib').mkdir(exist_ok=True)
(sdk / 'lib/vlccore.def').write_text('LIBRARY libvlccore.dll\nEXPORTS\n' + '\n'.join(exports) + '\n')
print(f'SDK headers + {len(exports)} runtime exports prepared: {sdk}', flush=True)
