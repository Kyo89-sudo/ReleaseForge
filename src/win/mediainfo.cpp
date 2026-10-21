#include "mediainfo.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <mutex>

#include "../core/json.h"
#include "../core/strutil.h"
#include "process.h"
#include "winutil.h"

namespace rf {

namespace {

// API di MediaInfoLib (MediaInfoDLL.h, varianti Unicode esportate come MediaInfo_*).
typedef void*(__stdcall* MI_New)();
typedef void(__stdcall* MI_Delete)(void*);
typedef size_t(__stdcall* MI_Open)(void*, const wchar_t*);
typedef void(__stdcall* MI_Close)(void*);
typedef const wchar_t*(__stdcall* MI_Inform)(void*, size_t);
typedef const wchar_t*(__stdcall* MI_Get)(void*, int, size_t, const wchar_t*, int, int);
typedef const wchar_t*(__stdcall* MI_Option)(void*, const wchar_t*, const wchar_t*);
typedef size_t(__stdcall* MI_CountGet)(void*, int, size_t);

enum { kGeneral = 0, kVideo = 1, kAudio = 2, kText = 3 };
enum { kInfoName = 0, kInfoText = 1 };

struct MiDll {
    HMODULE module = nullptr;
    std::wstring path;
    MI_New New = nullptr;
    MI_Delete Delete = nullptr;
    MI_Open Open = nullptr;
    MI_Close Close = nullptr;
    MI_Inform Inform = nullptr;
    MI_Get Get = nullptr;
    MI_Option Option = nullptr;
    MI_CountGet CountGet = nullptr;
    std::string version;
};

std::mutex g_dllMutex;
MiDll g_dll;

std::wstring RegDefault(HKEY root, const wchar_t* key, REGSAM view) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &h) != ERROR_SUCCESS) return L"";
    wchar_t buf[2048];
    DWORD size = sizeof buf, type = 0;
    std::wstring out;
    if (RegQueryValueExW(h, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
        out.assign(buf, wcsnlen(buf, size / sizeof(wchar_t)));
    RegCloseKey(h);
    if (!out.empty() && out.front() == L'"') out = out.substr(1, out.find(L'"', 1) - 1);
    return out;
}

// Prova a caricare una MediaInfo.dll; restituisce messaggio di errore o "".
std::string LoadDll(const std::wstring& path) {
    if (g_dll.module) {
        if (_wcsicmp(g_dll.path.c_str(), path.c_str()) == 0) return "";
        FreeLibrary(g_dll.module);
        g_dll = MiDll();
    }
    if (!FileExists(path)) return "file non trovato";
    // LOAD_WITH_ALTERED_SEARCH_PATH: carica anche le dipendenze accanto alla DLL (es. LIBCURL.DLL della GUI).
    HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) {
        DWORD e = GetLastError();
        if (e == ERROR_BAD_EXE_FORMAT)
#if defined(_WIN64)
            return "MediaInfo.dll a 32 bit: serve la versione a 64 bit (installa MediaInfo x64)";
#else
            return "MediaInfo.dll a 64 bit: questa build di ReleaseForge è a 32 bit";
#endif
        return U8(LastErrorMessage(e));
    }
    MiDll d;
    d.module = m;
    d.path = path;
    d.New = reinterpret_cast<MI_New>(GetProcAddress(m, "MediaInfo_New"));
    d.Delete = reinterpret_cast<MI_Delete>(GetProcAddress(m, "MediaInfo_Delete"));
    d.Open = reinterpret_cast<MI_Open>(GetProcAddress(m, "MediaInfo_Open"));
    d.Close = reinterpret_cast<MI_Close>(GetProcAddress(m, "MediaInfo_Close"));
    d.Inform = reinterpret_cast<MI_Inform>(GetProcAddress(m, "MediaInfo_Inform"));
    d.Get = reinterpret_cast<MI_Get>(GetProcAddress(m, "MediaInfo_Get"));
    d.Option = reinterpret_cast<MI_Option>(GetProcAddress(m, "MediaInfo_Option"));
    d.CountGet = reinterpret_cast<MI_CountGet>(GetProcAddress(m, "MediaInfo_Count_Get"));
    if (!d.New || !d.Delete || !d.Open || !d.Close || !d.Inform || !d.Get || !d.Option || !d.CountGet) {
        FreeLibrary(m);
        return "la DLL non espone l'API MediaInfo attesa";
    }
    void* h = d.New();
    if (h) {
        const wchar_t* v = d.Option(h, L"Info_Version", L"");
        d.version = v ? U8(v) : "";
        d.Delete(h);
    }
    g_dll = d;
    return "";
}

std::vector<std::wstring> DllCandidates(const MediaInfoSetup& setup, std::wstring* cliOut) {
    std::vector<std::wstring> c;
    std::wstring p = Trim(U8(setup.mediaInfoPath)).empty() ? L"" : setup.mediaInfoPath;
    if (!p.empty() && p.front() == L'"') p = p.substr(1, p.size() - 2);
    if (!p.empty()) {
        if (DirExists(p)) c.push_back(PathJoin(p, L"MediaInfo.dll"));
        else if (PathExtension(p) == L".dll") c.push_back(p);
        else if (PathExtension(p) == L".exe") {
            std::wstring sibling = PathJoin(PathParent(p), L"MediaInfo.dll");
            if (FileExists(sibling)) c.push_back(sibling);  // GUI completa: usa la sua DLL
            else if (cliOut) *cliOut = p;                    // MediaInfo CLI
        }
    }
    std::wstring installed = FindInstalledMediaInfoDir();
    if (!installed.empty()) c.push_back(PathJoin(installed, L"MediaInfo.dll"));
    c.push_back(PathJoin(ExeDir(), L"MediaInfo.dll"));
    c.push_back(PathJoin(ExeDir(), L"tools\\mediainfo\\MediaInfo.dll"));
    c.push_back(PathJoin(LocalAppDataDir(), L"tools\\mediainfo\\MediaInfo.dll"));
    return c;
}

std::wstring FindCli(const MediaInfoSetup& setup, const std::wstring& explicitCli) {
    if (!explicitCli.empty() && FileExists(explicitCli)) return explicitCli;
    for (auto& dir : {PathJoin(ExeDir(), L"tools\\mediainfo"), PathJoin(LocalAppDataDir(), L"tools\\mediainfo")}) {
        std::wstring exe = PathJoin(dir, L"MediaInfo.exe");
        if (FileExists(exe)) return exe;
    }
    std::wstring inPath = FindInPath(L"MediaInfo.exe");
    // Un MediaInfo.exe nel PATH con la DLL accanto è la GUI: va usata la DLL, non l'eseguibile.
    if (!inPath.empty() && !FileExists(PathJoin(PathParent(inPath), L"MediaInfo.dll"))) return inPath;
    (void)setup;
    return L"";
}

std::wstring ResolveFfmpeg(const std::wstring& p) {
    if (!p.empty() && FileExists(p)) return p;
    return L"";
}

int64_t ParseMs(const std::wstring& v) {
    if (v.empty()) return 0;
    return int64_t(_wtof(v.c_str()));
}

std::string ReplacePathWithName(std::string text, const std::wstring& file) {
    text = NormalizeNewlines(text);
    std::string full = U8(file), name = U8(PathFileName(file));
    text = ReplaceAll(text, full, name);
    std::wstring longp = LongPath(file);
    if (longp != file) text = ReplaceAll(text, U8(longp), name);
    // Rimuove spazi/righe vuote finali come makerelease (rstrip("\r\n")).
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

MediaReport AnalyzeWithDll(const std::wstring& file) {
    MediaReport r;
    std::lock_guard<std::mutex> lock(g_dllMutex);
    void* h = g_dll.New();
    if (!h) { r.error = "MediaInfo_New fallita"; return r; }
    g_dll.Option(h, L"Complete", L"");
    g_dll.Option(h, L"Language", L"");
    if (g_dll.Open(h, file.c_str()) == 0) {
        g_dll.Delete(h);
        r.error = "MediaInfo non riesce ad aprire il file";
        return r;
    }
    const wchar_t* inform = g_dll.Inform(h, 0);
    r.text = ReplacePathWithName(inform ? U8(inform) : "", file);
    auto get = [&](int kind, size_t n, const wchar_t* param) -> std::wstring {
        const wchar_t* v = g_dll.Get(h, kind, n, param, kInfoText, kInfoName);
        return v ? v : L"";
    };
    MediaTracks& t = r.tracks;
    t.valid = true;
    t.durationMs = ParseMs(get(kGeneral, 0, L"Duration"));
    size_t nv = g_dll.CountGet(h, kVideo, size_t(-1));
    for (size_t i = 0; i < nv; ++i) {
        VideoTrack v;
        v.width = _wtoi(get(kVideo, i, L"Width").c_str());
        v.height = _wtoi(get(kVideo, i, L"Height").c_str());
        v.format = U8(get(kVideo, i, L"Format"));
        v.scanType = U8(get(kVideo, i, L"ScanType"));
        std::wstring par = get(kVideo, i, L"PixelAspectRatio");
        v.pixelAspect = par.empty() ? 1.0 : _wtof(par.c_str());
        v.hdrFormat = U8(get(kVideo, i, L"HDR_Format")) + " / " + U8(get(kVideo, i, L"HDR_Format_Compatibility"));
        v.transfer = U8(get(kVideo, i, L"transfer_characteristics"));
        t.video.push_back(v);
    }
    size_t na = g_dll.CountGet(h, kAudio, size_t(-1));
    for (size_t i = 0; i < na; ++i) {
        AudioTrack a;
        std::string l3 = U8(get(kAudio, i, L"Language/String3"));
        a.language = !l3.empty() ? ToLower(l3) : NormalizeLanguage3(U8(get(kAudio, i, L"Language")));
        a.format = U8(get(kAudio, i, L"Format"));
        a.formatProfile = U8(get(kAudio, i, L"Format_Profile"));
        a.channelLayout = U8(get(kAudio, i, L"ChannelLayout"));
        a.channels = _wtoi(get(kAudio, i, L"Channel(s)").c_str());
        a.commercial = U8(get(kAudio, i, L"Format_Commercial_IfAny"));
        a.features = U8(get(kAudio, i, L"Format_AdditionalFeatures"));
        t.audio.push_back(a);
    }
    size_t ns = g_dll.CountGet(h, kText, size_t(-1));
    for (size_t i = 0; i < ns; ++i) {
        TextTrack s;
        std::string l3 = U8(get(kText, i, L"Language/String3"));
        s.language = !l3.empty() ? ToLower(l3) : NormalizeLanguage3(U8(get(kText, i, L"Language")));
        t.text.push_back(s);
    }
    g_dll.Close(h);
    g_dll.Delete(h);
    r.backend = MediaInfoBackend::Dll;
    return r;
}

MediaReport AnalyzeWithCli(const std::wstring& cli, const std::wstring& file, const std::atomic<bool>* cancel) {
    MediaReport r;
    ProcessOptions o;
    o.cancel = cancel;
    o.timeoutMs = 180000;
    ProcessResult text = RunProcess(cli, {file}, o);
    if (!text.started) { r.error = "MediaInfo CLI non avviabile: " + text.startError; return r; }
    std::string report = AnsiToUtf8(text.out);
    if (text.exitCode != 0 || Trim(report).empty()) { r.error = "MediaInfo CLI non ha prodotto un report"; return r; }
    r.text = ReplacePathWithName(report, file);
    ProcessResult js = RunProcess(cli, {L"--Output=JSON", file}, o);
    try {
        Json j = Json::Parse(AnsiToUtf8(js.out));
        MediaTracks& t = r.tracks;
        t.valid = true;
        for (auto& tr : j["media"]["track"].Items()) {
            std::string type = tr["@type"].Str();
            if (type == "General") t.durationMs = int64_t(std::atof(tr["Duration"].Str().c_str()) * 1000.0);
            else if (type == "Video") {
                VideoTrack v;
                v.width = std::atoi(tr["Width"].Str().c_str());
                v.height = std::atoi(tr["Height"].Str().c_str());
                v.format = tr["Format"].Str();
                v.scanType = tr["ScanType"].Str();
                std::string par = tr["PixelAspectRatio"].Str();
                v.pixelAspect = par.empty() ? 1.0 : std::atof(par.c_str());
                v.hdrFormat = tr["HDR_Format"].Str() + " / " + tr["HDR_Format_Compatibility"].Str();
                v.transfer = tr["transfer_characteristics"].Str();
                t.video.push_back(v);
            } else if (type == "Audio") {
                AudioTrack a;
                a.language = NormalizeLanguage3(tr["Language"].Str());
                a.format = tr["Format"].Str();
                a.formatProfile = tr["Format_Profile"].Str();
                a.channelLayout = tr["ChannelLayout"].Str();
                a.channels = std::atoi(tr["Channels"].Str().c_str());
                a.commercial = tr["Format_Commercial_IfAny"].Str();
                a.features = tr["Format_AdditionalFeatures"].Str();
                t.audio.push_back(a);
            } else if (type == "Text") {
                TextTrack s;
                s.language = NormalizeLanguage3(tr["Language"].Str());
                t.text.push_back(s);
            }
        }
    } catch (...) {
        // Report testuale comunque valido; il titolo userà i dati del nome file.
    }
    r.backend = MediaInfoBackend::Cli;
    return r;
}

MediaReport AnalyzeWithFfmpeg(const std::wstring& ffmpeg, const std::wstring& file, const std::atomic<bool>* cancel) {
    MediaReport r;
    ProcessOptions o;
    o.cancel = cancel;
    o.timeoutMs = 120000;
    ProcessResult p = RunProcess(ffmpeg, {L"-hide_banner", L"-i", file}, o);
    if (!p.started) { r.error = "FFmpeg non avviabile: " + p.startError; return r; }
    std::string all = AnsiToUtf8(p.err);
    r.tracks = ParseFfmpegProbe(all);
    std::vector<std::string> keep;
    bool inMeta = false;
    for (auto& raw : SplitLines(all)) {
        std::string t = Trim(raw);
        bool important = StartsWith(t, "Input #") || StartsWith(t, "Duration:") || StartsWith(t, "Stream #") || StartsWith(t, "Chapter");
        if (StartsWith(t, "Metadata:")) { inMeta = true; keep.push_back(raw); continue; }
        if (important) inMeta = false;
        if (important || (inMeta && !raw.empty() && (raw[0] == ' ' || raw[0] == '\t'))) keep.push_back(raw);
    }
    if (keep.empty()) { r.error = "FFmpeg non ha restituito dettagli"; return r; }
    r.text = ReplacePathWithName("Report tecnico generato con FFmpeg (MediaInfo non disponibile)\n" + Join(keep, "\n"), file);
    r.backend = MediaInfoBackend::Ffmpeg;
    return r;
}

}  // namespace

std::wstring FindInstalledMediaInfoDir() {
    for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            std::wstring exe = RegDefault(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\MediaInfo.exe", view);
            if (!exe.empty() && FileExists(PathJoin(PathParent(exe), L"MediaInfo.dll"))) return PathParent(exe);
        }
    }
    for (const wchar_t* env : {L"ProgramW6432", L"ProgramFiles", L"LOCALAPPDATA"}) {
        std::wstring base = EnvVar(env);
        if (base.empty()) continue;
        std::wstring dir = PathJoin(base, wcscmp(env, L"LOCALAPPDATA") == 0 ? L"Programs\\MediaInfo" : L"MediaInfo");
        if (FileExists(PathJoin(dir, L"MediaInfo.dll"))) return dir;
    }
    return L"";
}

MediaInfoStatus DetectMediaInfo(const MediaInfoSetup& setup) {
    MediaInfoStatus s;
    std::wstring cli;
    std::string lastError;
    {
        std::lock_guard<std::mutex> lock(g_dllMutex);
        for (auto& c : DllCandidates(setup, &cli)) {
            if (!FileExists(c)) continue;
            std::string err = LoadDll(c);
            if (err.empty()) {
                s.backend = MediaInfoBackend::Dll;
                s.location = c;
                s.version = g_dll.version;
                s.note = "MediaInfo completo tramite MediaInfo.dll (report identico a MediaInfo GUI/CLI).";
                return s;
            }
            lastError = U8(c) + ": " + err;
        }
    }
    std::wstring exe = FindCli(setup, cli);
    if (!exe.empty()) {
        ProcessOptions o;
        o.timeoutMs = 10000;
        ProcessResult v = RunProcess(exe, {L"--Version"}, o);
        std::string out = Trim(AnsiToUtf8(v.out));
        if (v.started && out.find("MediaInfo") != std::string::npos) {
            s.backend = MediaInfoBackend::Cli;
            s.location = exe;
            s.version = ReplaceAll(out, "\n", " ");
            s.note = "MediaInfo CLI (ripiego). Per usare la versione completa installa MediaInfo GUI.";
            return s;
        }
    }
    if (!ResolveFfmpeg(setup.ffmpegPath).empty()) {
        s.backend = MediaInfoBackend::Ffmpeg;
        s.location = setup.ffmpegPath;
        s.note = "MediaInfo non trovato: il report sarà generato con FFmpeg (ridotto).";
    } else {
        s.note = "MediaInfo e FFmpeg non trovati.";
    }
    if (!lastError.empty()) s.note += " Ultimo errore DLL: " + lastError;
    return s;
}

MediaReport AnalyzeMedia(const std::wstring& file, const MediaInfoSetup& setup, const std::atomic<bool>* cancel) {
    MediaInfoStatus st = DetectMediaInfo(setup);
    MediaReport r;
    std::string notes;
    if (st.backend == MediaInfoBackend::Dll) {
        r = AnalyzeWithDll(file);
        if (r.backend == MediaInfoBackend::Dll) return r;
        notes = "MediaInfo.dll: " + r.error + ". ";
        std::wstring cli;
        DllCandidates(setup, &cli);
        std::wstring exe = FindCli(setup, cli);
        if (!exe.empty()) st.backend = MediaInfoBackend::Cli, st.location = exe;
        else st.backend = MediaInfoBackend::Ffmpeg;
    }
    if (st.backend == MediaInfoBackend::Cli) {
        r = AnalyzeWithCli(st.location, file, cancel);
        if (r.backend == MediaInfoBackend::Cli) return r;
        notes += r.error + ". ";
    }
    std::wstring ff = ResolveFfmpeg(setup.ffmpegPath);
    if (!ff.empty()) {
        r = AnalyzeWithFfmpeg(ff, file, cancel);
        if (r.backend == MediaInfoBackend::Ffmpeg) { r.error = notes; return r; }
        notes += r.error;
    }
    r = MediaReport();
    r.error = notes.empty() ? "nessuno strumento di analisi disponibile (MediaInfo o FFmpeg)" : notes;
    return r;
}

}  // namespace rf
