[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('Check','Install','CheckUninstall','Uninstall')][string]$Action,
  [Parameter(Mandatory=$true)][string]$ApplicationRoot,
  [string]$PayloadDirectory = '',
  [string]$VlcDirectory = '',
  [string]$SandboxRoot = '',
  [Parameter(Mandatory=$true)][string]$LogPath
)
$ErrorActionPreference = 'Stop'
$test = -not [string]::IsNullOrWhiteSpace($SandboxRoot)
New-Item -ItemType Directory -Force (Split-Path $LogPath) | Out-Null
try {
  & {
    Write-Output ('VLC Waveform Setup: '+$Action+' / '+(Get-Date -Format o))
    if (Get-Process vlc -ErrorAction SilentlyContinue) {
      throw 'Bitte VLC vollstaendig schliessen und dann erneut versuchen. Setup beendet keine laufenden Programme.'
    }
    if ($test) {
      # Only the separately compiled setup test can supply this argument.
      # No real VLC settings or user environment registry are changed.
      $env:LOCALAPPDATA = Join-Path $SandboxRoot 'Local'
      $env:APPDATA = Join-Path $SandboxRoot 'Roaming'
      $env:VLC_PLUGIN_PATH = 'C:\UnrelatedSetupTestPlugin'
    }
    if (-not $test -and [IO.Path]::GetFullPath($ApplicationRoot).TrimEnd('\') -ne (Join-Path $env:LOCALAPPDATA 'VLCWaveform')) {
      throw 'Der Installationsordner muss %LOCALAPPDATA%\VLCWaveform sein.'
    }
    if ($Action -in @('Check','Install')) {
      $exe = Join-Path $VlcDirectory 'vlc.exe'
      if (-not (Test-Path -LiteralPath $exe)) { throw 'Im ausgewaehlten Ordner wurde vlc.exe nicht gefunden.' }
      if ((Get-Item -LiteralPath $exe).VersionInfo.FileMajorPart -ne 3) { throw 'Dieses Plugin benoetigt VLC 3.x.' }
      $bytes = [IO.File]::ReadAllBytes($exe)
      $pe = [BitConverter]::ToInt32($bytes,0x3c)
      if ([BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664) { throw 'Bitte VLC 3 in der 64-Bit-Version auswaehlen.' }
    }
    if ($Action -eq 'Install') {
      $config = Join-Path $env:APPDATA 'vlc\vlcrc'
      if (-not (Test-Path -LiteralPath $config)) {
        New-Item -ItemType Directory -Force (Split-Path $config) | Out-Null
        [IO.File]::WriteAllText($config,"[core]`r`n",(New-Object Text.UTF8Encoding($false)))
      }
      & (Join-Path $PayloadDirectory 'Installieren.ps1') -VlcDirectory $VlcDirectory -TestMode:$test
      if (-not (Test-Path (Join-Path $env:LOCALAPPDATA 'VLCWaveform\native-install-state.json'))) {
        throw 'Die Einrichtung hat keinen Installationsnachweis angelegt.'
      }
    } elseif ($Action -eq 'Uninstall') {
      $script = Join-Path $env:LOCALAPPDATA 'VLCWaveform\Deinstallieren.ps1'
      if (Test-Path -LiteralPath $script) { & $script -TestMode:$test }
    }
    Write-Output 'Erfolgreich abgeschlossen.'
  } *> $LogPath
  exit 0
} catch {
  ($_ | Out-String) | Add-Content -LiteralPath $LogPath -Encoding UTF8
  exit 1
}
