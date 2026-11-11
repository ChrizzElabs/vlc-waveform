# Native VLC Waveform

Ziel: installierbare VLC-3-x64-DLL, ohne Browser, HTTP-Steuerung,
Python-Laufzeit oder separates Anzeigefenster.

Die Visualisierung verwendet VLCs `aout_filter_RequestVout` / `vout_PutPicture`
wie das vorhandene Visualizer-Modul. Mausereignisse werden auf dieser
Videoausgabe über die vorhandenen VLC-Variablen `mouse-moved` und
`mouse-button-down` empfangen (wie VLCs Gesten-Modul). Die echte
Input-Variable `position` wird gesetzt, nicht eine nachgebildete Zeitleiste.

Grundlagen/Referenzen:

* VideoLAN VLC 3, `modules/visualization/visual/visual.c`, LGPL-2.1-or-later
* VideoLAN VLC 3, `modules/control/gestures.c`, GPL-2.0-or-later
* Maxim Biro: <https://github.com/nurupo/vlc-pause-click-plugin>, LGPL-2.1-or-later

Der neue Quelltext in diesem Ordner steht unter GPL-2.0-or-later.
Analyse: FFmpeg als unsichtbarer, kontrollierter Unterprozess, keine Shell.
Cache: `%LOCALAPPDATA%\VLCWaveform\cache`.

## Bauen

`prepare-tools.ps1` lädt die offizielle VLC-3.0.24-Laufzeit und Zig 0.13
und prüft deren SHA256-Werte. Das aktuelle VLC-Windows-Archiv enthält kein SDK.
`python prepare-sdk.py` ergänzt deshalb die Header aus dem SHA256-geprüften,
offiziellen VLC-3.0.24-Quellarchiv und die Importdefinition aus der Laufzeit-DLL.
`build.ps1` kompiliert die Plugin-DLL damit.
Die Downloads liegen ausschließlich im OpenCode-Temp-Ordner.

Weitergabefassung 1.0.4: `build.ps1 -Release` schreibt separat nach
`bin-release`, ohne Debugdaten und mit neutralisierten Compilerpfaden.
Die Minimalfassung entfernt Zoom/Dragging, zeichnet nur bei Änderungen und
zwischenspeichert die statische Wellenform. Im ausgelieferten
Quellordner liegen passende SDK-Header und `vlccore.def` unter `plugin/sdk`.
Für einen Neubau können diese nach
`%LOCALAPPDATA%\Temp\opencode\vlc-native-tools\vlc-3.0.24\sdk` kopiert werden;
Zig 0.13.0 wird über `prepare-tools.ps1` beschafft. Der vollständige VLC-
Quelltext und Zig-Laufzeitquellen liegen daneben unter `source/runtime`.
Die Entwicklungs-/Testskripte sind keine zusätzliche Laufzeitvoraussetzung.

## Bedienung 1.0.4 (10.11.2026)

- Native GDI-Schaltflächen mit eigenen Symbolen: Standard (türkis, bipolar),
  Kompakt (ruhige blaue Impulsbalken), Kontrast (schwarz/gelb mit weißem Cursor).
  Alle zeigen die ganze Datei auf derselben 800x240-Fläche, ohne Zoom/Dragging.
- Aus schaltet die Visualisierung und neue Analyse/Vorpufferung ab. Ein bereits
  aktiver RAM-Tonpuffer bleibt bis zum Titelende gültig, kein neuer Netzwerkaussetzer
  durch das Abschalten. Wieder an über Audio → Visualisierungen → Wellenform.
- Menüerweiterung gehört zum Interface, nicht zum zerstörbaren Ausgabefilter.
  Gehaltenes Aout, Callback-Abmeldung ohne Kontextlocks vor Freigabe.
  Der Audio-Visual-Callback übersetzt VLCs generisches `visual` callbackfrei per
  `VLC_VAR_SETVALUE`; kein rekursives Setzen/Deadlock und keine Callback-Reihenfolgeannahme.
- Eigene `%LOCALAPPDATA%\\VLCWaveform\\waveform-settings.txt` speichert nur Ein/Aus
  und Ansicht. Optionaler `--waveform-settings`-Pfad für getrennte Profile/Tests.
  Keine Speicherung der gesamten VLC-Konfiguration. Pause und Abspielposition
  bleiben beim Umschalten erhalten; Filterneustart ggf. erst beim Fortsetzen.
- Sicherer Publisher: einmaliges schwarzes Decoder-Startbild, weitere komplette
  RGBA-Bitmapbilder als opaque native VLC-Subpicture-Regionen auf eigenem Kanal.
  Kein externer Overlay/Player, keine wiederholten `vout_GetPicture`-Pool-Waits,
  keine privaten VLC-Adressen oder Threadabbruch. Genau ein dauerhaftes Subpicture
  mit nativem Updater, höchstens eine wartende Bitmap; neue Bilder ersetzen die
  wartende statt eine unbeschränkte Kommandowarteschlange zu füllen. Bei Speicher-
  knappheit bleibt die Anzeigeänderung bis zum nächsten Allokationsversuch offen.
  Referenzgezählte Übergabe; beim Abbau erst Publisher stoppen/joinen, dann Kanal
  leeren und Vout synchron schließen. Erst danach GDI-/Updaterdaten freigeben.
- `test-minimal.c`: echte unsichtbare GDI-/Mouse-Funktionen, drei Pixelstile,
  Pausenruhe, Schalter und eigene Settings. `test-controls.py`/`.lua`: echter
  VLC-Menü-/Callback-/Filterabbau, wiederholtes Aus/Ein und OFF nach Neustart.
  Lua dient nur dem Test, nicht der Installation/Laufzeit des Plugins.
- Reproduzierbares Original-Setupicon aus `installer/make-icon.py`; Pillow nur
  optional beim Neubauen dieses statischen Icons, nicht im Produkt benötigt.

## NAS-MP3-Vorlesepuffer 1.0.3 (10.11.2026)

Zusätzlicher nativer Streamfilter `waveformnetcache` im selben Plugin. Nur bei
aktivem Waveform-Interface und UNC-MP3 bis 16 MiB: komprimierte Originalbytes
einmal vollständig vor Wiedergabestart in begrenztes RAM einlesen; anschließend
Read/Seek aus diesem Speicher. Keine Playlistumschreibung, keine globale
Cache-Einstellung und kein zusätzlicher Dienst oder persistenter Dateicache.
Lokale Dateien, größere Dateien und andere Formate werden nicht vorgepuffert.
Vorschau/Bibliotheksanalyse sowie konservativ auch Quiet-Modus werden ausgenommen.
Erstes Einlesen kann bei langsamem NAS dauern. Wellenformanalyse bleibt getrennt
und darf die Originaldatei wie bisher einmal lesen.

`test-net-cache.c` prüft die echten Auswahl-/Read-/Seek-/EOF-Funktionen;
`test-buffering.py --nas-only --real-output --version 1.0.3 --plugin-dll ...`
prüft die ganze beanstandete MP3 direkt vom NAS mit echten VLC-Ausgaben.
Alte 1.0.2 und Rückweg bleiben erhalten. Kein Versprechen einer allgemeinen
Netzwerk-/Treiberreparatur für alle Mediengrößen/-formate.

## Minimalfassung 1.0.2 (10.11.2026)

800x240 statt 1200x420, höchstens 8 statt ca. 30 Bilder/s; unveränderte Pause
ohne weitere Bilder. Zeit-/Cursoränderungen kopieren einen statisch vorbereiteten
Hintergrund, ohne die Wellenform erneut zu zeichnen. GDI arbeitet außerhalb der
gemeinsamen Zustandsperre. FFmpeg mit einem Decoderthread und Leerlaufpriorität,
Analyseworker im Hintergrundmodus; erkannte Videospuren werden übersprungen. Cache und 48-kHz-
Stereo-Spitzenermittlung unverändert, um kurze Klappengeräusche zu erhalten.

`test-minimal.py` misst tatsächlich gestartetes VLC mit altem/neuem/ohne Plugin
bei gleicher MP3, getrennten Profilen und Dummy-Ausgabe (kein Bildschirmtest).
`test-minimal.c` prüft die echten Maus-/Zeichenfunktionen auf unsichtbaren
Bitmapflächen: Klick in Pause, ignoriertes Mausrad, 1000 unveränderte Pausen-
Snapshots ohne neues Zeichnen, Wiederverwendung der statischen Wellenform.
Belege: `test-results/release-1.0.2/`. Eine praktische Nutzerprüfung der konkreten
problematischen MP3 ist durch diese Messung nicht ersetzt.

## Frühere Verifikation (VLC 3.0.24 x64, 09.11.2026)

* Native DLL lädt und beendet sich im ABI-/Lebensdauertest.
* WAV, FLAC, MP3 und M4A: eingebettete Ausgabe, pausierter Klick-Seek,
  Space spielt ab der gesetzten Position; Titelwechsel ohne Neustart.
* Native Uhr und Cursor verändern sich tatsächlich in VLC-Snapshots;
  der pausierte Cursor wird nach einem Seek neu gezeichnet.
* Native Schaltflächen Zoom + und Alles funktionieren auch bei Pause.
* MP4 mit Video: normale Videoausgabe, keine zweite Wellenform-Ausgabe.
* Fehlender Decoder: Fehlermeldung in der Wellenform, VLC spielt weiter;
  pausierter Klick-Seek bleibt bedienbar.
* Wechsel während der Analyse einer 24-Stunden-RF64-Datei: alte Analyse wird
  abgebrochen, Wellenform der neuen Datei erscheint, normales Beenden.
* Installer im getrennten Testprofil: Migration, wiederholtes Update,
  Rückbau und Erhalt fremder/geänderter Einstellungen geprüft.

Testprogramme: `test-load.py`, `test-native.py`, `test-scenarios.py`, `test-installed.py`,
`test-installer.ps1`. Ergebnisse: `test-results/`. Nur die Integrationstests
nutzen HTTP (lokaler Port 8099); das ausgelieferte Plugin braucht das nicht.
Die portable Testlaufzeit verwendet einen eigenen `portable/`-Profilordner.

Bedienung: Klick in die Wellenform setzt VLCs echte Position. Ein pausierter
Player bleibt pausiert; Space setzt die Wiedergabe ab dieser Stelle fort.
Kein Live-Scrubbing, keine Zoom-Schaltflächen in 1.0.2.

Bei Zig/UCRT ist die Speicherkompatibilität wichtig: VLC 3 verwendet MSVCRT.
Das Plugin benutzt deshalb für alle Austauschpuffer den MSVCRT-Allocator,
auch innerhalb der öffentlichen Inline-Helfer aus den VLC-Headern.
