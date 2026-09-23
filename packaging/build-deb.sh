#!/bin/sh
# Baut fgfly_<version>_armel.deb fuer die Nokia N9/N950.
#
# Im Paket stecken nur eigene Programme: der Renderer, der Backofen und die
# Umsetzer.  Szenerie und Flugzeuge kommen nicht mit - die holt man sich mit
# den mitgelieferten Werkzeugen und legt sie nach MyDocs.
#
#   packaging/build-deb.sh [--version 0.1.0]
#
# Erwartet die gebauten ARM-Programme unter cockpit/build/ und bake/build/.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VERSION=0.1.0
[ "$1" = "--version" ] && VERSION=$2

STAGE=$ROOT/build/deb
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/opt/fgfly/bin" "$STAGE/opt/fgfly/share" \
         "$STAGE/usr/share/applications" "$STAGE/usr/share/icons/hicolor/80x80/apps" \
         "$STAGE/usr/share/themes/base/meegotouch/icons"

for f in "$ROOT/cockpit/build/cockpit" "$ROOT/bake/build/btgbake"; do
    [ -f "$f" ] || { echo "fehlt: $f - erst build-n9.sh laufen lassen" >&2; exit 1; }
    cp "$f" "$STAGE/opt/fgfly/bin/"
done
cp "$HERE/fgfly-start.sh" "$HERE/fgfly-fetch.sh" "$HERE/fgfly-aircraft.sh" \
   "$STAGE/opt/fgfly/bin/"
cp "$ROOT/bake/acbake.py" "$ROOT/bake/matcolors.py" "$ROOT/cockpit/acftconv.py" \
   "$STAGE/opt/fgfly/bin/"
cp "$HOME/fgfs-work/fgfs-scenery" "$STAGE/opt/fgfly/bin/" 2>/dev/null || true
cp "$ROOT/bake/materials.txt" "$ROOT/bake/airports.txt" "$ROOT/bake/aircraft.txt" \
   "$STAGE/opt/fgfly/share/"
cp "$HERE/fgfly.desktop" "$STAGE/usr/share/applications/"
cp "$HERE/icon-80.png" "$STAGE/usr/share/icons/hicolor/80x80/apps/fgfly.png"
# Der Harmattan-Starter sucht nicht im hicolor-Thema, sondern genau hier -
# und die .desktop-Datei nennt den vollen Pfad.  Ohne das bleibt ein rotes
# Rechteck stehen.
cp "$HERE/icon-80.png" "$STAGE/usr/share/themes/base/meegotouch/icons/fgfly-80.png"
chmod 755 "$STAGE/opt/fgfly/bin/"*

# Das 64er Icon geht base64 ins control - so zeigt es auch der Paketmanager.
ICON=$(python3 -c "
import base64, textwrap
raw = base64.b64encode(open('$HERE/icon-64.png','rb').read()).decode()
print('\n'.join(' ' + line for line in textwrap.wrap(raw, 76)))
")
python3 - "$HERE/control.in" "$STAGE/DEBIAN/control" "$VERSION" <<'PY'
import sys
src, dst, version = sys.argv[1:4]
icon = sys.stdin.read() if False else None
text = open(src).read().replace("@VERSION@", version)
open(dst, "w").write(text)
PY
python3 - "$STAGE/DEBIAN/control" "$VERSION" <<PY
import sys
path, version = sys.argv[1], sys.argv[2]
text = open(path).read().replace("@ICON@", """$ICON""")
open(path, "w").write(text)
PY

python3 "$HERE/mkdeb.py" "$STAGE" "$ROOT/build/fgfly_${VERSION}_armel.deb"
ls -la "$ROOT/build/fgfly_${VERSION}_armel.deb"
