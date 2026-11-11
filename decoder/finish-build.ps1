$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$local = Join-Path $env:LOCALAPPDATA 'Temp\opencode\vlc-waveform-decoder'
$source = Join-Path $root 'third_party\ffmpeg\build\ffmpeg.exe'
if (-not (Test-Path $source)) { throw 'No NAS decoder result; no cleanup.' }
$report = Get-Content (Join-Path $root 'native\test-results\release-1.0.1\decoder-test.json') -Raw | ConvertFrom-Json
if (-not $report.ok -or (Get-FileHash $source).Hash -ne $report.decoder_sha256) { throw 'No successful matching decoder test; no cleanup.' }
$target = Join-Path $root 'native\test-results\release-1.0.1\build-diagnostics'
New-Item -ItemType Directory -Force $target | Out-Null
$copied = @()
if (Test-Path (Join-Path $local 'output')) {
  foreach ($file in (Get-ChildItem (Join-Path $local 'output') -File)) {
    $destination = Join-Path $target $file.Name
    Copy-Item $file.FullName $destination -Force
    if ((Get-FileHash $file.FullName).Hash -ne (Get-FileHash $destination).Hash) { throw "NAS diagnostics differ: $($file.Name)" }
    $copied += [pscustomobject]@{file=$file.Name;bytes=$file.Length;sha256=(Get-FileHash $destination).Hash}
  }
}
# Exact private work directory created by this project's build script, not a
# TEMP-age sweep, package uninstall, distro shutdown or shared build cache.
$linux = '/root/.cache/vlc-waveform-ffmpeg-9.0.2'
$check = 'test -f '+$linux+'/toolchain-ready-v4 && test -d '+$linux+'/ffmpeg-9.0.2 && test -d '+$linux+'/build'
& wsl.exe -d NetbookMini-Build-20261024 --cd / --exec sh -c $check
if ($LASTEXITCODE -ne 0) { throw 'Private WSL build identity check failed; preserved.' }
& wsl.exe -d NetbookMini-Build-20261024 --cd / --exec rm -rf -- $linux
if ($LASTEXITCODE -ne 0) { throw 'Private WSL cleanup failed.' }
if (Test-Path $local) { Remove-Item -LiteralPath $local -Recurse -Force }
$record = [pscustomobject]@{copiedDiagnostics=$copied;removedPrivateWslWork=$linux;removedPrivateWindowsWork=$local;productionInstallationChanged=$false}
$record | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $target 'cleanup.json') -Encoding UTF8
$record | ConvertTo-Json -Depth 5
