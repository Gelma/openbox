# Aggiustare.md — Analisi statica di Openbox (solo diagnosi)

Stato: **analisi completata, nessuna modifica al codice sorgente**.
Questa fase produce solo il presente report; le correzioni eventuali verranno
fatte in una sessione successiva, partendo dalla lista d'azione in fondo.

- Tree analizzato: `/tmp/openbox`
- Commit di riferimento: `ca930ad6` (`docs: add SpecAI.md with architecture analysis and desktop layout specs`)
- Data analisi: 2026-11-05
- Build di analisi: `.analysis-build/` (gcc 14.2, out-of-tree) e `.scan-build/` (clang 23, out-of-tree)
- Artefatti grezzi: `.analysis-build/`, `.scan-build/` — **non tracciati da git**

---

## 1. Strumenti e metodo

| Strumento | Versione | Uso |
|---|---|---|
| cppcheck | 2.23 dev | `--enable=all --inconclusive --check-level=exhaustive --std=c11 --library=posix`, progetto da `compile_commands.json`, `-j8`, cache per `unusedFunction` |
| semgrep | 1.183.0 | ruleset `p/c`, `p/security-audit`, `p/cwe-top-25` (846 regole), output JSON |
| scan-build | 23.1.5 (clang 23) | wrapper `ccc-analyzer` + `c++-analyzer`, build clang out-of-tree |
| clang | 23.1.5 | ricompilazione di ogni TU con `-Wall -Wextra -fsyntax-only` |

### 1.1 Perché serve `compile_commands.json`

cppcheck e clang hanno bisogno degli stessi `-D`/`-I` del build reale: senza
`HAVE_CONFIG_H`, `XRANDR`, `XINERAMA`, `USE_LIBRSVG`, ecc. l'analisi vede codice
morto o tipi sbagliati.

Procedura usata (tutta dentro il repo, nessun file sorgente modificato):

```bash
cd /tmp/openbox
./bootstrap
mkdir -p .analysis-build && cd .analysis-build
../configure --disable-nls --enable-startup-notification --disable-session-management
make clean && make V=1 -j8 > build_v1.log
python3 mk_compile_db.py build_v1.log      # genera compile_commands.json (100 TU)
```

Nota importante: `mk_compile_db.py` conserva la riga di comando **grezza**.
Usare `shlex`/`shlex.split` su queste righe mangia le virgolette dei define
(`-DCONFIGDIR=\"/usr/local/etc/xdg\"`), trasformandoli in macro invalide: la
prima passata aveva prodotto falsi positivi (es. `screen.c:1407`) proprio per
questo motivo. La versione attuale non usa `shlex`.

Comandi di analisi:

```bash
# cppcheck (risultato finale, con unusedFunction)
cppcheck --project=compile_commands.json --enable=all --inconclusive \
  --check-level=exhaustive --std=c11 --library=posix --inline-suppr \
  --suppress=missingIncludeSystem --suppress=unmatchedSuppression \
  --cppcheck-build-dir=cppcheck-cache \
  --output-file=cppcheck-full.xml --xml --xml-version=2 -j8

# semgrep
semgrep --config=p/c --config=p/security-audit --config=p/cwe-top-25 \
  --metrics=off --timeout=30 --json --output=.analysis-build/semgrep.json \
  --exclude=.analysis-build --exclude=.git --exclude=po --exclude=doc .

# scan-build (build dir dedicata, clang; NON usare --keep-cc, bypassa l'analizzatore)
cd /tmp/openbox/.scan-build
CC=clang-23 CXX=clang++-23 ../configure --disable-nls \
  --enable-startup-notification --disable-session-management
scan-build-23 -o scan-out make CC=/usr/lib/llvm-23/libexec/ccc-analyzer \
  CXX=/usr/lib/llvm-23/libexec/c++-analyzer -j8

# warning del compilatore clang
cd /tmp/openbox/.analysis-build && python3 clang_warnings.py
```

### 1.2 Copertura

- File `.c` tracciati: **148**; unità compilate e analizzate: **100**.
- Non analizzati (**48**): `tests/*.c` (test X11 interattivi), `obt/tests/*.c`,
  `obrender/test.c`. Non fanno parte del build di default di Openbox e non sono
  raggiunti da `make`; per analizzarli serve un harness dedicato.
- semgrep: 287 file scansionati (include script e XML), ma **26 file C non
  sono stati parsati** (vedi §4.2): la copertura semgrep sul C è parziale.
- cppcheck: 100/100 TU; qualche header di sistema non viene risolto
  (2 diagnostiche "error" di parsing, vedi §3.3).

---

## 2. Sintesi

| Strumento | Diagnostiche | Dopo il rumore noto | Da valutare/azione |
|---|---:|---:|---:|
| cppcheck | 287 | 60 | 6-8 |
| semgrep | 3 | 3 | 3 (1 informativa) |
| scan-build | 46 | 46 | 10-12 |
| clang `-Wall -Wextra` | 27 | 11 | 4 |

Tema trasversale emerso da tutti gli strumenti: **il build di release disabilita
le asserzioni** (`-DNDEBUG -DG_DISABLE_ASSERT`, cfr. `m4/openbox.m4:76`).
Diverse funzioni usano `g_assert()` come unica validazione di ingressi o di
errori X11 (`g_assert(sumXY != 0)`, `g_assert(ret != BadWindow)`,
`g_assert(qnum > 0)`, `g_assert_not_reached()`): in produzione quei controlli
**non esistono**. Le segnalazioni di "uninitialized"/"division by zero" che
dipendono da queste asserzioni non sono rumore dello strumento: sono percorsi
realmente non protetti nel binario release.

---

## 3. cppcheck

Esecuzione: 287 diagnostiche totali — 3 `error`, 129 `warning`, 153 `style`,
1 `portability`, 1 `information`.

### 3.1 Rumore noto (da non inseguire)

| ID | N. | Perché è rumore |
|---|---:|---|
| `invalidPrintfArgType_uint` | 124 | Tutte in `obt/bsearch_unittest.c`, generate dalla macro `EXPECT_UINT_EQ` di `obt/unittest_base.h:40`: stampa `%u` con costanti `int` nei test. Falso positivo da macro, non un bug di produzione. |
| `unusedFunction` | 103 | Analisi per-TU: funzioni `static` usate solo via callback/tabelle, più helper di libreria (`obt/xml.c` 27, `obt/xqueue.c` 14, `obt/prop.c` 10...). Da rivedere solo se si vuole ridurre API morta. |
| `funcArgNamesDifferent` | 19 | Nomi dei parametri diversi tra prototipo e definizione (`obt/xml.c`, `obt/link.c`, `obt/prop.c`). Stile. |
| `constParameterPointer` / `constVariablePointer` / `constParameterCallback` / `constStatement` | 21 | Suggerimenti `const`, incluso il `(void)junk, (void)major, (void)minor;` di `obt/display.c:67`. Stile. |
| `variableScope` | 5 | Riduzione dello scope di variabili locali. Stile. |
| `arrayIndexOutOfBoundsCond` | 4 | `obt/bsearch_unittest.c` (macro `BSEARCH`), test. |
| `preprocessorErrorDirective`, `syntaxError` | 2 | Header di sistema: `pango/pangoxft-render.h:34` (`#error` su versione Xft) e `sys/types.h:163` (`__GNUC_PREREQ`). Limite di parsing, non del progetto. |

### 3.2 Segnalazioni che meritano una verifica (cppcheck)

| # | Posizione | ID | Contenuto | Valutazione |
|---|---|---|---|---|
| C1 | `obt/link.c:181` | `uninitvar`, `unreadVariable` | `c = end = end+1; /* next */` dentro il ciclo su `Categories`: `end` è dichiarata a riga 173 e **mai inizializzata**; `c` è assegnata e mai riletta. | **Reale (UB)**. Riga rimasta da un refactoring del 2010 (commit `78a8f407`). Confermata anche da scan-build (§5). clang `-Wunused-but-set-variable` non scatta perché `c` è letta altrove nella funzione (ciclo su `link->d.app.exec`, righe 129-140): lo store morto è visibile solo all'analizzatore di percorso. |
| C2 | `obt/link.c:61` | `unusedStructMember` | `struct _ObtLinkDir { } dir;` dentro `_ObtLinkData`: struct vuota annidata in `d.dir`, mai usata. | Pulizia: campo morto (estensione GNU "empty struct"). Verificare se previsto per i link `.directory`. |
| C3 | `obt/paths.c:107-117` | `getpwuidCalled`, `unreadVariable`, `unusedVariable` | `pw = getpwuid(*u); name = pw->pw_name;`: `name` mai usata, `gr` dichiarata e mai usata, `pw` dereferenziata senza controllo NULL, `getpwuid` non reentrant. | **Reale (robustezza)**: se `getpwuid()` fallisce (UID non in `/etc/passwd`) si ha deref di NULL. `getpwuid_r` sarebbe la variante corretta. |
| C4 | `obt/ddparse.c:759` | `unreadVariable` | `parse.lineno` assegnato e mai letto. | Pulizia; verificare che il numero di riga non serva nei messaggi d'errore. |

### 3.3 Nota sui due `error` di parsing

Non sono difetti del progetto: cppcheck non riesce a valutare
`__GNUC_PREREQ` in `sys/types.h` né la guardia di versione Xft in
`pangoxft-render.h`. Il resto della tu viene comunque analizzato.

---

## 4. semgrep

846 regole su 287 file, **3 findings**.

| # | Posizione | Regola | Contenuto | Valutazione |
|---|---|---|---|---|
| S1 | `obt/xml.c:155` | `cpp.libxml2.security.libxml2-audit-parser` (CWE-611) | Uso di libxml2 per il parsing XML: verificare che i documenti non fidati non possano caricare DTD/XInclude o accedere alla rete. | **Informativa**: `obt/xml.c` legge `rc.xml`, `menu.xml`, temi e file `.desktop`. Sono file locali dell'utente, ma un tema/menu scaricato è input non fidato. Da verificare `xmlReadFile`/opzioni (`XML_PARSE_NONET`, `XML_PARSE_NOENT` assente). |
| S2 | `tools/gdm-control/gdm-control.c:179` | `insecure-use-string-copy-fn` (CWE-676) | `strcpy(addr.sun_path, GDM_PROTOCOL_SOCKET_PATH1)` | **Basso**: le sorgenti sono costanti di compilazione; il rischio è solo se un path supera `sizeof(addr.sun_path)` (108). Meglio `g_strlcpy`/`strncpy` + terminazione. |
| S3 | `tools/gdm-control/gdm-control.c:181` | idem | `strcpy(addr.sun_path, GDM_PROTOCOL_SOCKET_PATH2)` | Come S2. |

### 4.1 Cosa non ha trovato

Nessun findings per: command injection, `system()`/`exec` non controllati,
formati stampa, `sprintf`, permission, secrets. Le funzioni pericolose
(`strcpy`, `sprintf`, `gets`) sono poche e concentrate nei tool.

### 4.2 Limite di copertura (26 file non parsati)

Il parser C di semgrep si ferma su costrutti GNU/GLib. File con errore di
parsing: `obrender/color.c`, `obrender/font.c`, `obrender/gradient.c`,
`obrender/image.c`, `obrender/theme.h`, `obt/bsearch.h`,
`obt/bsearch_unittest.c`, `obt/ddparse.c`, `obt/display.h`, `obt/keyboard.c`,
`obt/keyboard.h`, `obt/link.h`, `obt/paths.h`, `obt/prop.c`, `obt/prop.h`,
`obt/signal.h`, `obt/unittest_base.h`, `obt/xml.c`, `obt/xml.h`,
`obt/xqueue.h`, `openbox/actions/if.c`, `openbox/event.c`, `openbox/focus.c`,
`openbox/openbox.c`, `openbox/place_overlap.c`, `openbox/screen.c`.

Conseguenza: i 3 findings sono un sottoinsieme; per queste TU valgono
cppcheck/scan-build come copertura primaria.

---

## 5. scan-build (clang static analyzer)

46 report. Report HTML: `.scan-build/scan-out/2026-11-05-230139-95230-1/index.html`.

Distribuzione: 20 dead assignment, 8 "assigned value is uninitialized",
8 "uninitialized argument value", 2 null deref, 2 division by zero,
2 "garbage or undefined", 1 dead increment, 1 dead nested assignment,
1 stream EOF, 1 "use of zero allocated".

### 5.1 Findings da valutare (con verifica sul codice)

| # | Posizione | Tipo | Dettaglio | Valutazione |
|---|---|---|---|---|
| B1 | `openbox/menu.c:819` | Uninitialized argument (3°) | `sort_range(self, start, last, len)` con `last` e `len` non inizializzate quando `self->entries == NULL`: il ciclo non viene eseguito, poi `sort_range()` testa `if (!len) return;` su un valore indefinito e, se `len != 0`, dereferenzia `start` (NULL) e `g_list_next(end)` con `end` indefinito. | **Reale (UB)**: menu vuoto (menu da pipe vuoto, menu generato dinamicamente) può mandare in crash. Inizializzare `last = NULL; len = 0;` prima del ciclo. |
| B2 | `obt/link.c:181` | Result of operation is garbage + Dead assignment | `end+1` su puntatore non inizializzato. | **Reale (UB)**, coincide con C1: la riga va rimossa. |
| B3 | `obrender/image.c:794` | Division by zero | `red /= sumXY` (e gli altri canali). `sumXY` resta 0 se i cicli interni non girano; la guardia è `g_assert(sumXY != 0)`, **disabilitata** in release. `ratioX = (srcW << FRACTION) / dstW` è 0 quando `srcW == 0`. | **Reale su input degenere**: `RrImageDrawRGBA`/`RrImageDrawImage` con immagine di larghezza/altezza 0 (tema/icona malformati o non caricati) → SIGFPE. Serve un controllo runtime, non un assert. |
| B4 | `openbox/client.c:1280-1282` | Dead assignment (`ret`) | `ret = XGetWindowAttributes(...); g_assert(ret != BadWindow);` — l'assert sparisce in release: se la chiamata fallisce, `wattrib` resta non inizializzato e viene usato per `RECT_SET`/`border_width`. | **Reale (robustezza)**: gestire esplicitamente l'errore. Confermato da clang `-Wunused-but-set-variable` (§6). |
| B5 | `openbox/frame.c:70` | Dead assignment (`ret`) | Stessa struttura di B4 (`XGetWindowAttributes` + `g_assert`). | **Reale (stessa classe)**. |
| B6 | `openbox/place.c:474` | Dead assignment (`ret`) | `ret = place_...(...) || ...` e poi `ret` non usato (la funzione ritorna `TRUE`?). | Da verificare: probabile refactoring incompleto; nessun impatto funzionale evidente. |
| B7 | `obt/xqueue.c:81,106` | Use of zero allocated + Division by zero | `grow()` con `qsz == 0` calcola `newsz = 0`; poi `qend = (qend + 1) % qsz` divide per zero. `xqueue_init()` imposta `qsz = MINSZ (16)` e viene chiamata da `obt/display.c:125`, quindi in condizioni normali il percorso non è raggiungibile. | **Basso**: difetto di robustezza se `read_events()` viene usata prima di `xqueue_init()` o dopo `xqueue_destroy()`. Aggiungere `if (qsz == 0) newsz = MINSZ;` renderebbe l'API autosufficiente. |
| B8 | `obt/ddparse.c:361` | Stream already in EOF | `fread()` chiamata quando lo stream è già a EOF; il ciclo esce comunque per la condizione `feof(f) && *read < *size`. | **Innocuo**: solo una lettura inutile. |
| B9 | `tools/obxprop/obxprop.c:174` | Uninitialized argument (2°) | `XGetAtomName(d, ((guint32*)val)[i])`: l'analizzatore non modella la garanzia che `val` sia grande `n*4` quando `nret>0`. | **Probabile falso positivo**; utile comunque un controllo `val != NULL`/`n > 0`. |
| B10 | `openbox/event.c:832,837` | Null pointer dereference | `event_enter_client(client)` con `client` NULL. Il chiamante (`event.c:1158`) può avere `client == NULL`, ma `frame_context(NULL, ...)` restituisce `OB_FRAME_CONTEXT_NONE` (`frame.c:1403`) e il caso `FRAME` non è raggiungibile. | **Probabile falso positivo** (analisi inter-TU assente). Difesa consigliata: `if (!client) return;` in `event_enter_client`; oggi la funzione si affida a un invariante non documentato. |
| B11 | `event.c:1240,1446,1609`; `client.c:606`; `client_menu.c:173,227` | Uninitialized argument | Tutti `event_end_ignore_all_enters(ignore_start)` dove `ignore_start` è assegnata sotto `if (!config_focus_under_mouse)`. L'analizzatore ipotizza che la globale `config_focus_under_mouse` possa cambiare tra le due letture (in mezzo ci sono chiamate a `frame_hide`, `stacking_restack_request`, `client_set_state`, ...). | **Teorico/falso positivo in pratica**, ma è un pattern fragile ripetuto 6 volte: la variabile va inizializzata alla dichiarazione o la coppia start/end va racchiusa in un helper. |
| B12 | `openbox/moveresize.c:549` | Garbage value (`trydh`) | `trydh` è assegnata solo dentro `if (!keyboard && (max_ratio || min_ratio))`, la stessa condizione che protegge l'uso. | **Falso positivo**; eventualmente inizializzare `trydw = trydh = 0` per chiarezza. |
| B13 | `obt/paths.c:108,117` | Dead assignment / unread | Vedi C3. | **Reale (robustezza)**, non solo rumore. |
| B14 | `obrender/render.c:179` | Dead assignment (`transferred`) | `transferred = 1;` dentro `if (!transferred)`, dopo il ciclo; il valore non viene più letto. | Innocuo, pulizia. |
| B15 | `openbox/popup.c:217` | Dead nested assignment | `emptyx = emptyy = 0;`: il valore di `emptyy` non viene riletto. | Innocuo, pulizia. |
| B16 | `openbox/menuframe.c:692,695,697` | Dead assignment/increment | `e`, `tw`, `th` calcolati e mai usati nel ramo di rendering. | Da verificare: potrebbe indicare un calcolo di layout rimasto incompleto. |
| B17 | `openbox/resist.c:241,251,269,279,369,380,395,406`; `openbox/moveresize.c:814,824`; `openbox/event.c:728,746` | Dead assignment | Variabili `dlt`, `drb`, `resist`, `used` assegnate e non lette in tutti i rami dello `switch`. | Pulizia; nessun impatto funzionale. |
| B18 | `openbox/place.c:181,183,191,202,205,227` | Assigned value is uninitialized | `choice[i].flags |= ...`: l'analizzatore non vede l'inizializzazione in `for (i...) choice[i].flags = 0;` (righe 166-169) o non modella `g_new`. | **Probabile falso positivo**; confermare con un test di placement su più monitor. |
| B19 | `openbox/screen.c:1407` | Assigned value is uninitialized | Lettura di `(*xin_areas)[0]` appena scritta da `RECT_SET` nel ramo Xinerama. | **Probabile falso positivo**; da riverificare se si tocca `get_xinerama_screens`. |
| B20 | `obrender/image.c:65` | Assigned value is uninitialized | `pic->sum += *(data++)` su dati immagine forniti da X/rsvg. | Rumore: la memoria è scritta da X; l'analizzatore non lo sa. |

---

## 6. clang `-Wall -Wextra`

27 warning su 100 TU. Elenco completo in `.analysis-build/clang-warnings.txt`.

| Categoria | N. | Note |
|---|---:|---|
| `-Wdeprecated-declarations` | 16 | GLib (`g_pattern_match*`, `g_memdup`), librsvg (`rsvg_handle_*`), pango-xft. Manutenzione a lungo termine, non bug. |
| `-Wunused-but-set-variable` | 5 | `client.c:1280`, `frame.c:70`, `place.c:474` (vedi B4/B5/B6), `obt/paths.c:108` (`name`), `tools/obxprop/obxprop.c:133` (`i`). |
| `-Wincompatible-pointer-types-discards-qualifiers` | 2 | `openbox/menu.c:411,412`: assegnazione di stringhe letterali a `gchar *` (`name`, `title`, `collate_key`). Solo qualificatore, ma `menu.c` scrive? Verificare: se non scrive, il tipo dovrebbe essere `const gchar *`. |
| `-Wc23-extensions` | 1 | `openbox/actions/moveresizeto.c:122`: label a fine blocco composto; accettato come estensione C23. |
| `-Wunused-but-set-global` | 1 | `obt/display.c:56`: `xerror_ignore` è letta solo dentro `#ifdef DEBUG` (riga 145); in release è scritta e mai letta. Fisiologico, non un difetto. |
| `-Wunused-variable` | 1 | `obt/paths.c:109`: `gr`. |
| `-Wsometimes-uninitialized` | 1 | **`tools/gnome-panel-control/gnome-panel-control.c:66`**: `act_atom` non inizializzata se lo `switch` cade nel `default` (dove c'è `assert(0)`, disabilitato in release). | 

Nota: `gnome-panel-control` costruisce `act_atom` solo per `MAIN_MENU` e
`RUN_DIALOG`; se `parse_options` restituisse un valore fuori enum, in release
si invierebbe un ClientMessage con atom indefinito. Inizializzare
`act_atom = None` e gestire il default senza `assert`.

---

## 7. Correlazioni

- **`obt/link.c:181`**: unico punto segnalato da cppcheck (`uninitvar`,
  `unreadVariable`), scan-build (`garbage value`, `dead assignment`) e
  logicamente coerente con `-Wunused-but-set-variable` (non emesso perché `c`
  è a sua volta usata nella stessa espressione). È il candidato n. 1 per una
  correzione a basso rischio.
- **Asserzioni disabilitate**: spiegano B3, B4, B5 e il warning su
  `gnome-panel-control`. Sono i casi in cui la "diagnosi statica" descrive un
  comportamento reale del binario di release.
- **`obt/paths.c`**: tre strumenti convergono su codice morto
  (`name`, `gr`) e c'è un deref di `pw` non controllato (C3): intervento
  piccolo ma con impatto reale su utenti con UID di rete.
- **`openbox/menu.c:819`**: unico caso in cui un percorso di crash è
  raggiungibile con input normale (menu vuoto), non solo con dati corrotti.

## 8. Limiti dell'analisi

- Nessuna analisi dinamica (niente Valgrind/ASan in questa fase), nessun test
  runtime su X11.
- scan-build analizza TU per TU senza LTO: i falsi positivi su null-deref e
  variabili globali (B10, B11, B18) sono attesi.
- `tests/` e gli harness manuali non sono compilati dal build di default:
  48 file su 148 non sono coperti.
- semgrep non parsa 26 file C: la sua copertura è complementare, non esaustiva.
- cppcheck non risolve due header di sistema: `pango` e `sys/types.h`.
- Il build usato è una configurazione "tipica" (NLS off, session management
  off, librsvg/Imlib2/Xcursor/StartupNotification on): con `--enable-debug` le
  asserzioni sarebbero attive e alcuni percorsi cambierebbero.

---

## 9. Lista d'azione proposta (per la prossima sessione)

Nessuna di queste modifiche è stata applicata. Ordine suggerito.

### P1 — Difetti reali, intervento piccolo

- [ ] **A1** `obt/link.c:181`: rimuovere `c = end = end+1;` e la dichiarazione
      `gchar *end;` (riga 173). Verificare che `c` non serva altrove nel blocco.
- [ ] **A2** `openbox/menu.c:819`: inizializzare `last = NULL; len = 0;` prima
      del ciclo in `menu_sort_entries()` e proteggere `sort_range()` da
      `start == NULL` (menu vuoto).
- [ ] **A3** `obt/paths.c:107-117`: controllare `pw != NULL` prima di
      `pw->pw_name`; rimuovere `name` e `gr`; valutare `getpwuid_r`.
- [ ] **A4** `tools/gnome-panel-control/gnome-panel-control.c:66`:
      `Atom act_atom = None;` e `default:` senza `assert(0)` (o con ritorno
      d'errore).
- [ ] **A5** `openbox/client.c:1280` e `openbox/frame.c:70`: sostituire
      `g_assert(ret != BadWindow)` con gestione esplicita dell'errore
      (`XGetWindowAttributes` fallita → non usare `wattrib`).

### P2 — Robustezza / input degenere

- [ ] **A6** `obrender/image.c:794`: sostituire `g_assert(sumXY != 0)` con un
      controllo runtime e un comportamento definito (es. pixel trasparente o
      ritorno senza scalare), e validare `srcW/srcH/dstW/dstH > 0` in
      `ResizeImage()`.
- [ ] **A7** `obt/xqueue.c:71-85`: `newsz = qsz ? qsz*2 : MINSZ;` e guardia su
      `qsz == 0` in `read_events()`; valutare `g_return_if_fail` su init.
- [ ] **A8** `openbox/event.c:826`: aggiungere `if (!client) return;` in
      `event_enter_client()` (documenta l'invariante e chiude B10).
- [ ] **A9** Pattern `ignore_start`: inizializzare `ignore_start = 0` alla
      dichiarazione in `event.c`, `client.c`, `client_menu.c` (6 punti), o
      introdurre un helper `event_ignore_enters_begin/end()`.

### P3 — Pulizia e manutenzione

- [ ] **A10** `obt/link.c:61`: rimuovere la struct vuota `_ObtLinkDir` e il
      campo `d.dir` di `_ObtLinkData` (C2).
- [ ] **A11** `openbox/menu.c:411-412`: valutare `const gchar *` per `name`,
      `title`, `collate_key` (warning qualificatori).
- [ ] **A12** Dead store minori: `resist.c` (8), `moveresize.c` (2),
      `event.c` (2), `menuframe.c` (3), `render.c:179`, `popup.c:217`,
      `ddparse.c:759`, `obxprop.c:133`.
- [ ] **A13** Deprecazioni GLib/librsvg/pango (16 warning): pianificare la
      migrazione (`g_memdup2`, `g_pattern_spec_*`, `rsvg_handle_*`).
- [ ] **A14** `obt/xml.c:155` (S1): verificare le opzioni di parsing libxml2 e
      aggiungere `XML_PARSE_NONET` se non presente; documentare la fiducia
      riposta in temi/menu esterni.
- [ ] **A15** `tools/gdm-control/gdm-control.c:179,181` (S2/S3): sostituire
      `strcpy` con `g_strlcpy` o `strncpy` + terminazione esplicita.

### Test di non regressione suggeriti

Prima di toccare A1-A5 conviene avere:
- un test unitario su `menu_sort_entries()` con menu vuoto e con soli
  separatori (A2);
- un test su `obt_link_from_ddfile()` con campo `Categories` presente (A1);
- un test di `ResizeImage()` con dimensioni nulle (A6), selezionabile con una
  piccola harness dato che `obrender/test.c` non è nel build.

---

## 10. Come riprodurre

```bash
cd /tmp/openbox
./bootstrap
mkdir -p .analysis-build && cd .analysis-build
../configure --disable-nls --enable-startup-notification --disable-session-management
make clean && make V=1 -j8 > build_v1.log
python3 mk_compile_db.py build_v1.log

mkdir -p cppcheck-cache
cppcheck --project=compile_commands.json --enable=all --inconclusive \
  --check-level=exhaustive --std=c11 --library=posix --inline-suppr \
  --suppress=missingIncludeSystem --suppress=unmatchedSuppression \
  --cppcheck-build-dir=cppcheck-cache --output-file=cppcheck-full.xml \
  --xml --xml-version=2 -j8

python3 clang_warnings.py     # usa compile_commands.json + clang-23

cd /tmp/openbox
semgrep --config=p/c --config=p/security-audit --config=p/cwe-top-25 \
  --metrics=off --json --output=.analysis-build/semgrep.json \
  --exclude=.analysis-build --exclude=.git .

mkdir -p .scan-build && cd .scan-build
CC=clang-23 CXX=clang++-23 ../configure --disable-nls \
  --enable-startup-notification --disable-session-management
scan-build-23 -o scan-out make CC=/usr/lib/llvm-23/libexec/ccc-analyzer \
  CXX=/usr/lib/llvm-23/libexec/c++-analyzer -j8
```

I due script (`mk_compile_db.py`, `clang_warnings.py`) sono in
`.analysis-build/` e non fanno parte del repository.

---

## 11. Conclusione

Il codice è in condizioni statiche complessivamente buone: nessun buffer
overflow evidente, un solo uso di `strcpy` in un tool, tre findings semgrep di
cui uno informativo. I problemi concreti sono pochi e circoscritti:

1. una riga di dead code con UB in `obt/link.c` (A1);
2. un percorso di crash su menu vuoto in `openbox/menu.c` (A2);
3. la gestione degli errori X11 affidata ad asserzioni disabilitate in release
   (A5, A6);
4. un deref di `NULL` da `getpwuid` (A3);
5. un atom non inizializzato in `gnome-panel-control` (A4).

Il resto è rumore da macro, stile, o robustezza difensiva. La lista P3 è
opzionale e può essere affrontata in un secondo momento.
