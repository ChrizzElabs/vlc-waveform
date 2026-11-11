[CmdletBinding()]
param([string]$Version = '1.0.4')
$ErrorActionPreference = 'Stop'
# Never repackage an arbitrary installed FFmpeg build. Only the source-verified
# project build is accepted by the release packager.
& python (Join-Path $PSScriptRoot 'package-release.py') --version $Version
if ($LASTEXITCODE -ne 0) { throw 'Source-complete release packaging failed.' }
