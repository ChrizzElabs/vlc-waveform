# FFmpeg-Audiobuild

FFmpeg **9.0.2**, aktueller stabiler offizieller Quellstand bei der Recherche
am 09.11.2026: https://ffmpeg.org/download.html. Herkunft und SHA256 werden in
`third_party/ffmpeg/upstream.json` festgehalten. Die offizielle OpenPGP-Signatur
wird gegen den festgelegten Schlüssel `FCF986EA15E6E293A5644F10B4322F04D67658D8`
geprüft. Lizenzgrundlage: https://ffmpeg.org/legal.html und FFmpeg `LICENSE.md`.

## Auswahl

Der vorher vorhandene Gyan-Allzweckbuild wird **nicht** weiterverwendet.
Eigener unveränderter Upstream-Quellstand, reine Audioauswahl, keine externen
Codec-Bibliotheken, kein `--enable-gpl`, `--enable-version3` oder `--enable-nonfree`.
Keine Netzwerkprotokolle. Ergebnis: LGPL-2.1-or-later; separates FFmpeg-Programm,
keine statische FFmpeg-Verknüpfung mit dem VLC-Plugin.

PCM/WAV, FLAC, MP3, AAC/M4A, ALAC, AIFF, Vorbis/Opus sowie AC3/EAC3 sind ausgewählt;
Matroska/MOV dienen als Container. Kein allgemeiner Ersatz für einen Vollbuild.
Das ist ein audioorientierter Build ohne Videodecoder; FFmpegs CLI aktiviert
automatisch auch einige grundlegende Videofilter. Der tatsächliche Komponenten-
bestand steht in `build-record/configure-output.txt` und `config_components.h`.

## Bau

1. Windows/Python 3: `python native/decoder/prepare-sources.py` und
   `python native/decoder/prepare-runtime-sources.py` laden die Quellen aufs NAS.
2. `build-ffmpeg.ps1` kopiert nur benötigte Dateien in den lokalen Arbeitscache
   und ruft das vorhandene Debian-13-WSL mit `build-ffmpeg.sh` auf.
3. Das Shellskript lädt über APT Debian-Pakete gemäß signierten Repository-Metadaten
   in ein privates Verzeichnis und entpackt sie dort. Keine globale Paketinstallation,
   keine Änderungen an System-Compilern, Diensten oder fremden Projekten.
4. `build-ffmpeg.sh WORK SOURCES OUTPUT` ist auch unter Debian 13 amd64 direkt
   ausführbar; SOURCES enthält das offizielle FFmpeg-Archiv, Signatur und Schlüsselring.
   Werkzeugversionen, configure-Zeile, generierte Konfiguration und Signaturbeleg
   werden nach OUTPUT geschrieben. Zwei Make-Jobs begrenzen Last und Speicherbedarf.

Der offizielle Quelltext wird nicht gepatcht. Für Neubauten auf anderem Pfad
die privaten Pfade mit dem Shellskript neu erzeugen; sie sind keine erforderlichen
Netzwerk-/Benutzerpfade. Entsprechende Windows-PE-Abhängigkeiten prüfen und
`ffmpeg.exe -version`, `-L` und reale Audioausgabe testen.

## Mitgelieferte Quellen / Neuverknüpfung

Das Release enthält den vollständigen FFmpeg-Quelltext, MinGW-CRT-Quellen,
Zig-Runtimequellen für das unveränderte Plugin, Lizenzen, Buildskripte und die
erzeugte Konfiguration. `source/ffmpeg/relink-objects.tar.gz` enthält zusätzlich
Frontend-Objektdateien und generierte Header. Zum Neuverknüpfen die geänderten
LGPL-Bibliotheken mit derselben Konfiguration bauen, dann **nur** `fftools/`
aus dem Archiv im neuen Buildverzeichnis wiederherstellen. Die neue Konfiguration
nicht mit der archivierten `ffbuild/config.mak` überschreiben. Alte `.d`-Dateien
unter dem wiederhergestellten `fftools/` entfernen; `ffmpeg.exe`/`ffmpeg_g.exe`
im eigenen Buildverzeichnis entfernen und `make ffmpeg.exe` zum erneuten Linken
ausführen. Der komplette
Frontend-Neubau ist ebenfalls möglich; bevorzugter Quelltext ist vollständig dabei.

GCC-Runtime-Library-Exception 3.1 gilt für entsprechend markierte Compiler-Runtime-
Bestandteile. Deren Lizenz-/Copyrightangaben und die MinGW-Angaben werden erhalten.
Die Software darf vom Empfänger entsprechend den freien Lizenzen verändert,
neu gebaut und weitergegeben werden; keine zusätzliche EULA beschränkt das.

## Release

`native/package-release.py` wählt Dateien explizit aus. Alte Browserdateien,
private Konfigurationen, Testmedien und Logs gelangen nicht ins Release.
Das Paket wird nicht automatisch veröffentlicht. Ein vollständiges Offline-Setup
enthält auch die Quellen; der Empfänger muss keine Drittserver nach alten
Quellen durchsuchen. Landes-/nutzungsspezifische Patent- und Markenfragen sind
damit nicht pauschal rechtlich entschieden.

## Offline aus dem ausgelieferten Quellordner bauen

Unter Debian 13 amd64 den installierten `source`-Ordner (unverändert einschließlich
der Archive) verwenden; in Bash etwa:

```sh
SOURCE_ROOT="$(pwd)/source"
bash "$SOURCE_ROOT/decoder/build-ffmpeg.sh" \
  "$HOME/.cache/vlc-waveform-ffmpeg-9.0.2" "$SOURCE_ROOT/ffmpeg" "$PWD/decoder-output"
```

Der Schlüsselring liegt in `source/ffmpeg/ffmpeg-release-key.gpg` bereits bei.
Die FFmpeg-Quellen müssen nicht erneut heruntergeladen werden. Standard-
Buildwerkzeuge werden beim ersten Bau von Debian bezogen; sie sind nicht Teil
des Produkts. `prepare-*.py` sind nur für einen Neubau aus dem Entwicklungsbaum
mit `native/` und `third_party/`, nicht Voraussetzung für den gelieferten Quellenordner.
Für das Plugin die Dateien unter `source/plugin/` verwenden; der Releasebau
erfolgt mit `build.ps1 -Release` und neutralisierten Baupfaden ohne Debugdaten.
