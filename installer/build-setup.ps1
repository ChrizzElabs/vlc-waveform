[CmdletBinding()]
param([switch]$TestSetup,[string]$Version = '1.0.4')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$payload = Join-Path $root ('dist\VLC-Waveform-'+$Version+'-x64')
if (-not (Test-Path (Join-Path $payload 'SHA256.json'))) { throw 'Build native/build-package.ps1 first.' }
$compiler = Join-Path $env:LOCALAPPDATA 'Temp\opencode\vlc-native-tools\inno-7.1.1\ISCC.exe'
if (-not (Test-Path $compiler)) { throw 'Run native/installer/prepare-compiler.ps1 first.' }
$build = Join-Path $env:LOCALAPPDATA ('Temp\opencode\vlc-native-tools\installer-build-'+$Version+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $build | Out-Null
Copy-Item (Join-Path $PSScriptRoot '*') $build -Force
$stage = Join-Path $build 'payload'
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $payload '*') $stage -Recurse -Force
# Also ship the preferred source form of the executable installer's scripts.
$installerSource = Join-Path $stage 'source\installer'
New-Item -ItemType Directory -Force $installerSource | Out-Null
Copy-Item (Join-Path $PSScriptRoot '*') $installerSource -Force
$manifest = @(Get-ChildItem $stage -File -Recurse | Where-Object { $_.FullName -ne (Join-Path $stage 'SHA256.json') } | ForEach-Object {
  [pscustomobject]@{file=$_.FullName.Substring($stage.Length+1);sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}
})
$manifest | ConvertTo-Json | Set-Content (Join-Path $stage 'SHA256.json') -Encoding UTF8
$output = Join-Path $build 'output'
New-Item -ItemType Directory -Force $output | Out-Null
$test = if ($TestSetup) { '1' } else { '0' }
& $compiler "/DPayload=$stage" "/DOutputPath=$output" "/DSetupTest=$test" "/DAppRelease=$Version" (Join-Path $build 'VLC-Waveform.iss')
if ($LASTEXITCODE -ne 0) { throw "Setup compiler failed: $LASTEXITCODE" }
$name = if ($TestSetup) { 'VLC-Waveform-Setup-Test.exe' } else { 'VLC-Waveform-Setup-'+$Version+'-x64.exe' }
$target = Join-Path $root ('dist\'+$name)
Copy-Item (Join-Path $output $name) $target -Force
Get-Item $target | Select-Object FullName,Length
Get-FileHash $target -Algorithm SHA256
