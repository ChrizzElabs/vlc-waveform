[CmdletBinding()]
param([switch]$TestMode)
$ErrorActionPreference = 'Stop'
if (Get-Process vlc -ErrorAction SilentlyContinue) { throw 'Bitte VLC vor der Deinstallation schliessen.' }
$root = Join-Path $env:LOCALAPPDATA 'VLCWaveform'
$statePath = Join-Path $root 'native-install-state.json'
if (-not (Test-Path $statePath)) { Write-Host 'Keine native Installation registriert.'; exit 0 }
$state = Get-Content $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
$text = [IO.File]::ReadAllText($state.vlcrc)
foreach ($prop in $state.installed.PSObject.Properties) {
  $rx = [regex]::new('(?m)^'+[regex]::Escape($prop.Name)+'=[^\r\n]*')
  $m = $rx.Match($text)
  # Preserve settings changed by the user after installation.
  if ($m.Success -and $m.Value -eq "$($prop.Name)=$($prop.Value)") {
    $old = $state.original.($prop.Name)
    if ($null -eq $old) { $old = '' }
    $text = $text.Substring(0,$m.Index)+$old+$text.Substring($m.Index+$m.Length)
  } elseif ($m.Success -and $prop.Name -eq 'extraintf') {
    # The user may have added another interface after installation. Remove
    # only our module, never their newly added interfaces.
    $parts = @($m.Value.Substring('extraintf='.Length) -split ':' | Where-Object { $_ -and $_ -ne 'waveformintf' })
    $text = $text.Substring(0,$m.Index)+'extraintf='+($parts -join ':')+$text.Substring($m.Index+$m.Length)
  }
}
[IO.File]::WriteAllText($state.vlcrc,$text,(New-Object Text.UTF8Encoding($false)))
$envTarget = if ($TestMode) { 'Process' } else { 'User' }
$parts = @([Environment]::GetEnvironmentVariable('VLC_PLUGIN_PATH',$envTarget) -split ';' | Where-Object { $_ -and $_ -ne $state.pluginPath })
$new = if ($parts.Count) { $parts -join ';' } else { $null }
[Environment]::SetEnvironmentVariable('VLC_PLUGIN_PATH',$new,$envTarget)
$env:VLC_PLUGIN_PATH = $new
foreach ($p in @((Join-Path $state.pluginPath 'libwaveform_plugin.dll'),(Join-Path $root 'decoder\ffmpeg.exe'),$statePath)) {
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}
Write-Host 'Native Wellenform entfernt. Backups und Cache bleiben erhalten.' -ForegroundColor Green
Write-Host 'Nach der Deinstallation Explorer/Programme gegebenenfalls neu starten.'
