#!/bin/sh
# Starter fuer FG Fly.
#
# Die Daten liegen auf MyDocs (dort sind Gigabytes frei, auf /home nicht),
# die Programme in /opt - MyDocs ist noexec eingehaengt.  Python 3 kommt aus
# /opt/wunderw, falls es da ist; die Umsetzer brauchen es, der Simulator nicht.
export PATH=/opt/fgfly/bin:/opt/wunderw/bin:$PATH
export COCKPIT_DATA=${COCKPIT_DATA:-/home/user}
exec /opt/fgfly/bin/cockpit "$@"
