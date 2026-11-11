$ErrorActionPreference = 'Stop'
$tools = Join-Path $env:LOCALAPPDATA 'Temp\opencode\vlc-native-tools'
New-Item -ItemType Directory -Force -Path $tools | Out-Null
$wc = New-Object Net.WebClient
try {
  $vlcZip = Join-Path $tools 'vlc-3.0.24-win64.zip'
  if (-not (Test-Path $vlcZip)) {
    $wc.DownloadFile('https://download.videolan.org/pub/videolan/vlc/3.0.24/win64/vlc-3.0.24-win64.zip', $vlcZip)
  }
  $sum = $wc.DownloadString('https://download.videolan.org/pub/videolan/vlc/3.0.24/win64/vlc-3.0.24-win64.zip.sha256').Split(' ')[0].Trim()
  if ((Get-FileHash $vlcZip -Algorithm SHA256).Hash -ne $sum) { throw 'VLC SHA256 mismatch' }
  if (-not (Test-Path (Join-Path $tools 'vlc-3.0.24\vlc.exe'))) {
    Expand-Archive -LiteralPath $vlcZip -DestinationPath $tools -Force
  }
  $zigZip = Join-Path $tools 'zig.zip'
  if (-not (Test-Path $zigZip)) {
    $wc.DownloadFile('https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip', $zigZip)
  }
  $index = $wc.DownloadString('https://ziglang.org/download/index.json') | ConvertFrom-Json
  $expected = $index.'0.13.0'.'x86_64-windows'.shasum
  if ((Get-FileHash $zigZip -Algorithm SHA256).Hash -ne $expected) { throw 'Zig SHA256 mismatch' }
  if (-not (Test-Path (Join-Path $tools 'zig-windows-x86_64-0.13.0\zig.exe'))) {
    Expand-Archive -LiteralPath $zigZip -DestinationPath $tools -Force
  }
  if (-not (Test-Path (Join-Path $tools 'vlc-3.0.24\sdk\lib\vlccore.def'))) {
    & python (Join-Path $PSScriptRoot 'prepare-sdk.py')
    if ($LASTEXITCODE -ne 0) { throw 'SDK preparation failed' }
  }
  Write-Output "Runtime + SDK + Compiler ready: $tools"
} finally { $wc.Dispose() }
