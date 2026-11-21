# Changelog

## 1.0.5 - 2026-11-21

- One Audio > Visualizations > Waveform entry. Re-enabling starts in Contour.
- Contour, Pulse and Grid have independent saved colors; Grid shows measured peaks.
- Optional slate-clap markers, independently of the display style. Detection is not exhaustive.
- Toggle hours, minutes, seconds, milliseconds and frame count separately. Frame rate is a separate setting. With hours hidden, the clock continues from `59:59` to `60:00` without shifting seconds.
- The waveform cache is saved only after regular pipe EOF and a successful FFmpeg exit. Old cache entries are rebuilt on demand.

## 1.0.4 - 2026-11-11

First public release.

- Full-file waveform visualization for WAV, FLAC, MP3 and M4A
- Click-to-seek navigation; seeking from pause keeps playback paused
- Three predefined views: Standard, Compact and Contrast with custom icons
- Enable/disable switch, re-enable through VLC's Audio > Visualizations menu
- Optional read-ahead buffer for MP3 files on network shares (up to 16 MiB)
- Offline per-user installer (no administrator rights), complete matching
  sources and licenses included

Versions 1.0.0 to 1.0.3 were internal development states and were never
published.
