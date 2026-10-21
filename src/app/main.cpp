// ReleaseForge - interfaccia Win32 nativa.
#include "../win/winutil.h"

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "../core/jinja.h"
#include "../core/strutil.h"
#include "../core/torrent.h"
#include "../resource.h"
#include "../win/http.h"
#include "../win/mediainfo.h"
#include "../win/pipeline.h"
#include "../win/settings.h"
#include "../win/tools.h"
#include "../win/trackerlists.h"
#include "dialogs.h"

using namespace rf;

namespace {

enum : int {
    IDC_SOURCE = 1000, IDC_SRC_FILE, IDC_SRC_DIR, IDC_TYPE, IDC_TMDB, IDC_CREW, IDC_OUTPUT, IDC_OUT_BROWSE,
    IDC_CHK_SHOTS, IDC_CHK_TORRENT, IDC_CHK_PRIVATE, IDC_CHK_UPLOAD, IDC_CHK_BITRATE, IDC_CHK_RESUME,
    IDC_TITLE, IDC_COPY_TITLE, IDC_TAB,
    IDC_LOG, IDC_CLEAR_LOG,
    IDC_POST_COMBO, IDC_POST_TEXT, IDC_POST_COPY, IDC_POST_OPEN,
    IDC_TPL_COMBO, IDC_TPL_TEXT, IDC_TPL_SAVE, IDC_TPL_NEW, IDC_TPL_DELETE, IDC_TPL_RESET, IDC_TPL_FOLDER, IDC_TPL_HELP,
    IDC_TRK_TEXT, IDC_TRK_SAVE, IDC_TRK_RESET, IDC_TRK_INFO, IDC_TRK_LISTS, IDC_TRK_UPDATE,
    IDC_SHOT_TEXT, IDC_SHOT_SAVE, IDC_SHOT_RESET, IDC_SHOT_INFO,
    IDC_STATUS, IDC_PROGRESS, IDC_DETAILS, IDC_CANCEL, IDC_RUN,
    IDC_LBL_SOURCE, IDC_LBL_TYPE, IDC_LBL_TMDB, IDC_LBL_CREW, IDC_LBL_OUTPUT, IDC_LBL_TITLE,
    IDM_OPEN_FILE = 2000, IDM_OPEN_DIR, IDM_EXIT, IDM_SETTINGS, IDM_DL_FFMPEG, IDM_INSTALL_MI, IDM_CONFIG_DIR, IDM_OUTPUT_DIR,
    IDM_CHECK_TOOLS, IDM_VARS, IDM_ABOUT, IDM_MAKERELEASE
};

constexpr UINT WM_APP_DONE = WM_APP + 1;
constexpr UINT WM_APP_CHOOSE = WM_APP + 2;
constexpr UINT_PTR TIMER_UI = 1;

enum Tab { TabLog, TabPost, TabTemplate, TabTracker, TabShots, TabCount };

struct ChooseRequest {
    const std::vector<TmdbHit>* hits;
    std::string searched;
    TmdbChoice result;
};

struct App {
    HWND hwnd = nullptr;
    HFONT font = nullptr, mono = nullptr, bold = nullptr;
    UINT dpi = 96;
    std::map<int, HWND> c;
    Settings settings;

    // lavoro in corso
    std::thread worker;
    std::atomic<bool> running{false}, cancel{false};
    bool closePending = false;
    std::mutex mu;
    std::vector<std::string> pendingLog;
    double progress = 0;
    std::string stage = "Pronto", details;
    bool progressDirty = true;
    std::function<void(HWND)> onDone;  // eseguita sul thread UI a fine lavoro

    // risultati
    JobResult result;
    std::wstring tplCurrent;
    bool tplDirty = false;
} g;

int S(int v) { return MulDiv(v, int(g.dpi), 96); }
HWND C(int id) { return g.c[id]; }

std::wstring GetText(int id) {
    HWND h = C(id);
    int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}
void SetText(int id, const std::wstring& t) { SetWindowTextW(C(id), t.c_str()); }
void SetTextU8(int id, const std::string& t) { SetText(id, W(ToCrlf(t))); }
bool Checked(int id) { return SendMessageW(C(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void SetChecked(int id, bool v) { SendMessageW(C(id), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

void Log(const std::string& line) {
    std::lock_guard<std::mutex> lock(g.mu);
    g.pendingLog.push_back("[" + NowTimeHMS() + "] " + line);
}

void FlushUi() {
    std::vector<std::string> lines;
    double progress;
    std::string stage, details;
    bool dirty;
    {
        std::lock_guard<std::mutex> lock(g.mu);
        lines.swap(g.pendingLog);
        progress = g.progress;
        stage = g.stage;
        details = g.details;
        dirty = g.progressDirty;
        g.progressDirty = false;
    }
    if (!lines.empty()) {
        HWND log = C(IDC_LOG);
        std::wstring add;
        for (auto& l : lines) add += W(ToCrlf(l)) + L"\r\n";
        int len = GetWindowTextLengthW(log);
        if (len > 2'000'000) {  // limita la memoria del registro
            SendMessageW(log, EM_SETSEL, 0, len / 2);
            SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)L"[...]\r\n");
            len = GetWindowTextLengthW(log);
        }
        SendMessageW(log, EM_SETSEL, len, len);
        SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)add.c_str());
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
    }
    if (dirty) {
        SendMessageW(C(IDC_PROGRESS), PBM_SETPOS, WPARAM(progress * 10), 0);
        wchar_t pct[16];
        swprintf(pct, 16, L"%.0f%%", progress);
        SetText(IDC_STATUS, W(stage) + (g.running ? L"  —  " + std::wstring(pct) : L""));
        SetText(IDC_DETAILS, W(details));
    }
}

void SetProgress(double p, const std::string& stage, const std::string& details) {
    std::lock_guard<std::mutex> lock(g.mu);
    g.progress = std::max(0.0, std::min(100.0, p));
    g.stage = stage;
    g.details = details;
    g.progressDirty = true;
}

class UiHost : public JobHost {
public:
    void Log(const std::string& line) override { ::Log(line); }
    void Progress(double percent, const std::string& stage, const std::string& details) override { SetProgress(percent, stage, details); }
    TmdbChoice ChooseTmdb(const std::vector<TmdbHit>& hits, const std::string& searched) override {
        ChooseRequest req{&hits, searched, {}};
        SendMessageW(g.hwnd, WM_APP_CHOOSE, 0, reinterpret_cast<LPARAM>(&req));
        return req.result;
    }
};

void UpdateRunState() {
    bool r = g.running;
    EnableWindow(C(IDC_RUN), !r);
    EnableWindow(C(IDC_CANCEL), r);
    for (int id : {IDC_TRK_TEXT, IDC_TRK_SAVE, IDC_TRK_RESET, IDC_TRK_LISTS, IDC_TRK_UPDATE})
        EnableWindow(C(id), !r);
    for (int id : {IDC_SOURCE, IDC_SRC_FILE, IDC_SRC_DIR, IDC_TYPE, IDC_TMDB, IDC_CREW, IDC_OUTPUT, IDC_OUT_BROWSE})
        EnableWindow(C(id), !r);
    HMENU m = GetMenu(g.hwnd);
    for (int id : {IDM_SETTINGS, IDM_DL_FFMPEG, IDM_INSTALL_MI, IDM_CHECK_TOOLS}) EnableMenuItem(m, UINT(id), r ? MF_GRAYED : MF_ENABLED);
}

// Esegue body su un thread di lavoro; done (thread UI) riceve l'eventuale errore.
void StartJob(std::function<void()> body, std::function<void(bool ok, const std::string& error, bool cancelled)> done) {
    if (g.running) return;
    if (g.worker.joinable()) g.worker.join();
    g.running = true;
    g.cancel = false;
    UpdateRunState();
    SetProgress(0, "Avvio", "");
    g.worker = std::thread([body = std::move(body), done = std::move(done)]() mutable {
        HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool ok = false, cancelled = false;
        std::string error;
        try {
            body();
            ok = true;
        } catch (Cancelled&) {
            cancelled = true;
        } catch (std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "errore sconosciuto";
        }
        if (SUCCEEDED(co)) CoUninitialize();
        auto* cb = new std::function<void(HWND)>([=](HWND) { done(ok, error, cancelled); });
        PostMessageW(g.hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(cb));
    });
}

// ------------------------------------------------------------------ configurazione

void LoadTemplateList(const std::wstring& select = L"") {
    HWND cb = C(IDC_TPL_COMBO);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    auto list = ListTemplates();
    int sel = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        std::wstring label = PathFileName(list[i]) + L"  →  " + PostNameForTemplate(list[i]);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        if (!select.empty() && _wcsicmp(list[i].c_str(), select.c_str()) == 0) sel = int(i);
    }
    if (list.empty()) {
        g.tplCurrent.clear();
        SetText(IDC_TPL_TEXT, L"");
        return;
    }
    SendMessageW(cb, CB_SETCURSEL, sel, 0);
    g.tplCurrent = list[size_t(sel)];
    SetTextU8(IDC_TPL_TEXT, StripUtf8Bom(ReadTextFile(g.tplCurrent)));
    g.tplDirty = false;
}

bool SaveTemplate(bool quiet) {
    if (g.tplCurrent.empty()) return true;
    std::string text = NormalizeNewlines(U8(GetText(IDC_TPL_TEXT)));
    try {
        ValidateTemplate(text);
    } catch (TemplateError& e) {
        if (MessageBoxW(g.hwnd, (L"Il template contiene un errore:\n\n" + W(e.what()) + L"\n\nSalvare comunque?").c_str(), L"Template",
                        MB_ICONWARNING | MB_YESNO) != IDYES)
            return false;
    }
    if (!WriteFileBytes(g.tplCurrent, text)) {
        MessageBoxW(g.hwnd, (L"Impossibile salvare " + g.tplCurrent).c_str(), L"Template", MB_ICONERROR);
        return false;
    }
    g.tplDirty = false;
    if (!quiet) Log("Template salvato: " + U8(PathFileName(g.tplCurrent)));
    return true;
}

bool ConfirmTemplateChanges() {
    if (!g.tplDirty) return true;
    int r = MessageBoxW(g.hwnd, (L"Il template " + PathFileName(g.tplCurrent) + L" è stato modificato. Salvare?").c_str(), L"Template",
                        MB_ICONQUESTION | MB_YESNOCANCEL);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return SaveTemplate(false);
    g.tplDirty = false;
    return true;
}

void LoadConfigTexts() {
    SetTextU8(IDC_TRK_TEXT, ReadConfigFile(L"trackers.txt"));
    SetTextU8(IDC_SHOT_TEXT, ReadConfigFile(L"screenshots.txt"));
}

void SaveTrackers(bool quiet) {
    const auto list = ParseTrackerList(U8(GetText(IDC_TRK_TEXT)));
    if (!WriteConfigFile(L"trackers.txt", U8(GetText(IDC_TRK_TEXT)))) {
        Log("ERRORE: impossibile salvare trackers.txt.");
        return;
    }
    SetText(IDC_TRK_INFO, L"Tracker nell'elenco: " + std::to_wstring(list.size()));
    if (!quiet) Log("Elenco tracker salvato (" + std::to_string(list.size()) + " validi).");
}

void RunUpdateTrackerLists() {
    if (Checked(IDC_CHK_PRIVATE)) {
        MessageBoxW(g.hwnd, L"Per un torrent privato usa nell'elenco solo i tracker del sito. "
                    L"L'importazione da liste pubbliche è disattivata mentre Torrent privato è selezionato.",
                    L"Tracker", MB_ICONINFORMATION);
        return;
    }
    if (TrackerListSources().empty()) {
        MessageBoxW(g.hwnd, L"Nessuna fonte attiva. Apri Liste remote e togli # davanti all’URL della fonte da usare, poi salva.",
                    L"Tracker", MB_ICONINFORMATION);
        return;
    }
    const auto current = ParseTrackerList(U8(GetText(IDC_TRK_TEXT)));
    auto remote = std::make_shared<std::vector<std::string>>();
    StartJob(
        [remote] {
            SetProgress(20, "Importazione tracker", "");
            *remote = UpdateRemoteTrackers(g.settings.trackerListsMax, &g.cancel,
                                           [](const std::string& line) { Log(line); });
        },
        [remote, current](bool ok, const std::string& err, bool cancelled) {
            if (ok) {
                const auto merged = MergeTrackers(current, *remote);
                const auto added = merged.size() - current.size();
                const std::string text = Join(merged, "\r\n") + "\r\n";
                // Persist before replacing the visible list, so failed writes are not reported as success.
                if (!WriteConfigFile(L"trackers.txt", text)) {
                    Log("ERRORE: tracker scaricati, ma impossibile salvare l'elenco. Elenco precedente mantenuto.");
                    SetProgress(0, "Elenco non salvato", "Impossibile scrivere trackers.txt");
                    return;
                }
                SetTextU8(IDC_TRK_TEXT, text);
                g.settings.trackerListsLast = UnixNow();
                g.settings.Save();
                Log("Tracker scaricati: " + std::to_string(remote->size()) + "; nuovi aggiunti: " +
                    std::to_string(added) + "; totale nell'elenco: " + std::to_string(merged.size()) + ".");
                SetProgress(100, "Elenco tracker aggiornato", std::to_string(added) + " nuovi tracker");
                SaveTrackers(true);
            } else if (!err.empty()) {
                Log("Importazione tracker non completata: " + err + ". Elenco precedente mantenuto.");
                SetProgress(0, "Elenco tracker non aggiornato", err);
            } else if (cancelled) {
                SetProgress(0, "Importazione annullata", "Elenco precedente mantenuto");
            }
        });
}

void ResetTrackers() {
    const std::string defaults = DefaultContent(DefaultFile::Trackers);
    if (!WriteConfigFile(L"trackers.txt", defaults)) {
        MessageBoxW(g.hwnd, L"Impossibile salvare l'elenco predefinito.", L"Tracker", MB_ICONERROR);
        return;
    }
    SetTextU8(IDC_TRK_TEXT, defaults);
    // Keep configured URLs as commented examples, ready to enable for a later import.
    std::string sources;
    for (const auto& line : SplitLines(ReadConfigFile(L"tracker_lists.txt"))) {
        const auto trimmed = Trim(line);
        sources += (!trimmed.empty() && trimmed[0] != '#' ? "# " : "") + line + "\n";
    }
    const bool sourcesSaved = WriteConfigFile(L"tracker_lists.txt", sources);
    const bool cacheCleared = WriteConfigFile(L"trackers_remote.txt", "");
    if (!sourcesSaved || !cacheCleared)
        Log("AVVISO: elenco ripristinato, ma pulizia delle fonti o della cache non completata.");
    g.settings.trackerListsAuto = false;
    g.settings.trackerListsLast = 0;
    g.settings.Save();
    SaveTrackers(true);
    Log(sourcesSaved ? "Elenco tracker ripristinato ai predefiniti. Fonti remote disattivate."
                     : "Elenco tracker ripristinato ai predefiniti. Controlla le fonti in Liste remote.");
}

void SaveShots(bool quiet) {
    WriteConfigFile(L"screenshots.txt", U8(GetText(IDC_SHOT_TEXT)));
    if (!quiet) Log("screenshots.txt salvato.");
}

void SaveUiSettings() {
    g.settings.crew = Trim(U8(GetText(IDC_CREW)));
    std::wstring out = GetText(IDC_OUTPUT);
    g.settings.outputDir = (Trim(U8(out)).empty() || _wcsicmp(out.c_str(), DefaultOutputDir().c_str()) == 0) ? L"" : out;
    g.settings.releaseType = int(SendMessageW(C(IDC_TYPE), CB_GETCURSEL, 0, 0));
    g.settings.screenshots = Checked(IDC_CHK_SHOTS);
    g.settings.torrent = Checked(IDC_CHK_TORRENT);
    g.settings.privateTorrent = Checked(IDC_CHK_PRIVATE);
    g.settings.upload = Checked(IDC_CHK_UPLOAD);
    g.settings.bitrateGraph = Checked(IDC_CHK_BITRATE);
    g.settings.resume = Checked(IDC_CHK_RESUME);
    WINDOWPLACEMENT wp{};
    wp.length = sizeof wp;
    if (GetWindowPlacement(g.hwnd, &wp)) {
        g.settings.winMax = wp.showCmd == SW_SHOWMAXIMIZED;
        g.settings.winX = wp.rcNormalPosition.left;
        g.settings.winY = wp.rcNormalPosition.top;
        g.settings.winW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        g.settings.winH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    }
    g.settings.Save();
}

// ------------------------------------------------------------------ strumenti

void CheckTools(bool offerSetup) {
    std::wstring ff = FindFfmpeg(g.settings.ffmpegPath);
    MediaInfoStatus mi = DetectMediaInfo({g.settings.mediaInfoPath, ff});
    if (ff.empty()) Log("FFmpeg: NON trovato (Strumenti > Scarica FFmpeg). Servono per screenshot e grafico bitrate.");
    else Log("FFmpeg: " + U8(ff) + (FindFfprobe(ff).empty() ? "  (ffprobe mancante: niente grafico bitrate)" : ""));
    switch (mi.backend) {
        case MediaInfoBackend::Dll: Log("MediaInfo completo: " + mi.version + " - " + U8(mi.location)); break;
        case MediaInfoBackend::Cli: Log("MediaInfo: solo CLI (" + U8(mi.location) + "). Consigliata la versione completa: Strumenti > Installa MediaInfo."); break;
        default: Log("MediaInfo completo: NON trovato (Strumenti > Installa MediaInfo). " + mi.note);
    }
    if (offerSetup && !g.settings.toolsPromptShown && (ff.empty() || mi.backend != MediaInfoBackend::Dll)) {
        g.settings.toolsPromptShown = true;
        g.settings.Save();
        std::wstring msg = L"Per funzionare al meglio ReleaseForge usa:\n\n";
        msg += std::wstring(ff.empty() ? L"✖" : L"✔") + L" FFmpeg (screenshot, grafico bitrate)\n";
        msg += std::wstring(mi.backend == MediaInfoBackend::Dll ? L"✔" : L"✖") + L" MediaInfo completo (report e titolo della release)\n\n";
        msg += L"Apro Impostazioni e strumenti per scaricarli/installarli?";
        if (MessageBoxW(g.hwnd, msg.c_str(), L"ReleaseForge - strumenti", MB_ICONINFORMATION | MB_YESNO) == IDYES)
            PostMessageW(g.hwnd, WM_COMMAND, IDM_SETTINGS, 0);
    }
}

void RunDownloadFfmpeg() {
    Log("Download FFmpeg (gyan.dev, essentials) con verifica SHA-256...");
    auto path = std::make_shared<std::wstring>();
    StartJob(
        [path] {
            *path = DownloadFfmpeg(&g.cancel, [](const std::string& s) { Log(s); }, [](uint64_t d, uint64_t t) {
                char b[96];
                snprintf(b, sizeof b, "%.1f / %.1f MiB", d / 1048576.0, t / 1048576.0);
                SetProgress(t ? 100.0 * double(d) / double(t) : 0, "Download FFmpeg", b);
            });
        },
        [path](bool ok, const std::string& err, bool cancelled) {
            if (ok) {
                g.settings.ffmpegPath.clear();  // rilevamento automatico trova tools\ffmpeg
                g.settings.Save();
                SetProgress(100, "FFmpeg installato", U8(*path));
                MessageBoxW(g.hwnd, (L"FFmpeg installato in:\n" + *path).c_str(), L"ReleaseForge", MB_ICONINFORMATION);
            } else if (cancelled) {
                SetProgress(0, "Download annullato", "");
            } else {
                Log("ERRORE download FFmpeg: " + err);
                SetProgress(0, "Errore", err);
                MessageBoxW(g.hwnd, (L"Download FFmpeg non riuscito:\n\n" + W(err) + L"\n\nIn alternativa installa FFmpeg con:\nwinget install Gyan.FFmpeg").c_str(),
                            L"ReleaseForge", MB_ICONERROR);
            }
        });
}

void RunInstallMediaInfo() {
    if (!WingetAvailable()) {
        MessageBoxW(g.hwnd, L"winget non è disponibile su questo PC.\nSi apre la pagina ufficiale: scarica e installa \"MediaInfo\" (GUI, 64 bit).", L"MediaInfo", MB_ICONINFORMATION);
        OpenUrl(L"https://mediaarea.net/it/MediaInfo/Download/Windows");
        return;
    }
    if (MessageBoxW(g.hwnd, L"Verrà installato MediaInfo (versione completa con interfaccia) tramite winget:\n\n"
                            L"winget install --id MediaArea.MediaInfo.GUI\n\nWindows potrebbe chiedere l'autorizzazione di amministratore. Continuare?",
                    L"Installa MediaInfo completo", MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;
    auto err = std::make_shared<std::string>();
    auto ok = std::make_shared<bool>(false);
    StartJob(
        [err, ok] {
            SetProgress(10, "Installazione MediaInfo (winget)", "attendere...");
            *ok = InstallMediaInfoWithWinget([](const std::string& s) { Log(s); }, *err);
        },
        [err, ok](bool, const std::string& e, bool) {
            if (*ok) {
                SetProgress(100, "MediaInfo installato", "");
                CheckTools(false);
                MessageBoxW(g.hwnd, L"MediaInfo completo installato: ReleaseForge userà la sua MediaInfo.dll.", L"MediaInfo", MB_ICONINFORMATION);
            } else {
                std::string m = e.empty() ? *err : e;
                Log("MediaInfo: " + m);
                SetProgress(0, "Installazione non riuscita", m);
                if (MessageBoxW(g.hwnd, (W(m) + L"\n\nAprire la pagina di download ufficiale?").c_str(), L"MediaInfo", MB_ICONWARNING | MB_YESNO) == IDYES)
                    OpenUrl(L"https://mediaarea.net/it/MediaInfo/Download/Windows");
            }
        });
}

// ------------------------------------------------------------------ release

void ShowPosts() {
    HWND cb = C(IDC_POST_COMBO);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (auto& p : g.result.posts) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)(PathFileName(p.file) + L"  (" + p.templateName + L")").c_str());
    if (!g.result.posts.empty()) {
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        SetTextU8(IDC_POST_TEXT, g.result.posts[0].text);
    } else {
        SetText(IDC_POST_TEXT, L"");
    }
    EnableWindow(C(IDC_POST_COPY), !g.result.posts.empty());
    EnableWindow(C(IDC_POST_OPEN), !g.result.outputDir.empty());
}

void SelectTab(int tab);

void StartRelease() {
    std::wstring source = GetText(IDC_SOURCE);
    while (!source.empty() && (source.back() == L' ' || source.back() == L'"')) source.pop_back();
    while (!source.empty() && (source.front() == L' ' || source.front() == L'"')) source.erase(0, 1);
    if (source.empty() || (!FileExists(source) && !DirExists(source))) {
        MessageBoxW(g.hwnd, L"Seleziona un file video o una cartella esistente (puoi anche trascinarli sulla finestra).", L"ReleaseForge", MB_ICONWARNING);
        return;
    }
    if (Trim(g.settings.tmdbKey).empty()) {
        MessageBoxW(g.hwnd, L"Manca la chiave TheMovieDB. Inseriscila in Strumenti > Impostazioni e strumenti.", L"ReleaseForge", MB_ICONWARNING);
        PostMessageW(g.hwnd, WM_COMMAND, IDM_SETTINGS, 0);
        return;
    }
    if (!ConfirmTemplateChanges()) return;
    SaveTrackers(true);
    SaveShots(true);
    SaveUiSettings();

    JobInput in;
    in.source = source;
    in.type = ReleaseType(SendMessageW(C(IDC_TYPE), CB_GETCURSEL, 0, 0));
    in.tmdbRef = Trim(U8(GetText(IDC_TMDB)));
    in.crew = g.settings.crew;
    in.outputRoot = EffectiveOutputDir(g.settings);
    in.screenshots = Checked(IDC_CHK_SHOTS);
    in.torrent = Checked(IDC_CHK_TORRENT);
    in.privateTorrent = Checked(IDC_CHK_PRIVATE);
    in.upload = Checked(IDC_CHK_UPLOAD);
    in.bitrateGraph = Checked(IDC_CHK_BITRATE);
    in.resume = Checked(IDC_CHK_RESUME);
    in.pieceSizeKiB = g.settings.pieceSizeKiB;
    in.trackers = ParseTrackerList(U8(GetText(IDC_TRK_TEXT)));
    for (auto& l : SplitLines(U8(GetText(IDC_SHOT_TEXT)))) if (!Trim(l).empty()) in.timestamps.push_back(Trim(l));
    in.settings = g.settings;
    if (in.torrent && in.trackers.empty()) {
        MessageBoxW(g.hwnd, L"Nessun tracker valido: compila la scheda Tracker (uno per riga).", L"ReleaseForge", MB_ICONWARNING);
        SelectTab(TabTracker);
        return;
    }

    g.result = JobResult();
    SetText(IDC_TITLE, L"");
    EnableWindow(C(IDC_COPY_TITLE), FALSE);
    ShowPosts();
    SelectTab(TabLog);
    Log("==== Avvio release: " + U8(source));
    auto result = std::make_shared<JobResult>();
    StartJob(
        [in, result]() mutable {
            UiHost host;
            host.Log("Tracker usati: " + std::to_string(in.trackers.size()) + " (elenco visibile nella scheda Tracker).");
            *result = RunRelease(in, host, g.cancel);
        },
        [result](bool ok, const std::string& err, bool cancelled) {
            g.settings.Save();
            SaveTrackers(true);
            if (ok) {
                g.result = *result;
                SetText(IDC_TITLE, W(g.result.forumTitle));
                EnableWindow(C(IDC_COPY_TITLE), !g.result.forumTitle.empty());
                ShowPosts();
                Log("Completato. File in " + U8(g.result.outputDir));
                if (!g.result.warnings.empty()) Log(std::to_string(g.result.warnings.size()) + " avvisi: controlla il registro.");
                SetProgress(100, "Completato", U8(g.result.outputDir));
                SelectTab(TabPost);
            } else if (cancelled) {
                Log("Operazione annullata.");
                SetProgress(g.progress, "Annullato", "");
            } else {
                Log("ERRORE: " + err);
                SetProgress(g.progress, "Errore", err);
                MessageBoxW(g.hwnd, W(err).c_str(), L"ReleaseForge - errore", MB_ICONERROR);
            }
        });
}

// ------------------------------------------------------------------ layout

HWND Make(const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.hwnd, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(nullptr), nullptr);
    g.c[id] = h;
    return h;
}

const int kTabIds[TabCount][9] = {
    {IDC_LOG, IDC_CLEAR_LOG},
    {IDC_POST_COMBO, IDC_POST_TEXT, IDC_POST_COPY, IDC_POST_OPEN},
    {IDC_TPL_COMBO, IDC_TPL_TEXT, IDC_TPL_SAVE, IDC_TPL_NEW, IDC_TPL_DELETE, IDC_TPL_RESET, IDC_TPL_FOLDER, IDC_TPL_HELP},
    {IDC_TRK_TEXT, IDC_TRK_SAVE, IDC_TRK_RESET, IDC_TRK_INFO, IDC_TRK_LISTS, IDC_TRK_UPDATE},
    {IDC_SHOT_TEXT, IDC_SHOT_SAVE, IDC_SHOT_RESET, IDC_SHOT_INFO},
};

void SelectTab(int tab) {
    TabCtrl_SetCurSel(C(IDC_TAB), tab);
    for (int t = 0; t < TabCount; ++t)
        for (int id : kTabIds[t]) if (id) ShowWindow(C(id), t == tab ? SW_SHOW : SW_HIDE);
}

void ApplyFonts() {
    if (g.font) DeleteObject(g.font);
    if (g.mono) DeleteObject(g.mono);
    if (g.bold) DeleteObject(g.bold);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    using SpiForDpi = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    auto spi = reinterpret_cast<SpiForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));
    if (!(spi && spi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, g.dpi))) {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, int(g.dpi), 96);
    }
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW b = ncm.lfMessageFont;
    b.lfWeight = FW_SEMIBOLD;
    g.bold = CreateFontIndirectW(&b);
    LOGFONTW m{};
    m.lfHeight = -MulDiv(10, int(g.dpi), 72);
    m.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(m.lfFaceName, L"Consolas");
    g.mono = CreateFontIndirectW(&m);
    for (auto& [id, h] : g.c) {
        bool isMono = id == IDC_LOG || id == IDC_POST_TEXT || id == IDC_TPL_TEXT || id == IDC_TRK_TEXT || id == IDC_SHOT_TEXT;
        HFONT f = isMono ? g.mono : (id == IDC_TITLE ? g.bold : g.font);
        SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    }
}

void Layout() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int m = S(12), gap = S(8), rowH = S(25), lbl = S(92), btn = S(92);
    HDWP d = BeginDeferWindowPos(80);
    auto put = [&](int id, int x, int y, int w, int h) {
        d = DeferWindowPos(d, C(id), nullptr, x, y, std::max(0, w), std::max(0, h), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    auto label = [&](int id, int x, int y, int w) { put(id, x, y + S(4), w, rowH - S(4)); };
    int y = m;
    // Riga 1: sorgente
    label(IDC_LBL_SOURCE, m, y, lbl);
    put(IDC_SRC_DIR, W - m - btn, y, btn, rowH);
    put(IDC_SRC_FILE, W - m - 2 * btn - gap, y, btn, rowH);
    put(IDC_SOURCE, m + lbl, y, W - 2 * m - lbl - 2 * btn - 2 * gap, rowH);
    y += rowH + gap;
    // Riga 2: tipo + TMDB
    label(IDC_LBL_TYPE, m, y, lbl);
    int typeW = S(220);
    put(IDC_TYPE, m + lbl, y, typeW, S(200));
    int x2 = m + lbl + typeW + gap * 2;
    label(IDC_LBL_TMDB, x2, y, S(110));
    put(IDC_TMDB, x2 + S(110), y, W - m - x2 - S(110), rowH);
    y += rowH + gap;
    // Riga 3: crew + destinazione
    label(IDC_LBL_CREW, m, y, lbl);
    put(IDC_CREW, m + lbl, y, typeW, rowH);
    label(IDC_LBL_OUTPUT, x2, y, S(110));
    put(IDC_OUT_BROWSE, W - m - btn, y, btn, rowH);
    put(IDC_OUTPUT, x2 + S(110), y, W - m - x2 - S(110) - btn - gap, rowH);
    y += rowH + gap;
    // Riga 4: opzioni
    int cx = m + lbl;
    for (auto [id, w] : {std::pair{IDC_CHK_SHOTS, 110}, {IDC_CHK_TORRENT, 110}, {IDC_CHK_PRIVATE, 130}, {IDC_CHK_UPLOAD, 130}, {IDC_CHK_BITRATE, 130}, {IDC_CHK_RESUME, 190}}) {
        put(id, cx, y, S(w), rowH);
        cx += S(w) + gap;
    }
    y += rowH + gap;
    // Riga 5: titolo forum
    label(IDC_LBL_TITLE, m, y, lbl);
    put(IDC_COPY_TITLE, W - m - btn - S(20), y, btn + S(20), rowH + S(2));
    put(IDC_TITLE, m + lbl, y, W - 2 * m - lbl - btn - S(20) - gap, rowH + S(2));
    y += rowH + S(2) + gap + S(2);

    // Barra inferiore
    const int bottomH = S(70);
    int by = H - m - bottomH;
    put(IDC_STATUS, m, by, W - 2 * m, S(20));
    put(IDC_PROGRESS, m, by + S(22), W - 2 * m, S(14));
    put(IDC_RUN, W - m - S(140), by + S(42), S(140), S(28));
    put(IDC_CANCEL, W - m - S(140) - gap - btn, by + S(42), btn, S(28));
    put(IDC_DETAILS, m, by + S(46), W - 2 * m - S(140) - btn - 2 * gap, S(22));

    // Schede
    int tabH = by - gap - y;
    put(IDC_TAB, m, y, W - 2 * m, tabH);
    RECT tr{m, y, W - m, y + tabH};
    TabCtrl_AdjustRect(C(IDC_TAB), FALSE, &tr);
    InflateRect(&tr, -S(4), -S(4));
    int tx = tr.left, ty = tr.top, tw = tr.right - tr.left, th = tr.bottom - tr.top;
    int toolbarH = rowH + gap;
    // Registro
    put(IDC_CLEAR_LOG, tx + tw - btn, ty, btn, rowH);
    put(IDC_LOG, tx, ty + toolbarH, tw, th - toolbarH);
    // Post
    put(IDC_POST_COMBO, tx, ty, S(320), S(200));
    put(IDC_POST_COPY, tx + S(320) + gap, ty, S(110), rowH);
    put(IDC_POST_OPEN, tx + S(320) + gap * 2 + S(110), ty, S(150), rowH);
    put(IDC_POST_TEXT, tx, ty + toolbarH, tw, th - toolbarH);
    // Template
    int px = tx;
    put(IDC_TPL_COMBO, px, ty, S(300), S(200));
    px += S(300) + gap;
    for (auto [id, w] : {std::pair{IDC_TPL_SAVE, 70}, {IDC_TPL_NEW, 80}, {IDC_TPL_DELETE, 70}, {IDC_TPL_RESET, 140}, {IDC_TPL_FOLDER, 110}, {IDC_TPL_HELP, 90}}) {
        put(id, px, ty, S(w), rowH);
        px += S(w) + gap / 2;
    }
    put(IDC_TPL_TEXT, tx, ty + toolbarH, tw, th - toolbarH);
    // Tracker e screenshot
    for (auto [text, save, reset, info] : {std::tuple{IDC_TRK_TEXT, IDC_TRK_SAVE, IDC_TRK_RESET, IDC_TRK_INFO}, {IDC_SHOT_TEXT, IDC_SHOT_SAVE, IDC_SHOT_RESET, IDC_SHOT_INFO}}) {
        int extra = text == IDC_TRK_TEXT ? S(260) + gap * 2 : 0;
        put(save, tx, ty, S(80), rowH);
        put(reset, tx + S(80) + gap, ty, S(150), rowH);
        if (extra) {
            put(IDC_TRK_LISTS, tx + S(230) + gap * 2, ty, S(120), rowH);
            put(IDC_TRK_UPDATE, tx + S(350) + gap * 3, ty, S(140), rowH);
        }
        put(info, tx + S(230) + gap * 2 + extra, ty + S(4), tw - S(230) - gap * 2 - extra, rowH - S(4));
        put(text, tx, ty + toolbarH, tw, th - toolbarH);
    }
    EndDeferWindowPos(d);
}

HWND MakeEdit(int id, bool multiline, bool readOnly, bool wrap = false) {
    DWORD style = WS_TABSTOP | ES_AUTOHSCROLL;
    if (multiline) {
        style = WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL;
        if (!wrap) style |= ES_AUTOHSCROLL | WS_HSCROLL;
    }
    if (readOnly) style |= ES_READONLY;
    HWND h = Make(L"EDIT", L"", style, id, WS_EX_CLIENTEDGE);
    if (multiline) SendMessageW(h, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    return h;
}

HWND MakeButton(int id, const std::wstring& text, DWORD extra = BS_PUSHBUTTON) { return Make(L"BUTTON", text, WS_TABSTOP | extra, id); }
HWND MakeCheck(int id, const std::wstring& text) { return Make(L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, id); }
HWND MakeLabel(int id, const std::wstring& text, DWORD extra = 0) { return Make(L"STATIC", text, SS_LEFT | SS_NOPREFIX | extra, id); }

void CreateControls() {
    MakeLabel(IDC_LBL_SOURCE, L"Sorgente");
    MakeEdit(IDC_SOURCE, false, false);
    SendMessageW(C(IDC_SOURCE), EM_SETCUEBANNER, TRUE, (LPARAM)L"File video o cartella (trascina qui)");
    MakeButton(IDC_SRC_FILE, L"File…");
    MakeButton(IDC_SRC_DIR, L"Cartella…");
    MakeLabel(IDC_LBL_TYPE, L"Tipo release");
    HWND type = Make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_TYPE);
    for (int i = 0; i <= 4; ++i) SendMessageW(type, CB_ADDSTRING, 0, (LPARAM)W(ReleaseTypeLabel(ReleaseType(i))).c_str());
    MakeLabel(IDC_LBL_TMDB, L"TMDB ID / URL");
    MakeEdit(IDC_TMDB, false, false);
    SendMessageW(C(IDC_TMDB), EM_SETCUEBANNER, TRUE, (LPARAM)L"Facoltativo: vuoto = ricerca automatica dal nome");
    MakeLabel(IDC_LBL_CREW, L"Crew");
    MakeEdit(IDC_CREW, false, false);
    SendMessageW(C(IDC_CREW), EM_SETCUEBANNER, TRUE, (LPARAM)L"Facoltativo, es. MIRCrew");
    MakeLabel(IDC_LBL_OUTPUT, L"Destinazione");
    MakeEdit(IDC_OUTPUT, false, false);
    MakeButton(IDC_OUT_BROWSE, L"Sfoglia…");
    MakeCheck(IDC_CHK_SHOTS, L"Screenshot");
    MakeCheck(IDC_CHK_TORRENT, L"Crea .torrent");
    MakeCheck(IDC_CHK_PRIVATE, L"Torrent privato");
    MakeCheck(IDC_CHK_UPLOAD, L"Carica immagini");
    MakeCheck(IDC_CHK_BITRATE, L"Grafico bitrate");
    MakeCheck(IDC_CHK_RESUME, L"Riprendi (riusa torrent/report)");
    MakeLabel(IDC_LBL_TITLE, L"Titolo forum");
    MakeEdit(IDC_TITLE, false, true);
    SendMessageW(C(IDC_TITLE), EM_SETCUEBANNER, TRUE, (LPARAM)L"Comparirà qui al termine (formato makerelease)");
    MakeButton(IDC_COPY_TITLE, L"Copia titolo");

    HWND tab = Make(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, IDC_TAB);
    const wchar_t* tabs[] = {L"Registro", L"Post generato", L"Template", L"Tracker", L"Screenshot"};
    for (int i = 0; i < TabCount; ++i) {
        TCITEMW it{};
        it.mask = TCIF_TEXT;
        it.pszText = const_cast<LPWSTR>(tabs[i]);
        TabCtrl_InsertItem(tab, i, &it);
    }
    MakeEdit(IDC_LOG, true, true);
    MakeButton(IDC_CLEAR_LOG, L"Pulisci");
    Make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_POST_COMBO);
    MakeEdit(IDC_POST_TEXT, true, true);
    MakeButton(IDC_POST_COPY, L"Copia post");
    MakeButton(IDC_POST_OPEN, L"Apri cartella output");
    Make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_TPL_COMBO);
    MakeEdit(IDC_TPL_TEXT, true, false);
    MakeButton(IDC_TPL_SAVE, L"Salva");
    MakeButton(IDC_TPL_NEW, L"Nuovo…");
    MakeButton(IDC_TPL_DELETE, L"Elimina");
    MakeButton(IDC_TPL_RESET, L"Standard makerelease");
    MakeButton(IDC_TPL_FOLDER, L"Apri cartella");
    MakeButton(IDC_TPL_HELP, L"Variabili…");
    MakeEdit(IDC_TRK_TEXT, true, false);
    MakeButton(IDC_TRK_SAVE, L"Salva");
    MakeButton(IDC_TRK_RESET, L"Ripristina predefiniti");
    MakeLabel(IDC_TRK_INFO, L"Elenco usato nel torrent. Un tracker per riga.", SS_ENDELLIPSIS);
    MakeButton(IDC_TRK_LISTS, L"Liste remote\u2026");
    MakeButton(IDC_TRK_UPDATE, L"Aggiorna dalle liste");
    MakeEdit(IDC_SHOT_TEXT, true, false);
    MakeButton(IDC_SHOT_SAVE, L"Salva");
    MakeButton(IDC_SHOT_RESET, L"Ripristina predefiniti");
    MakeLabel(IDC_SHOT_INFO, L"Un orario per riga (HH:MM:SS). Gli orari oltre la durata del video vengono saltati.");

    MakeLabel(IDC_STATUS, L"Pronto", SS_ENDELLIPSIS);
    Make(PROGRESS_CLASSW, L"", PBS_SMOOTH, IDC_PROGRESS);
    SendMessageW(C(IDC_PROGRESS), PBM_SETRANGE32, 0, 1000);
    MakeLabel(IDC_DETAILS, L"", SS_ENDELLIPSIS);
    MakeButton(IDC_CANCEL, L"Annulla");
    MakeButton(IDC_RUN, L"Genera release", BS_DEFPUSHBUTTON);
    // La scheda va in fondo allo z-order così non copre i controlli che contiene visivamente.
    SetWindowPos(tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

HMENU CreateMainMenu() {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_OPEN_FILE, L"Apri &file video…\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_OPEN_DIR, L"Apri &cartella…\tCtrl+Shift+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_OUTPUT_DIR, L"Apri cartella di &output");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"&Esci");
    AppendMenuW(tools, MF_STRING, IDM_SETTINGS, L"&Impostazioni e strumenti…\tCtrl+,");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, IDM_DL_FFMPEG, L"Scarica/aggiorna &FFmpeg");
    AppendMenuW(tools, MF_STRING, IDM_INSTALL_MI, L"Installa &MediaInfo completo (winget)");
    AppendMenuW(tools, MF_STRING, IDM_CHECK_TOOLS, L"&Verifica strumenti");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, IDM_CONFIG_DIR, L"Apri cartella &configurazione (template, tracker)");
    AppendMenuW(help, MF_STRING, IDM_VARS, L"&Variabili dei template");
    AppendMenuW(help, MF_STRING, IDM_MAKERELEASE, L"Progetto &makerelease (GitHub)");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&Informazioni");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Strumenti");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Aiuto");
    return bar;
}

const wchar_t* kVarsHelp =
    L"Variabili (standard makerelease):\n\n"
    L"{{ TITLE }}  {{ ORIGINAL_TITLE }}  {{ YEAR }}  {{ RUNTIME }}  {{ PLOT }}\n"
    L"{{ GENRE }}  {{ DIRECTOR }}  {{ COUNTRY }}  {{ POSTER_URL }}  {{ TMDB_URL }}\n"
    L"{{ TRAILER }}  {{ SCREENSHOTS }}  {{ BITRATE_GRAPH }}  {{ SIZE }}  {{ MAGNET }}\n"
    L"{{ REPORT_MEDIAINFO }}  {{ REPORT_AVINAPTIC }}\n"
    L"{{ TREE }}  - solo cartelle (film+extra, serie): usare {% if TREE is defined %}\n"
    L"{{ EP_COUNT }}  - solo serie: usare {% if EP_COUNT is defined %}\n\n"
    L"Cast:\n{% for person in CAST %}[*] {{ person.name }}: {{ person.character }}\n{% endfor %}\n"
    L"(person.actor è un alias di person.name)\n\n"
    L"Aggiunte ReleaseForge: {{ FORUM_TITLE }}  {{ RELEASE_NAME }}  {{ CREW }}  {{ TMDB_ID }}  {{ MEDIA_TYPE }}\n\n"
    L"Sintassi Jinja2: if/elif/else, for (loop.index, loop.last), set, filtri default, upper, lower,\n"
    L"title, trim, replace, join, length, truncate. trim_blocks e lstrip_blocks attivi come in makerelease.\n\n"
    L"Multi-template: ogni file template_NOME.jinja nella cartella di configurazione\n"
    L"genera post_NOME.txt (template.jinja genera post.txt).";

void OnCommand(int id, int code) {
    switch (id) {
        case IDC_SRC_FILE:
        case IDM_OPEN_FILE: {
            std::wstring p = GetText(IDC_SOURCE);
            if (PickFile(g.hwnd, L"Seleziona il file video", {{L"Video", L"*.mkv;*.mp4;*.avi;*.m2ts;*.ts;*.mov;*.webm;*.wmv;*.m4v"}}, p)) SetText(IDC_SOURCE, p);
            break;
        }
        case IDC_SRC_DIR:
        case IDM_OPEN_DIR: {
            std::wstring p = GetText(IDC_SOURCE);
            if (PickFolder(g.hwnd, L"Seleziona la cartella della release", p)) SetText(IDC_SOURCE, p);
            break;
        }
        case IDC_OUT_BROWSE: {
            std::wstring p = GetText(IDC_OUTPUT);
            if (PickFolder(g.hwnd, L"Cartella di destinazione", p)) SetText(IDC_OUTPUT, p);
            break;
        }
        case IDC_SOURCE:
            if (code == EN_CHANGE && SendMessageW(C(IDC_TYPE), CB_GETCURSEL, 0, 0) == 0) {
                std::wstring s = GetText(IDC_SOURCE);
                ReleaseType t = (FileExists(s) || DirExists(s)) ? DetectReleaseType(s) : ReleaseType::Auto;
                SetText(IDC_DETAILS, t == ReleaseType::Auto ? L"" : L"Tipo rilevato: " + W(ReleaseTypeLabel(t)));
            }
            break;
        case IDC_COPY_TITLE:
            if (SetClipboardText(g.hwnd, GetText(IDC_TITLE))) SetText(IDC_DETAILS, L"Titolo copiato negli appunti.");
            break;
        case IDC_CLEAR_LOG: SetText(IDC_LOG, L""); break;
        case IDC_POST_COMBO:
            if (code == CBN_SELCHANGE) {
                int i = int(SendMessageW(C(IDC_POST_COMBO), CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < int(g.result.posts.size())) SetTextU8(IDC_POST_TEXT, g.result.posts[size_t(i)].text);
            }
            break;
        case IDC_POST_COPY:
            if (SetClipboardText(g.hwnd, GetText(IDC_POST_TEXT))) SetText(IDC_DETAILS, L"Post copiato negli appunti.");
            break;
        case IDC_POST_OPEN:
        case IDM_OUTPUT_DIR: {
            std::wstring dir = g.result.outputDir.empty() ? EffectiveOutputDir(g.settings) : g.result.outputDir;
            CreateDirs(dir);
            OpenInExplorer(dir);
            break;
        }
        case IDC_TPL_COMBO:
            if (code == CBN_SELCHANGE) {
                auto list = ListTemplates();
                int i = int(SendMessageW(C(IDC_TPL_COMBO), CB_GETCURSEL, 0, 0));
                std::wstring target = i >= 0 && i < int(list.size()) ? list[size_t(i)] : L"";
                if (!ConfirmTemplateChanges()) {
                    // Annullato: ripristina la selezione senza ricaricare (le modifiche restano nell'editor).
                    for (size_t k = 0; k < list.size(); ++k)
                        if (_wcsicmp(list[k].c_str(), g.tplCurrent.c_str()) == 0) SendMessageW(C(IDC_TPL_COMBO), CB_SETCURSEL, WPARAM(k), 0);
                    break;
                }
                LoadTemplateList(target);
            }
            break;
        case IDC_TPL_TEXT:
            if (code == EN_CHANGE) g.tplDirty = true;
            break;
        case IDC_TPL_SAVE: SaveTemplate(false); break;
        case IDC_TPL_NEW: {
            if (!ConfirmTemplateChanges()) break;
            std::wstring name = L"forum";
            if (!ShowInput(g.hwnd, L"Nuovo template", L"Nome del template (diventa template_NOME.jinja e genera post_NOME.txt):", name)) break;
            std::string clean;
            for (char ch : U8(name)) if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') clean += ch;
            if (clean.empty()) { MessageBeep(MB_ICONWARNING); break; }
            std::wstring path = PathJoin(ConfigDir(), L"template_" + W(clean) + L".jinja");
            if (FileExists(path)) { MessageBoxW(g.hwnd, L"Esiste già un template con questo nome.", L"Template", MB_ICONWARNING); break; }
            WriteFileBytes(path, NormalizeNewlines(DefaultContent(DefaultFile::Template)));
            LoadTemplateList(path);
            Log("Creato " + U8(PathFileName(path)) + " (copia dello standard makerelease).");
            break;
        }
        case IDC_TPL_DELETE: {
            if (g.tplCurrent.empty()) break;
            if (ListTemplates().size() <= 1) { MessageBoxW(g.hwnd, L"Serve almeno un template.", L"Template", MB_ICONWARNING); break; }
            if (MessageBoxW(g.hwnd, (L"Eliminare " + PathFileName(g.tplCurrent) + L"?").c_str(), L"Template", MB_ICONQUESTION | MB_YESNO) != IDYES) break;
            DeleteFileW(g.tplCurrent.c_str());
            g.tplDirty = false;
            LoadTemplateList();
            break;
        }
        case IDC_TPL_RESET:
            if (MessageBoxW(g.hwnd, L"Sostituire il testo nell'editor con il template standard di makerelease?\n(Le modifiche diventano definitive con Salva.)",
                            L"Template", MB_ICONQUESTION | MB_YESNO) == IDYES) {
                SetTextU8(IDC_TPL_TEXT, DefaultContent(DefaultFile::Template));
                g.tplDirty = true;
            }
            break;
        case IDC_TPL_FOLDER:
        case IDM_CONFIG_DIR: OpenInExplorer(ConfigDir()); break;
        case IDC_TPL_HELP:
        case IDM_VARS: MessageBoxW(g.hwnd, kVarsHelp, L"Variabili dei template", MB_ICONINFORMATION); break;
        case IDC_TRK_SAVE: SaveTrackers(false); break;
        case IDC_TRK_LISTS:
            if (!g.running && ShowTrackerLists(g.hwnd, g.settings)) {
                g.settings.Save();
                SaveTrackers(true);
                if (!TrackerListSources().empty() &&
                    MessageBoxW(g.hwnd, L"Scaricare adesso i tracker dalle liste?", L"Tracker", MB_ICONQUESTION | MB_YESNO) == IDYES)
                    RunUpdateTrackerLists();
            }
            break;
        case IDC_TRK_UPDATE: if (!g.running) RunUpdateTrackerLists(); break;
        case IDC_TRK_RESET: if (!g.running) ResetTrackers(); break;
        case IDC_SHOT_SAVE: SaveShots(false); break;
        case IDC_SHOT_RESET: SetTextU8(IDC_SHOT_TEXT, DefaultContent(DefaultFile::Screenshots)); break;
        case IDC_RUN: if (!g.running) StartRelease(); break;
        case IDC_CANCEL:
            if (g.running) {
                g.cancel = true;
                SetText(IDC_STATUS, L"Annullamento in corso…");
            }
            break;
        case IDM_SETTINGS: {
            if (g.running) break;
            int action = 0;
            if (ShowSettings(g.hwnd, g.settings, action)) {
                g.settings.Save();
                Log("Impostazioni salvate.");
                if (action == 1) RunDownloadFfmpeg();
                else if (action == 2) RunInstallMediaInfo();
            }
            break;
        }
        case IDM_DL_FFMPEG: RunDownloadFfmpeg(); break;
        case IDM_INSTALL_MI: RunInstallMediaInfo(); break;
        case IDM_CHECK_TOOLS: CheckTools(false); SelectTab(TabLog); break;
        case IDM_MAKERELEASE: OpenUrl(L"https://github.com/c137ricksanchez/makerelease"); break;
        case IDM_ABOUT: {
            std::wstring about = std::wstring(L"ReleaseForge " RF_VERSION_W L"\nApp Win32 nativa per preparare release: metadati TMDB, report MediaInfo,\n"
                        L"screenshot, torrent, grafico bitrate e post BBCode da template.\n\n"
                        L"Template standard, titolo della release e flusso basati su makerelease\n"
                        L"(c) Rick Sanchez & Norman, licenza MIT.\n\nImpostazioni: ") + SettingsPath() + L"\nConfigurazione: " + ConfigDir();
            MessageBoxW(g.hwnd, about.c_str(), L"Informazioni", MB_ICONINFORMATION);
            break;
        }
        case IDM_EXIT: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            g.dpi = GetDpiForWindow(hwnd);
            SetMenu(hwnd, CreateMainMenu());
            CreateControls();
            ApplyFonts();
            DragAcceptFiles(hwnd, TRUE);
            return 0;
        }
        case WM_SIZE: Layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {S(980), S(620)};
            return 0;
        }
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            ApplyFonts();
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_COMMAND: OnCommand(LOWORD(wp), HIWORD(wp)); return 0;
        case WM_NOTIFY: {
            auto* n = reinterpret_cast<NMHDR*>(lp);
            if (n->idFrom == IDC_TAB && n->code == TCN_SELCHANGE) SelectTab(TabCtrl_GetCurSel(C(IDC_TAB)));
            return 0;
        }
        case WM_TIMER: if (wp == TIMER_UI) FlushUi(); return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            // Controlli dentro le schede: sfondo del corpo della scheda (bianco) invece del grigio della finestra.
            int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (id == IDC_TRK_INFO || id == IDC_SHOT_INFO || id == IDC_CLEAR_LOG || id == IDC_POST_COPY || id == IDC_POST_OPEN ||
                (id >= IDC_TPL_SAVE && id <= IDC_TPL_HELP) || id == IDC_TRK_SAVE || id == IDC_TRK_RESET || id == IDC_TRK_LISTS || id == IDC_TRK_UPDATE || id == IDC_SHOT_SAVE || id == IDC_SHOT_RESET) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            wchar_t path[MAX_PATH * 4];
            if (DragQueryFileW(drop, 0, path, UINT(std::size(path))) && !g.running) SetText(IDC_SOURCE, path);
            DragFinish(drop);
            return 0;
        }
        case WM_APP_CHOOSE: {
            auto* req = reinterpret_cast<ChooseRequest*>(lp);
            FlushUi();
            req->result = ShowTmdbChoice(hwnd, *req->hits, req->searched);
            return 0;
        }
        case WM_APP_DONE: {
            auto* cb = reinterpret_cast<std::function<void(HWND)>*>(lp);
            if (g.worker.joinable()) g.worker.join();
            g.running = false;
            FlushUi();
            if (!g.closePending) (*cb)(hwnd);
            delete cb;
            UpdateRunState();
            {
                std::lock_guard<std::mutex> lock(g.mu);
                g.progressDirty = true;
            }
            FlushUi();
            if (g.closePending) DestroyWindow(hwnd);
            return 0;
        }
        case WM_CLOSE:
            if (g.running) {
                if (MessageBoxW(hwnd, L"Un'operazione è in corso. Annullarla e chiudere?", L"ReleaseForge", MB_ICONQUESTION | MB_YESNO) != IDYES) return 0;
                g.closePending = true;
                g.cancel = true;
                return 0;
            }
            if (!ConfirmTemplateChanges()) return 0;
            SaveTrackers(true);
            SaveShots(true);
            SaveUiSettings();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_UI);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);  // anche dal manifest
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    EnsureConfig();
    g.settings = Settings::Load();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ReleaseForgeMain";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES, wc.lpszClassName, L"ReleaseForge " RF_VERSION_W,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Posizione salvata (solo se visibile su un monitor), altrimenti dimensione proporzionale ai DPI.
    RECT saved{g.settings.winX, g.settings.winY, g.settings.winX + g.settings.winW, g.settings.winY + g.settings.winH};
    if (g.settings.winW > 400 && g.settings.winH > 300 && MonitorFromRect(&saved, MONITOR_DEFAULTTONULL)) {
        WINDOWPLACEMENT wp{};
    wp.length = sizeof wp;
        wp.rcNormalPosition = saved;
        wp.showCmd = g.settings.winMax ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp);
    } else {
        SetWindowPos(hwnd, nullptr, 0, 0, S(1120), S(820), SWP_NOMOVE | SWP_NOZORDER);
        ShowWindow(hwnd, show);
    }
    // Stato iniziale dei controlli
    SendMessageW(C(IDC_TYPE), CB_SETCURSEL, g.settings.releaseType, 0);
    SetText(IDC_CREW, W(g.settings.crew));
    SetText(IDC_OUTPUT, EffectiveOutputDir(g.settings));
    SetChecked(IDC_CHK_SHOTS, g.settings.screenshots);
    SetChecked(IDC_CHK_TORRENT, g.settings.torrent);
    SetChecked(IDC_CHK_PRIVATE, g.settings.privateTorrent);
    SetChecked(IDC_CHK_UPLOAD, g.settings.upload);
    SetChecked(IDC_CHK_BITRATE, g.settings.bitrateGraph);
    SetChecked(IDC_CHK_RESUME, g.settings.resume);
    EnableWindow(C(IDC_COPY_TITLE), FALSE);
    LoadConfigTexts();
    SaveTrackers(true);  // aggiorna il riepilogo (manuali + liste remote)
    LoadTemplateList();
    ShowPosts();
    SelectTab(TabLog);
    UpdateRunState();
    Layout();
    SetTimer(hwnd, TIMER_UI, 100, nullptr);
    Log("ReleaseForge " RF_VERSION_STR " - configurazione: " + U8(ConfigDir()));
    Log("Template attivi: " + std::to_string(ListTemplates().size()) + " (standard makerelease in template.jinja)");

    // Argomento da riga di comando / "Apri con": sorgente precompilata.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine && *cmdLine ? GetCommandLineW() : L"x", &argc);
    if (argv && argc > 1) SetText(IDC_SOURCE, argv[1]);
    if (argv) LocalFree(argv);

    PostMessageW(hwnd, WM_APP + 10, 0, 0);
    ACCEL accels[] = {{FCONTROL | FVIRTKEY, 'O', IDM_OPEN_FILE}, {FCONTROL | FSHIFT | FVIRTKEY, 'O', IDM_OPEN_DIR},
                      {FCONTROL | FVIRTKEY, VK_OEM_COMMA, IDM_SETTINGS}, {FVIRTKEY, VK_F5, IDC_RUN}};
    HACCEL acc = CreateAcceleratorTableW(accels, int(std::size(accels)));
    MSG msg;
    bool toolsChecked = false;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == hwnd && msg.message == WM_APP + 10 && !toolsChecked) {
            toolsChecked = true;
            CheckTools(true);
            continue;
        }
        if (TranslateAcceleratorW(hwnd, acc, &msg)) continue;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyAcceleratorTable(acc);
    if (g.worker.joinable()) g.worker.join();
    CoUninitialize();
    return 0;
}
