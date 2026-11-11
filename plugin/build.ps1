[CmdletBinding()]
param([switch]$Release)
$ErrorActionPreference = 'Stop'
$tools = Join-Path $env:LOCALAPPDATA 'Temp\opencode\vlc-native-tools'
$zig = Join-Path $tools 'zig-windows-x86_64-0.13.0\zig.exe'
$sdk = Join-Path $tools 'vlc-3.0.24\sdk'
if (-not (Test-Path $zig) -or -not (Test-Path $sdk)) { throw 'Run native\prepare-tools.ps1 first' }
$stage = Join-Path $tools $(if ($Release) { 'release-build' } else { 'build' })
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'waveform.c') $stage -Force
$out = Join-Path $PSScriptRoot $(if ($Release) { 'bin-release' } else { 'bin' })
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $zig dlltool -m i386:x86-64 -d (Join-Path $sdk 'lib\vlccore.def') -l (Join-Path $sdk 'lib\libvlccore.a')
if ($LASTEXITCODE -ne 0) { throw 'Import library generation failed' }
$args = @('cc','-target','x86_64-windows-gnu','-std=gnu11','-O2',
  '-shared','-D__PLUGIN__','-D_FILE_OFFSET_BITS=64',
  '-I', (Join-Path $sdk 'include'), '-I', (Join-Path $sdk 'include\vlc\plugins'),
  (Join-Path $stage 'waveform.c'), '-L', (Join-Path $sdk 'lib'),
  '-lvlccore','-lgdi32','-luser32','-lshell32','-lole32',
  '-o', (Join-Path $stage 'libwaveform_plugin.dll'))
if ($Release) {
  $env:ZIG_LOCAL_CACHE_DIR = Join-Path $stage 'zig-local-cache'
  $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $stage 'zig-global-cache'
  $args += @("-ffile-prefix-map=$tools=/vlc-waveform-build", "-fdebug-prefix-map=$tools=/vlc-waveform-build")
} else { $args += '-g' }
& $zig @args
if ($LASTEXITCODE -ne 0) { throw "Compiler failed: $LASTEXITCODE" }
Copy-Item (Join-Path $stage 'libwaveform_plugin.dll') $out -Force
Get-FileHash (Join-Path $out 'libwaveform_plugin.dll') -Algorithm SHA256
