# ReleaseForge

Prima versione pubblica: **0.1.0** (Windows x64).

ReleaseForge prepara una release completa partendo da un file video o da una cartella:
metadati TheMovieDB, report MediaInfo, screenshot, file `.torrent` + magnet, grafico del bitrate,
**titolo della release per il forum** e post BBCode generati da template.
Il flusso, il template standard e il formato del titolo sono quelli di
[makerelease](https://github.com/c137ricksanchez/makerelease) (MIT).

L'app è un unico `ReleaseForge.exe` **Win32 nativo (C++)**: niente .NET, niente WinUI 3 /
Windows App SDK, niente VC++ Redistributable. Funziona su Windows 10 1809+ e Windows 11 (x64).

## Uso rapido

1. Avvia `ReleaseForge.exe` (portabile) oppure installa con il setup.
2. Al primo avvio, se mancano, l'app propone **Strumenti → Impostazioni e strumenti**:
   - **Scarica FFmpeg**: scarica FFmpeg *essentials* da gyan.dev, verifica lo SHA-256 e lo mette in
     `%LOCALAPPDATA%\ReleaseForge\tools\ffmpeg` (nessuna modifica al PATH).
   - **Installa MediaInfo**: installa **MediaInfo completo** (la versione con interfaccia) con
     `winget install --id MediaArea.MediaInfo.GUI`. Senza winget si apre la pagina ufficiale.
   - Inserisci la chiave **TMDB** (API key v3 o API Read Access Token) e, se vuoi caricare le immagini,
     l'host per le immagini: **ImgBB** (chiave), **Pixhost**, **Imgur** o **Catbox** (anonimi), oppure
     un sito **Chevereto** qualsiasi (freeimage.host, lensdump, ptscreens, onlyimage… con URL API e chiave),
     più un **host di riserva** usato per le immagini che il primo non riesce a caricare.
3. Trascina il file o la cartella sulla finestra, controlla tipo release/crew e premi **Genera release** (F5).
4. A fine lavoro trovi in alto il **Titolo forum** con il pulsante *Copia titolo*, e nella scheda
   **Post generato** il BBCode da copiare. Tutto è salvato anche in `Documenti\ReleaseForge\Output\<nome>_files`.

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
- contenuti 4:3 o pillarbox (es. 1440×1080) davano "810p": ora si usa l'altezza reale quando è maggiore;
- `MBAFF`/`PAFF` riconosciuti come interlacciati (`1080i`);
- lingue duplicate (`ITA ITA AC3 5.1`) unite; tracce senza canali non producono `None.0`;
- sottotitoli senza lingua non fanno più fallire la generazione;
- anno mancante: niente `()` vuote;
- MP2 (`MPEG Audio Layer 2`), DTS-HD/`DTS XLL`, TrueHD e VC-1 con nomi puliti;
- 4320p riconosciuto; spazi doppi rimossi.
Senza MediaInfo il titolo usa i dati di FFmpeg.

## Template, tracker e screenshot (personalizzabili)

Cartella: **Strumenti → Apri cartella configurazione** (`%APPDATA%\ReleaseForge\config`).

- `template.jinja` — al primo avvio è il **template standard di makerelease** (`config_example/template.jinja`).
- Multi-template come makerelease: ogni `template_NOME.jinja` genera `post_NOME.txt`.
- Scheda **Template**: modifica, salva (con controllo sintassi e numero di riga), crea, elimina,
  ripristina lo standard. In `config\esempi` c'è anche un template in stile "classico".
- Schede **Tracker** e **Screenshot**: `trackers.txt` (uno per riga; righe vuote e `#` ignorate) e `screenshots.txt` (`HH:MM:SS`).
- **Liste remote** (scheda Tracker → *Liste remote…*): URL di liste pubbliche (es. ngosang/trackerslist) in
  `tracker_lists.txt`. I tracker scaricati si aggiungono a quelli manuali senza duplicati, con un limite
  configurabile (30 di default); aggiornamento manuale o automatico prima della release (max 1 volta al giorno),
  cache in `trackers_remote.txt` se la rete non risponde. Mai usate per i torrent privati.

Il motore dei template è compatibile con Jinja2 per ciò che serve ai post (`if/elif/else`, `for`
con `loop`, `set`, filtri comuni, `is defined`, `trim_blocks`/`lstrip_blocks` come makerelease).

Variabili: `TITLE ORIGINAL_TITLE YEAR RUNTIME PLOT GENRE DIRECTOR COUNTRY POSTER_URL TMDB_URL TRAILER
SCREENSHOTS BITRATE_GRAPH REPORT_MEDIAINFO REPORT_AVINAPTIC SIZE MAGNET TREE EP_COUNT CAST`
(`person.name`/`person.actor`, `person.character`) più `FORUM_TITLE RELEASE_NAME CREW TMDB_ID MEDIA_TYPE`.

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
- **Installer**: `build-installer.bat` (richiede Inno Setup 6.2+), produce `dist\ReleaseForge-0.1.0-win64-setup.exe`.
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
