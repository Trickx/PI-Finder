# Finder for PixInsight

Ein natives PixInsight-PCL-Modul mit einem kompakten Suchfenster für installierte
Prozesse und PJSR-Skripte.

## Bedienung

Finder steht im Menü unter **Process > Tricx > Finder**.

- Die Trefferliste erscheint erst, sobald man in das Suchfeld tippt.
- Die Treffer werden beim Tippen nach Prozess- bzw. Skriptnamen gefiltert und
  mit dem jeweiligen Prozess- bzw. Skript-Icon angezeigt. Die Trefferliste ist
  drei Zeilen hoch; weitere Treffer erreicht man durch Scrollen.
- Pfeiltasten (und Bild auf/ab) bewegen die Auswahl; Return oder Doppelklick
  startet den ausgewählten Prozess oder führt das Skript global aus.
- Das Suchfenster bleibt danach im Hintergrund geöffnet; die Sucheingabe wird
  geleert und die Trefferliste ausgeblendet. Escape leert ebenfalls nur das
  Suchfeld.
- Mit der Option **Minimize** schrumpft das Suchfenster auf eine schmale
  Suchzeile, sobald es den Fokus verliert. Ein Klick in die Suchzeile (oder ein
  erneuter Start von Finder) zeigt wieder das vollständige Suchfenster.
- Über das blaue Dreieck lässt sich eine Finder-Instanz auf den
  Arbeitsbereich ziehen; deren Ausführung (oder ein Doppelklick darauf) öffnet
  das Suchfenster. So geöffnet, schließt es sich nach dem Start eines Prozesses
  oder Skripts immer wieder.
- Die Liste wird beim ersten Öffnen aufgebaut; neu hinzugefügte Skripte
  erscheinen nach einem Neustart von PixInsight.
- Die Optionen **Open at startup**, **Close after launch** und **Minimize**
  werden über den Preferences-Knopf (Schraubenschlüssel) neben dem blauen
  Dreieck eingestellt.
- Mit der Option **Close after launch** schließt sich das Suchfenster, wenn ein
  Prozess oder Skript mit Return gestartet wird. Ohne die Option bleibt es
  geöffnet (mit **Minimize** als schmale Suchzeile).
- Mit der Option **Open at startup** öffnet sich das Suchfenster automatisch
  beim Start von PixInsight. Die Fensterposition wird über Sitzungen hinweg
  wiederhergestellt.

## Skriptordner

Das Modul durchsucht den standardmäßigen PixInsight-Skriptordner
`/Applications/PixInsight/src/scripts` sowie `~/PixInsight/Scripts` und
`~/Documents/PixInsight/Scripts`. Zusätzliche Skriptordner lassen sich über die
Umgebungsvariable `PIXINSIGHT_SCRIPT_PATHS` angeben; mehrere Ordner werden auf
macOS mit Doppelpunkten getrennt.

## Skript-Icons

Als Skript-Icon wird die per `#feature-icon` angegebene Datei (SVG, XPM, PNG, …)
angezeigt:

- Pfade ohne Präfix werden relativ zum Ordner des Skripts aufgelöst.
- Pfade mit dem Präfix `@script_icons_dir/` werden in
  `<PixInsight>/rsc/icons/script` gesucht, auch in Unterordnern.

Skripte ohne (ladbares) Icon erhalten ein generisches Dokument-Icon.

## Bauen

Fertige (unsignierte) Module für alle Plattformen erzeugt der
[Build-Workflow](.github/workflows/build.yml); sie lassen sich im Tab
*Actions* als Workflow-Artefakte herunterladen:

| Artefakt | Plattform |
|---|---|
| `Finder-macosx-arm64` | macOS, Apple Silicon |
| `Finder-macosx-x64` | macOS, Intel (auf Apple Silicon cross-kompiliert) |
| `Finder-linux-x64` | Linux x86_64 (Ubuntu 22.04, GCC 12) |
| `Finder-windows-x64` | Windows x64 (Visual Studio 2022) |

Der Workflow baut gegen das öffentliche PCL-Repository
(https://gitlab.com/pixinsight/PCL, standardmäßig `master`; beim manuellen
Start lässt sich eine andere Ref wählen).

### Lokal bauen

```sh
./build.sh                 # native Architektur
./build.sh --arch=x64      # macOS: Intel-Build auf Apple Silicon
./build.sh clean
```

- **macOS:** Xcode.
- **Linux:** GCC ≥ 12 und make.
- **Windows:** aus Git Bash mit Visual Studio 2022 (MSBuild).

PCL wird aus `build/PCL` verwendet. Fehlt der Ordner, werden Header und
Quellen aus der lokalen PixInsight-Installation kopiert (`--pi=<dir>` für einen
anderen Ort). Die PCL-Bibliotheken werden einmal pro Ziel nach
`build/PCL/lib/<platform>-<arch>` gebaut, das Modul landet in
`bin/<platform>-<arch>/`. Bei einer abweichenden PixInsight-Installation muss
der Standard-Skriptordner beim Kompilieren mit `PIXINSIGHT_SCRIPT_DIR`
überschrieben werden.

Dem öffentlichen PCL-Repository fehlt das Windows-Projekt der PCL-Bibliothek
selbst; [windows/vc17/PCL.vcxproj](windows/vc17/PCL.vcxproj) ersetzt es.

## Installieren

1. Modul signieren (PixInsight lädt nur signierte Module): `./sign.sh` signiert
   alle Module in `bin/` mit der im Skript eingetragenen Schlüsseldatei und
   fragt nach deren Passwort. Modul-Dateien lassen sich auch als Argumente
   angeben; `--pi=<dir>` wählt eine andere PixInsight-Installation.
2. In PixInsight: **Process > Modules > Install Modules…**, das Modul aus
   `bin/<platform>-<arch>/` wählen. Zwischen zwei Installationsversuchen
   empfiehlt sich ein Neustart von PixInsight.
3. Aufruf über **Process > Tricx > Finder**.

Das Modul hieß früher **ProcessSearch** (`ProcessSearch-pxm.dylib`) bzw.
**Search** (`Search-pxm.dylib`). Vor der Installation von `Finder-pxm.dylib`
muss ein solches altes Modul in PixInsight deinstalliert werden, sonst sind
beide Module gleichzeitig geladen. Die Einstellungen (Open at startup, Close
after launch, Minimize) und die Fensterposition werden unter dem neuen Namen
gespeichert und müssen einmal neu gesetzt werden.

## Lizenz

GPL-3.0, siehe [LICENSE](LICENSE).
