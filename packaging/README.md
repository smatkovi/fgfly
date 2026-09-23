# Das Paket

    build-deb.sh [--version 0.1.0]     ->  build/fgfly_0.1.0_armel.deb (75 KB)

Drin sind nur eigene Programme — Renderer, Backofen, Umsetzer — und kein
einziges Byte Szenerie oder Flugzeug: beides holt man sich mit den
mitgelieferten Werkzeugen und legt es nach MyDocs.

    /opt/fgfly/bin/cockpit            der Simulator
    /opt/fgfly/bin/btgbake            der Backofen
    /opt/fgfly/bin/fgfs-scenery       TerraSync holen (braucht Python 3)
    /opt/fgfly/bin/acftconv.py        Flugmodell aus JSBSim oder YASim
    /opt/fgfly/bin/acbake.py          3D-Modell aus AC3D
    /opt/fgfly/bin/matcolors.py       Materialfarben aus FGData
    /opt/fgfly/share/materials.txt
    /opt/fgfly/bin/fgfly-start.sh     Starter
    /usr/share/applications/fgfly.desktop
    /usr/share/themes/base/meegotouch/icons/fgfly-80.png
    /usr/share/icons/hicolor/80x80/apps/fgfly.png

Gebaut wird das .deb mit `mkdeb.py` aus dem NFS-Shift-Port — dpkg-deb gibt es
auf keinem der beteiligten Rechner, und GNU ar schreibt Mitgliedsnamen mit
angehängtem Schrägstrich, was Harmattans dpkg 1.15 nie gesehen hat.

## Das Icon

Es ist das FlightGear-Icon, das wir für Sailfish gezeichnet haben, umgeschnitten
auf **exakt** die Silhouette der Standard-Apps:

    ~/ps/meego-icon-tool/squircle.py --out . \
        --fill /usr/share/icons/hicolor/172x172/apps/harbour-fgview.png

Gegengeprüft: der Alphakanal des Ergebnisses ist mit dem des Stock-Icons
**bildpunktgleich** — 82,5 % Deckung, null abweichende Punkte von 6400.

**Und eine Falle, die ein rotes Rechteck erzeugt:** der Harmattan-Starter
sucht nicht im hicolor-Thema. Das Icon muss als `<name>-80.png` unter
`/usr/share/themes/base/meegotouch/icons/` liegen, und die `.desktop`-Datei
muss den **vollen Pfad** in `Icon=` nennen, nicht den Namen. Sonst steht auf
dem Startbildschirm der rote Platzhalter.

Das 64er Icon geht zusätzlich base64 als `Maemo-Icon-26` ins control, damit es
auch der Paketmanager zeigt.

`startseite.png` zeigt es zwischen den anderen Apps.

## Eine Falle, die einen ganzen Umweg gekostet hat (23.09.2026)

Gebaut wird auf dem Baurechner, gepackt wird hier — und dazwischen muss das
Programm **abgeholt** werden. Einmal ist genau das ausgeblieben: das Paket
0.3.5 trug ein `cockpit` von drei Änderungen vorher, also ohne die Finger
über X, und am Gerät änderte sich nichts, obwohl „neu installiert“ dastand.

`build-deb.sh` prüft deshalb jetzt, ob eine Quelle jünger ist als
`cockpit/build/cockpit`, und packt dann nicht:

    Quellen sind juenger als cockpit/build/cockpit:
      /home/.../cockpit/xtouch.c
    erst build-n9.sh laufen lassen und das Ergebnis herholen

Zum Gegenprüfen, was wirklich im Paket liegt:

    python3 packaging/mkdeb.py --info build/fgfly_0.3.6_armel.deb
