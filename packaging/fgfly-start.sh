#!/bin/sh
# Starter fuer FG Fly.
#
# Die Daten liegen auf MyDocs (dort sind Gigabytes frei, auf /home nicht),
# die Programme in /opt - MyDocs ist noexec eingehaengt.  Python 3 kommt aus
# /opt/wunderw, falls es da ist; die Umsetzer brauchen es, der Simulator nicht.
export PATH=/opt/fgfly/bin:/opt/wunderw/bin:$PATH
export COCKPIT_DATA=${COCKPIT_DATA:-/home/user}
# Was der Simulator sagt, landet in einer Datei - aus der Ferne ist sonst
# nicht zu sehen, was eine Geste oder ein Tipp bewirkt hat.  Bei jedem Start
# faengt sie neu an; COCKPIT_DEBUG=1 schreibt Sicht, Geste und Fingerzahl dazu.
exec /opt/fgfly/bin/cockpit "$@" > /home/user/MyDocs/fgfly.log 2>&1
