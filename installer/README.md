# Windows-Setup

`build-setup.ps1` erstellt den deutschen Inno-Setup-Assistenten aus dem bereits
geprüften Offline-Paket unter `dist/VLC-Waveform-<Version>-x64` (aktuell 1.0.4).
Keine neue DLL-Kompilierung. `-Version` wählt die Paketfassung für Bau und Test.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native\installer\prepare-compiler.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File native\installer\build-setup.ps1 -TestSetup
powershell -NoProfile -ExecutionPolicy Bypass -File native\installer\test-setup.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File native\installer\build-setup.ps1
```

Der offizielle Compiler wird hash- und signaturgeprüft portabel im lokalen
Build-Cache eingerichtet. Das fertige Setup ist ohne eigenes Zertifikat **nicht signiert**.

Installation pro Benutzer nach `%LOCALAPPDATA%\VLCWaveform`, ohne Adminrechte,
Internetdownload, Browser oder automatischen VLC-Start. Registrierung in Windows
Apps und Startmenü; native Deinstallation mit Rückbau der Plugin-Einstellungen.
Laufende VLC-Prozesse werden nicht beendet; Setup und Deinstallation lehnen ab.
VLC 3.x x64 muss bereits installiert sein.

Die Testfassung hat eine eigene AppId, ein eigenes Startmenü und ein Sandboxprofil.
Sie verändert weder die echte VLC-Konfiguration noch den Benutzer-Plugin-Pfad.
Geprüft: ungültiger VLC-Pfad, laufendes VLC, vollständige Installation,
Wiederinstallation, Windows-Apps-Eintrag, DLL-Prüfsumme und Decodergröße,
Deinstallation, Erhalt geänderter Einstellungen und unverändertes echtes Profil.
Nur selbst gestartete, kopflose VLC-Testprozesse werden anschließend beendet.
Prüfprotokoll: `native/test-results/setup-exe-test.json`.

Das Setup enthält den projektgebundenen LGPL-2.1-or-later-FFmpeg-9.0.2-Build
samt passenden vollständigen Quellen und Bauinformationen. Lizenz- und
Weitergabehinweise liegen im Paket; kein Gyan-Allzweckbuild mehr.
