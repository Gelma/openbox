#!/usr/bin/env bash
# Test the hard-coded 4x3 desktop grid inside a private Xvfb.
set -euo pipefail

REPO="/home/gelma/dev/prg/intelligenti/openbox"
OB="$REPO/openbox/openbox"
DISP=":97"
WORK="$(mktemp -d)"
export DISPLAY="$DISP"
export XDG_CONFIG_HOME="$WORK/config"
mkdir -p "$XDG_CONFIG_HOME/openbox"

# Hostile rc.xml: asks for 6 desktops in a 6x1 vertical layout
sed -e 's|<number>4</number>|<number>6</number>|' "$REPO/data/rc.xml" \
    > "$XDG_CONFIG_HOME/openbox/rc.xml"
python3 - "$XDG_CONFIG_HOME/openbox/rc.xml" <<'EOF'
import re, sys
p = sys.argv[1]; s = open(p).read()
s = re.sub(r'<layout>.*?</layout>', '', s, flags=re.S)
s = s.replace('<desktops>', '<desktops><layout><orientation>vertical</orientation>'
              '<columns>6</columns><rows>1</rows></layout>', 1)
open(p, 'w').write(s)
EOF

FAIL=0
cleanup() {
    kill "${OBPID:-}" "${XPID:-}" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

show() {
    local label="$1"
    local lay num
    sleep 0.7
    lay="$(xprop -root _NET_DESKTOP_LAYOUT)"
    num="$(xprop -root _NET_NUMBER_OF_DESKTOPS)"
    printf '%-38s | %s | %s\n' "$label" "$lay" "$num"
    if [[ "$lay" != *"= 0, 4, 3, 0" || "$num" != *"= 12" ]]; then
        echo "   ^^^ FAIL"
        FAIL=1
    fi
}

clientmsg_numdesk() {
    python3 - "$1" <<'EOF'
import sys
from Xlib import display, X
from Xlib.protocol import event
d = display.Display(); r = d.screen().root
a = d.intern_atom('_NET_NUMBER_OF_DESKTOPS')
ev = event.ClientMessage(window=r, client_type=a, data=(32, [int(sys.argv[1]), 0, 0, 0, 0]))
r.send_event(ev, event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
d.flush()
EOF
}

Xvfb "$DISP" -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XPID=$!
sleep 1

# Pre-existing hostile root properties before the WM starts
xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "1,2,3,0"
xprop -root -f _NET_NUMBER_OF_DESKTOPS 32c -set _NET_NUMBER_OF_DESKTOPS 6

"$OB" --sm-disable >"$WORK/ob.log" 2>&1 &
OBPID=$!
sleep 1.5
show "startup (rc=6 vert 6x1, root=6/2x3)"

xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,12,1,0"
show "pager writes 12x1"

xprop -root -remove _NET_DESKTOP_LAYOUT
show "pager deletes layout"

clientmsg_numdesk 4
show "client asks 4 desktops"

clientmsg_numdesk 20
show "client asks 20 desktops"

"$OB" --reconfigure
show "openbox --reconfigure"

"$OB" --restart
sleep 1.5
show "openbox --restart"

xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT "0,12,1,0"
show "pager writes 12x1 after restart"

echo "--- openbox log ---"
cat "$WORK/ob.log"
if [[ "$FAIL" -eq 0 ]]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; exit 1; fi
