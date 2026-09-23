#!/bin/sh
# Copies the probe and a shader corpus to the N9/N950 and runs it there.
#
#   run-n9.sh <corpus-dir> [output-file]
#
# The PowerVR EGL stack talks to X, so the probe needs a display - it is run
# with DISPLAY=:0 against the running session.  N9_HOST/N9_USER pick the
# device (default is the N9 at 192.168.1.12; the N950 is .8).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
CORPUS=${1:?usage: run-n9.sh <corpus-dir> [output-file]}
REPORT=${2:-$HERE/build/sgxprobe-n9.txt}
SSH="$HERE/../nfsshift-sfos/tools/n9ssh.sh"
HOST=${N9_HOST:-192.168.1.12}
USER=${N9_USER:-user}
SCP="scp -oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -i $HOME/.ssh/id_rsa_n9"

[ -x "$HERE/build/n9/sgxprobe" ] || { echo "build/n9/sgxprobe missing - run build-n9.sh on the build machine" >&2; exit 1; }

sh "$SSH" 'rm -rf /home/user/sgxprobe && mkdir -p /home/user/sgxprobe/corpus'
$SCP "$HERE/build/n9/sgxprobe" "$USER@$HOST:/home/user/sgxprobe/"
$SCP "$CORPUS"/*.vert "$CORPUS"/*.frag "$USER@$HOST:/home/user/sgxprobe/corpus/"
mkdir -p "$(dirname "$REPORT")"
sh "$SSH" 'cd /home/user/sgxprobe && chmod +x sgxprobe && DISPLAY=:0 ./sgxprobe corpus 2>&1; echo "exit=$?"' \
    | tee "$REPORT"
echo "report: $REPORT"
