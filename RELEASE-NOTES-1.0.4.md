# VLC Waveform 1.0.4 - First Public Release

Independent waveform visualization plugin for VLC 3.x on Windows x64.

## Features

- Full-file waveform visualization for WAV, FLAC, MP3 and M4A
- Click-to-seek navigation; seeking from pause keeps playback paused
- Three predefined views: Standard, Compact and Contrast with custom icons
- Enable/disable switch, re-enable through VLC's Audio > Visualizations menu
- Automatic RAM read-ahead buffering for NAS MP3 files up to 16 MiB

## Release 1.0.4

First public release. Includes waveform visualization, three display modes,
click-to-seek navigation and automatic RAM buffering for supported NAS MP3
files.

Tested:

- Pause, seek and resume
- NAS playback without dropouts during testing
- Uninstallation
- Preservation of user settings

Results describe the tested configurations and do not guarantee identical
behavior on every system.

## Downloads

| File | Description |
|---|---|
| `VLC-Waveform-Setup-1.0.4-x64.exe` | Offline installer, per user, no administrator rights |
| `VLC-Waveform-1.0.4-x64.zip` | Portable payload package |
| `VLC-Waveform-Quellen-1.0.4.zip` | Complete corresponding sources, licenses and build records |
| `SHA256SUMS.txt` | SHA-256 checksums of the files above |

## Security and Verification

The installer is not digitally signed. Windows SmartScreen may display a
warning. Some Windows security configurations may block unsigned
applications.

SHA-256 checksums are provided for the release files. A checksum proves that a
file is identical to the published one; it does not make a file trustworthy by
itself. Trust comes from the published source code, the reproducible build
documentation and the unchanged release files.

Full corresponding source code and license files are included in the
distribution.

## Licensing

VLC Waveform is licensed under GPL-2.0-or-later.

The bundled FFmpeg components are distributed under their applicable licenses,
including LGPL-2.1-or-later.

See the included license files for details.

## Disclaimer

VLC is a trademark of VideoLAN.

VLC Waveform is an independent third-party project.

It is not developed, maintained, affiliated with or endorsed by VideoLAN.
