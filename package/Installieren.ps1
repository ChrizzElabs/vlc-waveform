[CmdletBinding()]
param([string]$VlcDirectory = '', [switch]$TestMode)
$ErrorActionPreference = 'Stop'
if (Get-Process vlc -ErrorAction SilentlyContinue) { throw 'Bitte VLC vor der Installation vollstaendig schliessen.' }
if (-not $VlcDirectory) {
  $VlcDirectory = Join-Path $env:ProgramFiles 'VideoLAN\VLC'
}
$exe = Join-Path $VlcDirectory 'vlc.exe'
if (-not (Test-Path $exe)) { throw 'VLC nicht gefunden. -VlcDirectory angeben.' }
$version = (Get-Item $exe).VersionInfo.FileMajorPart
if ($version -ne 3) { throw 'Dieses Plugin benoetigt VLC 3.x (64 Bit).' }
$bytes = [IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) { throw 'VLC muss die 64-Bit-Version sein.' }
$root = Join-Path $env:LOCALAPPDATA 'VLCWaveform'
$plugins = Join-Path $root 'plugins'
$statePath = Join-Path $root 'native-install-state.json'
$vlcrc = Join-Path $env:APPDATA 'vlc\vlcrc'
if (-not (Test-Path $vlcrc)) { throw 'Bitte VLC einmal starten und schliessen, damit die Einstellungen angelegt werden.' }
$dll = Join-Path $PSScriptRoot 'libwaveform_plugin.dll'
$ffmpeg = Join-Path $PSScriptRoot 'decoder\ffmpeg.exe'
foreach ($p in @($dll,$ffmpeg)) { if (-not (Test-Path $p)) { throw "Paket unvollstaendig: $p" } }
$manifestPath = Join-Path $PSScriptRoot 'SHA256.json'
if (Test-Path $manifestPath) {
  foreach ($item in (Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json)) {
    $file = Join-Path $PSScriptRoot $item.file
    if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $item.sha256) {
      throw "Paket-Pruefsumme stimmt nicht: $($item.file). Bitte ZIP erneut entpacken."
    }
  }
} elseif (-not $TestMode) { throw 'Paket-Pruefsummen fehlen (SHA256.json).' }
New-Item -ItemType Directory -Force -Path $root,$plugins,(Join-Path $root 'decoder') | Out-Null
$text = [IO.File]::ReadAllText($vlcrc)
$utf8 = New-Object Text.UTF8Encoding($false)

function Set-Option([string]$Text,[string]$Name,[string]$Value,[string]$Section) {
  $rx = [regex]::new('(?m)^#?' + [regex]::Escape($Name) + '=[^\r\n]*')
  $m = $rx.Match($Text)
  if ($m.Success) { return $Text.Substring(0,$m.Index)+"$Name=$Value"+$Text.Substring($m.Index+$m.Length) }
  $sec = [regex]::Match($Text,'(?m)^\['+[regex]::Escape($Section)+'\][^\r\n]*')
  if ($sec.Success) { return $Text.Insert($sec.Index+$sec.Length,"`r`n$Name=$Value") }
  return $Text.TrimEnd()+"`r`n`r`n[$Section]`r`n$Name=$Value`r`n"
}

# Save original option lines ONCE (updates must not overwrite the rollback state).
$names = @('extraintf','lua-intf','audio-visual','waveform-ffmpeg','qt-video-autoresize')
if (Test-Path $statePath) {
  $state = Get-Content $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
} else {
  $backup = Join-Path $root ('vlcrc.before-native-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
  Copy-Item $vlcrc $backup
  $original = [ordered]@{}
  foreach ($name in $names) {
    $m = [regex]::Match($text,'(?m)^#?'+[regex]::Escape($name)+'=[^\r\n]*')
    $original[$name] = if ($m.Success) { $m.Value } else { $null }
  }
  $state = [pscustomobject]@{version=1;vlcrc=$vlcrc;backup=$backup;pluginPath=$plugins;original=[pscustomobject]$original;installed=[pscustomobject]@{};installedAt=(Get-Date -Format o)}
}
$current = [regex]::Match($text,'(?m)^extraintf=([^\r\n]*)').Groups[1].Value
$parts = @($current -split ':' | Where-Object { $_ -and $_ -notin @('waveformintf','luaintf') })
# Remove the legacy lua interface only when it is our old waveform launcher.
$oldLua = [regex]::Match($text,'(?m)^lua-intf=([^\r\n]*)').Groups[1].Value
if ($oldLua -ne 'waveform' -and (($current -split ':') -contains 'luaintf')) { $parts += 'luaintf' }
if ($oldLua -eq 'waveform') {
  $text = Set-Option $text 'lua-intf' '' 'lua'
  $state.installed | Add-Member NoteProperty 'lua-intf' '' -Force
  # The HTTP interface was introduced for the legacy Python helper. Native
  # playback needs no listening socket. Preserve HTTP if not using our launcher.
  $parts = @($parts | Where-Object { $_ -ne 'http' })
}
$parts += 'waveformintf'
$values = [ordered]@{
  'extraintf'=($parts -join ':')
  'audio-visual'='waveformvis'
  'waveform-ffmpeg'=(Join-Path $root 'decoder\ffmpeg.exe')
  'qt-video-autoresize'='0'
}
foreach ($name in $values.Keys) {
  $section = if ($name -eq 'waveform-ffmpeg') { 'waveform' } elseif ($name -eq 'qt-video-autoresize') { 'qt' } else { 'core' }
  $text = Set-Option $text $name $values[$name] $section
  $state.installed | Add-Member NoteProperty $name $values[$name] -Force
}
Copy-Item $dll (Join-Path $plugins 'libwaveform_plugin.dll') -Force
Copy-Item $ffmpeg (Join-Path $root 'decoder\ffmpeg.exe') -Force
Get-ChildItem (Join-Path $PSScriptRoot 'decoder') -File | Where-Object { $_.Extension -eq '.txt' } | ForEach-Object {
  Copy-Item $_.FullName (Join-Path $root 'decoder') -Force
}
foreach ($file in @('Deinstallieren.ps1','Deinstallieren.cmd','LIESMICH.txt','COPYING.txt','WEITERGABE.txt','AENDERUNGEN.txt','release-manifest.json','waveform.ico')) {
  if (Test-Path (Join-Path $PSScriptRoot $file)) { Copy-Item (Join-Path $PSScriptRoot $file) $root -Force }
}
# Carry the corresponding sources with ZIP/script installations too, not only
# with Inno Setup. They must remain available to every binary recipient.
if (Test-Path (Join-Path $PSScriptRoot 'source')) {
  New-Item -ItemType Directory -Force (Join-Path $root 'source') | Out-Null
  Copy-Item (Join-Path $PSScriptRoot 'source\*') (Join-Path $root 'source') -Recurse -Force
}
if (Test-Path (Join-Path $PSScriptRoot 'decoder\notices')) {
  Copy-Item (Join-Path $PSScriptRoot 'decoder\notices') (Join-Path $root 'decoder') -Recurse -Force
}
$envTarget = if ($TestMode) { 'Process' } else { 'User' }
$envOld = [Environment]::GetEnvironmentVariable('VLC_PLUGIN_PATH',$envTarget)
$envParts = @($envOld -split ';' | Where-Object { $_ -and $_ -ne $plugins }) + @($plugins)
[Environment]::SetEnvironmentVariable('VLC_PLUGIN_PATH',($envParts -join ';'),$envTarget)
$env:VLC_PLUGIN_PATH = $envParts -join ';'
[IO.File]::WriteAllText($vlcrc,$text,$utf8)
[IO.File]::WriteAllText($statePath,($state | ConvertTo-Json -Depth 8),$utf8)
if (-not ('WaveformEnvironment' -as [type])) {
  Add-Type @'
using System; using System.Runtime.InteropServices;
public class WaveformEnvironment {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,string l,uint f,uint t,out IntPtr r);
}
'@
}
$result=[IntPtr]::Zero
if (-not $TestMode) { $null=[WaveformEnvironment]::SendMessageTimeout([IntPtr]0xffff,0x1a,[IntPtr]::Zero,'Environment',2,5000,[ref]$result) }
Write-Host 'VLC Waveform installiert. Kein Browser, kein Autostart-Dienst, kein Python.' -ForegroundColor Green
Write-Host 'VLC normal starten und eine Audiodatei oeffnen.'
Write-Host 'Falls VLC aus einem bereits laufenden Programm gestartet wird: dieses zuerst neu starten.'
Write-Host 'Deinstallation:' (Join-Path $root 'Deinstallieren.cmd')
