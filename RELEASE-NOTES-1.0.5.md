## What changed

- One Audio > Visualizations > Waveform entry. Re-enabling starts in Contour.
- Contour, Pulse and Grid have independent saved colors; Grid shows measured peaks.
- Optional slate-clap markers, independently of the display style. Detection is not exhaustive.
- Toggle hours, minutes, seconds, milliseconds and frame count separately. Frame rate is a separate setting. With hours hidden, the clock continues from `59:59` to `60:00` without shifting seconds.
- The waveform cache is saved only after regular pipe EOF and a successful FFmpeg exit. Old cache entries are rebuilt on demand.

Windows 10+ x64 with VLC 3.x x64. The offline per-user installer is unsigned. Verify downloads using `SHA256SUMS.txt`. Full corresponding sources and licenses are included.
