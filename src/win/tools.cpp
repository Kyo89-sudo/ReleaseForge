#include "tools.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <shellapi.h>
#include <shlobj.h>

#include <stdexcept>

#include "../core/strutil.h"
#include "http.h"
#include "mediainfo.h"
#include "process.h"
#include "winutil.h"

namespace rf {

namespace {

// Versione fissata con hash pubblicato dal fornitore (verificare a ogni aggiornamento).
const char* kFfmpegPinnedUrl = "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.zip";
const char* kFfmpegPinnedSha = "db580001caa24ac104c8cb856cd113a87b0a443f7bdf47d8c12b1d740584a2ec";
// Ripiego: ultima release essentials con il suo .sha256 pubblicato da gyan.dev.
const char* kFfmpegLatestUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip";
const char* kFfmpegLatestSha = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip.sha256";

std::wstring FindIn(const std::wstring& dir, const wchar_t* exe) {
    if (!DirExists(dir)) return L"";
    for (auto& e : ListFilesRecursive(dir)) if (_wcsicmp(PathFileName(e.fullPath).c_str(), exe) == 0) return e.fullPath;
    return L"";
}

std::wstring ResolveConfigured(const std::wstring& configured, const wchar_t* exe) {
    std::wstring p = configured;
    if (!p.empty() && p.front() == L'"') p = p.substr(1, p.size() - 2);
    if (p.empty()) return L"";
    if (FileExists(p)) return p;
    if (DirExists(p)) {
        for (auto& c : {PathJoin(p, exe), PathJoin(PathJoin(p, L"bin"), exe)}) if (FileExists(c)) return c;
    }
    return L"";
}

std::wstring WingetPackagesSearch(const wchar_t* prefix, const wchar_t* exe) {
    std::wstring base = PathJoin(EnvVar(L"LOCALAPPDATA"), L"Microsoft\\WinGet\\Packages");
    for (auto& d : ListSubdirs(base)) {
        std::wstring name = PathFileName(d);
        if (_wcsnicmp(name.c_str(), prefix, wcslen(prefix)) == 0) {
            std::wstring f = FindIn(d, exe);
            if (!f.empty()) return f;
        }
    }
    return L"";
}

}  // namespace

std::wstring FindFfmpeg(const std::wstring& configured) {
    std::wstring p = ResolveConfigured(configured, L"ffmpeg.exe");
    if (!p.empty()) return p;
    for (auto& dir : {PathJoin(LocalAppDataDir(), L"tools\\ffmpeg"), PathJoin(ExeDir(), L"tools\\ffmpeg"), PathJoin(ExeDir(), L"tools")}) {
        p = FindIn(dir, L"ffmpeg.exe");
        if (!p.empty()) return p;
    }
    p = FindInPath(L"ffmpeg.exe");
    if (!p.empty()) return p;
    std::wstring links = PathJoin(EnvVar(L"LOCALAPPDATA"), L"Microsoft\\WinGet\\Links\\ffmpeg.exe");
    if (FileExists(links)) return links;
    return WingetPackagesSearch(L"Gyan.FFmpeg", L"ffmpeg.exe");
}

std::wstring FindFfprobe(const std::wstring& ffmpegExe) {
    if (!ffmpegExe.empty()) {
        std::wstring sibling = PathJoin(PathParent(ffmpegExe), L"ffprobe.exe");
        if (FileExists(sibling)) return sibling;
    }
    std::wstring p = FindInPath(L"ffprobe.exe");
    if (!p.empty()) return p;
    return WingetPackagesSearch(L"Gyan.FFmpeg", L"ffprobe.exe");
}

std::wstring FindAvinaptic(const std::wstring& configured) {
    std::wstring p = ResolveConfigured(configured, L"avinaptic2-cli.exe");
    if (!p.empty()) return p;
    for (auto& dir : {PathJoin(LocalAppDataDir(), L"tools\\avinaptic"), PathJoin(ExeDir(), L"tools")}) {
        p = FindIn(dir, L"avinaptic2-cli.exe");
        if (!p.empty()) return p;
    }
    return FindInPath(L"avinaptic2-cli.exe");
}

std::string ToolVersion(const std::wstring& exe) {
    if (exe.empty()) return "";
    ProcessOptions o;
    o.timeoutMs = 10000;
    try {
        ProcessResult r = RunProcess(exe, {L"-version"}, o);
        auto lines = SplitLines(AnsiToUtf8(r.out + r.err));
        return lines.empty() ? "" : Trim(lines[0]);
    } catch (...) {
        return "";
    }
}

std::wstring DownloadFfmpeg(const std::atomic<bool>* cancel, const ToolLog& log, const ToolProgress& progress) {
    std::wstring base = PathJoin(LocalAppDataDir(), L"tools");
    CreateDirs(base);
    std::wstring zip = PathJoin(base, L"ffmpeg-download.zip");

    auto download = [&](const std::string& url) -> int {
        log("Download " + url);
        HttpRequest req;
        req.url = url;
        req.saveTo = zip;
        req.cancel = cancel;
        req.timeoutSeconds = 300;
        req.progress = progress;
        return HttpSend(req).status;
    };

    std::string expected = kFfmpegPinnedSha;
    int status = 0;
    try {
        status = download(kFfmpegPinnedUrl);
    } catch (Cancelled&) {
        throw;
    } catch (std::exception& e) {
        log(std::string("Versione fissata non scaricabile: ") + e.what());
    }
    if (status != 200) {
        if (status) log("Versione fissata non disponibile (HTTP " + std::to_string(status) + "): uso l'ultima release essentials.");
        HttpRequest shaReq;
        shaReq.url = kFfmpegLatestSha;
        shaReq.cancel = cancel;
        HttpResponse sha = HttpSend(shaReq);
        expected = ToLower(Trim(sha.body.substr(0, sha.body.find_first_of(" \r\n"))));
        if (sha.status != 200 || expected.size() != 64) throw std::runtime_error("impossibile ottenere lo SHA-256 pubblicato da gyan.dev");
        status = download(kFfmpegLatestUrl);
        if (status != 200) throw std::runtime_error("download FFmpeg fallito (HTTP " + std::to_string(status) + ")");
    }
    log("Verifica SHA-256...");
    std::string got = Sha256Hex(zip);
    if (got != expected) {
        DeleteFileW(zip.c_str());
        throw std::runtime_error("SHA-256 non corrispondente: archivio scartato (atteso " + expected + ", ottenuto " + got + ")");
    }
    log("SHA-256 verificato.");

    std::wstring dest = PathJoin(base, L"ffmpeg");
    std::wstring tmp = PathJoin(base, L"ffmpeg-new");
    CreateDirs(tmp);
    // tar.exe (bsdtar) è incluso in Windows 10 1803+ e Windows 11 ed estrae gli zip.
    std::wstring tar = PathJoin(KnownFolder(FOLDERID_System), L"tar.exe");
    ProcessOptions o;
    o.cancel = cancel;
    o.timeoutMs = 600000;
    ProcessResult r = RunProcess(tar, {L"-xf", zip, L"-C", tmp}, o);
    if (!r.started || r.exitCode != 0) throw std::runtime_error("estrazione fallita: " + (r.started ? AnsiToUtf8(r.err) : r.startError));
    std::wstring exe = FindIn(tmp, L"ffmpeg.exe");
    if (exe.empty()) throw std::runtime_error("ffmpeg.exe non presente nell'archivio");
    // Sostituisce l'eventuale versione precedente.
    if (DirExists(dest)) {
        std::wstring old = PathJoin(base, L"ffmpeg-old-" + std::to_wstring(UnixNow()));
        MoveFileExW(dest.c_str(), old.c_str(), 0);
        SHFILEOPSTRUCTW op{};
        std::wstring from = old + L'\0';
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_NO_UI;
        SHFileOperationW(&op);
    }
    if (!MoveFileExW(tmp.c_str(), dest.c_str(), 0)) throw std::runtime_error("impossibile spostare FFmpeg in " + U8(dest));
    DeleteFileW(zip.c_str());
    exe = FindIn(dest, L"ffmpeg.exe");
    log("FFmpeg installato: " + U8(exe));
    return exe;
}

static std::wstring WingetExe() {
    std::wstring p = FindInPath(L"winget.exe");
    if (!p.empty()) return p;
    std::wstring alias = PathJoin(EnvVar(L"LOCALAPPDATA"), L"Microsoft\\WindowsApps\\winget.exe");
    // L'alias di esecuzione è un reparse point: GetFileAttributes funziona comunque.
    if (GetFileAttributesW(alias.c_str()) != INVALID_FILE_ATTRIBUTES) return alias;
    return L"";
}

bool WingetAvailable() { return !WingetExe().empty(); }

bool InstallMediaInfoWithWinget(const ToolLog& log, std::string& error) {
    std::wstring winget = WingetExe();
    if (winget.empty()) {
        error = "winget non disponibile: scarica MediaInfo dal sito ufficiale.";
        return false;
    }
    log("Avvio: winget install --id MediaArea.MediaInfo.GUI (potrebbe comparire la richiesta di autorizzazione di Windows)");
    ProcessOptions o;
    o.timeoutMs = 15 * 60 * 1000;
    ProcessResult r = RunProcess(winget, {L"install", L"--id", L"MediaArea.MediaInfo.GUI", L"-e", L"--silent",
                                          L"--accept-package-agreements", L"--accept-source-agreements"}, o);
    std::string out = AnsiToUtf8(r.out);
    for (auto& line : SplitLines(out)) {
        std::string t = Trim(line);
        // winget stampa barre di avanzamento con caratteri di controllo: tiene solo righe leggibili
        if (t.size() > 3 && t.find('\x1b') == std::string::npos && t.find("\xE2\x96") == std::string::npos) log("winget: " + t);
    }
    if (!FindInstalledMediaInfoDir().empty()) return true;
    error = r.started ? "winget ha terminato con codice " + std::to_string(r.exitCode) + " ma MediaInfo.dll non risulta installata."
                      : "winget non avviabile: " + r.startError;
    return false;
}

}  // namespace rf
