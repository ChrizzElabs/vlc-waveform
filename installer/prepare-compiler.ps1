$ErrorActionPreference = 'Stop'
$tools = Join-Path $env:LOCALAPPDATA 'Temp\opencode\vlc-native-tools'
$compiler = Join-Path $tools 'inno-7.1.1'
if (Test-Path (Join-Path $compiler 'ISCC.exe')) { Write-Output $compiler; exit 0 }
$download = Join-Path $tools 'innosetup-7.1.1-x64.exe'
$expected = '8a5a6711c3766a6e5fb74f280ba0e3551e719ce94538d901fdbd1f8f9920b3f0'
New-Item -ItemType Directory -Force $tools | Out-Null
if (-not (Test-Path $download)) {
  $client = New-Object Net.WebClient
  try { $client.DownloadFile('https://github.com/jrsoftware/issrc/releases/download/is-7_1_1/innosetup-7.1.1-x64.exe',$download) }
  finally { $client.Dispose() }
}
if ((Get-FileHash $download -Algorithm SHA256).Hash -ne $expected) { throw 'Inno Setup compiler download: SHA256 mismatch' }
$signature = Get-AuthenticodeSignature $download
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*Pyrsys*') {
  throw "Inno Setup compiler signature invalid: $($signature.Status)"
}
# Official portable mode: no machine installation, file association, shortcuts,
# compiler UI, restart, or browser. All files stay in the approved build cache.
$arguments = '/PORTABLE=1 /CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOICONS /DIR="'+$compiler+'"'
$process = Start-Process -FilePath $download -ArgumentList $arguments -PassThru -Wait
if ($process.ExitCode -ne 0 -or -not (Test-Path (Join-Path $compiler 'ISCC.exe'))) {
  throw "Portable compiler preparation failed: $($process.ExitCode)"
}
Write-Output $compiler
