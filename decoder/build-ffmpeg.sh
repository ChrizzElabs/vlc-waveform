#!/bin/bash
# Private cross-build on Debian 13 amd64. No apt install, service or global tool changes.
set -euo pipefail
umask 022
export LC_ALL=C
WORK=${1:?Usage: build-ffmpeg.sh private-work-directory source-directory output-directory}
SOURCES=${2:?}
OUTPUT=${3:?}
mkdir -p "$WORK" "$OUTPUT"
WORK=$(cd "$WORK" && pwd)
TC="$WORK/toolchain"
mkdir -p "$WORK/debs" "$TC" "$WORK/gnupg"
chmod 700 "$WORK/gnupg"

packages=(gcc-mingw-w64-x86-64-win32 gcc-mingw-w64-x86-64-win32-runtime
  gcc-mingw-w64-base binutils-mingw-w64-x86-64 binutils-common
  mingw-w64-x86-64-dev mingw-w64-common make nasm gpgv
  libgmp10 libisl23 libmpc3 libmpfr6 libstdc++6 libzstd1 zlib1g
  libbz2-1.0 libgcrypt20 libgpg-error0)
packages+=(gcc-14-x86-64-linux-gnu cpp-14-x86-64-linux-gnu libgcc-14-dev
  libc6-dev linux-libc-dev binutils-x86-64-linux-gnu libbinutils libctf0 libctf-nobfd0
  libsframe1 libjansson4 libc6)
cd "$WORK/debs"
if [ ! -f "$WORK/toolchain-ready-v4" ]; then
  # apt-get download uses existing Debian signed package metadata. It does not
  # install packages or change the package database in the existing WSL distro.
  apt-get download "${packages[@]}"
  python3 - "$OUTPUT/toolchain-packages.json" <<'PY'
import hashlib, json, pathlib, subprocess, sys
records=[]
for p in sorted(pathlib.Path('.').glob('*.deb')):
    name=subprocess.check_output(['dpkg-deb','-f',str(p),'Package'],text=True).strip()
    version=subprocess.check_output(['dpkg-deb','-f',str(p),'Version'],text=True).strip()
    records.append({'package':name,'version':version,'archive':p.name,'bytes':p.stat().st_size,
                    'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
print('Downloaded toolchain bytes:',sum(x['bytes'] for x in records),flush=True)
pathlib.Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
PY
  for archive in ./*.deb; do dpkg-deb -x "$archive" "$TC"; done
  touch "$WORK/toolchain-ready-v4"
fi
if [ ! -e "$TC/lib" ]; then ln -s usr/lib "$TC/lib"; fi
if [ ! -e "$TC/lib64" ]; then ln -s usr/lib64 "$TC/lib64"; fi
export PATH="$TC/usr/bin:$PATH"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GNUPGHOME="$WORK/gnupg"
gpgv --status-fd 1 --keyring "$SOURCES/ffmpeg-release-key.gpg" \
  "$SOURCES/ffmpeg-9.0.2.tar.xz.asc" "$SOURCES/ffmpeg-9.0.2.tar.xz" \
  > "$OUTPUT/signature-verification.txt" 2>&1
grep -q 'VALIDSIG FCF986EA15E6E293A5644F10B4322F04D67658D8' "$OUTPUT/signature-verification.txt"

if [ ! -d "$WORK/ffmpeg-9.0.2" ]; then
  python3 - "$SOURCES/ffmpeg-9.0.2.tar.xz" "$WORK" <<'PY'
import sys,tarfile
with tarfile.open(sys.argv[1]) as archive:
    archive.extractall(sys.argv[2],filter='data')
PY
fi
python3 - "$SOURCES/ffmpeg-9.0.2.tar.xz" "$WORK" "$OUTPUT/source-tree-verification.txt" <<'PY'
import hashlib,pathlib,sys,tarfile
count=0
with tarfile.open(sys.argv[1]) as archive:
    for member in archive:
        if member.isfile():
            path=pathlib.Path(sys.argv[2])/member.name
            assert path.is_file(),str(path)
            assert hashlib.sha256(path.read_bytes()).digest()==hashlib.sha256(archive.extractfile(member).read()).digest(),str(path)
            count+=1
pathlib.Path(sys.argv[3]).write_text(f'PASS: {count} upstream source files match the signature-verified archive byte for byte.\n')
PY
mkdir -p "$WORK/build"
cd "$WORK/build"
CC="$TC/usr/bin/x86_64-w64-mingw32-gcc-win32"
ln -sf x86_64-w64-mingw32-gcc-win32 "$TC/usr/bin/x86_64-w64-mingw32-gcc"
ln -sf x86_64-linux-gnu-gcc-14 "$TC/usr/bin/gcc"
ln -sf x86_64-linux-gnu-as "$TC/usr/bin/as"
ln -sf x86_64-linux-gnu-ld "$TC/usr/bin/ld"
"$CC" --version > "$OUTPUT/compiler-version.txt"
# Standalone FFmpeg CLI, statically using only FFmpeg's own audio libraries and
# the ordinary MinGW/Windows runtime. No external codecs, GPL or nonfree parts.
"$WORK/ffmpeg-9.0.2/configure" \
  --target-os=mingw32 --arch=x86_64 --enable-cross-compile \
  --cross-prefix="$TC/usr/bin/x86_64-w64-mingw32-" --cc="$CC" \
  --host-cc="$TC/usr/bin/gcc" --host-cflags="--sysroot=$TC" \
  --host-ldflags="--sysroot=$TC -B$TC/usr/lib/x86_64-linux-gnu/ -static-libgcc" \
  --windres="$TC/usr/bin/x86_64-w64-mingw32-windres --preprocessor=$CC --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED" \
  --disable-autodetect --disable-network \
  --disable-everything --disable-doc --disable-debug --disable-shared \
  --enable-static --enable-ffmpeg --disable-ffprobe --disable-ffplay \
  --disable-avdevice --disable-swscale --enable-w32threads \
  --enable-protocol=file,pipe --enable-muxer=pcm_s16le \
  --enable-demuxer=wav,flac,mp3,mov,aiff,ogg,aac,ac3,eac3,matroska \
  --enable-decoder='pcm_*,flac,mp3,mp3float,aac,aac_fixed,alac,vorbis,opus,ac3,eac3' \
  --enable-parser=aac,aac_latm,ac3,flac,mpegaudio,opus,vorbis \
  --enable-encoder=pcm_s16le --enable-filter=aresample,aformat,anull \
  --extra-cflags="-I$TC/usr/x86_64-w64-mingw32/include" \
  --extra-ldflags="-L$TC/usr/x86_64-w64-mingw32/lib -static-libgcc" \
  --prefix=/vlc-waveform-audio > "$OUTPUT/configure-output.txt" 2>&1
# Always rebuild the small host generator; a previous cross-configure must not
# leave a Windows object linked into the Linux build tool.
rm -f ffbuild/bin2c_host.o ffbuild/bin2c
make -j2 ffmpeg.exe > "$OUTPUT/make-output.txt" 2>&1
cp ffmpeg.exe "$OUTPUT/ffmpeg.exe"
# Include CLI objects and generated files so recipients can also relink the
# statically used LGPL libraries without rebuilding the command-line frontend.
tar -czf "$OUTPUT/relink-objects.tar.gz" fftools \
  config.h config_components.h libavutil/avconfig.h libavutil/ffversion.h ffbuild/config.mak
cp config.h config_components.h config.mak "$OUTPUT/" 2>/dev/null || \
  cp config.h config_components.h ffbuild/config.mak "$OUTPUT/"
cp "$WORK/ffmpeg-9.0.2"/COPYING.LGPLv2.1 "$WORK/ffmpeg-9.0.2"/LICENSE.md "$OUTPUT/"
"$TC/usr/bin/x86_64-w64-mingw32-objdump" -p ffmpeg.exe > "$OUTPUT/pe-dependencies.txt"
cp "$TC/usr/share/doc/gcc-mingw-w64-x86-64-win32-runtime/copyright" "$OUTPUT/GCC-runtime-copyright.txt"
echo "Built $OUTPUT/ffmpeg.exe"
