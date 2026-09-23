#!/bin/sh
# Ein Flugzeug aus FlightGears Hangar holen und umsetzen.
#
#   fgfly-aircraft.sh <kennung> <url>
#
# Geladen wird mit Python (das Geraet hat kein unzip und kein TLS 1.2 im
# System-curl), umgesetzt mit acftconv.py und acbake.py.  Uebrig bleiben das
# Flugmodell und das Modell - ein paar hundert Kilobyte statt zwanzig Megabyte;
# das Hangar-Paket wird danach geloescht.
ID=$1
URL=$2
HOME_DIR=/home/user
WORK=/home/user/MyDocs/fgfly-acft
NOTE=/home/user/MyDocs/fgfly-fetch.txt
LOG=/home/user/MyDocs/fgfly-acft.log
PY=/opt/wunderw/bin/python3
[ -x "$PY" ] || PY=python3

# Der Simulator kann abbrechen (er schiesst die ganze Prozessgruppe ab) -
# dann soll kein halbes Paket liegenbleiben.
trap 'rm -rf "$WORK"; echo "ABGEBROCHEN" > "$NOTE"; exit 1' INT TERM

rm -rf "$WORK"
mkdir -p "$WORK"
echo "LADE $ID" > "$NOTE"
"$PY" - "$URL" "$WORK/paket.zip" <<'PYEOF' >> "$LOG" 2>&1
import sys, urllib.request
url, out = sys.argv[1], sys.argv[2]
with urllib.request.urlopen(url, timeout=120) as r, open(out, "wb") as f:
    while True:
        chunk = r.read(262144)
        if not chunk:
            break
        f.write(chunk)
PYEOF
[ -s "$WORK/paket.zip" ] || { echo "FEHLER LADEN" > "$NOTE"; exit 1; }

echo "PACKE AUS" > "$NOTE"
"$PY" - "$WORK/paket.zip" "$WORK" <<'PYEOF' >> "$LOG" 2>&1
import sys, zipfile
zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])
PYEOF

# Die Spielart, die gewaehlt wurde: ein Paket wie A320-family enthaelt fuenf
# `-set.xml`, und darin steht, welches Flugmodell und welches Modell dazu
# gehoeren.  Ohne das erwischt man irgendeine - und als Modell `empty.ac`.
SET=$(find "$WORK" -maxdepth 3 -name "$ID-set.xml" | head -1)
[ -n "$SET" ] || SET=$(find "$WORK" -maxdepth 3 -name '*-set.xml' | head -1)
[ -n "$SET" ] || { echo "KEIN FLUGZEUG" > "$NOTE"; exit 1; }

echo "FLUGMODELL" > "$NOTE"
"$PY" /opt/fgfly/bin/acftconv.py "$SET" "$HOME_DIR/$ID.fdm" >> "$LOG" 2>&1

echo "MODELL" > "$NOTE"
"$PY" /opt/fgfly/bin/acbake.py "$SET" "$HOME_DIR/$ID.model.fgb" >> "$LOG" 2>&1

rm -rf "$WORK"
if [ -f "$HOME_DIR/$ID.fdm" ]; then
    echo "FERTIG $ID" > "$NOTE"
else
    echo "FEHLER UMSETZEN" > "$NOTE"
fi
