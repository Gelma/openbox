# SpecAI — Openbox: layout della griglia di desktop virtuali

> Documento di memoria di progetto. Va letto all'inizio di ogni sessione: contiene stato,
> scoperte verificate, decisioni e il piano di lavoro.
>
> Stato al 2026-11-05 (branch `gelma`, base upstream `main` @ `3a2fbc65`, serie 3.7):
> **analisi completata e verificata a runtime; design proposto; implementazione NON iniziata.**
> L'unica modifica di codice in questo repository è, finora, questo documento.

---

## 1. Contesto e sintomo

Setup d'uso (home di riferimento copiata in `/tmp/testnec/.config/openbox`):

- `rc.xml`: `<desktops><number>12</number></desktops>` e keybind `W-F1..F12`, `W-C-d`.
- `autostart` lancia `set_desktop_layout.py 4 3 &`, che scrive sulla root window la proprietà
  EWMH `_NET_DESKTOP_LAYOUT = [0, 4, 3, 0]` (HORZ, 4 colonne, 3 righe, angolo top-left).
- Openbox non ha alcun supporto di configurazione per righe/colonne: legge solo la proprietà.

Sintomo riportato: durante l'uso la griglia 4×3 collassa in un'unica striscia 1 riga × 12
colonne; la navigazione verticale e il popup dei desktop diventano una fila; un riavvio di
Openbox ripristina la griglia.

---

## 2. Mappa del codice rilevante

| File | Punto | Ruolo |
|------|-------|-------|
| `openbox/screen.c` | `64` | `ObDesktopLayout screen_desktop_layout` (unica copia in memoria) |
| `openbox/screen.c` | `1146-1195` | `screen_update_layout()`: reset incondizionato + lettura proprietà |
| `openbox/screen.c` | `1101-1139` | `screen_validate_layout()`: adatta righe/colonne a `screen_num_desktops` |
| `openbox/screen.c` | `826-930` | `get_row_col()`, `translate_row_col()`: matematica della griglia |
| `openbox/screen.c` | `992-1100` | `screen_find_desktop()`: navigazione direzionale (N/S/E/W) |
| `openbox/screen.c` | `507-530` | `screen_set_num_desktops()` → chiama `screen_update_layout()` |
| `openbox/screen.c` | `335-445` | `screen_startup()`: sessione XSMP, conteggio desktop, poi layout |
| `openbox/screen.c` | `947-985` | `screen_show_desktop_popup()`: dimensione icone dal layout |
| `openbox/event.c` | `808-815` | `PropertyNotify` su `_NET_DESKTOP_LAYOUT` → `screen_update_layout()` |
| `openbox/popup.c` | `400-470` | disegno del pager popup con `columns`/`rows` |
| `openbox/config.c` | `806-842` | `parse_desktops()`: solo `number`, `firstdesk`, `names`, `popupTime` |
| `openbox/config.c` | `1141-1146` | default: `config_desktops_num = 4`, `popupTime = 875` |
| `data/rc.xml` | `122-142` | sezione `<desktops>` documentata |
| `data/rc.xsd` | `109-125` | schema `ob:desktops` |
| `obt/prop.c` | `174-179` | `NET_WM_ORIENTATION_*` = 0/1 e `NET_WM_TOP*` = 0..3 (**valori numerici, non atomi internati**) |
| `obt/prop.c` | `251-289` | `get_all()`: `g_malloc` del buffer; richiede `format == 32` e `ret_items > 0` |
| `openbox/screen.c` | `230` | `_NET_DESKTOP_LAYOUT` è in `_NET_SUPPORTED` (ma solo **letto**) |
| `openbox/openbox.c` | `330-370` | `--startup` eseguito solo al primo avvio, non su `--restart` |
| `openbox/openbox.c` | `635-650` | `--startup` viene **rimosso** da `argv` (quindi non sopravvive al re-exec) |

Conseguenza importante: **Openbox non scrive mai `_NET_DESKTOP_LAYOUT`**; l'unica fonte è un
client esterno (pager/pannello/script). Non esiste alcun tag di configurazione per la geometria.

---

## 3. Comportamento verificato a runtime

Harness: `Xvfb :101 -screen 0 1280x800x24 -nolisten tcp -noreset`, binario in-tree
(`openbox/openbox` wrapper libtool), `rc.xml` con 12 desktop, stato interno letto con
`gdb -p PID -ex 'x/4dw &screen_desktop_layout'` (ordine: orientation, start_corner, rows,
columns; `HORZ=0`, `TOPLEFT=0`).

| Caso | Azione | Layout risultante | Esito |
|------|--------|-------------------|-------|
| L0 | proprietà `[0,4,3,0]`, avvio | `0 0 3 4` | corretto |
| — | dopo l'avvio | proprietà invariata | Openbox **non scrive** la proprietà |
| L1 | `xprop -root -remove _NET_DESKTOP_LAYOUT` | `0 0 1 12` | **collasso riprodotto** |
| L2 | riscrittura `[0,4,3,0]` | `0 0 3 4` | recupero automatico via `PropertyNotify` |
| L3 | riscrittura `[0,12,1,0]` (come farebbe un pager) | `0 0 1 12` | il pager vince, sintomo identico |
| L4 | proprietà a 3 elementi `[0,4,3]` | `0 0 3 4` | gestita (start corner = top-left) |
| L5 | proprietà a 2 elementi (malformata) | `0 0 1 12` | **collasso, silenzioso** |

Altre verifiche:

- `openbox --reconfigure` rilegge la proprietà: se è assente/malformata, **ricollassa** a 1×12.
- `openbox --restart` **non** riesegue `--startup` (marker su file rimasto a 1 esecuzione):
  il processo si ri-esegue con lo stesso PID e gli argomenti residui, quindi l'autostart
  (e lo script Python) **non** viene rilanciato.
- Un server X fresco (senza `-noreset`) cancella atomi e proprietà quando l'ultimo client si
  disconnette: nei test va sempre usato `-noreset`, altrimenti si osservano falsi negativi.
- La proprietà vuota o con `format != 32` viene rifiutata da `get_all()` → stesso fallback 1×12.

---

## 4. Valutazione dell'analisi precedente

**Confermato (con evidenza):**

1. `screen_update_layout()` azzera lo stato a `1 × N` **prima** di leggere la proprietà: è la
   causa diretta del collasso quando la lettura fallisce (L1, L5).
2. `event.c` reagisce a qualunque `PropertyNotify` sull'atomo, inclusa la cancellazione, senza
   distinguere `PropertyDelete` da un nuovo valore.
3. Openbox è consumatore passivo: nessuna scrittura della proprietà, nessun tag in
   `rc.xml`/`rc.xsd`, `parse_desktops()` limitato ai quattro tag noti.
4. Dopo un collasso non esiste alcun meccanismo di rilettura periodica: serve un nuovo evento
   (o un riavvio del WM) per riallineare lo stato.
5. La storia git è coerente: `a055fd9e` introduce la validazione e il fallback 1×N; `6f6ab8c4`
   salva/ripristina il layout **solo** via XSMP; `ead75d7f` aggiunge la rilettura post-reconfigure.

**Impreciso / da correggere:**

1. **"Il restart riesegue l'autostart e quindi lo script"**: falso per `openbox --restart`
   (verificato: `--startup` è rimosso da `argv` e non viene rilanciato). Quindi la spiegazione
   del "il restart risolve" non può essere quella. Le spiegazioni compatibili sono due:
   (a) la proprietà è ancora presente e valida, e lo stato interno era rimasto "latched" a 1×12
   per una lettura fallita in un istante precedente; (b) il "restart" dell'utente è in realtà un
   riavvio di sessione/`openbox-session`, che rilancia l'autostart. Va discriminato in campo
   (§8, `xprop -spy`).
2. La citazione EWMH ("la proprietà è del Pager, non del WM") è corretta come intento della
   specifica, ma non vieta al WM di scriverla; il progetto deve comunque dichiarare una policy
   esplicita di precedenza (§6.1).
3. Non è vero che serva un "pager esterno perché il layout funzioni": il popup e la navigazione
   usano solo lo stato interno, che è alimentato anche da uno script (come in questo setup).

**Mancante nell'analisi precedente:**

1. Il caso **L5** (proprietà malformata, `format` errato, 0 elementi) produce lo stesso collasso
   in modo silenzioso: non c'è alcun log, quindi la diagnosi in campo è impossibile.
2. **Perdita di memoria**: nei percorsi `return` di `screen_update_layout()` il buffer `data`
   (allocato con `g_malloc` in `get_all()`) non viene liberato.
3. La **precedenza**: un pager che scrive un layout valido diverso (es. 12×1) vince sul layout
   dell'utente — è un caso reale di collasso "legittimo" (L3), non solo un guasto.
4. Il comportamento al **cambio del numero di desktop** (`AddDesktop`/`RemoveDesktop`,
   `_NET_NUMBER_OF_DESKTOPS`): il layout viene ricalcolato dalla proprietà o dal fallback, ma la
   proprietà non viene mai aggiornata, quindi resta incoerente con il nuovo conteggio.
5. La necessità di **diagnostica** (`ob_debug`) per distinguere "proprietà assente" da
   "proprietà invalida" in produzione.

---

## 5. Modello di guasto

Lo stato interno può diventare 1×N solo passando da `screen_update_layout()`; quindi il collasso
richiede **una lettura fallita** in quel momento. Le cause possibili, tutte verificate o
verificabili:

- **A. Proprietà cancellata** (pannello/pager che esce, `xprop -remove`, tool che ripulisce la
  root). L'evento di cancellazione stesso scatena il ricalcolo → 1×12 (L1).
- **B. Proprietà sostituita** da un valore valido ma diverso (pager con 12 desktop su una riga).
  Nessun guasto del WM: vince il pager (L3).
- **C. Proprietà malformata / tipo o formato sbagliato / 0 elementi**: `get_all()` fallisce o il
  parsing fa `return` → 1×12 senza log (L5).
- **D. Stato "latched"**: dopo A o C, se nessuno riscrive la proprietà non arriva più alcun
  evento; lo stato resta 1×12 anche se la proprietà tornasse valida senza notifica (es. scritta
  mentre il WM non era in ascolto). Serve una rilettura esplicita (restart/reconfigure) per
  riallineare — coerente con l'osservazione "il restart risolve".
- **E. Cambio conteggio desktop**: ricalcolo su proprietà non aggiornata (nessuna scrittura lato
  WM) → griglia incoerente con `screen_num_desktops`.

Punto chiave di progetto: **Openbox non ha memoria della geometria voluta**. Tutto dipende da una
proprietà esterna che chiunque può cancellare o riscrivere, e il fallback è una costante compilata.

---

## 6. Modus operandi proposto

### 6.1 Principi

1. **La configurazione è l'autorità di default.** Se `rc.xml` dichiara una geometria, quella
   vince: è l'unico modo per garantire una griglia stabile contro attori esterni. La proprietà
   EWMH diventa un *hint* che il WM pubblica e difende, non l'unica fonte di verità.
2. **Non distruggere mai uno stato valido.** Se la proprietà manca o è invalida, si conserva il
   layout corrente (se ancora valido per il numero di desktop), poi la configurazione, poi il
   fallback 1×N.
3. **Nessun conflitto infinito.** Il WM riscrive la proprietà solo quando il valore presente
   differisce da quello voluto; confronto prima della scrittura, così il proprio
   `PropertyNotify` non genera un loop.
4. **Diagnostica sempre.** Ogni fallback va loggato con `ob_debug` (assente / invalida /
   conteggio cambiato), così il problema è ricostruibile dai log.
5. **EWMH rispettata quando non c'è configurazione.** Senza `<layout>` in `rc.xml` il
   comportamento resta quello attuale (pager proprietario, fallback 1×N): nessuna regressione
   per chi non usa la funzione.

### 6.2 Modello dati

In `openbox/config.h` / `config.c`:

```c
typedef struct ObDesktopLayout {     /* già esistente in screen.h */
    ObOrientation orientation;       /* OB_ORIENTATION_HORZ | VERT   */
    ObCorner      start_corner;      /* OB_CORNER_*                 */
    guint         rows;              /* 0 = automatico              */
    guint         columns;           /* 0 = automatico              */
} ObDesktopLayout;

extern ObDesktopLayout config_desktops_layout;  /* NEW */
extern gboolean        config_desktops_layout_set; /* NEW: tag presenti */
```

- Default: struttura a zero e `layout_set = FALSE` → comportamento legacy.
- Parsing tollerante: `<rows>`, `<columns>`, `<orientation>horizontal|vertical`,
  `<startingCorner>top-left|top-right|bottom-left|bottom-right`.
- Validazione contro `<number>`: se `rows*columns < number` si espande con
  `screen_validate_layout()`; se uno dei due è 0 resta automatico (come oggi).

### 6.3 Algoritmo `screen_update_layout()` (pseudo)

```c
void screen_update_layout(void)
{
    ObDesktopLayout prop, cfg = config_desktops_layout;
    gboolean has_prop = read_layout_property(&prop);   /* libera sempre il buffer */

    if (has_prop && screen_validate_layout(&prop)) {
        if (config_desktops_layout_set && !layout_equal(&prop, &cfg)) {
            /* configurazione esplicita: il WM ha l'autorità */
            apply_layout(&cfg);          /* valida contro screen_num_desktops */
            publish_layout(&cfg);        /* XChangeProperty, solo se diversa */
        } else {
            apply_layout(&prop);         /* pager/EWMH */
        }
        return;
    }

    /* proprietà assente o invalida */
    ob_debug("desktop layout: %s, fallback", has_prop ? "invalid" : "missing");

    if (layout_valid_for(&screen_desktop_layout, screen_num_desktops)) {
        /* conserva lo stato corrente: nessun collasso */
        publish_layout(&screen_desktop_layout);
        return;
    }
    if (config_desktops_layout_set) {
        apply_layout(&cfg);
        publish_layout(&cfg);
        return;
    }
    screen_desktop_layout = (ObDesktopLayout){ OB_ORIENTATION_HORZ,
                                               OB_CORNER_TOPLEFT,
                                               1, screen_num_desktops };
}
```

Dettagli:

- `layout_valid_for()` = `screen_validate_layout()` su una copia, senza mutare lo stato.
- `apply_layout()` valida sempre contro `screen_num_desktops` (anche dopo
  `AddDesktop`/`RemoveDesktop`).
- `publish_layout()` scrive `[orientation, columns, rows, start_corner]` come `CARDINAL[4]`
  solo se il valore presente è diverso (guardia anti-loop) e imposta `layout_published = TRUE`.
- Il layout di sessione XSMP, se presente, mantiene la precedenza attuale (è una scelta esplicita
  salvata nella sessione).

### 6.4 Pubblicazione e ownership

- All'avvio, se `config_desktops_layout_set`, il WM scrive la proprietà: i pannelli/pager vedono
  subito la griglia giusta e non serve più alcuno script in `autostart`.
- Su `PropertyNotify`:
  - valore valido uguale → nulla da fare;
  - valore valido diverso con configurazione esplicita → il WM riasserisce la configurazione e
    logga (policy "config wins", default);
  - valore assente/malformato → ripristina (stato corrente valido o configurazione) e ripubblica.
- Policy alternativa per chi vuole l'EWMH puro: attributo `<layout respectPager="yes">` che fa
  vincere il pager. Non necessario per il caso d'uso, utile per non imporre un comportamento.

### 6.5 Cambio del numero di desktop

In `screen_set_num_desktops()` il ricalcolo usa lo stesso algoritmo; se la geometria
configurata non copre più il conteggio viene adattata e **ripubblicata**, così la proprietà non
resta incoerente. Con geometria automatica si conserva il comportamento attuale.

### 6.6 Sintassi di configurazione proposta

```xml
<desktops>
  <number>12</number>
  <firstdesk>1</firstdesk>
  <layout>
    <orientation>horizontal</orientation>  <!-- horizontal | vertical -->
    <columns>4</columns>
    <rows>3</rows>
    <startingCorner>top-left</startingCorner>
  </layout>
  <names>...</names>
  <popupTime>875</popupTime>
</desktops>
```

Aggiornamenti necessari: `data/rc.xml` (esempio + commento), `data/rc.xsd`
(`complexType name="desktopLayout"`, `xsd:all`), `doc/openbox.1.in` e documentazione rc se
presente, `CHANGELOG` (voce in cima alla sezione 3.7).

### 6.7 Diagnostica

Aggiungere un `ob_debug_type(OB_DEBUG_APP_BUGS, ...)` (o nuova categoria) in ogni punto di
fallback, con: valore letto, esito della validazione, `screen_num_desktops`, layout applicato.
Senza questo, in campo non è possibile distinguere A da C.

---

## 7. Piano di implementazione a fasi

| Fase | Contenuto | File | Rischio |
|------|-----------|------|---------|
| **F1** | Hardening senza nuova configurazione: niente reset distruttivo se la proprietà manca/è invalida; `g_free(data)` su tutti i percorsi; log dei fallback | `openbox/screen.c` | basso, isolato |
| **F2** | Configurazione `<layout>`: globals, parser, default, `rc.xsd`, `rc.xml`, docs | `config.c/h`, `data/rc.xsd`, `data/rc.xml`, `doc/openbox.1.in`, `CHANGELOG` | medio (compatibilità config) |
| **F3** | Pubblicazione della proprietà + ownership + guardia anti-loop + ripubblicazione al cambio conteggio | `screen.c` (nuove helper statiche), `screen.h` se serve | medio: interazione con pager |
| **F4** | Test: unit test della funzione pura di risoluzione (§6.3) + test d'integrazione Xvfb (matrice L0–L5 + restart/reconfigure/cambio conteggio) | `tests/` (nuovo harness) o `obt/obt_unittests` se si estrae la logica pura | basso |

Ordine consigliato: F1 (risolve già il 90% dei collassi: A e C non distruggono più lo stato),
poi F3/F2 (rendono il WM autosufficiente), quindi F4.

Verifica obbligatoria per ogni fase: build completa (`make -j`), matrice Xvfb, `git diff` senza
toccare file non correlati, `valgrind` sul percorso di `screen_update_layout` (il leak F1 è
dimostrabile con `--leak-check`).

---

## 8. Riproduzione e test

Harness usato (nessuna dipendenza extra, `xdotool` assente):

```sh
Xvfb :101 -screen 0 1280x800x24 -nolisten tcp -noreset &
DISPLAY=:101 python3 set_desktop_layout.py 4 3
DISPLAY=:101 ./openbox/openbox --config-file /tmp/ob-grid-test/rc.xml &
gdb -p $! -batch -ex 'file ./openbox/.libs/openbox' -ex 'x/4dw &screen_desktop_layout'
# collasso:      xprop -root -remove _NET_DESKTOP_LAYOUT
# pager 12x1:    python3 set_desktop_layout.py 12 1
# malformata:    XChangeProperty con 2 elementi / format 16
```

Casi da coprire nei test automatici: L0–L5, `--reconfigure`, `--restart`, cambio conteggio
(12→13→12), proprietà con `format 16`, proprietà con 0 elementi, nessuna configurazione
(comportamento legacy invariato).

**Procedura per identificare il colpevole in campo** (da consegnare all'utente, fuori dal repo):
avviare `xprop -spy -root _NET_DESKTOP_LAYOUT | while read ...` con timestamp in un file, usare il
desktop finché si presenta il difetto, poi correlare l'ora con i log delle applicazioni avviate.
Se alla comparsa del difetto la proprietà risulta ancora `0, 4, 3, 0`, il guasto è di tipo D
(stato latched, nessun evento) e il restart riallinea; se la proprietà è sparita o è `0, 12, 1, 0`
il guasto è A o B e si può risalire al client che l'ha scritta.

---

## 9. Rischi, compatibilità, domande aperte

1. **Policy di precedenza** (decisione richiesta all'utente): configurazione che vince sempre
   (default proposto) oppure pager che vince quando scrive un layout valido. La prima elimina il
   sintomo per definizione; la seconda è più fedele a EWMH ma lascia il caso B.
2. **obconf**: riscrive `rc.xml` quando si modificano le impostazioni. Va verificato che non
   elimini il nuovo blocco `<layout>`; se lo elimina, valutare un file separato o una patch a
   obconf (fuori scope).
3. **Pannelli con layout proprio**: con "config wins" il pannello può mostrare una geometria
   diversa da quella del WM; documentare e prevedere `respectPager` come valvola di sfogo.
4. **XSMP**: il layout salvato in sessione deve continuare a prevalere; non toccare
   `session.c` in F1/F2.
5. **Multi-monitor / RandR**: il layout è globale; i test devono includere un `xrandr` a caldo
   per escludere effetti collaterali su `screen_resize()` (che non chiama `screen_update_layout`).
6. **Traduzioni**: eventuali nuove stringhe utente vanno in `po/` (le modifiche `po/*.po` già
   presenti nel worktree sono di un'altra sessione: non vanno incluse nei commit di questo
   lavoro).

---

## 10. Storico delle decisioni

- 2026-11-05: analisi iniziale (commit `ca930ad6`), poi spostamento dei file (`4eb8307f`).
- 2026-11-05: verifica runtime con Xvfb+gdb; correzione della diagnosi sul `--restart`;
  individuati i casi L3/L5 e il leak; definito il piano a fasi F1–F4. Implementazione da
  concordare.
