#!/bin/sh
# Ferma il monitor uccidendo l'intero gruppo di processi.
# Uso: stop-watch.sh [FILE_PGID]
set -eu
pgidfile=${1:-"$HOME/openbox-layout-log/monitor.pgid"}
[ -f "$pgidfile" ] || { echo "nessun file $pgidfile" >&2; exit 1; }
pgid=$(cat "$pgidfile")
kill -- -"$pgid" 2>/dev/null || true
rm -f "$pgidfile"
echo "monitor fermato (pgid $pgid)"
