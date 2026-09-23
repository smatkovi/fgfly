# Backen: FlightGear-Szenerie für einen eigenen ES2-Renderer

Der Weg, für den wir uns entschieden haben: die Szenerie **vorher** in eine
Form bringen, die ein kleines OpenGL-ES-2-Programm auf dem N9 direkt hochladen
und zeichnen kann — so, wie NFS Shift seine Strecken mitbringt. Nicht
FlightGear auf dem Gerät, sondern FlightGears Daten.

`btgbake.py` ist der erste Schritt davon: Leser und Backofen für
`.btg.gz`-Kacheln.

    btgbake.py census <verzeichnis>      zählt über alle Kacheln darunter
    btgbake.py bake <kachel.btg.gz> [ziel.fgb]

## Warum überhaupt gebacken werden muss

FlightGear hält eine Kachel als **getrennte** Listen für Eckpunkte, Normalen
und Texturkoordinaten; ein Dreieck trägt drei Indizes, die in verschiedene
Listen zeigen. OpenGL kennt nur **einen** Index je Eckpunkt. Backen heißt
deshalb zuerst: die Tripel eindeutig machen, einen verschachtelten
Eckpunktpuffer bauen, je Material einen zusammenhängenden Indexbereich.

Das Format ist SimGears `SGBinObject` (`sg_binobj.cxx`): little endian, Kopf
`SG` plus Version, dann Objekte aus Eigenschaften und Elementen. Version 7
zählt und indiziert mit 16 Bit, ab Version 10 mit 32 — **welche Breite eine
v10-Datei benutzt, steht nicht drin**; der Leser probiert 32 Bit und wiederholt
mit 16, wenn ein Index aus der Eckpunktliste zeigt. Normalen stecken in je drei
Bytes, geschrieben als `(n + 1) * 127.5` und dabei abgeschnitten; beim Lesen
gehört das halbe Byte wieder drauf, sonst sind sie im Mittel ein Prozent zu
kurz (Median 0,991 statt 1,000).

## Gemessen: eine Kachel neben LOWW

`3220096.btg.gz`, Version 7:

| | |
|---|---|
| gelesen | 29 101 Eckpunkte, 28 736 Normalen, 30 436 Texturkoordinaten |
| Gruppen | 17 Materialien, **97 416 Dreiecke** |
| gebacken | 30 717 Eckpunkte (1,06×), 292 248 Indizes, **17 Zeichenaufrufe** |
| Bündel | 1,3 MB, 24 Bytes je Eckpunkt |

Gegengeprüft gegen die Quelle: der Erdradius am Mittelpunkt kommt auf
6 366 348 m (für 48° Nord richtig), der weiteste Eckpunkt liegt 10 435 m vom
Mittelpunkt, innerhalb des mitgelieferten Kachelradius von 11 647 m, jeder
Index zeigt in die Eckpunktliste, und die Indexbereiche der 17 Materialien
decken alle 292 248 Indizes lückenlos ab.

Die 17 Zeichenaufrufe sind der Punkt: FlightGear zeichnet dieselbe Kachel als
viele hundert Drawables — die Messung auf dem Jolla fand rund 1000 je Bild
(BEFUNDE P1), und die Zeit steckt nachweislich in der Zahl der Aufrufe, nicht
in der Füllrate (P2).

## Gemessen: der ganze Kachelsack um LOWW

`census` über `e016n48` (1° × 1°, der Sack, in dem LOWW liegt), 40 Kacheln:
**2 378 450 Dreiecke**, 688 182 Eckpunkte, 69 Materialien. Im Mittel 59 000
Dreiecke je Kachel, die größte 129 378 (`census-e016n48.txt`).

Wohin die Dreiecke gehen:

| Material | Dreiecke | Anteil |
|---|---|---|
| DryCrop | 442 639 | 18,6 % |
| Railroad | 337 284 | 14,2 % |
| Town | 324 927 | 13,7 % |
| Freeway | 270 588 | 11,4 % |
| Road | 246 197 | 10,4 % |
| DeciduousForest | 189 180 | 8,0 % |
| übrige 63 | 567 635 | 23,7 % |

**Die Hälfte der Geometrie sind Bänder**: Bahn, Autobahn, Straße, dazu die
Ortsflächen. Die sähe ein gebackenes Bild als *Pixel* statt als Dreiecke — aber
Vorsicht: diese Bänder liegen nicht auf dem Gelände obendrauf, sie sind in die
Triangulierung **hineingeschnitten**. Lässt man ihre Materialgruppen einfach
weg, bleiben Löcher. Die Dreiecke zurückzugewinnen heißt, sie mit den
Nachbarflächen zu verschmelzen und neu zu vernetzen — ein eigener Schritt, kein
Nebenertrag des Bildes.

Die zwei Kacheln der Version 10 im Sack sind erheblich dichter: `3220105`
allein trägt 378 278 Dreiecke in 22 Materialien (115 222 gebackene Eckpunkte,
7,0 MB, 32-Bit-Indizes). Auch sie ist gegengeprüft — Erdradius 6 366 302 m,
weitester Eckpunkt 10 419 m bei 11 614 m Kachelradius, alle Indizes in der
Liste.

Eine Kachel ist rund 16 × 14 km; bei brauchbarer Sicht liegen einige im Bild,
also grob 250 000 bis 500 000 Dreiecke je Bild, ungebacken. Was die SGX530
davon in einem Bild schafft, ist noch nicht gemessen — das braucht einen
Zeichentest auf dem Gerät, nicht bloß die Übersetzung der Shader.

## `btgbake.c` — derselbe Backofen, für das Gerät (22.09.2026)

Die Python-Fassung ist die lesbare; die C-Fassung ist die, die auf dem Telefon
läuft. **Ihre Ausgabe ist Byte für Byte dieselbe** — das ist die Probe, dass
die Umsetzung stimmt, und `cmp` sagt es nach jedem Lauf.

Auf dem Baurechner, beide Kacheln:

| | Python | C | |
|---|---|---|---|
| `3220096`, 97 k Dreiecke | 0,32 s | **0,018 s** | 18× |
| `3220105`, 378 k Dreiecke | 1,23 s | **0,071 s** | 17× |
| Spitzenspeicher (dichte Kachel) | **1,29 GB** | **92 MB** | 14× |

**Und auf der N950 selbst**, mit 1 GB Arbeitsspeicher im ganzen Gerät:

| | Zeit | Spitzenspeicher |
|---|---|---|
| `3220096` (v7, 97 k Dreiecke) | **1,5 s** | 24 MB |
| `3220105` (v10, 378 k Dreiecke) | **6,6 s** | 90 MB |

Die selbst gebackene Kachel ist bitgleich mit der vom Jolla, und das Cockpit
fliegt darüber mit 32 Bildern je Sekunde. Damit ist die Kette geschlossen:
**das Gerät kann seine Szenerie selbst backen.** Eine 1-Grad-Schachtel aus 40
Kacheln wären rund zwei bis drei Minuten — einmal, beim Herunterladen, und
danach kann die `.btg.gz` weg.

Was in C anders ist als in Python: die Ecken merken sich ihr Material in einem
Byte je Ecke (dasselbe Material kann in mehreren Objekten vorkommen, und im
Bündel gehören seine Dreiecke zusammen), die Tripel gehen in eine offene
Streuwerttabelle, und der Spitzenspeicher steht am Ende der Ausgabe — er ist
hier schließlich der Punkt.

    build-n9.sh          baut für die N950 (braucht nur zlib aus dem Sysroot)
    gcc -O2 -o btgbake btgbake.c -lz -lm      für den Rechner

Eine Falle: `clock_gettime` liegt bei Harmattans glibc 2.10 noch in `librt`,
also `-lrt`.

## Ausdünnen (22.09.2026)

    btgbake <kachel.btg.gz> <ziel.fgb> [zellgroesse_m]

Mit einer Zellgröße schreibt der Backofen zusätzlich `<ziel>.lod.fgb`: die
Eckpunkte werden auf ein Gitter zusammengezogen (alles in derselben Zelle wird
ein Eckpunkt, der Mittelwert), Dreiecke mit zwei gleichen Ecken fallen weg.
Grob, aber genau das, was aus der Ferne niemand sieht — und es braucht einen
Durchgang und eine Streuwerttabelle, keine Kantenbewertung.

Kachel `3220096`, 97 416 Dreiecke:

| Zelle | Dreiecke | weg | Datei |
|---|---|---|---|
| 100 m | 25 594 | 74 % | 463 KB |
| **200 m** | **13 263** | **86 %** | 239 KB |
| 400 m | 4 634 | 95 % | 83 KB |
| 800 m | 1 353 | 99 % | 24 KB |

Der Renderer nimmt `X.lod.fgb` automatisch als grobe Fassung von `X.fgb` und
schaltet nach Entfernung um (`COCKPIT_LOD`, Vorgabe 8 km).

## Das Bild je Kachel (22.09.2026)

    matcolors.py <fgdata> materials.txt
    btgbake <kachel.btg.gz> <ziel.fgb> [zelle_m] [farbtafel.txt] [bildgroesse]

`matcolors.py` nimmt jedem Material die Farbe, die FlightGear ihm ohnehin gibt:
den Mittelwert über die Textur, die es benutzt. 164 Materialien mit Farbe in
3,9 KB — `EvergreenForest 23 44 14`, `Lake 44 63 74`, `DryCrop 92 97 63`.
Der PNG-Leser darin kommt ohne fremde Bibliothek aus (zlib plus Entfilterung,
Farbtyp 2 und 6).

Der Backofen malt die Kachel damit einmal von oben in ein Bild
(`<ziel>.tex`, RGB565 mit kleinem Kopf): 512 × 512 sind 36 m je Bildpunkt und
512 KB. Gezeichnet wird von hinten nach vorn — die Landbedeckung steht in der
`.btg` **hinter** den Bändern, also muss sie zuerst, damit Straßen und Bäche
obenauf liegen. Entartete Dreiecke (und davon sind Straßen fast nur gemacht)
werden als Strich gezogen statt gefüllt, sonst verschwänden sie.

`kachelbild.png` ist so ein Bild: Wälder, Felder, Straßen als Striche, die
Donau am unteren Rand.

Zwei Dinge, die das Bild einbringt, und beide zählen mehr als das Aussehen:

* **Die grobe Fassung sieht aus wie die feine.** Das Bild hängt am Ort, nicht
  an den Dreiecken — 5 % der Geometrie genügen, die Landschaft bleibt dieselbe.
* **Ein Zeichenaufruf je Kachel** statt einer je Material (17 bis 31).

## Das Bündelformat `.fgb`

    "FGB1", uint32 flags (Bit 0: 32-Bit-Indizes)
    double  center[3]          Mittelpunkt der Kachel, erdfest
    float   radius
    uint32  vertices, indices, groups
    uint32  namebytes, dann die Materialnamen, je mit \0
    je Gruppe: uint32 start, count
    je Eckpunkt: float pos[3] (zum Mittelpunkt), int8 normal[3], 1 Byte Füllung,
                 float uv[2]                                   = 24 Bytes
    dann die Indizes

Bewusst so, dass `glBufferData` die Bytes unverändert nimmt.

## Flugplatzbeläge und Länder (23.09.2026)

**Der Asphalt fehlte in der Farbtafel.** `matcolors.py` hat aus FGData zwar
`pa_shoulder` und die aufgemalten Linien (`lf_*`) mitgenommen, aber nicht
`pa_taxiway` und `pc_taxiway` — also genau die Materialien, aus denen Bahn und
Rollweg bestehen. `palette_lookup` färbt unbekannte Namen als Wiese, und damit
wurde die Bahn als Wiese gebacken. Zwei Änderungen:

* `materials.txt` hat die Beläge jetzt von Hand (73/70/67 für Asphalt,
  155/156/153 für Beton, wie die verwandten `*_shoulder`);
* `btgbake.c` entscheidet bei einem unbekannten Namen nach der **Familie**
  (`pa_` Asphalt, `pc_` Beton, `lf_` Markierung) — es gibt Dutzende solcher
  Materialien, jede Ziffer einer Bahnkennung ist eines.

Die Kachel eines Flugplatzes zeichnet der Renderer seit demselben Tag ohne
Bild, also nach Materialgruppen: 21 636 Dreiecke sind billig, und das Bild hat
über fünf Kilometer rund fünf Meter je Bildpunkt — da ist die Bahn neun Punkte
breit. Schon gebackene Kacheln müssen dafür **nicht** neu gebacken werden.

**`airports.txt` hat eine Spalte mehr: das Land.**

    Kennung Breite Laenge Hoehe_ft Art Bahnlaenge_m Bahnrichtungen Land Name
    LOWW 48.12300 16.53300 600 0 3599 110,160,290,340 OESTERREICH WIEN SCHWECHAT

    aptcountry.py airports.txt [~/fgfs-work/fgview/airports]

`aptcountry.py` trägt sie nach: welches Land, steht in der fgview-JSON, in der
die Kennung vorkommt (dort hat `make-airports.py` die Plätze nach der
ICAO-Vorsilbe schon einmal sortiert). Alle 23 529 Zeilen haben eines bekommen,
keine blieb übrig. Die Namen sind deutsch, in Großbuchstaben ohne Umlaute
(`OESTERREICH`, `TUERKEI`), Leerzeichen als Unterstrich — die Schrift im
Cockpit ist ein 5×7-Raster und kennt nur ASCII; lange Namen sind auf gut
zwanzig Zeichen gekürzt, weil eine Zeile nicht breiter wird.

Der Leser im Cockpit erkennt eine alte Datei ohne die Spalte daran, dass an
der Stelle kein Wort aus Großbuchstaben steht — dann fängt dort der Name an.

## Was als Nächstes ansteht

1. **Textur.** Jedes Material zeigt auf genau eine Textur mit einer Weltgröße
   (`Materials/dds/global-summer.xml`: `DeciduousForest` →
   `Terrain/mixedforest1c.dds`, 2000 × 2000 m), die Koordinaten wiederholen
   sich. Ein Atlas verträgt keine Wiederholung — der Ausweg ist, je Kachel
   **ein** Bild zu backen (FlightGear kennt das als Orthophoto; die Shader
   haben schon ein `orthophotoTexCoord`-Attribut). Dann bleibt ein
   Zeichenaufruf je Kachel und ein Shader ohne Materialwissen.
   Das ist ein **Rendervorgang**: die Kachel von oben, mit den
   Materialtexturen. Dafür den PNG-Satz aus `Materials/regions` nehmen, nicht
   `dds/` — sonst muss der Backofen DXT auspacken. Höchstens 2048 × 2048, mehr
   nimmt die SGX530 nicht; auf dem Gerät als PVRTC, 2048² mit 4 bpp sind 2 MB
   je Kachel, und das ist das Argument dafür.
2. **Dreiecke reduzieren.** 97 000 je Kachel sind für die SGX530 zu viel;
   Zusammenfassen nach Entfernung (LOD) gehört in denselben Durchlauf.
3. **Der Renderer.** ES2, ein Shader, Bündel laden, Kacheln nach Entfernung
   zeichnen — zuerst auf dem Jolla gegen dasselbe EGL-Pbuffer-Gerüst wie
   `sgxprobe`, dann auf dem N9.

Kleinigkeit beim Lesen: 145 der 28 736 Normalen einer Kachel sind auch nach der
Korrektur zu kurz (bis hinunter zu 0,39) — das steckt so in den Quelldaten. Der
Shader normalisiert sie, dann spielt es keine Rolle.

## Was der Renderer zeigen soll

Sebastians Umfang, festgehalten am 22.09.2026:

* **Cockpit nur in 2D.** Kein Cockpitmodell im Bild, keine Innenansicht aus
  Dreiecken — eine Instrumententafel über der Außenansicht, wie die
  Panel-Ansicht der frühen FlightGear-Fassungen. Damit entfällt der gesamte
  Innenraum eines Flugzeugmodells: bei der c172p sind das die AC3D-Modelle,
  deren Zifferblätter uns auf dem SFOS-Port die `GL_QUADS`-Jagd eingebracht
  haben.
* **Instrumente: nur fünf** — Fluglageanzeiger, Höhenmesser, Variometer,
  Fahrtmesser, Drehzahlmesser. Alles andere fällt weg. Damit braucht es weder
  FlightGears Panel-Maschinerie noch die Canvas-Anzeigen: fünf 2D-Zifferblätter
  über dem Bild, eine Handvoll Zeichenaufrufe.
* **Standardmäßig aus:** Bäume, Wolken, Texturfilterung, Partikel, Gebäude,
  KI-Flugzeuge.

Das trifft genau die teuren Posten der Jolla-Messung: Bäume waren dort rund
60 % aller Drawables je Bild (BEFUNDE: 2404 von ~4000; ohne sie 68,5 → 45,3 ms),
Wolken weitere 5 %. Und es nimmt den drei Programmen, die auf der SGX530
voraussichtlich nicht binden, ihre Aufgabe — sie zeichnen Gelände mit
Materialmischung, die es nach dem Backen nicht mehr gibt.

## Weiche Übergänge (23.09.2026)

X-Plane blendet zwischen zwei Geländearten mit einer zweiten Textur als Rampe
(`#if BORDER` in seinem `terrain.glsl`: `4.0 * (ramp_alpha - tex.a)`) —
deshalb hört dort kein Feld an einer geraden Linie auf. Wir haben statt
Einzeltexturen **ein** gebackenes Bild je Kachel, also machen wir die Grenzen
**im Bild** weich:

1. erst die Landbedeckung malen (Wald, Feld, Wiese, Ort),
2. **zweimal einen gewichteten 3×3-Kasten darüber** — bei 36 m je Bildpunkt
   ist das ein Saum von gut hundert Metern,
3. **dann erst** die Bänder: Straßen, Bahnen, Bäche, Flugplatzbeläge. Die
   bleiben scharf, denn ein Fluss hört sehr wohl an einer Linie auf.

Was ein Band ist, entscheidet `is_band()` am Materialnamen (`Road`, `Freeway`,
`Railroad`, `Stream`, `Canal`, `pa_`, `pc_`, `lf_`, `rwy`, …).

`fgfly-rebake.sh` backt alles noch einmal, was schon geholt ist — der Holer
selbst überspringt, was daliegt, und nach einer Änderung am Backofen muss man
eben alles neu machen. Auf der N950 sind das ein paar Minuten.
