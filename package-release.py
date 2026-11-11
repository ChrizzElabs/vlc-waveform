"""Allowlisted, source-complete offline release. Does not publish or install it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import uuid
import zipfile

ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'native'
THIRD = ROOT / 'third_party'
TOOLS = Path(os.environ['LOCALAPPDATA']) / 'Temp/opencode/vlc-native-tools'

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)

def zip_tree(directory, target, prefix=''):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                z.write(path, prefix + path.relative_to(directory).as_posix())

ap = argparse.ArgumentParser()
ap.add_argument('--version', default='1.0.4')
args = ap.parse_args()
assert re.fullmatch(r'\d+\.\d+\.\d+', args.version)
version = args.version
build = THIRD / 'ffmpeg/build'
decoder = build / 'ffmpeg.exe'
assert decoder.is_file(), 'Build the private FFmpeg decoder first'
configuration = subprocess.check_output([str(decoder), '-version'], stderr=subprocess.STDOUT, text=True)
assert f'ffmpeg version {json.loads((THIRD / "ffmpeg/upstream.json").read_text())["version"]}' in configuration
assert '--disable-autodetect' in configuration and '--disable-network' in configuration
assert '--enable-gpl' not in configuration and '--enable-nonfree' not in configuration
assert '--enable-version3' not in configuration
runtime_manifest = json.loads((THIRD / 'runtime-sources/upstream.json').read_text())
for item in runtime_manifest['files']:
    assert digest(THIRD / 'runtime-sources' / item['file']) == item['sha256']
packages = json.loads((build / 'toolchain-packages.json').read_text())
for name in ('mingw-w64-common', 'mingw-w64-x86-64-dev'):
    assert next(p['version'] for p in packages if p['package'] == name) == '12.0.0-5'
signature = (build / 'signature-verification.txt').read_text()
assert 'VALIDSIG FCF986EA15E6E293A5644F10B4322F04D67658D8' in signature
assert 'License: LGPL version 2.1 or later' in (build / 'configure-output.txt').read_text()
source_manifest = json.loads((THIRD / 'ffmpeg/upstream.json').read_text())
for item in source_manifest['files']:
    assert digest(THIRD / 'ffmpeg' / item['file']) == item['sha256']

stage = ROOT / 'dist' / ('.release-' + version + '-' + uuid.uuid4().hex)
stage.mkdir(parents=True)
for path in (NATIVE / 'package').iterdir():
    if path.is_file():
        copy(path, stage / path.name)
copy(NATIVE / 'COPYING.txt', stage / 'COPYING.txt')
copy(TOOLS / 'inno-7.1.1/License.txt', stage / 'decoder/notices/Inno-Setup-License.txt')
copy(NATIVE / 'bin-release/libwaveform_plugin.dll', stage / 'libwaveform_plugin.dll')
copy(NATIVE / 'installer/waveform.ico', stage / 'waveform.ico')
assert b'C:\\Users\\' not in (stage / 'libwaveform_plugin.dll').read_bytes()
copy(decoder, stage / 'decoder/ffmpeg.exe')
# Public text must not expose builder-specific paths. The exact generated
# config remains in source/ffmpeg/build-record for reproduction.
public_config = re.sub(r'/root/\.cache/vlc-waveform-ffmpeg-9\.0\.2', '${PRIVATE_BUILD}', configuration)
(stage / 'decoder/ffmpeg-version.txt').write_text(public_config)
for name in ('COPYING.LGPLv2.1', 'LICENSE.md'):
    copy(build / name, stage / 'decoder' / (name + '.txt'))
copy(build / 'GCC-runtime-copyright.txt', stage / 'decoder/notices/GCC-runtime-copyright.txt')
with tarfile.open(THIRD / 'ffmpeg/ffmpeg-9.0.2.tar.xz') as upstream:
    (stage / 'decoder/COPYING.GPLv2.txt').write_bytes(upstream.extractfile('ffmpeg-9.0.2/COPYING.GPLv2').read())
for path in (THIRD / 'runtime-sources/notices').glob('*.txt'):
    copy(path, stage / 'decoder/notices' / path.name)

# Exact source archives and generated build configuration, no local profiles,
# test media, tokens, process logs, backups or old browser implementation.
for name in ('ffmpeg-9.0.2.tar.xz', 'ffmpeg-9.0.2.tar.xz.asc', 'ffmpeg-devel.asc', 'ffmpeg-release-key.gpg', 'upstream.json'):
    copy(THIRD / 'ffmpeg' / name, stage / 'source/ffmpeg' / name)
for name in ('signature-verification.txt', 'compiler-version.txt', 'toolchain-packages.json',
             'config.h', 'config_components.h', 'config.mak', 'configure-output.txt'):
    copy(build / name, stage / 'source/ffmpeg/build-record' / name)
copy(build / 'source-tree-verification.txt', stage / 'source/ffmpeg/build-record/source-tree-verification.txt')
copy(build / 'relink-objects.tar.gz', stage / 'source/ffmpeg/relink-objects.tar.gz')
for path in (THIRD / 'runtime-sources').iterdir():
    if path.is_file():
        copy(path, stage / 'source/runtime' / path.name)
for name in ('waveform.c', 'build.ps1', 'prepare-tools.ps1', 'prepare-sdk.py', 'README.md', 'COPYING.txt'):
    copy(NATIVE / name, stage / 'source/plugin' / name)
sdk = TOOLS / 'vlc-3.0.24/sdk'
shutil.copytree(sdk / 'include', stage / 'source/plugin/sdk/include')
copy(sdk / 'lib/vlccore.def', stage / 'source/plugin/sdk/lib/vlccore.def')
for folder in ('decoder', 'installer', 'package'):
    for path in (NATIVE / folder).iterdir():
        if path.is_file() and path.suffix in ('.py', '.ps1', '.sh', '.iss', '.cmd', '.txt', '.md', '.ico'):
            copy(path, stage / 'source' / folder / path.name)
for path in (NATIVE / 'release').iterdir():
    if path.is_file():
        copy(path, stage / 'source/release' / path.name)
for name in ('build-package.ps1', 'package-release.py'):
    copy(NATIVE / name, stage / 'source' / name)
copy(NATIVE / 'release/QUELLEN.txt', stage / 'source/QUELLEN.txt')
copy(NATIVE / 'release/WEITERGABE.txt', stage / 'WEITERGABE.txt')
copy(NATIVE / 'release/AENDERUNGEN.txt', stage / 'AENDERUNGEN.txt')

record = {'name': 'VLC Waveform', 'version': version, 'source_complete_release': True,
          'decoder_version': '9.0.2', 'decoder_license': 'LGPL-2.1-or-later',
          'decoder_sha256': digest(decoder), 'plugin_sha256': digest(stage / 'libwaveform_plugin.dll'),
          'ffmpeg_source_sha256': digest(THIRD / 'ffmpeg/ffmpeg-9.0.2.tar.xz'),
          'upstream_signature_fingerprint': source_manifest['signer_fingerprint'],
          'external_codec_libraries': [], 'public_upload_performed': False}
(stage / 'release-manifest.json').write_text(json.dumps(record, indent=2) + '\n')
manifest = [{'file': path.relative_to(stage).as_posix(), 'sha256': digest(path)}
            for path in sorted(stage.rglob('*')) if path.is_file()]
(stage / 'SHA256.json').write_text(json.dumps(manifest, indent=2) + '\n')
out = ROOT / 'dist' / f'VLC-Waveform-{version}-x64'
if out.exists():
    old = json.loads((out / 'release-manifest.json').read_text())
    assert old['name'] == 'VLC Waveform' and old['version'] == version
    # Only remove our own fully generated staging output, never an unknown folder.
    shutil.rmtree(out)
stage.rename(out)
zip_tree(out, ROOT / 'dist' / f'VLC-Waveform-{version}-x64.zip', out.name + '/')
zip_tree(out / 'source', ROOT / 'dist' / f'VLC-Waveform-Quellen-{version}.zip', 'source/')
print(json.dumps(record, indent=2), flush=True)
print('Payload files:', len(manifest), 'Source archive included inside installer payload:', out / 'source/ffmpeg/ffmpeg-9.0.2.tar.xz')
