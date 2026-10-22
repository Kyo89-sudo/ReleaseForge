<p align="center">
  <img src="ReleaseForge.svg" width="128" height="128" alt="ReleaseForge">
</p>

# ReleaseForge

Versione: **0.1.3** (Windows x64).

ReleaseForge è un’applicazione Windows per creare torrent e preparare release di film e serie TV. Recupera i metadati da TMDB e genera report MediaInfo, screenshot, grafici del bitrate, link magnet, titoli e post BBCode da template.

È una riscrittura in C++ di [makerelease](https://github.com/c137ricksanchez/makerelease) (MIT), da cui riprende il template standard e le regole di composizione del titolo.

È distribuita come unico eseguibile e funziona su Windows 10 1809+ e Windows 11 x64, senza runtime aggiuntivi.

## Uso rapido

1. Avvia `ReleaseForge.exe` (portabile) oppure installa con il setup.
2. Al primo avvio, se mancano, l'app propone **Strumenti → Impostazioni e strumenti**:
   - **Scarica FFmpeg**: scarica FFmpeg *essentials* da gyan.dev, verifica lo SHA-256 e lo mette in
     `%LOCALAPPDATA%\ReleaseForge\tools\ffmpeg` (nessuna modifica al PATH).
   - **Installa MediaInfo**: installa **MediaInfo completo** (la versione con interfaccia) con
     `winget install --id MediaArea.MediaInfo.GUI`. Senza winget si apre la pagina ufficiale.
   - Inserisci la chiave **TMDB** (API key v3 o API Read Access Token) e, se vuoi caricare le immagini,
     l'host per le immagini: **ImgBB** (predefinito e consigliato, chiave API gratuita), **Pixhost**, **Imgur** o **Catbox** (anonimi), oppure
     un sito **Chevereto** qualsiasi (freeimage.host, lensdump, ptscreens, onlyimage… con URL API e chiave),
     più un **host di riserva** usato per le immagini che il primo non riesce a caricare.
3. Trascina il file o la cartella sulla finestra, controlla tipo release/crew e premi **Genera release** (F5).
4. A fine lavoro trovi in alto il **Titolo forum** con il pulsante *Copia titolo*, e nella scheda
   **Post generato** il BBCode da copiare. Tutto è salvato anche in `Documenti\ReleaseForge\Output\<nome>_files`.

## Host immagini e chiavi API

**ImgBB è il servizio predefinito e consigliato di ReleaseForge.** L'upload automatico
richiede una chiave API personale: il servizio gratuito non significa accesso senza chiave.

1. Apri [api.imgbb.com](https://api.imgbb.com/) oppure premi **Ottieni chiave**
   in **Strumenti → Impostazioni e strumenti**.
2. Accedi o crea un account ImgBB e ottieni la tua chiave API.
3. Incollala nel campo **ImgBB — chiave API gratuita** dell'app.
4. Seleziona **ImgBB (consigliato, chiave API)** come host principale e premi **Salva**.
5. Nella finestra principale attiva **Carica immagini**.

La chiave ImgBB serve solo per ImgBB ed è distinta dalla chiave TMDB per i metadati.
Le chiavi vengono salvate nelle impostazioni personali: non condividere `settings.json`.

| Host | Configurazione | Limite nell'app | Miniature |
|---|---|---|---|
| **ImgBB — consigliato** | Chiave personale da [api.imgbb.com](https://api.imgbb.com/) | 32 MiB | Anteprima fornita dal servizio |
| Pixhost | Nessuna chiave o registrazione per l'API pubblica | 10 MiB | Sì |
| Imgur | L'app usa un Client-ID incorporato; non richiede una chiave all'utente | 10 MiB | Sì |
| Catbox | Nessuna chiave per upload anonimi | 200 MiB | No: nel post viene usata l'immagine originale |
| Chevereto | URL API e, se richiesto dal sito, chiave personale | Dipende dal sito | Se restituite dall'API |

I limiti riportati sono quelli applicati dal codice di ReleaseForge. Disponibilità,
quote e condizioni dei servizi possono cambiare; l'inclusione nell'elenco non implica
che ogni host sia stato verificato con un upload reale in questa versione.

**Pixhost non richiede una API key**: ReleaseForge usa direttamente la sua
[API pubblica](https://pixhost.to/api/index.html). È il servizio di riserva predefinito:
se ImgBB fallisce, l'app prova a caricare l'immagine su Pixhost. Per non usare un
servizio di riserva seleziona **Nessun upload** nel relativo menu.

Se l'host principale è **Nessun upload**, tutte le immagini restano in locale.

## Dipendenze esterne

| Strumento | A cosa serve | Come viene trovato |
|---|---|---|
| **MediaInfo completo** (GUI) | report `REPORT_MEDIAINFO`, durata, tracce per il titolo | `MediaInfo.dll` dell'installazione GUI, caricata direttamente (registro *App Paths* → `Program Files\MediaInfo`). Si può indicare la cartella, la DLL o `MediaInfo.exe` della GUI. |
| MediaInfo CLI | ripiego se la GUI non c'è | `tools\mediainfo`, PATH |
| **FFmpeg** (`ffmpeg.exe`) | screenshot; report ridotto se manca MediaInfo | impostazioni → `%LOCALAPPDATA%\ReleaseForge\tools\ffmpeg` → PATH → pacchetto winget `Gyan.FFmpeg` |
| `ffprobe.exe` | grafico bitrate | accanto a ffmpeg.exe |
| AVInaptic (opzionale) | `REPORT_AVINAPTIC` | solo se un template usa la variabile |

Con la DLL di MediaInfo il report è identico a quello di MediaInfo/pymediainfo (testo predefinito),
senza console e senza dover installare la versione CLI. Il percorso completo del file nel report
viene sostituito dal solo nome, come in makerelease.

## Titolo per il forum

Porting di `makerelease/tag.py`:
`Titolo (Anno) 1080p H264 ITA ENG AC3 5.1 Sub Ita Eng [Crew]` — titolo e anno italiani da TMDB,
risoluzione/codec dalla traccia video, audio raggruppato per formato e canali, sottotitoli
(`MultiSub` oltre 3 lingue), crew facoltativa. Caratteri non validi nei nomi file rimossi.

Aggiunte: **HDR** dopo il codec (`2160p H265 DV HDR10`, `HDR10+`, `HLG`) e **Atmos / DTS:X** sull'audio
(`ENG TrueHD 7.1 Atmos`, `ENG DTS-X 7.1`), letti da MediaInfo (HDR_Format, transfer, Format_Commercial,
Format_AdditionalFeatures) o, in ripiego, da FFmpeg.

Correzioni rispetto all'originale:
- per i video con bande laterali rimosse, considera anche l’altezza effettiva. Un file 1792×1080 viene quindi indicato come **1080p**; makerelease, calcolando la risoluzione solo dalla larghezza, lo indicava come **1008p**;
- `MBAFF`/`PAFF` riconosciuti come interlacciati (`1080i`);
- raggruppa le tracce audio evitando ripetizioni delle lingue, anche tra formati diversi, e mantiene distinti codec e canali;
- se non sono disponibili né la disposizione né il numero dei canali, omette il dato dal titolo;
- sottotitoli senza lingua non fanno più fallire la generazione;
- se l'anno non è disponibile, omette le parentesi vuote; quando presente, lo include normalmente nel titolo;
- DTS-HD Master Audio riconosciuto come **DTS-HD MA**, distinto dal DTS standard anche nel raggruppamento delle lingue; DTS:X mantiene la propria etichetta;
- MP2 (`MPEG Audio Layer 2`), TrueHD e VC-1 con nomi puliti;
- 4320p riconosciuto; spazi doppi rimossi.
Senza MediaInfo il titolo usa i dati di FFmpeg.

## Template, tracker e screenshot (personalizzabili)

Cartella: **Strumenti → Apri cartella configurazione** (`%APPDATA%\ReleaseForge\config`).

- `template.jinja` — al primo avvio è il **template standard di makerelease** (`config_example/template.jinja`).
- Multi-template come makerelease: ogni `template_NOME.jinja` genera `post_NOME.txt`.
- Scheda **Template**: modifica, salva (con controllo sintassi e numero di riga), crea, elimina,
  ripristina lo standard. In `config\esempi` c'è anche un template in stile "classico".
- Schede **Tracker** e **Screenshot**: `trackers.txt` (uno per riga; righe vuote e `#` ignorate) e `screenshots.txt` (`HH:MM:SS`).
- **Liste remote** (scheda Tracker → *Liste remote…*): configura gli URL delle fonti
  in `tracker_lists.txt`. Le righe con `#` sono disattivate: togli `#` davanti all’URL,
  salva e premi **Aggiorna dalle liste** per importare i tracker nell’elenco visibile.
  Non vengono aggiunti tracker automaticamente all’avvio della release.


Il motore dei template è compatibile con Jinja2 per ciò che serve ai post (`if/elif/else`, `for`
con `loop`, `set`, filtri comuni, `is defined`, `trim_blocks`/`lstrip_blocks` come makerelease).

Variabili: `TITLE ORIGINAL_TITLE YEAR RUNTIME PLOT GENRE DIRECTOR COUNTRY POSTER_URL TMDB_URL TRAILER
SCREENSHOTS BITRATE_GRAPH REPORT_MEDIAINFO REPORT_AVINAPTIC SIZE MAGNET TREE EP_COUNT CAST`
(`person.name`/`person.actor`, `person.character`) più `FORUM_TITLE RELEASE_NAME CREW TMDB_ID MEDIA_TYPE`.

## Elenco tracker e ripristino

La scheda **Tracker** contiene un solo elenco modificabile: è quello usato per
creare il torrent. Puoi inserire tracker manualmente oppure importarli dalle
fonti con **Aggiorna dalle liste**. Il comando aggiunge all’elenco visibile i
tracker nuovi (senza duplicati) e salva la lista risultante, con una riga vuota
tra un tracker e l’altro. Le righe vuote servono solo a leggere meglio l’elenco:
nel torrent ogni tracker resta in un tier proprio, nell’ordine dell’elenco.

Le fonti sono esempi commentati nelle nuove configurazioni. Per usare
[NewTrackon stable](https://newtrackon.com/list), apri **Liste remote…**, togli
`#` davanti a `https://newtrackon.com/api/stable` e salva. Non serve una chiave API.
Se l’esempio NewTrackon manca in una configurazione esistente, viene aggiunto
come commento al successivo avvio; le altre fonti restano invariate.
La lista stable seleziona tracker con oltre il 95% di uptime e almeno 10 giorni
di storico; la raggiungibilità dalla tua rete può variare.

Il limite predefinito è **8 tracker nell’elenco finale**: con i 6 predefiniti,
un’importazione ne aggiunge 2. I tracker già presenti non vengono mai tolti; se
l’elenco è già al limite non si aggiunge nulla. Imposta **0** per aggiungere tutti
i tracker validi delle fonti attive.

**Attenzione:** togliere o disattivare una fonte non elimina i tracker già importati.
Restano nell’elenco e saranno usati finché non li cancelli o ripristini i predefiniti.
Se il download fallisce, l’elenco visibile rimane invariato; la cache non viene
unita di nascosto al torrent.

**Ripristina predefiniti** salva la lista iniziale, svuota la cache delle importazioni
e disattiva le fonti, conservando i loro URL come righe commentate.

Per i torrent privati l’importazione dalle fonti pubbliche è disabilitata.
Prima di crearne uno, inserisci nell’elenco soltanto i tracker previsti dal sito:
i tracker già visibili non vengono filtrati automaticamente.

## Tipi di release

Automatico, oppure: Film (file), Film + extra (cartella con un video e cartelle Extra),
Serie TV stagione (cartella con più episodi), Serie TV completa (cartella di stagioni).
Per le cartelle vengono compilati `TREE` e, per le serie, `EP_COUNT`. File spazzatura
(`._*`, `.DS_Store`, `Thumbs.db`, `*.tmp`) sono esclusi dal torrent senza cancellarli.

## Ripresa

Con **Riprendi** attivo (predefinito), se nella cartella di output ci sono già:
- `<nome>.torrent` → viene riusato senza ricalcolare gli hash solo se descrive esattamente gli stessi file
  (nomi, dimensioni, dimensione pezzi, flag privato) e nessun file è stato modificato dopo la sua creazione;
  i tracker vengono comunque aggiornati (l'info-hash non cambia);
- `report_mediainfo.txt` → riusato così com'è (le modifiche manuali restano), se più recente del video;
- `bitrate.png` → riusato se più recente del video.
Il titolo usa comunque i dati tecnici letti dal file. Togli la spunta per rigenerare tutto.

## Torrent

BitTorrent v1, un tracker per tier (come torf), pezzi secondo la tabella MIRCrew di makerelease
(32 KiB fino a 50 MB … 16 MiB oltre 16 GB) o dimensione manuale nelle impostazioni,
opzione privato, magnet con `dn`, `xl` e `tr`. SHA-1 con BCrypt di Windows.

## Compilare

- **Visual Studio 2022** con "Sviluppo di applicazioni desktop con C++": doppio clic su `build.bat`
  (CMake di Visual Studio, build x64 Release statica, esegue i test, copia in `dist`).
  In alternativa *File → Apri → Cartella* in Visual Studio e compila il target `ReleaseForge`.
- **Installer**: `build-installer.bat` produce `dist\ReleaseForge-0.1.3-win64-setup.exe`.
  L'installer è nativo (`installer/setup.cpp`, stesso compilatore dell'app): non serve Inno Setup.
  Installa per l'utente corrente senza diritti di amministratore in `%LOCALAPPDATA%\Programs\ReleaseForge`,
  crea il collegamento nel menu Start (icona sul desktop facoltativa), si registra in *App installate*
  e aggiorna senza problemi le installazioni fatte con il vecchio setup Inno.
  Opzioni: `/S` silenzioso, `/DIR="cartella"`, `/DESKTOP`, `/MEDIAINFO`, `/LAUNCH`.
  La disinstallazione (`uninstall.exe`, `/S` silenziosa, `/PURGE` anche impostazioni e strumenti)
  rimuove solo i file installati e chiede se conservare impostazioni e template.
- **Da Linux**: `build-mingw.sh` con llvm-mingw.
- Test del core (anche su Linux/macOS): `g++ -std=c++17 -Isrc tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests`.

## File e cartelle

- Impostazioni: `%APPDATA%\ReleaseForge\settings.json` (importazione compatibile da `MakeReleaseWin`).
- Configurazione: `%APPDATA%\ReleaseForge\config`.
- Strumenti scaricati: `%LOCALAPPDATA%\ReleaseForge\tools`.
- Output: `Documenti\ReleaseForge\Output\<nome>_files` con `post*.txt`, `titolo.txt`,
  `report_mediainfo.txt`, `<nome>.torrent`, screenshot `HH.MM.SS.png`, `bitrate.png`.

## Autore e licenza

ReleaseForge è sviluppato da [Kyos](https://github.com/Kyo89-sudo) e distribuito
sotto licenza [MIT](LICENSE).

Il progetto include parti derivate da [makerelease](https://github.com/c137ricksanchez/makerelease)
di Rick Sanchez & Norman. Crediti, componenti riutilizzati e testo della licenza
originale sono in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

Per preparare il repository e la prima release: [PUBLISHING.md](PUBLISHING.md).
