#!/bin/sh
# Registra con timestamp ogni cambiamento di _NET_DESKTOP_LAYOUT e del numero di desktop.
# Uso: watch-layout.sh FILE_LOG FILE_PGID
# Va avviato con setsid (vedi ~/Openbox-Desktop-Layout-Test.md, Fase B).
set -eu
log=${1:?uso: watch-layout.sh FILE_LOG FILE_PGID}
pgidfile=${2:?uso: watch-layout.sh FILE_LOG FILE_PGID}

# xprop -spy non vede atomi creati dopo il suo avvio: richiedi che esista.
if ! xprop -root _NET_DESKTOP_LAYOUT 2>/dev/null | grep -q '='; then
    echo "ERRORE: _NET_DESKTOP_LAYOUT non esiste ancora." >&2
    exit 1
fi

printf '%s\n' "$$" > "$pgidfile"   # setsid rende $$ capogruppo: si ferma con kill -- -PGID

stdbuf -oL xprop -spy -root \
    _NET_DESKTOP_LAYOUT _NET_NUMBER_OF_DESKTOPS 2>&1 |
while IFS= read -r line; do
    printf '%s | %s\n' "$(date --iso-8601=seconds)" "$line"
done >> "$log"
