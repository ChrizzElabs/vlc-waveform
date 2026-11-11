"""Download upstream source, never a third-party all-codecs executable.

Authenticity is verified with the pinned upstream OpenPGP key by build-ffmpeg.sh.
Files stay at the NAS project; an existing different file is not silently replaced.
"""
import base64
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'third_party/ffmpeg'
VERSION = '9.0.2'
FINGERPRINT = 'FCF986EA15E6E293A5644F10B4322F04D67658D8'
DEST.mkdir(parents=True, exist_ok=True)
urls = {
    f'ffmpeg-{VERSION}.tar.xz': f'https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz',
    f'ffmpeg-{VERSION}.tar.xz.asc': f'https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz.asc',
    'ffmpeg-devel.asc': 'https://ffmpeg.org/ffmpeg-devel.asc',
}
for name, url in urls.items():
    path = DEST / name
    if not path.exists():
        request = urllib.request.Request(url, headers={'User-Agent': 'VLC-Waveform-source-preparation'})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read()
        temporary = path.with_suffix(path.suffix + '.partial')
        temporary.write_bytes(data)
        temporary.replace(path)

# Convert ASCII armor to an OpenPGP keyring for gpgv, checking the primary key
# fingerprint rather than trusting whichever key happens to be served today.
armor = (DEST / 'ffmpeg-devel.asc').read_text()
body = armor.split('\n\n', 1)[1].split('-----END PGP PUBLIC KEY BLOCK-----')[0]
key = base64.b64decode(''.join(line for line in body.splitlines() if line and not line.startswith('=')))
assert key[0] & 0x80
if key[0] & 0x40:
    assert key[0] & 0x3f == 6
    size = key[1]
    if size < 192:
        start = 2
    elif size < 224:
        size, start = ((size - 192) << 8) + key[2] + 192, 3
    else:
        assert size == 255
        size, start = int.from_bytes(key[2:6], 'big'), 6
else:
    assert (key[0] >> 2) & 15 == 6
    length_bytes = (1, 2, 4)[key[0] & 3]
    size, start = int.from_bytes(key[1:1 + length_bytes], 'big'), 1 + length_bytes
packet = key[start:start + size]
assert packet[0] == 4
actual = hashlib.sha1(b'\x99' + len(packet).to_bytes(2, 'big') + packet).hexdigest().upper()
assert actual == FINGERPRINT, actual
(DEST / 'ffmpeg-release-key.gpg').write_bytes(key)
manifest = {'version': VERSION, 'signer_fingerprint': FINGERPRINT, 'files': [
    {'file': name, 'url': url, 'bytes': (DEST / name).stat().st_size,
     'sha256': hashlib.sha256((DEST / name).read_bytes()).hexdigest()}
    for name, url in urls.items()
]}
(DEST / 'upstream.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2), flush=True)
