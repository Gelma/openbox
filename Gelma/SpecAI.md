# SpecAI — Openbox: layout della griglia di desktop virtuali

> Documento di memoria di progetto. Va letto all'inizio di ogni sessione: contiene stato,
> scoperte verificate, decisioni e il piano di lavoro.
>
> Stato al 2026-11-05 (branch `gelma`, base upstream `main` @ `3a2fbc65`, serie 3.7):
> **F1–F4 implementate e verificate** (hardening, configurazione `<layout>`, pubblicazione
> EWMH, unit test). Build pulita, `make check` verde, matrice d'integrazione Xvfb verde,
> Valgrind senza perdite nel codice nuovo.

---

## 1. Contesto e sintomo

Setup d'uso (home di riferimento copiata in `/tmp/testnec/.config/openbox`):

- `rc.xml`: `<desktops><number>12</number></desktops>` e keybind `W-F1..F12`, `W-C-d`.
- `autostart` lancia `set_desktop_layout.py 4 3 &`, che scrive sulla root window la proprietà
  EWMH `_NET_DESKTOP_LAYOUT = [0, 4, 3, 0]` (HORZ, 4 colonne, 3 righe, angolo top-left).
- Prima di questo lavoro Openbox non aveva alcun supporto di configurazione per righe/colonne:
  leggeva solo la proprietà.

Sintomo riportato: durante l'uso la griglia 4×3 collassa in un'unica striscia 1 riga × 12
colonne; la navigazione verticale e il popup dei desktop diventano una fila; un riavvio di
Openbox ripristina la griglia.

---

## 2. Mappa del codice rilevante

| File | Punto | Ruolo |
|------|-------|-------|
| `openbox/layout.h` | tutto | **nuovo**: `ObDesktopLayout`, `ob_layout_validate()`, `ob_layout_resolve()` |
| `openbox/layout.c` | tutto | **nuovo**: logica pura, senza dipendenze X (testabile) |
| `openbox/layout_unittest.c` | tutto | **nuovo**: unit test (`make check`) |
| `openbox/screen.c` | `1101-1224` | `screen_update_layout()`, parse della proprietà, pubblicazione |
| `openbox/screen.c` | `826-930` | `get_row_col()`, `translate_row_col()`: matematica della griglia |
| `openbox/screen.c` | `992-1100` | `screen_find_desktop()`: navigazione direzionale (N/S/E/W) |
| `openbox/screen.c` | `507-530` | `screen_set_num_desktops()` → chiama `screen_update_layout()` |
| `openbox/screen.c` | `335-445` | `screen_startup()`: sessione XSMP, conteggio desktop, poi layout |
| `openbox/screen.c` | `947-985` | `screen_show_desktop_popup()`: dimensione icone dal layout |
| `openbox/event.c` | `808-815` | `PropertyNotify` su `_NET_DESKTOP_LAYOUT` → `screen_update_layout()` |
| `openbox/popup.c` | `400-520` | disegno del pager popup (già robusto a celle in eccesso) |
| `openbox/config.c` | `806-900` | `parse_desktops()`, `parse_desktop_layout()` |
| `openbox/config.c` | `1211-1222` | default: `config_desktops_num = 4`, layout non impostato |
| `data/rc.xml` | `122-160` | sezione `<desktops>` con esempio commentato di `<layout>` |
| `data/rc.xsd` | `109-142`, `570-591` | `ob:desktopLayout`, `ob:orientation`, `ob:startingcorner` |
| `obt/prop.c` | `174-179` | `NET_WM_ORIENTATION_*` = 0/1 e `NET_WM_TOP*` = 0..3 (**valori, non atomi internati**) |
| `obt/prop.c` | `251-289` | `get_all()`: `g_malloc` del buffer; richiede `format == 32` e `ritems > 0` |
| `openbox/openbox.c` | `330-370`, `635-650` | `--startup` eseguito solo al primo avvio e rimosso da `argv` |

Conseguenza importante (prima del fix): **Openbox non scriveva mai `_NET_DESKTOP_LAYOUT`**;
l'unica fonte era un client esterno (pager/pannello/script).

---

## 3. Comportamento verificato a runtime

Harness: `Xvfb :NN -screen 0 1280x800x24 -nolisten tcp -noreset`, binario in-tree
(`openbox/openbox` wrapper libtool), stato interno letto con
`gdb -p PID -ex 'x/4dw &screen_desktop_layout'` (ordine: orientation, start_corner, rows,
columns; `HORZ=0`, `TOPLEFT=0`). **`-noreset` è obbligatorio**: senza, il server X cancella
atomi e proprietà quando l'ultimo client si disconnette e i test danno falsi negativi.

### 3.1 Prima del fix (comportamento originale)

| Caso | Azione | Layout risultante |
|------|--------|-------------------|
| L0 | proprietà `[0,4,3,0]`, avvio | `3 x 4` ✔ |
| L1 | `xprop -root -remove _NET_DESKTOP_LAYOUT` | `1 x 12` ← collasso |
| L2 | riscrittura `[0,4,3,0]` | `3 x 4` (recupero via `PropertyNotify`) |
| L3 | riscrittura `[0,12,1,0]` (pager) | `1 x 12` ← sintomo identico |
| L4 | proprietà a 3 elementi `[0,4,3]` | `3 x 4` ✔ |
| L5 | proprietà a 2 elementi / formato errato | `1 x 12`, **silenzioso** |

Altro: `openbox --reconfigure` ricollassava se la proprietà era assente/malformata;
`openbox --restart` **non** rieseguiva `--startup` (marker su file rimasto a 1 esecuzione),
quindi non rilanciava l'autostart e lo script Python.

### 3.2 Dopo il fix (comportamento attuale)

Legacy, senza `<layout>` (comportamento EWMH preservato, nessuna regressione):

| Caso | Azione | Layout | Proprietà |
|------|--------|--------|-----------|
| A1 | avvio con `[0,4,3,0]` | `3 x 4` | invariata |
| A2 | proprietà cancellata | `3 x 4` (non collassa più) | resta assente (il WM non la impone) |
| A3 | pager scrive `[0,12,1,0]` | `1 x 12` (il pager vince) | `0,12,1,0` |
| A4 | proprietà malformata | conserva l'ultimo layout valido | invariata |
| A5 | nessuna proprietà all'avvio | `1 x 12` (default storico) | assente |

Con `<layout><columns>4</columns><rows>3</rows></layout>` (policy "config vince"):

| Caso | Azione | Layout | Proprietà |
|------|--------|--------|-----------|
| B1 | avvio **senza** proprietà | `3 x 4` | **pubblicata** `0,4,3,0` |
| B2 | proprietà cancellata | `3 x 4` | **ripubblicata** |
| B3 | pager scrive `[0,12,1,0]` | `3 x 4` | **riasserita** `0,4,3,0` |
| B4 | pager scrive `[0,3,4,0]` | `4 x 3` (config) | riasserita `0,4,3,0` |
| B5 | `openbox --reconfigure` | `4 x 3` | riallineata |
| B6 | `openbox --restart` | `3 x 4` | riallineata (config riletta) |
| B7 | `_NET_NUMBER_OF_DESKTOPS` 12→13 | `3 x 5` | pubblicata `0,5,3,0` |
| B8 | ritorno a 12 desktop | `3 x 4` | pubblicata `0,4,3,0` |

Con `<layout respectPager="yes">` (valvola EWMH):

| Caso | Azione | Layout | Proprietà |
|------|--------|--------|-----------|
| C1 | avvio senza proprietà | `3 x 4` | pubblicata |
| C2 | pager scrive `[0,12,1,0]` | `1 x 12` (pager vince) | `0,12,1,0` |
| C3 | proprietà cancellata | `3 x 4` (fallback config) | ripubblicata |

Valgrind (`--leak-check=full`, 20 scritture di proprietà malformata + delete + riscrittura):
nessuna perdita attribuibile a `screen_parse_layout_property()`, `screen_update_layout()` o
`obt_prop_get_array32()`. Le perdite residue sono preesistenti e fuori scope (pangoxft in
`XCloseDisplay`, `RrButtonNew`/`RrThemeNew`, fontconfig).

---

## 4. Valutazione dell'analisi precedente

**Confermato (con evidenza):**

1. `screen_update_layout()` azzerava lo stato a `1 × N` **prima** di leggere la proprietà: era
   la causa diretta del collasso quando la lettura falliva (L1, L5).
2. `event.c` reagiva a qualunque `PropertyNotify` sull'atomo, inclusa la cancellazione, senza
   distinguere `PropertyDelete` da un nuovo valore.
3. Openbox era consumatore passivo: nessuna scrittura della proprietà, nessun tag in
   `rc.xml`/`rc.xsd`, `parse_desktops()` limitato ai quattro tag noti.
4. Dopo un collasso non esisteva alcuna rilettura periodica: serviva un nuovo evento (o un
   riavvio del WM) per riallineare lo stato.
5. La storia git è coerente: `a055fd9e` introduce la validazione e il fallback 1×N; `6f6ab8c4`
   salva/ripristina il layout **solo** via XSMP; `ead75d7f` aggiunge la rilettura post-reconfigure.

**Impreciso / corretto:**

1. **"Il restart riesegue l'autostart e quindi lo script"**: falso per `openbox --restart`
   (verificato: `--startup` è rimosso da `argv` e non viene rilanciato). La spiegazione del
   "il restart risolve" è quindi una delle due: (a) la proprietà è ancora presente e valida e
   lo stato interno era rimasto "latched" a 1×12 per una lettura fallita precedente; (b) il
   "restart" dell'utente è in realtà un riavvio di sessione/`openbox-session`, che rilancia
   l'autostart. Va discriminato in campo (§8).
2. La citazione EWMH ("la proprietà è del Pager, non del WM") è corretta come intento della
   specifica, ma non vieta al WM di scriverla: il progetto ora dichiara una policy esplicita.
3. Non è vero che serve un pager esterno perché il layout funzioni: popup e navigazione usano
   solo lo stato interno, alimentato anche da uno script.

**Mancante nell'analisi precedente (ora risolto):**

1. Il caso L5 (proprietà malformata, `format` errato, 0 elementi) produceva lo stesso collasso
   in modo silenzioso.
2. **Perdita di memoria**: il buffer `data` (allocato da `get_all()`) non veniva liberato nei
   percorsi di `return` anticipato di `screen_update_layout()`.
3. La **precedenza**: un pager con un layout valido diverso (12×1) vinceva sul layout
   dell'utente (L3).
4. Il comportamento al **cambio del numero di desktop**: il layout veniva ricalcolato da una
   proprietà mai aggiornata, restando incoerente (B7/B8).
5. L'assenza di **diagnostica** nei fallback.

---

## 5. Modello di guasto

Lo stato interno può diventare 1×N solo passando da `screen_update_layout()`; quindi il collasso
richiedeva **una lettura fallita** in quel momento:

- **A. Proprietà cancellata** (pannello/pager che esce, tool che ripulisce la root) → l'evento
  di delete scatenava il ricalcolo → 1×12 (L1).
- **B. Proprietà sostituita** da un valore valido ma diverso (pager 12×1) → vinceva il pager (L3).
- **C. Proprietà malformata / tipo o `format` sbagliato / vuota** → fallback 1×N senza log (L5).
- **D. Stato "latched"**: dopo A o C nessun evento; lo stato restava 1×12 anche se la
  proprietà tornava valida senza notifica (es. scritta quando il WM non era in ascolto).
- **E. Cambio conteggio desktop**: ricalcolo su proprietà non aggiornata (nessuna scrittura lato
  WM) → griglia incoerente con `screen_num_desktops`.

Punto centrale: **Openbox non aveva memoria della geometria voluta**. Il fix introduce la
configurazione come fonte di verità e la rende persistente sulla root window.

---

## 6. Implementazione

### 6.1 Principi applicati

1. **La configurazione è l'autorità di default.** Se `rc.xml` dichiara una geometria, quella
   vince: Openbox la applica, la pubblica su `_NET_DESKTOP_LAYOUT` e la riasserisce se un altro
   client la cambia. `respectPager="yes"` ripristina la precedenza EWMH del pager.
2. **Mai distruggere uno stato valido.** Se la proprietà manca o è invalida e non c'è
   configurazione, si conserva il layout corrente (se ancora valido per il numero di desktop).
3. **Nessun conflitto infinito.** Il WM riscrive la proprietà solo quando il valore presente
   differisce; il proprio `PropertyNotify` non genera loop (verificato: stato stabile).
4. **Diagnostica.** Ogni fallback e ogni fonte scelta passa da `ob_debug`.
5. **EWMH rispettata quando non c'è configurazione.** Senza `<layout>` il comportamento è
   quello storico, salvo la retention (A2/A4), che evita il collasso.

### 6.2 Modello dati

```c
typedef struct ObDesktopLayout {   /* openbox/layout.h */
    ObOrientation orientation;     /* OB_ORIENTATION_HORZ | VERT   */
    ObCorner      start_corner;    /* OB_CORNER_*                 */
    guint         rows;            /* 0 = automatico              */
    guint         columns;         /* 0 = automatico              */
} ObDesktopLayout;
```

Configurazione (`openbox/config.h`):

```c
extern ObDesktopLayout config_desktops_layout;
extern gboolean config_desktops_layout_set;         /* tag <layout> presente   */
extern gboolean config_desktops_layout_respect_pager; /* attributo respectPager */
```

Sintassi:

```xml
<desktops>
  <number>12</number>
  <firstdesk>1</firstdesk>
  <layout respectPager="no">          <!-- attributo opzionale -->
    <orientation>horizontal</orientation>  <!-- horizontal | vertical -->
    <columns>4</columns>
    <rows>3</rows>
    <startingCorner>top-left</startingCorner>
  </layout>
  <names>...</names>
  <popupTime>875</popupTime>
</desktops>
```

`layout_set` diventa TRUE quando compare almeno `rows` o `columns` (l'altro viene calcolato).
Solo `orientation`/`startingCorner` non attivano la griglia: restano default HORZ/top-left.

### 6.3 Algoritmo (`ob_layout_resolve()`, `openbox/layout.c`)

Ordine di precedenza effettivo:

1. **config**, se il tag è presente e non c'è `respectPager` (policy di default);
2. **proprietà EWMH** (se parsabile e valida per `screen_num_desktops`);
3. **config** come fallback (caso `respectPager` con proprietà assente/invalida);
4. **layout corrente** (retention: non si collassa);
5. **default** `1 × N`.

Ogni candidato passa da `ob_layout_validate(&copy, num)`, che riempie righe/colonne mancanti e
limita la griglia a contenere `num` desktop (algoritmo storico, invariato ma ora parametrizzato).
`screen_update_layout()`:

- legge la proprietà in una copia (`screen_parse_layout_property()`, libera **sempre** il buffer);
- registra con `ob_debug` se la proprietà manca o è invalida;
- risolve e applica; se la fonte è la configurazione, chiama `screen_publish_layout()`;
- `screen_publish_layout()` confronta il valore corrente e scrive solo se diverso.

`screen_update_layout()` resta l'unico punto di calcolo, quindi `screen_set_num_desktops()`,
`--reconfigure`, `--restart` e `PropertyNotify` sono tutti coperti.

### 6.4 Pubblicazione EWMH

`_NET_DESKTOP_LAYOUT` viene scritta come `CARDINAL[4] = {orientation, columns, rows, corner}`
con la mappatura EWMH dell'angolo: top-left=0, top-right=1, **bottom-right=2, bottom-left=3**
(ordine diverso da `ObCorner`, mappato esplicitamente). `screen.c` continua a dichiarare
l'atomo in `_NET_SUPPORTED`.

---

## 7. Piano di implementazione — stato

| Fase | Contenuto | Stato | File |
|------|-----------|-------|------|
| **F1** | Hardening: niente reset distruttivo, `g_free` su tutti i percorsi, log dei fallback | **fatto** | `openbox/screen.c` |
| **F2** | Configurazione `<layout>`: globals, parser, default, `rc.xsd`, `rc.xml`, doc | **fatto** | `openbox/config.c/h`, `data/rc.xsd`, `data/rc.xml`, `doc/rc-mouse-focus.xml`, `CHANGELOG` |
| **F3** | Pubblicazione + ownership + guardia anti-loop + ripubblicazione al cambio conteggio | **fatto** | `openbox/screen.c`, `openbox/layout.c/h` |
| **F4** | Test: unit test della logica pura + matrice d'integrazione Xvfb | **fatto** | `openbox/layout_unittest.c`, `Makefile.am`, harness `/tmp` (non in git) |

Build: `make -j8` senza warning nuovi. Test: `make check` esegue `openbox/layout_unittest`
(18 controlli) tramite `check-local`.

---

## 8. Riproduzione e test

### 8.1 Unit test (in git)

```sh
make check          # compila e esegue openbox/layout_unittest
```

Copre: validazione (4×3, righe/colonne mancanti, 0/0, 12×1, espansione 2×2→2×6, orientamento
verticale, 0 desktop, clamp righe>desktop), uguaglianza, precedenza (config autoritaria,
`respectPager`, fallback config, retention, default, config invalida).

### 8.2 Integrazione (harness usa e getta in `/tmp/ob-grid-test/`)

```sh
Xvfb :105 -screen 0 1280x800x24 -nolisten tcp -noreset &
export DISPLAY=:105
python3 set_desktop_layout.py 4 3          # scrive EWMH [0,4,3,0]
./openbox/openbox --config-file /tmp/ob-grid-test/rc-grid.xml &
gdb -p $! -batch -ex 'file ./openbox/.libs/openbox' \
    -ex 'x/4dw &screen_desktop_layout'
# casi: xprop -root -remove, set_desktop_layout.py 12 1, proprietà a 2 elementi,
#       openbox --reconfigure, openbox --restart, setndesk 13 / 12
```

Copre le matrici A/B/C della §3.2. Harness non committato: dipende da gdb/Xvfb e non è
adatto all'upstream.

### 8.3 Procedura per identificare il colpevole in campo

Consegnata **fuori dal repo** il 2026-11-05 in `~/Openbox-Desktop-Layout-Test.md`: monitor
`xprop -spy` con timestamp (script `watch-layout.sh`/`stop-watch.sh`), marker manuali, trigger
deliberati (pannello, obconf, xrandr, cambio conteggio, sospensione), tabella di lettura del
log, bisezione dei componenti e verifica del fix. Se alla comparsa della perdita la proprietà è
ancora `0, 4, 3, 0` → caso D (stato latched); se è sparita o è `0, 12, 1, 0` → caso A/B e si
risale al client. Con il fix attuale il difetto non si presenta più con `<layout>` configurato,
ma la diagnosi serve per capire chi scriveva la proprietà nel setup originale.

Verifiche fatte prima di consegnarla: `xprop -spy` non segue atomi creati dopo l'avvio (lo
script si rifiuta di partire se la proprietà manca), registra anche riscritture con lo stesso
valore, mostra le cancellazioni come `not found`; avvio con `setsid` e arresto con
`kill -- -PGID` senza processi residui; script validati con `sh -n` e `shellcheck`.

Gli script sono già installati ed eseguibili in `~/openbox-layout-log/watch-layout.sh` e
`~/openbox-layout-log/stop-watch.sh`; smoke test eseguito sui file installati (rifiuto senza
proprietà, log di valore iniziale/marker/12×1/cancellazione/4×3, stop senza `xprop` residui).

---

## 9. Rischi, compatibilità, domande aperte

1. **obconf**: modifica `rc.xml` (stringhe note: `desktops/number`, `desktops/names`,
   `desktops/popupTime`). Non è verificabile senza interazione GTK: **rischio che riscrivendo
   la sezione `<desktops>` elimini `<layout>`**. Da provare a mano; se accade, valutare un file
   separato o una patch a obconf (fuori scope).
2. **Pannelli con layout proprio**: con la policy di default il WM riasserisce la griglia; il
   pannello può mostrare una geometria diversa. `respectPager="yes"` è la valvola di sfogo.
3. **XSMP**: il layout salvato in sessione mantiene la precedenza (percorso invariato in
   `screen_startup()`; il publish non avviene in quel ramo).
4. **Multi-monitor / RandR**: il layout è globale; `screen_resize()` non tocca il layout. Non
   testato con più monitor (Xvfb singolo schermo).
5. **Traduzioni**: i due nuovi messaggi di errore config (`g_message`) sono in inglese non
   tradotto; le modifiche `po/*.po` presenti nel worktree sono di un'altra sessione e non vanno
   incluse nei commit di questo lavoro.
6. **EWMH**: un WM che pubblica `_NET_DESKTOP_LAYOUT` è una deviazione dall'intento della
   specifica (di norma lo fa il pager). È attivata solo da `<layout>` esplicito, quindi nessuna
   regressione per chi non la usa.

---

## 10. Storico delle decisioni

- 2026-11-05: analisi iniziale (commit `ca930ad6`), spostamento file (`4eb8307f`).
- 2026-11-05: verifica runtime con Xvfb+gdb; correzione della diagnosi sul `--restart`;
  individuati i casi L3/L5 e il leak; piano F1–F4.
- 2026-11-05: decisione utente — **precedenza alla configurazione** (con `respectPager`
  opzionale) e implementazione di **tutte** le fasi F1–F4.
- 2026-11-05: implementazione completata; unit test e matrice d'integrazione verdi; Valgrind
  pulito sul codice nuovo. Resta aperto il solo rischio obconf (§9.1) e la diagnosi in campo
  del trigger originale (§8.3).
- 2026-11-05: consegnata la procedura di campo `~/Openbox-Desktop-Layout-Test.md` (fuori dal
  repo), con script di monitoraggio provati.
