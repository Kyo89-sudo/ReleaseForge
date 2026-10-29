#include "dialogs.h"

#include <commctrl.h>
#include <shobjidl.h>

#include <atomic>
#include <memory>
#include <thread>

#include "../core/strutil.h"
#include "../win/mediainfo.h"
#include "../win/tools.h"

namespace rf {

// ------------------------------------------------------------------ DlgBuilder

DlgBuilder::DlgBuilder(const std::wstring& title, short cx, short cy, DWORD extraStyle) {
    Word(1);       // dlgVer
    Word(0xFFFF);  // signature
    DWord(0);      // helpID
    DWord(0);      // exStyle
    DWord(DS_SHELLFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | extraStyle);
    countPos_ = buf_.size();
    Word(0);  // cDlgItems
    Word(0); Word(0);
    Word(WORD(cx)); Word(WORD(cy));
    Word(0);  // menu
    Word(0);  // classe
    Str(title);
    Word(9);    // punti
    Word(400);  // peso
    Word(0x0100);  // italic = 0, charset = DEFAULT_CHARSET
    Str(L"Segoe UI");
}

void DlgBuilder::Align() {
    if (buf_.size() % 2) buf_.push_back(0);
}

void DlgBuilder::Item(DWORD style, DWORD exStyle, short x, short y, short cx, short cy, int id, WORD atom, const std::wstring& text) {
    Align();
    DWord(0);  // helpID
    DWord(exStyle);
    DWord(style | WS_CHILD | WS_VISIBLE);
    Word(WORD(x)); Word(WORD(y)); Word(WORD(cx)); Word(WORD(cy));
    DWord(DWORD(id));
    Word(0xFFFF);
    Word(atom);
    Str(text);
    Word(0);  // extra
    ++count_;
    buf_[countPos_] = count_;
}

void DlgBuilder::Static(int id, const std::wstring& text, short x, short y, short cx, short cy, DWORD style) {
    Item(SS_LEFT | style, 0, x, y, cx, cy, id, 0x0082, text);
}
void DlgBuilder::Edit(int id, short x, short y, short cx, short cy, DWORD style) {
    Item(WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | style, 0, x, y, cx, cy, id, 0x0081, L"");
}
void DlgBuilder::Button(int id, const std::wstring& text, short x, short y, short cx, short cy, DWORD style) {
    Item(WS_TABSTOP | (style ? style : BS_PUSHBUTTON), 0, x, y, cx, cy, id, 0x0080, text);
}
void DlgBuilder::Check(int id, const std::wstring& text, short x, short y, short cx, short cy) {
    Item(WS_TABSTOP | BS_AUTOCHECKBOX, 0, x, y, cx, cy, id, 0x0080, text);
}
void DlgBuilder::Combo(int id, short x, short y, short cx, short cy) {
    Item(WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, x, y, cx, cy, id, 0x0085, L"");
}
void DlgBuilder::List(int id, short x, short y, short cx, short cy) {
    Item(WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, x, y, cx, cy, id, 0x0083, L"");
}
void DlgBuilder::Group(const std::wstring& text, short x, short y, short cx, short cy) {
    Item(BS_GROUPBOX, 0, x, y, cx, cy, -1, 0x0080, text);
}

namespace {
struct DlgCtx {
    std::function<INT_PTR(HWND, UINT, WPARAM, LPARAM)> proc;
};
INT_PTR CALLBACK DlgThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INITDIALOG) SetWindowLongPtrW(h, DWLP_USER, l);
    auto* ctx = reinterpret_cast<DlgCtx*>(GetWindowLongPtrW(h, DWLP_USER));
    if (!ctx) return FALSE;
    return ctx->proc(h, m, w, m == WM_INITDIALOG ? 0 : l);
}
}  // namespace

INT_PTR DlgBuilder::Run(HWND owner, std::function<INT_PTR(HWND, UINT, WPARAM, LPARAM)> proc) {
    Align();
    std::vector<WORD> copy = buf_;  // allineato a WORD; DialogBoxIndirect richiede DWORD per i singoli item, già garantito
    DlgCtx ctx{std::move(proc)};
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(copy.data()), owner, DlgThunk,
                                   reinterpret_cast<LPARAM>(&ctx));
}

std::wstring GetDlgText(HWND dlg, int id) {
    HWND c = GetDlgItem(dlg, id);
    int n = GetWindowTextLengthW(c);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(c, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}

// ------------------------------------------------------------------ TMDB

TmdbChoice ShowTmdbChoice(HWND owner, const std::vector<TmdbHit>& hits, const std::string& searched) {
    enum { IDC_INFO = 100, IDC_LIST, IDC_REF };
    DlgBuilder b(L"Scegli il titolo su TheMovieDB", 320, 206);
    b.Static(IDC_INFO, L"", 7, 7, 306, 18);
    b.List(IDC_LIST, 7, 27, 306, 124);
    b.Static(-1, L"Oppure ID o URL TMDB:", 7, 160, 90, 8);
    b.Edit(IDC_REF, 100, 158, 213, 13);
    b.Button(IDOK, L"Usa questo titolo", 175, 184, 75, 15, BS_DEFPUSHBUTTON);
    b.Button(IDCANCEL, L"Annulla", 255, 184, 58, 15);
    TmdbChoice choice;
    b.Run(owner, [&](HWND h, UINT m, WPARAM w, LPARAM) -> INT_PTR {
        switch (m) {
            case WM_INITDIALOG: {
                std::wstring info = hits.empty() ? L"Nessun risultato per «" + W(searched) + L"». Inserisci l'ID o l'URL TMDB."
                                                 : std::to_wstring(hits.size()) + L" risultati per «" + W(searched) +
                                                       L"»: scegli quello corretto (doppio clic per confermare).";
                SetDlgItemTextW(h, IDC_INFO, info.c_str());
                HWND list = GetDlgItem(h, IDC_LIST);
                for (auto& hit : hits) {
                    std::wstring line = W(hit.title) + (hit.year.empty() ? L"" : L" (" + W(hit.year) + L")");
                    if (!hit.originalTitle.empty() && hit.originalTitle != hit.title) line += L"  —  " + W(hit.originalTitle);
                    line += L"   [" + std::to_wstring(hit.id) + L"]";
                    SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
                }
                if (!hits.empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
                SetFocus(hits.empty() ? GetDlgItem(h, IDC_REF) : list);
                return FALSE;
            }
            case WM_COMMAND:
                if (LOWORD(w) == IDC_LIST && HIWORD(w) == LBN_DBLCLK) { PostMessageW(h, WM_COMMAND, IDOK, 0); return TRUE; }
                if (LOWORD(w) == IDOK) {
                    std::string ref = Trim(U8(GetDlgText(h, IDC_REF)));
                    int sel = int(SendDlgItemMessageW(h, IDC_LIST, LB_GETCURSEL, 0, 0));
                    if (!ref.empty()) { choice.cancelled = false; choice.reference = ref; }
                    else if (sel >= 0) { choice.cancelled = false; choice.index = sel; }
                    else { MessageBeep(MB_ICONWARNING); return TRUE; }
                    EndDialog(h, IDOK);
                    return TRUE;
                }
                if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
                break;
        }
        return FALSE;
    });
    return choice;
}

// ------------------------------------------------------------------ Input

bool ShowInput(HWND owner, const std::wstring& title, const std::wstring& prompt, std::wstring& value) {
    DlgBuilder b(title, 260, 72);
    b.Static(-1, prompt, 7, 7, 246, 18);
    b.Edit(100, 7, 27, 246, 13);
    b.Button(IDOK, L"OK", 134, 51, 58, 14, BS_DEFPUSHBUTTON);
    b.Button(IDCANCEL, L"Annulla", 195, 51, 58, 14);
    bool ok = false;
    b.Run(owner, [&](HWND h, UINT m, WPARAM w, LPARAM) -> INT_PTR {
        if (m == WM_INITDIALOG) {
            SetDlgItemTextW(h, 100, value.c_str());
            SetFocus(GetDlgItem(h, 100));
            return FALSE;
        }
        if (m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)) {
            if (LOWORD(w) == IDOK) { value = GetDlgText(h, 100); ok = true; }
            EndDialog(h, LOWORD(w));
            return TRUE;
        }
        return FALSE;
    });
    return ok;
}

// ------------------------------------------------------------------ Impostazioni

namespace {
enum {
    IDC_TMDB = 200, IDC_IMGBB, IDC_HOST, IDC_FFMPEG, IDC_FFMPEG_BROWSE, IDC_FFMPEG_STATUS, IDC_FFMPEG_DL,
    IDC_MI, IDC_MI_BROWSE, IDC_MI_STATUS, IDC_MI_INSTALL, IDC_AVI, IDC_AVI_BROWSE, IDC_PIECE, IDC_SHOWKEYS, IDC_HOST2, IDC_CHEV_URL, IDC_CHEV_KEY, IDC_IMGBB_HELP, IDC_TORRENT_FORMAT
};
const int kPieces[] = {0, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};

struct ToolStatusResult {
    std::wstring ffmpeg;
    std::wstring mediaInfo;
};

ToolStatusResult CheckToolStatus(const std::wstring& ffmpegPath, const std::wstring& mediaInfoPath) {
    std::wstring ff = FindFfmpeg(ffmpegPath);
    std::string ffv = ToolVersion(ff);
    std::wstring ffs = ff.empty() ? L"✖ FFmpeg non trovato: screenshot e grafico bitrate non disponibili."
                                  : L"✔ " + W(ffv.empty() ? "ffmpeg trovato" : ffv.substr(0, 60)) + L"\n" + ff;
    if (!ff.empty() && FindFfprobe(ff).empty()) ffs += L"  (ffprobe mancante)";


    MediaInfoSetup mis{mediaInfoPath, ff};
    MediaInfoStatus st = DetectMediaInfo(mis);
    std::wstring ms;
    switch (st.backend) {
        case MediaInfoBackend::Dll: ms = L"✔ MediaInfo completo: " + W(st.version) + L"\n" + st.location; break;
        case MediaInfoBackend::Cli: ms = L"⚠ Solo MediaInfo CLI: " + W(st.version) + L"\n" + st.location; break;
        case MediaInfoBackend::Ffmpeg: ms = L"⚠ MediaInfo non trovato: report ridotto con FFmpeg."; break;
        default: ms = L"✖ MediaInfo non trovato."; break;
    }
    return {std::move(ffs), std::move(ms)};
}
}  // namespace

bool ShowSettings(HWND owner, Settings& s, int& action) {
    action = 0;
    DlgBuilder b(L"Impostazioni e strumenti", 350, 374);
    b.Group(L"Chiavi API e host immagini", 7, 5, 336, 122);
    b.Static(-1, L"TheMovieDB \u2014 API key v3 oppure API Read Access Token", 15, 17, 250, 8);
    b.Edit(IDC_TMDB, 15, 27, 320, 13, ES_PASSWORD);
    b.Static(-1, L"ImgBB \u2014 chiave API gratuita (consigliato)", 15, 45, 250, 8);
    b.Edit(IDC_IMGBB, 15, 55, 155, 13, ES_PASSWORD);
    b.Check(IDC_SHOWKEYS, L"Mostra chiavi", 178, 57, 76, 10);
    b.Button(IDC_IMGBB_HELP, L"Ottieni chiave", 260, 54, 75, 15);
    b.Static(-1, L"Host immagini", 15, 74, 150, 8);
    b.Combo(IDC_HOST, 15, 83, 155, 100);
    b.Static(-1, L"Host di riserva (se il primo fallisce)", 178, 74, 157, 8);
    b.Combo(IDC_HOST2, 178, 83, 157, 100);
    b.Static(-1, L"Chevereto \u2014 URL API e chiave (es. https://freeimage.host/api/1/upload)", 15, 101, 320, 8);
    b.Edit(IDC_CHEV_URL, 15, 110, 200, 13);
    b.Edit(IDC_CHEV_KEY, 220, 110, 115, 13, ES_PASSWORD);

    b.Group(L"Strumenti (vuoto = rilevamento automatico)", 7, 132, 336, 160);
    b.Static(-1, L"FFmpeg — ffmpeg.exe o cartella", 15, 144, 200, 8);
    b.Edit(IDC_FFMPEG, 15, 154, 256, 13);
    b.Button(IDC_FFMPEG_BROWSE, L"Sfoglia…", 276, 153, 59, 14);
    b.Static(IDC_FFMPEG_STATUS, L"", 15, 171, 256, 18, SS_NOPREFIX);
    b.Button(IDC_FFMPEG_DL, L"Scarica FFmpeg", 276, 171, 59, 14);
    b.Static(-1, L"MediaInfo completo — cartella, MediaInfo.dll o MediaInfo.exe", 15, 196, 250, 8);
    b.Edit(IDC_MI, 15, 206, 256, 13);
    b.Button(IDC_MI_BROWSE, L"Sfoglia…", 276, 205, 59, 14);
    b.Static(IDC_MI_STATUS, L"", 15, 223, 256, 18, SS_NOPREFIX);
    b.Button(IDC_MI_INSTALL, L"Installa…", 276, 223, 59, 14);
    b.Static(-1, L"AVInaptic (opzionale) — avinaptic2-cli.exe", 15, 250, 250, 8);
    b.Edit(IDC_AVI, 15, 260, 256, 13);
    b.Button(IDC_AVI_BROWSE, L"Sfoglia…", 276, 259, 59, 14);
    b.Static(-1, L"Servono FFmpeg (screenshot/bitrate) e MediaInfo completo (report e titolo).", 15, 277, 320, 8);

    b.Group(L"Torrent", 7, 296, 336, 49);
    b.Static(-1, L"Formato torrent", 15, 310, 60, 8);
    b.Combo(IDC_TORRENT_FORMAT, 80, 307, 255, 70);
    b.Static(-1, L"Dimensione pezzi", 15, 330, 60, 8);
    b.Combo(IDC_PIECE, 80, 327, 255, 150);
    b.Button(IDOK, L"Salva", 223, 352, 58, 15, BS_DEFPUSHBUTTON);
    b.Button(IDCANCEL, L"Annulla", 285, 352, 58, 15);

    // The worker owns only input strings and its result. It never touches HWNDs
    // or Settings, so closing the dialog does not wait for a slow tool probe.
    struct ToolProbe {
        std::atomic<bool> done{false};
        ToolStatusResult result;
        unsigned generation = 0;
    };
    std::shared_ptr<ToolProbe> probe;
    unsigned generation = 0;
    constexpr UINT_PTR probeTimer = 1;
    auto refreshToolStatus = [&](HWND h) {
        ++generation;
        SetDlgItemTextW(h, IDC_FFMPEG_STATUS, L"Verifica FFmpeg in corso...");
        SetDlgItemTextW(h, IDC_MI_STATUS, L"Verifica MediaInfo in corso...");
        // Debounce path changes and let the dialog paint before probing.
        SetTimer(h, probeTimer, 100, nullptr);
    };
    auto startProbe = [&](HWND h) {
        auto job = std::make_shared<ToolProbe>();
        job->generation = generation;
        std::wstring ff = GetDlgText(h, IDC_FFMPEG);
        std::wstring mi = GetDlgText(h, IDC_MI);
        std::thread worker([job, ff = std::move(ff), mi = std::move(mi)] {
            try {
                job->result = CheckToolStatus(ff, mi);
            } catch (...) {
                job->result = {L"Verifica FFmpeg non riuscita.", L"Verifica MediaInfo non riuscita."};
            }
            job->done.store(true, std::memory_order_release);
        });
        worker.detach();
        probe = std::move(job);
    };

    bool saved = false;
    b.Run(owner, [&](HWND h, UINT m, WPARAM w, LPARAM) -> INT_PTR {
        switch (m) {
            case WM_INITDIALOG: {
                SetDlgItemTextW(h, IDC_TMDB, W(s.tmdbKey).c_str());
                SetDlgItemTextW(h, IDC_IMGBB, W(s.imgbbKey).c_str());
                SetDlgItemTextW(h, IDC_FFMPEG, s.ffmpegPath.c_str());
                SetDlgItemTextW(h, IDC_MI, s.mediaInfoPath.c_str());
                SetDlgItemTextW(h, IDC_AVI, s.avinapticPath.c_str());
                SetDlgItemTextW(h, IDC_CHEV_URL, W(s.cheveretoUrl).c_str());
                SetDlgItemTextW(h, IDC_CHEV_KEY, W(s.cheveretoKey).c_str());
                for (int cb : {IDC_HOST, IDC_HOST2}) {
                    const std::string& cur = cb == IDC_HOST ? s.imageHost : s.imageHostFallback;
                    int idx = 0, k = 0;
                    for (auto& hostItem : ImageHosts()) {
                        SendDlgItemMessageW(h, cb, CB_ADDSTRING, 0, (LPARAM)W(hostItem.second).c_str());
                        if (hostItem.first == cur) idx = k;
                        ++k;
                    }
                    SendDlgItemMessageW(h, cb, CB_SETCURSEL, idx, 0);
                }
                for (auto label : {L"Hybrid v1 + v2 (predefinito)", L"BitTorrent v1", L"BitTorrent v2"})
                    SendDlgItemMessageW(h, IDC_TORRENT_FORMAT, CB_ADDSTRING, 0, (LPARAM)label);
                SendDlgItemMessageW(h, IDC_TORRENT_FORMAT, CB_SETCURSEL, int(s.torrentFormat), 0);
                HWND piece = GetDlgItem(h, IDC_PIECE);
                int sel = 0;
                for (int i = 0; i < int(std::size(kPieces)); ++i) {
                    std::wstring label = kPieces[i] == 0 ? L"Automatica (tabella MIRCrew)"
                                                         : (kPieces[i] >= 1024 ? std::to_wstring(kPieces[i] / 1024) + L" MiB" : std::to_wstring(kPieces[i]) + L" KiB");
                    SendMessageW(piece, CB_ADDSTRING, 0, (LPARAM)label.c_str());
                    if (kPieces[i] == s.pieceSizeKiB) sel = i;
                }
                SendMessageW(piece, CB_SETCURSEL, sel, 0);
                if (!WingetAvailable()) SetDlgItemTextW(h, IDC_MI_INSTALL, L"Scarica…");
                refreshToolStatus(h);
                return TRUE;
            }
            case WM_TIMER:
                if (w != probeTimer) break;
                if (probe && !probe->done.load(std::memory_order_acquire)) return TRUE;
                if (probe && probe->generation == generation) {
                    SetDlgItemTextW(h, IDC_FFMPEG_STATUS, probe->result.ffmpeg.c_str());
                    SetDlgItemTextW(h, IDC_MI_STATUS, probe->result.mediaInfo.c_str());
                    probe.reset();
                    KillTimer(h, probeTimer);
                } else {
                    // Serialize probes, and ignore results for superseded paths.
                    try { startProbe(h); }
                    catch (...) {
                        SetDlgItemTextW(h, IDC_FFMPEG_STATUS, L"Impossibile avviare la verifica.");
                        SetDlgItemTextW(h, IDC_MI_STATUS, L"Chiudi e riapri le impostazioni per riprovare.");
                        KillTimer(h, probeTimer);
                    }
                }
                return TRUE;
            case WM_DESTROY:
                KillTimer(h, probeTimer);
                return TRUE;
            case WM_COMMAND: {
                int id = LOWORD(w);
                if (id == IDC_IMGBB_HELP) {
                    OpenUrl(L"https://api.imgbb.com/");
                    return TRUE;
                }
                if (id == IDC_SHOWKEYS) {
                    bool show = IsDlgButtonChecked(h, IDC_SHOWKEYS) == BST_CHECKED;
                    for (int e : {IDC_TMDB, IDC_IMGBB, IDC_CHEV_KEY}) {
                        SendDlgItemMessageW(h, e, EM_SETPASSWORDCHAR, show ? 0 : 0x25CF, 0);
                        InvalidateRect(GetDlgItem(h, e), nullptr, TRUE);
                    }
                    return TRUE;
                }
                if (id == IDC_FFMPEG_BROWSE || id == IDC_AVI_BROWSE) {
                    std::wstring p;
                    if (PickFile(h, id == IDC_FFMPEG_BROWSE ? L"Seleziona ffmpeg.exe" : L"Seleziona avinaptic2-cli.exe", {{L"Programmi", L"*.exe"}}, p)) {
                        SetDlgItemTextW(h, id == IDC_FFMPEG_BROWSE ? IDC_FFMPEG : IDC_AVI, p.c_str());
                        refreshToolStatus(h);
                    }
                    return TRUE;
                }
                if (id == IDC_MI_BROWSE) {
                    std::wstring p;
                    if (PickFile(h, L"Seleziona MediaInfo.dll o MediaInfo.exe (versione completa)", {{L"MediaInfo", L"MediaInfo.dll;MediaInfo.exe"}, {L"DLL e programmi", L"*.dll;*.exe"}}, p)) {
                        SetDlgItemTextW(h, IDC_MI, p.c_str());
                        refreshToolStatus(h);
                    }
                    return TRUE;
                }
                if ((id == IDC_FFMPEG || id == IDC_MI) && HIWORD(w) == EN_KILLFOCUS) { refreshToolStatus(h); return TRUE; }
                if (id == IDC_FFMPEG_DL || id == IDC_MI_INSTALL) {
                    if (id == IDC_MI_INSTALL && !WingetAvailable()) {
                        OpenUrl(L"https://mediaarea.net/it/MediaInfo/Download/Windows");
                        return TRUE;
                    }
                    action = id == IDC_FFMPEG_DL ? 1 : 2;
                    id = IDOK;  // salva e chiude, l'operazione parte nella finestra principale
                }
                if (id == IDOK) {
                    s.tmdbKey = Trim(U8(GetDlgText(h, IDC_TMDB)));
                    s.imgbbKey = Trim(U8(GetDlgText(h, IDC_IMGBB)));
                    auto hostAt = [&](int cb) {
                        int i = int(SendDlgItemMessageW(h, cb, CB_GETCURSEL, 0, 0));
                        return i >= 0 && i < int(ImageHosts().size()) ? ImageHosts()[size_t(i)].first : std::string("none");
                    };
                    s.imageHost = hostAt(IDC_HOST);
                    s.imageHostFallback = hostAt(IDC_HOST2);
                    s.cheveretoUrl = Trim(U8(GetDlgText(h, IDC_CHEV_URL)));
                    s.cheveretoKey = Trim(U8(GetDlgText(h, IDC_CHEV_KEY)));
                    s.ffmpegPath = W(Trim(U8(GetDlgText(h, IDC_FFMPEG))));
                    s.mediaInfoPath = W(Trim(U8(GetDlgText(h, IDC_MI))));
                    s.avinapticPath = W(Trim(U8(GetDlgText(h, IDC_AVI))));
                    int tf = int(SendDlgItemMessageW(h, IDC_TORRENT_FORMAT, CB_GETCURSEL, 0, 0));
                    s.torrentFormat = tf >= 0 && tf <= 2 ? TorrentFormat(tf) : TorrentFormat::Hybrid;
                    int ps = int(SendDlgItemMessageW(h, IDC_PIECE, CB_GETCURSEL, 0, 0));
                    s.pieceSizeKiB = ps >= 0 && ps < int(std::size(kPieces)) ? kPieces[ps] : 0;
                    saved = true;
                    EndDialog(h, IDOK);
                    return TRUE;
                }
                if (id == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
                break;
            }
        }
        return FALSE;
    });
    return saved;
}

// ------------------------------------------------------------------ Liste tracker

bool ShowTrackerLists(HWND owner, Settings& s) {
    enum { IDC_URLS = 300, IDC_MAX };
    DlgBuilder b(L"Fonti delle liste tracker", 330, 258);
    b.Static(-1, L"ATTENZIONE: le righe con # sono disattivate. Togli # davanti all’URL per attivare una fonte.\n"
                 L"Usa URL di liste in formato testo, con un tracker per riga.", 7, 7, 316, 30);
    b.Edit(IDC_URLS, 7, 42, 316, 120, ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL);
    b.Static(-1, L"Aggiorna dalle liste aggiunge tracker nuovi all’elenco visibile fino al massimo indicato (es. 6 + 2 = 8). Non ci sono aggiunte automatiche.",
             7, 168, 316, 22);
    b.Static(-1, L"Massimo tracker nell'elenco (0 = senza limite):", 7, 195, 170, 8);
    b.Edit(IDC_MAX, 180, 193, 40, 13, ES_NUMBER);
    b.Static(-1, L"Attenzione: togliere una fonte non elimina i tracker già importati. Rimuovili dall’elenco o premi Ripristina predefiniti.",
             7, 212, 316, 22);
    b.Button(IDOK, L"Salva", 203, 239, 58, 14, BS_DEFPUSHBUTTON);
    b.Button(IDCANCEL, L"Annulla", 265, 239, 58, 14);
    bool saved = false;
    b.Run(owner, [&](HWND h, UINT m, WPARAM w, LPARAM) -> INT_PTR {
        if (m == WM_INITDIALOG) {
            std::string text = ReadConfigFile(L"tracker_lists.txt");
            if (text.empty()) text = DefaultTrackerLists();
            SetDlgItemTextW(h, IDC_URLS, W(ToCrlf(text)).c_str());

            SetDlgItemInt(h, IDC_MAX, UINT(std::max(0, s.trackerListsMax)), FALSE);
            return TRUE;
        }
        if (m == WM_COMMAND && LOWORD(w) == IDOK) {
            WriteConfigFile(L"tracker_lists.txt", U8(GetDlgText(h, IDC_URLS)));
            s.trackerListsAuto = false;
            s.trackerListsMax = int(GetDlgItemInt(h, IDC_MAX, nullptr, FALSE));
            s.trackerListsLast = 0;  // forza l'aggiornamento con le nuove liste
            saved = true;
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        return FALSE;
    });
    return saved;
}

// ------------------------------------------------------------------ Selettori file

bool PickFile(HWND owner, const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& filters, std::wstring& path) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    std::vector<COMDLG_FILTERSPEC> spec;
    for (auto& f : filters) spec.push_back({f.first.c_str(), f.second.c_str()});
    spec.push_back({L"Tutti i file", L"*.*"});
    dlg->SetFileTypes(UINT(spec.size()), spec.data());
    dlg->SetTitle(title.c_str());
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    if (!path.empty()) {
        IShellItem* folder = nullptr;
        std::wstring dir = FileExists(path) ? PathParent(path) : path;
        if (DirExists(dir) && SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; ok = true; CoTaskMemFree(p); }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

bool PickFolder(HWND owner, const std::wstring& title, std::wstring& path) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title.c_str());
    if (!path.empty() && DirExists(path)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; ok = true; CoTaskMemFree(p); }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

}  // namespace rf
