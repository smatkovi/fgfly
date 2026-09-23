#!/bin/sh
# Alle schon geholten Kacheln noch einmal backen - mit dem Backofen, der
# gerade installiert ist.
#
# Der Holer ueberspringt, was schon daliegt; nach einer Aenderung am Backofen
# (weiche Uebergaenge, neue Farben) muss man aber alles neu machen.  Das hier
# dauert auf der N950 ein paar Minuten und laesst sich jederzeit abbrechen -
# fertige Kacheln bleiben liegen.
DATA=/home/user/MyDocs
NOTE=$DATA/fgfly-fetch.txt
LOG=$DATA/fgfly-backen.log
SIZE=${1:-1024}

trap 'echo "ABGEBROCHEN" > "$NOTE"; exit 1' INT TERM

: > "$LOG"
n=0
for f in $(find "$DATA/terrasync/Terrain" -name '*.btg.gz'); do
    b=$(basename "$f" .btg.gz)
    /opt/fgfly/bin/btgbake "$f" "$DATA/kacheln/$b.fgb" 200 \
        /opt/fgfly/share/materials.txt "$SIZE" >> "$LOG" 2>&1
    n=$((n + 1))
    echo "BACKE $n" > "$NOTE"
done
echo "FERTIG $n" > "$NOTE"
echo "$n Kacheln neu gebacken"
