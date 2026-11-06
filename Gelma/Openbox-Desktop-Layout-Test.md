# Procedura — chi perde o riscrive `_NET_DESKTOP_LAYOUT`?

Diagnosi in campo della perdita della griglia 4×3 (12 desktop virtuali) su Openbox.

> **File deliberatamente fuori dal repository**: non va committato e non fa parte della patch.
> I comandi sono stati provati su un Xvfb privato con `xprop` 1.2.7 (Debian).
>
> Contesto: la patch `openbox: keep and publish the configured desktop grid` (branch `gelma`)
> rende la griglia stabile quando in `rc.xml` c'è il blocco `<layout>`. Questa procedura serve a
> capire **chi**, nel setup originale, cancellava o sovrascriveva la proprietà: l'informazione
> resta utile per i pannelli/pager e per il rischio obconf.

---

## 0. Cosa si vuole scoprire

X11 **non registra chi scrive una proprietà**. Quello che si può fare è:

1. registrare con precisione **quando** e **con quale valore** la proprietà cambia o sparisce;
2. correlare l'istante con i processi/eventi della sessione (pannello riavviato, `obconf`,
   `xrandr`, cambio del numero di desktop, sospensione…);
3. restringere per bisezione l'insieme dei componenti finché resta un solo candidato.

La procedura seguente fa esattamente questo, senza installare nulla (`xdotool` non serve).

---

## 1. Riferimenti rapidi

Proprietà EWMH sulla root window: `_NET_DESKTOP_LAYOUT, CARDINAL[4]/32`

| Campo | Valori |
|-------|--------|
| `[0]` orientamento | `0` = orizzontale, `1` = verticale |
| `[1]` colonne | 4 nella griglia voluta |
| `[2]` righe | 3 nella griglia voluta |
| `[3]` angolo di partenza | `0` top-left, `1` top-right, `2` bottom-right, `3` bottom-left |

Valori di riferimento (12 desktop):

| Situazione | `_NET_DESKTOP_LAYOUT` | Layout interno |
|------------|-----------------------|----------------|
| griglia voluta | `0, 4, 3, 0` | 3 righe × 4 colonne |
| striscia (difetto) | `0, 12, 1, 0` | 1 riga × 12 colonne |
| proprietà assente | `not found` | con la patch: resta l'ultima griglia valida |
| atomo mai creato | `no such atom on any window` | — |

Altri valori utili: `_NET_NUMBER_OF_DESKTOPS = 12`, `_NET_CURRENT_DESKTOP` = indice 0..11.

File e percorsi del setup di riferimento (`testnec`):

- `~/.config/openbox/rc.xml` — `<number>12</number>`, keybind `W-F1..F12`, `W-C-d`, `popupTime` 875;
- `~/.config/openbox/autostart` — lancia lo script che scrive la proprietà;
- `~/.cache/openbox/openbox.log` — log del WM (spesso contiene solo il menu mancante).

Scorciatoie utili per la verifica visiva (da `rc.xml`): `Ctrl+Alt+Left/Right/Up/Down`
(`GoToDesktop`, `wrap="no"`) e `W-F1..F12` (desktop assoluto).

---

## 2. Prerequisiti

- Sessione X locale dell'utente che possiede Openbox (stesso `DISPLAY`, stesso utente: **non** root).
- `xprop`, `date`, `stdbuf` (coreutils), `sh`, `grep`, `xlsclients`, `xrandr` (già presenti su Debian).
- L'atomo `_NET_DESKTOP_LAYOUT` **deve esistere prima** di avviare il monitor: `xprop -spy` non
  segnala atomi creati dopo il suo avvio (verificato: resta a `no such atom on any window`).
  Nel setup normale lo crea lo script in autostart; se non c'è, crealo una volta con:
  `xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT 0,4,3,0`
- Spazio: il log è piccolo (poche righe per evento), ma lascialo attivo per ore.

Verifica iniziale:

```sh
echo "DISPLAY=$DISPLAY"
xprop -root _NET_DESKTOP_LAYOUT _NET_NUMBER_OF_DESKTOPS
pgrep -a -x openbox
```

---

## 3. Fase A — fotografia iniziale

Serve per sapere com'era la sessione prima dell'esperimento (componenti, autostart, rc.xml).

```sh
mkdir -p ~/openbox-layout-log
STAMP=$(date +%Y%m%d-%H%M%S)
{
  echo "data: $(date --iso-8601=seconds)"
  echo "DISPLAY=$DISPLAY"
  echo "--- openbox ---"; pgrep -a -x openbox || echo "nessun openbox"
  openbox --version 2>&1 | head -3
  echo "--- proprietà ---"
  xprop -root _NET_DESKTOP_LAYOUT
  xprop -root _NET_NUMBER_OF_DESKTOPS
  xprop -root _NET_CURRENT_DESKTOP
  echo "--- client X connessi ---"; xlsclients -l 2>/dev/null | head -120
  echo "--- processi utente ---"; ps -u "$USER" -o pid,etime,cmd --sort=start_time | tail -80
  echo "--- autostart ---"; sed -n '1,160p' "$HOME/.config/openbox/autostart" 2>/dev/null
  echo "--- rc.xml: sezione desktops ---"
  sed -n '/<desktops>/,/<\/desktops>/p' "$HOME/.config/openbox/rc.xml" 2>/dev/null
} > "$HOME/openbox-layout-log/baseline-$STAMP.txt" 2>&1
echo "salvato: $HOME/openbox-layout-log/baseline-$STAMP.txt"
```

Nota: `xlsclients -l` riporta anche i titoli delle finestre; il file resta nella home, non
condividerlo pubblicamente.

---

## 4. Fase B — i due script di monitoraggio

I due script sono **già installati** in `~/openbox-layout-log/` (creati il 2026-11-05, eseguibili,
`sh -n` e `shellcheck` puliti). Se li trovi già lì, salta la creazione e vai al punto 4.3; i
contenuti seguenti servono per ricrearli o verificarli.

### 4.1 `watch-layout.sh`

Crea `~/openbox-layout-log/watch-layout.sh` con questo contenuto:

```sh
#!/bin/sh
# Registra con timestamp ogni cambiamento di _NET_DESKTOP_LAYOUT e del numero di desktop.
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
```

Poi:

```sh
chmod +x ~/openbox-layout-log/watch-layout.sh
```

Comportamento verificato di `xprop -spy` (importante per leggere il log):

- stampa subito il valore iniziale di ogni proprietà;
- stampa **ogni** `XChangeProperty`, anche quando il valore è identico al precedente
  (utile: una riscrittura "inutile" è comunque visibile);
- una cancellazione appare come `... :  not found.`;
- se l'atomo non esiste all'avvio, stampa `no such atom on any window` e **non** segue la
  creazione successiva (per questo lo script si rifiuta di partire);
- non dice quale client ha scritto: per quello serve la bisezione della Fase F.

### 4.2 `stop-watch.sh`

```sh
#!/bin/sh
# Ferma il monitor uccidendo l'intero gruppo di processi.
set -eu
pgidfile=${1:-"$HOME/openbox-layout-log/monitor.pgid"}
[ -f "$pgidfile" ] || { echo "nessun file $pgidfile" >&2; exit 1; }
pgid=$(cat "$pgidfile")
kill -- -"$pgid" 2>/dev/null || true
rm -f "$pgidfile"
echo "monitor fermato (pgid $pgid)"
```

```sh
chmod +x ~/openbox-layout-log/stop-watch.sh
```

### 4.3 Avvio e arresto

```sh
LOG=$HOME/openbox-layout-log/layout-$(date +%Y%m%d-%H%M%S).log
setsid sh "$HOME/openbox-layout-log/watch-layout.sh" \
    "$LOG" "$HOME/openbox-layout-log/monitor.pgid" >/dev/null 2>&1 &
sleep 1
echo "log: $LOG"
cat "$LOG"
```

Deve comparire subito una riga con il valore corrente (es. `0, 4, 3, 0`) e una con
`_NET_NUMBER_OF_DESKTOPS(CARDINAL) = 12`.

Arresto:

```sh
sh "$HOME/openbox-layout-log/stop-watch.sh"
```

Verifica che non restino processi:

```sh
pgrep -a -x xprop || echo "nessun xprop residuo"
```

---

## 5. Fase C — far succedere il difetto

Mentre il monitor gira, usa il desktop normalmente. **Appena** noti la griglia piatta (popup dei
desktop disposto su una riga, `Ctrl+Alt+Up/Down` che non si muove), scrivi un marker con l'ora:

```sh
printf '%s | MARKER difetto visto\n' "$(date --iso-8601=seconds)" >> "$LOG"
```

(`$LOG` è la variabile definita nella stessa shell al punto 4.3: se apri un altro terminale,
riassegnala con `LOG=$HOME/openbox-layout-log/layout-....log` oppure usa il percorso esplicito.)

Poi prosegui con i trigger deliberati, uno alla volta, marcando ogni prova:

| # | Trigger | Comando / azione |
|---|---------|------------------|
| 1 | Riavvio del pannello/pager | individua il processo (`ps -u "$USER" \| grep -Ei 'tint2\|lxpanel\|polybar\|xfce4-panel'`), chiudilo e riavvialo come fa la sessione |
| 2 | Riavvio del compositor | `pkill -x picom` poi rilancialo (se usato) |
| 3 | `obconf` | apri obconf, cambia `popupTime`, applica, chiudi; controlla se `<layout>` è ancora in `rc.xml` (Fase F) |
| 4 | Cambio numero desktop | `~/openbox-layout-log/setndesk 13` poi `... 12` (Appendice A) |
| 5 | Cambio risoluzione / hotplug | `xrandr --query`, poi `xrandr --output … --mode …` (o attach/staccamento monitor) |
| 6 | Sospensione/ripresa | `systemctl suspend` (se disponibile) e riprendi |
| 7 | Logout/login di sessione | riavvio completo di `openbox-session` |
| 8 | Scorciatoia manuale | `W-C-d` (esegue lo script che riscrive la proprietà) |

Per ogni prova:

```sh
printf '%s | MARKER trigger N: descrizione\n' "$(date --iso-8601=seconds)" >> "$LOG"
```

Nota: se in `rc.xml` c'è `<layout>`, la patch riasserisce subito la griglia; il log mostrerà
comunque il valore "intruso" seguito dal valore del WM. Se vuoi vedere il valore intruso
**persistere** (comportamento originale), commenta temporaneamente il blocco `<layout>` e fai
`openbox --reconfigure`: in modalità legacy la griglia resta internamente stabile ma il WM non
riscrive la proprietà.

---

## 6. Fase D — leggere il log

```sh
less "$LOG"
```

Tabella di interpretazione:

| Riga osservata | Significato |
|----------------|-------------|
| `_NET_DESKTOP_LAYOUT:  not found.` | qualcuno ha **cancellato** la proprietà (pannello che esce, tool di pulizia, `xprop -remove`) |
| `= 0, 12, 1, 0` | qualcuno ha scritto una griglia 1 riga × 12 colonne (pager/pannello o configurazione) |
| `= 0, 4, 3, 0` subito dopo una riga anomala | con `<layout>` la patch ha **riasserito** la griglia |
| `= 0, 4, 3, 0` ripetuto a raffica | contesa tra WM e un client che insiste; annota l'intervallo |
| `_NET_NUMBER_OF_DESKTOPS(CARDINAL) = 13` | cambio conteggio desktop: la griglia viene ricalcolata e ripubblicata |
| nessuna riga anomala ma la griglia è piatta | non è un problema di proprietà: verifica che stia girando il binario patchato (`openbox --version`) o che il difetto non venga da un altro WM/compositor |
| nessuna riga oltre quella iniziale, e il difetto compare | il difetto non passa dalla proprietà: guarda `_NET_NUMBER_OF_DESKTOPS`, il popup, i log del pannello |

Esempio reale (test su Xvfb, con la patch e `<layout>` attivo):

```
2026-11-05T22:56:07+01:00 | _NET_DESKTOP_LAYOUT(CARDINAL) = 0, 4, 3, 0
2026-11-05T22:56:08+01:00 | MARKER difetto visto
2026-11-05T22:56:08+01:00 | _NET_DESKTOP_LAYOUT(CARDINAL) = 0, 12, 1, 0
2026-11-05T22:56:08+01:00 | _NET_DESKTOP_LAYOUT:  not found.
2026-11-05T22:56:09+01:00 | _NET_DESKTOP_LAYOUT(CARDINAL) = 0, 4, 3, 0
```

Qui: un client ha scritto 12×1, un altro ha cancellato, e il WM ha ripristinato 4×3.

Per una risoluzione al decimo di secondo (se serve correlare con log applicativi):

```sh
# variante: sostituisci nel watch-layout.sh
#   "$(date --iso-8601=seconds)"
# con
#   "$(date +%Y-%m-%dT%H:%M:%S.%N | cut -c1-23)"
```

---

## 7. Fase E — trovare il colpevole

X11 non espone il client che ha scritto: si procede per correlazione e bisezione.

1. **Istantanea dei client attorno all'evento**

   ```sh
   printf '%s | MARKER xlsclients prima\n' "$(date --iso-8601=seconds)" >> "$LOG"
   xlsclients -l > "$HOME/openbox-layout-log/clients-$(date +%H%M%S)-prima.txt"
   # ... riproduci l'evento ...
   printf '%s | MARKER xlsclients dopo\n' "$(date --iso-8601=seconds)" >> "$LOG"
   xlsclients -l > "$HOME/openbox-layout-log/clients-$(date +%H%M%S)-dopo.txt"
   diff -u <(sed 's/^ *//' clients-*-prima.txt) <(sed 's/^ *//' clients-*-dopo.txt) | head -60
   ```

   Un client comparso/sparito nell'istante dell'evento è il candidato principale.

2. **File di configurazione toccati**

   ```sh
   stat -c '%y %n' "$HOME/.config/openbox/rc.xml" "$HOME/.config/openbox/autostart"
   ```

   Se `rc.xml` cambia nell'istante dell'evento, qualcuno lo ha riscritto: con obconf è il
   sospetto numero uno (e può aver eliminato il blocco `<layout>`).

3. **Test obconf (rischio noto)**

   ```sh
   cp -p "$HOME/.config/openbox/rc.xml" "$HOME/openbox-layout-log/rc.xml.prima-obconf"
   grep -c '<layout' "$HOME/.config/openbox/rc.xml"
   obconf        # cambia popupTime o numero desktop, applica, chiudi
   grep -c '<layout' "$HOME/.config/openbox/rc.xml"
   diff -u "$HOME/openbox-layout-log/rc.xml.prima-obconf" "$HOME/.config/openbox/rc.xml" | head -40
   ```

   Se il conteggio passa da 1 a 0, obconf riscrive la sezione `<desktops>` e rimuove il blocco:
   in quel caso la griglia sopravvive solo finché rc.xml non viene riscritto da obconf.
   Ripristina il backup solo se hai verificato che non hai altre modifiche da tenere.

4. **Bisezione dei componenti di sessione**

   - commenta metà delle righe di `autostart` (escluse quelle indispensabili a Openbox),
   - `openbox --restart` (o logout/login),
   - monitor per 15–30 minuti usando il desktop,
   - se l'evento non compare, il colpevole è nella metà disabilitata → ripeti dimezzando;
   - se compare, il colpevole è nella metà attiva.

   Candidati tipici: pager di pannelli (`tint2`, `lxpanel`, `xfce4-panel`, `polybar`), tool di
   gestione display (`arandr`, script `xrandr`), session restore, script personali in autostart.

5. **Log applicativi**: `~/.cache/openbox/openbox.log`, log del pannello, `journalctl --user`
   (se disponibile) nell'intervallo di tempo del marker.

6. **Sospensione/ripresa**: se l'evento coincide con un resume, il sospetto è il tool di
   gestione schermo che ripristina le proprietà root; prova a ripetere con `xrandr --query`
   forzato dopo il resume.

---

## 8. Fase F — verificare la patch su Openbox

Da fare sulla build patchata (branch `gelma`). Con la configurazione esplicita la griglia deve
essere autosufficiente, senza script in autostart.

1. **Configura la griglia** in `~/.config/openbox/rc.xml`, dentro `<desktops>`:

   ```xml
   <layout>
     <orientation>horizontal</orientation>
     <columns>4</columns>
     <rows>3</rows>
     <startingCorner>top-left</startingCorner>
   </layout>
   ```

   Poi `openbox --reconfigure` (o `openbox --restart`).

2. **All'avvio il WM pubblica la proprietà** (non serve più lo script):

   ```sh
   xprop -root _NET_DESKTOP_LAYOUT     # atteso: 0, 4, 3, 0
   xprop -root _NET_NUMBER_OF_DESKTOPS # atteso: 12
   ```

3. **Cancellazione**: il WM la ricrea entro un secondo

   ```sh
   xprop -root -remove _NET_DESKTOP_LAYOUT
   sleep 1
   xprop -root _NET_DESKTOP_LAYOUT     # atteso: di nuovo 0, 4, 3, 0
   ```

4. **Pager ostile**: il WM riasserisce la configurazione

   ```sh
   xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT 0,12,1,0
   sleep 1
   xprop -root _NET_DESKTOP_LAYOUT     # atteso: 0, 4, 3, 0
   ```

5. **Prova visiva**: con 12 desktop e griglia 4×3,
   da desktop 5 (`W-F5` o `Ctrl+Alt+...`) premi `Ctrl+Alt+Up`: deve andare al desktop 1;
   `Ctrl+Alt+Down` dal 5 deve andare al 9; il popup dei desktop deve mostrare 4 colonne × 3 righe.
   Con la griglia piatta, invece, su/giù non cambiano desktop (wrap disattivato).

6. **Controllo interno (facoltativo, avanzato)**: la patch sposta la logica in
   `openbox/layout.c`; lo stato in memoria si legge con gdb sullo stesso utente. Usa il binario
   **non strip** della build patchata (quello installato potrebbe non avere i simboli):

   ```sh
   bin=$HOME/dev/prg/intelligenti/openbox/openbox/.libs/openbox
   pid=$(pgrep -x openbox | head -1)
   gdb -p "$pid" -batch \
       -ex "file $bin" \
       -ex 'x/4dw &screen_desktop_layout'
   # atteso: 0 0 3 4   (orientamento, angolo, righe, colonne)
   ```

   Se `gdb` risponde `No symbol table is loaded`, il binario è strip: ricompila senza strip o
   salta questo controllo. Serve `ptrace` consentito allo stesso utente; non serve root.

7. **Log atteso con la patch**: ogni scrittura esterna è seguita dalla riasserzione del WM,
   quindi vedrai coppie del tipo `= 0, 12, 1, 0` → `= 0, 4, 3, 0` a pochi millisecondi.

Se hai bisogno del comportamento EWMH puro (il pager vince), aggiungi
`<layout respectPager="yes">` e ripeti il punto 4: la proprietà deve restare `0, 12, 1, 0`,
mentre alla cancellazione (punto 3) il WM deve tornare a `0, 4, 3, 0`.

---

## 9. Fase G — chiudere e raccogliere

```sh
sh "$HOME/openbox-layout-log/stop-watch.sh"
pgrep -a -x xprop || echo "nessun xprop residuo"

tar czf "$HOME/openbox-layout-report-$(date +%Y%m%d-%H%M%S).tar.gz" \
    -C "$HOME" openbox-layout-log
ls -l "$HOME"/openbox-layout-report-*.tar.gz | tail -1
```

Nel pacchetto trovi: baseline, log con marker, eventuali `clients-*.txt`, backup di `rc.xml`.

---

## 10. Cosa può andare storto

| Sintomo | Causa | Rimedio |
|---------|-------|---------|
| `no such atom on any window` e nessuna riga successiva | monitor avviato prima che la proprietà esistesse: `xprop -spy` non segue la creazione | crea la proprietà (Appendice B) e riavvia il monitor |
| il log non cresce | pipeline in buffering o monitor morto | usa `stdbuf -oL` (già nello script); controlla `pgrep -a xprop` |
| `kill -- -PGID` dice "no such process" | il monitor è già uscito (atomo sparito all'avvio) | ignora; riavvia |
| nessuna riga al momento del difetto | il difetto non passa dalla proprietà | controlla `_NET_NUMBER_OF_DESKTOPS`, il popup, e che il binario sia quello patchato |
| righe duplicate identiche | un client riscrive lo stesso valore: è comunque un evento utile | annota l'intervallo e correla con `xlsclients` |
| la griglia torna 4×3 da sola | patch attiva con `<layout>` | comportamento atteso (auto-ripristino) |
| dopo obconf la griglia non torna più | obconf ha eliminato `<layout>` da `rc.xml` | ripristina il blocco; rischio noto, documentato in `SpecAI.md` §9.1 |
| nessun effetto da `W-C-d` | lo script in autostart non è più necessario con `<layout>` | la scorciatoia può restare, ma non serve |
| monitor e WM su `DISPLAY` diversi | `DISPLAY` non esportato nella shell del test | `export DISPLAY=:0` (o quello della sessione) prima di tutto |
| timestamp tutti uguali | risoluzione al secondo | usa la variante con `%N` (Fase D) |

---

## 11. Appendice A — cambiare il numero di desktop senza `xdotool`

Serve per il trigger 4 e per verificare il ricalcolo della griglia (12→13→12).
Compila una volta in `~/openbox-layout-log/`:

```sh
cat > "$HOME/openbox-layout-log/setndesk.c" <<'EOF'
#include <X11/Xlib.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    Window root;
    XEvent e;

    if (!d || argc < 2) return 1;
    root = DefaultRootWindow(d);
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = root;
    e.xclient.message_type = XInternAtom(d, "_NET_NUMBER_OF_DESKTOPS", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = atoi(argv[1]);
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(d);
    XCloseDisplay(d);
    return 0;
}
EOF
gcc -O2 -o "$HOME/openbox-layout-log/setndesk" "$HOME/openbox-layout-log/setndesk.c" -lX11
```

Uso (con la patch e `<layout>` 4×3): `setndesk 13` → la proprietà diventa `0, 5, 3, 0`;
`setndesk 12` → torna `0, 4, 3, 0`.

---

## 12. Appendice B — scrivere, leggere, cancellare la proprietà con `xprop`

Tutto verificato; non serve Python.

```sh
# leggere
xprop -root _NET_DESKTOP_LAYOUT

# scrivere la griglia 4x3 (orientamento, colonne, righe, angolo)
xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT 0,4,3,0

# simulare un pager che impone la striscia
xprop -root -f _NET_DESKTOP_LAYOUT 32c -set _NET_DESKTOP_LAYOUT 0,12,1,0

# simulare la perdita della proprietà
xprop -root -remove _NET_DESKTOP_LAYOUT
```

Nota: con la patch e `<layout>` attivo, dopo pochi istanti `xprop` mostra di nuovo `0, 4, 3, 0`:
è il WM che ha ripristinato la griglia.

---

## 13. Esito atteso

- Se il log mostra `not found` → un client cancella la proprietà: individua quale con la
  bisezione (Fase E).
- Se mostra `0, 12, 1, 0` → un pager/pannello impone la striscia: valuta la sua configurazione
  di righe/colonne o usa `respectPager="yes"` se preferisci che vinca.
- Se non mostra nulla → il difetto non passa da `_NET_DESKTOP_LAYOUT`: verifica
  `_NET_NUMBER_OF_DESKTOPS` e che stia girando la build patchata.
- Con `<layout>` e la patch, in tutti i casi la griglia 4×3 deve restare (o tornare entro un
  secondo) e lo script in autostart non è più necessario.
