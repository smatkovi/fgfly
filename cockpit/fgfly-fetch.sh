#!/bin/sh
# Szenerie fuer einen Flugplatz holen und backen.  Laeuft als eigener Vorgang,
# waehrend die Anzeige weiterlaeuft, und schreibt seinen Stand in eine Datei,
# die der Simulator liest.
#
#   fgfly-fetch.sh <KENNUNG> <BREITE> <LAENGE>
ICAO=$1
LAT=$2
LON=$3
DATA=/home/user/MyDocs
NOTE=$DATA/fgfly-fetch.txt
LOG=$DATA/fgfly-fetch.log
PY=/opt/wunderw/bin/python3
[ -x "$PY" ] || PY=python3

trap 'echo "ABGEBROCHEN" > "$NOTE"; exit 1' INT TERM

mkdir -p "$DATA/kacheln" "$DATA/terrasync"
echo "LADE SZENERIE" > "$NOTE"
"$PY" /opt/fgfly/bin/fgfs-scenery --lat "$LAT" --lon "$LON" --radius 0.12 \
    --target "$DATA/terrasync" --subtrees Terrain --downloader python --concurrent 6 \
    > "$LOG" 2>&1

echo "BACKE" > "$NOTE"
n=0
for f in $(find "$DATA/terrasync/Terrain" -name '*.btg.gz'); do
    b=$(basename "$f" .btg.gz)
    [ -f "$DATA/kacheln/$b.fgb" ] && continue
    /opt/fgfly/bin/btgbake "$f" "$DATA/kacheln/$b.fgb" 200 \
        /opt/fgfly/share/materials.txt 1024 >> "$LOG" 2>&1
    n=$((n + 1))
    echo "BACKE $n" > "$NOTE"
done
echo "FERTIG $n" > "$NOTE"
