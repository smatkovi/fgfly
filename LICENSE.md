# Lizenz

Dieser Baum steht unter der **GNU General Public License, Version 2** (oder
spaeter) - dieselbe wie FlightGear, aus dessen Daten die Tabellen hier
abgeleitet sind.

    Copyright (C) 2026 Sebastian Matkovich

    Dieses Programm ist freie Software: Sie koennen es weitergeben und/oder
    veraendern, gemaess den Bedingungen der GNU General Public License, wie von
    der Free Software Foundation veroeffentlicht, entweder Version 2 der
    Lizenz oder (nach Ihrer Wahl) jeder spaeteren Version.

    Die Weitergabe erfolgt in der Hoffnung, dass es nuetzlich ist, aber **ohne
    jede Gewaehrleistung**, sogar ohne die implizite Gewaehrleistung der
    Marktgaengigkeit oder der Eignung fuer einen bestimmten Zweck. Einzelheiten
    stehen in der GNU General Public License:
    <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>

## Woher die Daten kommen

Nichts davon ist hier erfunden, und alles stammt aus FlightGear (GPL-2.0):

| | |
|---|---|
| `bake/materials.txt` | Mittelwert je Materialtextur aus FGData (`matcolors.py`) |
| `bake/airports.txt` | aus FlightGears `apt.dat` ueber die fgview-Listen; Land ergaenzt (`aptcountry.py`) |
| `bake/aircraft.txt` | aus dem Hangar-Katalog `Aircraft-2020/catalog.xml` |
| `corpus-es2/`, `dump-es3/` | FlightGears eigene Shader, nach GLSL ES 1.00 umgeschrieben (Messung) |

Szenerie, Flugzeugmodelle und Texturen sind **nicht** dabei: die holt man sich
mit den mitgelieferten Werkzeugen aus TerraSync und dem Hangar.
