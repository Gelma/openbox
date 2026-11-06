#!/usr/bin/env bash
# Navigation test: after disturbing the layout/desktop properties, the
# GoToDesktop up/down/left/right keys must still move on a 4x3 grid.
set -euo pipefail

REPO="/home/gelma/dev/prg/intelligenti/openbox"
OB="${OB:-$REPO/openbox/openbox}"
DISP=":98"
WORK="$(mktemp -d)"
export DISPLAY="$DISP"
export XDG_CONFIG_HOME="$WORK/config"
mkdir -p "$XDG_CONFIG_HOME/openbox"
cp "$REPO/data/rc.xml" "$XDG_CONFIG_HOME/openbox/rc.xml"

FAIL=0
cleanup() { kill "${OBPID:-}" "${XPID:-}" 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT

cur() { xprop -root _NET_CURRENT_DESKTOP | awk '{print $3}'; }

key() {   # key <Keysym>  -> presses Ctrl+Alt+<Keysym> via XTEST
    python3 - "$1" <<'EOF'
import sys, time
from Xlib import display, X, XK
from Xlib.ext import xtest
d = display.Display()
codes = [d.keysym_to_keycode(XK.string_to_keysym(k))
         for k in ("Control_L", "Alt_L", sys.argv[1])]
for c in codes: xtest.fake_input(d, X.KeyPress, c)
for c in reversed(codes): xtest.fake_input(d, X.KeyRelease, c)
d.sync()
EOF
    sleep 0.4
}

goto() {
    python3 - "$1" <<'EOF'
import sys
from Xlib import display, X
from Xlib.protocol import event
d = display.Display(); r = d.screen().root
a = d.intern_atom('_NET_CURRENT_DESKTOP')
r.send_event(event.ClientMessage(window=r, client_type=a,
             data=(32, [int(sys.argv[1]), 0, 0, 0, 0])),
             event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
d.flush()
EOF
    sleep 0.4
}

numdesk() {
    python3 - "$1" <<'EOF'
import sys
from Xlib import display, X
from Xlib.protocol import event
d = display.Display(); r = d.screen().root
a = d.intern_atom('_NET_NUMBER_OF_DESKTOPS')
r.send_event(event.ClientMessage(window=r, client_type=a,
             data=(32, [int(sys.argv[1]), 0, 0, 0, 0])),
             event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
d.flush()
EOF
    sleep 0.4
}

check() {  # check <label> <from> <key> <expected>
    local got
    goto "$2"; key "$3"; got="$(cur)"
    printf '  %-28s from %-2s %-5s -> %-2s (expected %s)' "$1" "$2" "$3" "$got" "$4"
    if [[ "$got" == "$4" ]]; then echo "  ok"; else echo "  FAIL"; FAIL=1; fi
}

navset() {
    echo "== $1 | $(xprop -root _NET_DESKTOP_LAYOUT) | $(xprop -root _NET_NUMBER_OF_DESKTOPS)"
    check "$1" 0 Down 4
    check "$1" 4 Down 8
    check "$1" 5 Up 1
    check "$1" 5 Right 6
    check "$1" 3 Right 3
}

Xvfb "$DISP" -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XPID=$!
sleep 1
"$OB" --sm-disable >"$WORK/ob.log" 2>&1 &
OBPID=$!
sleep 1.5

navset "startup"

xprop -root -remove _NET_DESKTOP_LAYOUT; sleep 0.3
xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,4,3,0"
navset "layout deleted then 4x3"

numdesk 11; numdesk 12
navset "desktops 11 then 12"

xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,12,1,0"
xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,4,3,0"
navset "layout 12x1 then 4x3"

xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,5,3,0"
navset "invalid layout 5x3"

"$OB" --reconfigure; sleep 1
navset "after --reconfigure"

if [[ "$FAIL" -eq 0 ]]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; exit 1; fi
