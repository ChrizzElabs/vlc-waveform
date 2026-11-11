[CmdletBinding()]
param([string]$Version = '1.0.4')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$setup = Join-Path $root 'dist\VLC-Waveform-Setup-Test.exe'
$base = Join-Path $env:LOCALAPPDATA ('Temp\opencode\waveform-exe-test-'+[guid]::NewGuid().ToString('N'))
$app = Join-Path $base 'Setup'
$config = Join-Path $app 'test-profile\Roaming\vlc\vlcrc'
$runtime = Join-Path $app 'test-profile\Local\VLCWaveform'
$registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{716CCB26-F499-4D16-A14D-55A690B3EAF4}_is1'
$realConfig = Join-Path $env:APPDATA 'vlc\vlcrc'
$configHash = (Get-FileHash $realConfig).Hash
$userPluginPath = [Environment]::GetEnvironmentVariable('VLC_PLUGIN_PATH','User')
$tests = @()
New-Item -ItemType Directory -Force $base | Out-Null
function RunSetup([string]$Name,[string]$VlcDirectory) {
  $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="'+$app+'" /VLCDIR="'+$VlcDirectory+'" /LOG="'+(Join-Path $base ($Name+'.log'))+'"'
  $p = Start-Process $setup -ArgumentList $arguments -PassThru -Wait
  return $p.ExitCode
}
function StartPrivateVlc {
  $exe = Join-Path $env:ProgramFiles 'VideoLAN\VLC\vlc.exe'
  $arguments = '--no-crashdump --ignore-config --no-one-instance --intf=dummy --extraintf= --audio-visual= --no-media-library --no-plugins-cache --config="'+(Join-Path $base 'private-vlcrc')+'"'
  return Start-Process $exe -ArgumentList $arguments -PassThru
}
try {
  if (Test-Path $registry) { throw 'Separate test installer already registered; inspect it before testing.' }
  if ((RunSetup 'invalid-vlc' (Join-Path $base 'missing-vlc')) -eq 0) { throw 'Invalid VLC accepted' }
  if (Test-Path $registry) { throw 'Invalid VLC setup changed registration' }
  if (Test-Path $runtime) { throw 'Invalid VLC setup changed runtime' }
  $tests += 'Invalid VLC folder rejected before installation'
  New-Item -ItemType Directory -Force (Split-Path $config) | Out-Null
  $original = "[core]`r`nextraintf=gestures`r`n#audio-visual=`r`nvolume=87`r`n[qt]`r`n#qt-video-autoresize=1`r`n"
  [IO.File]::WriteAllText($config,$original)
  $vlc = Join-Path $env:ProgramFiles 'VideoLAN\VLC'
  $privateVlc = StartPrivateVlc
  try {
    if ((RunSetup 'running-vlc' $vlc) -eq 0) { throw 'Setup accepted running VLC' }
    $privateVlc.Refresh()
    if ($privateVlc.HasExited) { throw 'Setup closed VLC' }
    if (Test-Path $registry) { throw 'Running VLC setup changed registration' }
    $tests += 'Setup refuses running VLC without terminating it'
  } finally {
    if (-not $privateVlc.HasExited) { Stop-Process -Id $privateVlc.Id; $privateVlc.WaitForExit() }
  }
  foreach ($round in 1..2) {
    $code = RunSetup ('install-'+$round) $vlc
    if ($code -ne 0) { throw "Setup failed: $code; logs: $base" }
    if (-not (Test-Path $registry)) { throw 'Windows Apps entry missing' }
    if (-not (Test-Path (Join-Path $app 'unins000.exe'))) { throw 'Native uninstaller missing' }
    $dll = Join-Path $runtime 'plugins\libwaveform_plugin.dll'
    if ((Get-FileHash $dll).Hash -ne (Get-FileHash (Join-Path $root 'native\bin-release\libwaveform_plugin.dll')).Hash) { throw 'Installed DLL differs' }
    $release = Get-Content (Join-Path $root ('dist\VLC-Waveform-'+$Version+'-x64\release-manifest.json')) -Raw | ConvertFrom-Json
    if ((Get-FileHash (Join-Path $runtime 'decoder\ffmpeg.exe')).Hash -ne $release.decoder_sha256) { throw 'Decoder differs from source-complete release' }
    if (-not (Test-Path (Join-Path $app 'source\ffmpeg\ffmpeg-9.0.2.tar.xz'))) { throw 'Corresponding FFmpeg source missing' }
    if (-not (Test-Path (Join-Path $runtime 'source\ffmpeg\ffmpeg-9.0.2.tar.xz'))) { throw 'Runtime corresponding source missing' }
    $payload = Join-Path $root ('dist\VLC-Waveform-'+$Version+'-x64')
    foreach ($item in (Get-Content (Join-Path $payload 'SHA256.json') -Raw | ConvertFrom-Json)) {
      if ($item.file.StartsWith('source/') -or $item.file.StartsWith('decoder/')) {
        if ((Get-FileHash (Join-Path $runtime $item.file)).Hash -ne $item.sha256) { throw "Installed source/license/runtime differs: $($item.file)" }
      }
    }
    $text = [IO.File]::ReadAllText($config)
    if (-not $text.Contains('extraintf=gestures:waveformintf') -or -not $text.Contains('volume=87')) { throw 'Configuration incorrect' }
    if ($round -eq 1) { $first = $text } elseif ($first -ne $text) { throw 'Reinstall changed configuration unexpectedly' }
  }
  $tests += 'Offline EXE installation and reinstall; payload hashes and Windows Apps registration correct'
  $tests += 'All installed source, license and decoder files match release manifest byte for byte'
  $privateVlc = StartPrivateVlc
  try {
    $p = Start-Process (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -PassThru -Wait
    if ($p.ExitCode -eq 0) { throw 'Uninstaller accepted running VLC' }
    $privateVlc.Refresh()
    if ($privateVlc.HasExited) { throw 'Uninstaller closed VLC' }
    if (-not (Test-Path $registry)) { throw 'Blocked uninstall removed registration' }
    if ([IO.File]::ReadAllText($config) -ne $first) { throw 'Blocked uninstall changed configuration' }
    $tests += 'Uninstaller refuses running VLC without changing installation'
  } finally {
    if (-not $privateVlc.HasExited) { Stop-Process -Id $privateVlc.Id; $privateVlc.WaitForExit() }
  }
  [IO.File]::WriteAllText($config,$text.Replace('volume=87','volume=89'))
  $p = Start-Process (Join-Path $app 'unins000.exe') -ArgumentList ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="'+(Join-Path $base 'uninstall.log')+'"') -PassThru -Wait
  if ($p.ExitCode -ne 0) { throw "Uninstall failed: $($p.ExitCode)" }
  if (Test-Path $registry) { throw 'Windows Apps entry not removed' }
  if (Test-Path (Join-Path $runtime 'plugins\libwaveform_plugin.dll')) { throw 'DLL not removed' }
  if (Test-Path (Join-Path $runtime 'decoder\ffmpeg.exe')) { throw 'Decoder not removed' }
  $text = [IO.File]::ReadAllText($config)
  foreach ($line in @('extraintf=gestures','#audio-visual=','volume=89','#qt-video-autoresize=1')) {
    if (-not $text.Contains($line)) { throw "Uninstall rollback missing: $line" }
  }
  $tests += 'Native EXE uninstall restores settings, preserves user edits, removes payload and Apps entry'
} finally {
  if ((Get-FileHash $realConfig).Hash -ne $configHash) { throw 'Real VLC configuration changed by sandbox test!' }
  if ([Environment]::GetEnvironmentVariable('VLC_PLUGIN_PATH','User') -ne $userPluginPath) { throw 'Real user plugin path changed by sandbox test!' }
  $tests += 'Real VLC configuration and user plugin path unchanged'
   $result = [pscustomobject]@{ sandbox=$base; tests=$tests; realConfigSha256=$configHash; setupSha256=(Get-FileHash $setup).Hash }
  $result | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $root 'native\test-results\setup-exe-test.json') -Encoding UTF8
  $result | ConvertTo-Json -Depth 5
}
