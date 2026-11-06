# Test callgrind mirato: costo di un cambio workspace

Documento di lavoro (fuori dal repo). Scopo: misurare con il **grafo delle chiamate** quanto costa
uno switch fra workspace dopo il fix della cache del testo (SpecAI 131), e chi chiama
`FcCharSetIsSubset` — l'ultimo conto rimasto aperto.

Riferimenti già misurati (cachegrind, A/B 0 vs 20 switch, Xvfb, stesso binario):

| | Ir per switch |
|---|---|
| prima della cache del testo | 9,81 M |
| dopo (commit della cache) | **2,63 M** |

Scomposizione attesa nel nuovo dump: `RCombineImagesWithOpaqueness` ~877 k (5 blend),
`RConvertImage` ~400 k (5 conversioni), `RCreateImageFromXImage` ~432 k (la cattura),
`FcCharSetIsSubset` **assente**, tutto il resto in Xlib/Xft (`???`).

---

## 0. Prerequisiti

```bash
# il binario installato deve contenere il fix (ricompila e installa come fai di solito)
cd ~/dev/prg/intelligenti/wmaker/wmaker_git
make -j8 && make install

# lo script aggiornato (già copiato, con la modalità --callgrind-lazy)
ls -l /opt/wmaker/bin/wmaker-valgrind-session
```

Se hai cambiato `src/wconfig.h.in`, `make` rigenera da solo `src/wconfig.h` (la regola c'è).

---

## 1. Avvio della sessione (Fase A — prima degli switch)

Da un terminale di testo (non dentro la sessione grafica: lo script fa `exec`, quindi prende il posto
del processo), oppure dal tuo `.xinitrc`:

```bash
wmaker-valgrind-session --callgrind-lazy
```

Lo script stampa il percorso del log di Valgrind e scrive tutto in
`~/.local/share/wmaker/logs/wmaker-<timestamp>.log`. **Non** registra nulla finché non lo attivi.

In un secondo terminale, trova il pid del WM *strumentato che lavora* — è il figlio `--for-real`:

```bash
pgrep -af 'wmaker --for-real'
```

Esempio di risposta (il primo numero è il pid da usare):

```
10811 /opt/wmaker/bin/wmaker --for-real
```

Se ne vedi più di uno, quello buono è il più vecchio (il padre `/opt/wmaker/bin/wmaker` senza
`--for-real` è il processo che fa solo il re-exec e non contiene il lavoro).

Verifica che la strumentazione sia spenta:

```bash
callgrind_control 10811
```

Deve dire qualcosa come `instrumentation: no` (se dice `yes`, sei già in raccolta: fermala con
`callgrind_control -i off 10811`).

Attiva e azzera (lo zero butta via il poco che è stato registrato prima):

```bash
callgrind_control -i on 10811
callgrind_control -z    10811
```

---

## 2. I 20 switch (Fase B)

### Opzione 1 — con la tastiera (quella che usi normalmente)

20 volte, alternando due workspace, con ~0,5 s fra un cambio e l'altro:

```
Alt+1, Alt+2, Alt+1, Alt+2, ... (20 cambi in tutto)
```

Conta: 20, non "finché ti stanchi". Il numero serve per dividere.

### Opzione 2 — scriptato, se preferisci la ripetibilità

Non hai `xdotool`, quindi il cambio si manda come ClientMessage `_NET_CURRENT_DESKTOP`
(il WM lo gestisce, `src/wmspec.c`). Salva questo come `/tmp/wsswitch.c`:

```c
#include <stdlib.h>
#include <X11/Xlib.h>

int main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(NULL);
	Window root;
	XEvent ev;
	int ws = argc > 1 ? atoi(argv[1]) : 1;

	if (!dpy)
		return 1;
	root = DefaultRootWindow(dpy);
	ev.xclient.type = ClientMessage;
	ev.xclient.send_event = True;
	ev.xclient.display = dpy;
	ev.xclient.window = root;
	ev.xclient.message_type = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = ws;
	XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
	XCloseDisplay(dpy);
	return 0;
}
```

Compila e lancia i 20 switch:

```bash
gcc -O2 -o /tmp/wsswitch /tmp/wsswitch.c -lX11

for i in $(seq 1 20); do
    /tmp/wsswitch $(( i % 3 ))
    sleep 0.5
done
```

---

## 3. Dump e chiusura (Fase C — subito dopo gli switch)

```bash
# scrive <out>.1 (il primo dump), oppure .2, .3... se ne fai più di uno
callgrind_control -d sw 10811

# ferma la raccolta: da qui in poi l'uscita non sporca i numeri
callgrind_control -i off 10811
```

Poi esci dalla sessione come fai sempre (menu Quit o `kill -TERM 10811`). Il WM scrive comunque un
dump finale all'uscita: per l'analisi usa il file **`.1`**, cioè quello del comando `-d sw`.

---

## 4. Analisi (Fase D)

```bash
# i file prodotti: il primo .out è la sessione, il .1 è il dump dello scenario
ls -lt ~/.local/share/wmaker/logs/callgrind-wmaker-*.out* | head

F=$(ls -t ~/.local/share/wmaker/logs/callgrind-wmaker-*.out.1 | head -1)
echo "$F"
```

Totale e classifica per funzione (solo il dump dello scenario):

```bash
callgrind_annotate --auto=no --threshold=99 --inclusive=no "$F" | head -40
```

Chi chiama `FcCharSetIsSubset` (la domanda aperta):

```bash
callgrind_annotate --tree=caller --threshold=99 "$F" | grep -A 15 'FcCharSetIsSubset'
```

Costo per switch (dividi il totale per 20):

```bash
callgrind_annotate --auto=no --threshold=100 "$F" | awk '/PROGRAM TOTALS/{gsub(",","",$1); print $1/20" Ir per switch"}'
```

Confronta con **2,63 M**: se il numero è vicino, il fix della cache del testo è confermato anche con
il grafo delle chiamate; se è più alto, la differenza sta nel `???` (Xlib) e la prossima domanda è
quali richieste X fa lo switch.

---

## 5. Cose che possono andare storte

- **`callgrind_control` non trova il pid**: il WM ha fatto re-exec; usa il pid del figlio
  `--for-real` (`pgrep -af 'wmaker --for-real'`), non quello del padre.
- **Il dump è vuoto o quasi**: la raccolta non era attiva (`-i on` mancante) oppure hai fatto `-z`
  *dopo* gli switch.
- **Numeri gonfiati**: se nel frattempo gira una compilazione, il WM viene affamato e i tempi
  (non le istruzioni) si allungano; con callgrind contano le istruzioni, quindi il numero regge, ma
  evita di mescolare sessioni.
- **File di altri programmi nella stessa directory**: lo script ora salta `/opt/*` tranne il WM,
  quindi TeamViewer non viene più strumentato. Se vedi `cachegrind-wmaker-*` con
  `cmd: /opt/teamviewer/...`, stai usando la versione vecchia dello script.
- **Dump multipli**: ogni `-d` crea `.1`, `.2`, …; per il confronto usa quello con il tag `sw`.
