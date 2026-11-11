[CmdletBinding()]
param([string]$Distro = 'NetbookMini-Build-20261024')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$cache = Join-Path $env:LOCALAPPDATA 'Temp\opencode\vlc-waveform-decoder'
$sources = Join-Path $root 'third_party\ffmpeg'
if (-not (Test-Path (Join-Path $sources 'ffmpeg-9.0.2.tar.xz'))) { throw 'Run prepare-sources.py first.' }
$free = (Get-PSDrive -Name C).Free
if ($free -lt 2GB) { throw 'Need at least 2 GiB free for private toolchain, build and reserve.' }
New-Item -ItemType Directory -Force $cache,(Join-Path $cache 'sources'),(Join-Path $cache 'output') | Out-Null
Copy-Item (Join-Path $sources '*') (Join-Path $cache 'sources') -Force
Copy-Item (Join-Path $PSScriptRoot 'build-ffmpeg.sh') $cache -Force
function WslPath([string]$Path) {
  return '/mnt/'+$Path.Substring(0,1).ToLower()+'/'+$Path.Substring(3).Replace('\','/')
}
$linuxCache = WslPath $cache
& wsl.exe -d $Distro --cd / --exec bash ($linuxCache+'/build-ffmpeg.sh') "/root/.cache/vlc-waveform-ffmpeg-9.0.2" ($linuxCache+'/sources') ($linuxCache+'/output')
if ($LASTEXITCODE -ne 0) { throw "FFmpeg build failed: $LASTEXITCODE. Inspect $cache\output and private WSL build logs." }
$target = Join-Path $root 'third_party\ffmpeg\build'
New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item (Join-Path $cache 'output\*') $target -Force
$exe = Join-Path $target 'ffmpeg.exe'
& $exe -version 2>&1 | Set-Content (Join-Path $target 'ffmpeg-version.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'Built Windows decoder did not run.' }
Get-FileHash $exe -Algorithm SHA256
