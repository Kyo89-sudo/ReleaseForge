// ReleaseForge - installer nativo Win32 (per utente, senza diritti di amministratore).
//
// Un solo sorgente per due eseguibili:
//  - setup (ReleaseForge-x.y.z-win64-setup.exe): contiene l'app come risorsa RCDATA;
//  - uninstall.exe: stesso codice senza payload, copiato nella cartella dell'app.
// La modalità si decide a runtime: payload presente = installazione, assente = disinstallazione.
//
// Riga di comando
//   setup:     /S (o /SILENT, /VERYSILENT) installazione senza finestra
//              /DIR="cartella" oppure /D=cartella (ultimo argomento, stile NSIS)
//              /DESKTOP crea l'icona sul desktop, /MEDIAINFO installa MediaInfo con winget
//              /LAUNCH avvia l'app al termine (anche in modalità silenziosa)
//   uninstall: /S senza conferme, /PURGE rimuove anche impostazioni e strumenti scaricati
// Codici di uscita: 0 riuscito, 1 errore, 2 annullato.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

#include "../src/resource.h"
#include "setup_res.h"

namespace {

// ---------------------------------------------------------------------------------------------
// Costanti
// ---------------------------------------------------------------------------------------------

const wchar_t kAppName[] = L"ReleaseForge";
const wchar_t kAppExe[] = L"ReleaseForge.exe";
const wchar_t kUninstExe[] = L"uninstall.exe";
const wchar_t kPublisher[] = RF_AUTHOR_W;
const wchar_t kUrl[] = RF_REPOSITORY_W;
// Stesso AppId del vecchio installer Inno Setup: la voce in "App installate" resta una sola.
const wchar_t kUninstKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{D435F294-B487-42C4-AD02-C1B837976034}";
const wchar_t kInnoKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{D435F294-B487-42C4-AD02-C1B837976034}_is1";
const wchar_t kLinkName[] = L"ReleaseForge.lnk";

struct PayloadFile {
    int id;
    const wchar_t* name;
};
const PayloadFile kFiles[] = {
    {IDR_PAY_APP, L"ReleaseForge.exe"},
    {IDR_PAY_UNINST, L"uninstall.exe"},
    {IDR_PAY_LICENSE, L"LICENSE"},
    {IDR_PAY_README, L"README.md"},
    {IDR_PAY_NOTICES, L"THIRD-PARTY-NOTICES.txt"},
    {IDR_PAY_CHANGELOG, L"CHANGELOG.txt"},
};
// Residui del vecchio installer Inno Setup.
const wchar_t* const kLegacyFiles[] = {L"unins000.exe", L"unins000.dat", L"unins000.msg"};

enum ExitCode { kOk = 0, kError = 1, kCancelled = 2 };

bool g_silent = false;
HINSTANCE g_inst = nullptr;

// ---------------------------------------------------------------------------------------------
// Utilità
// ---------------------------------------------------------------------------------------------

bool IEq(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring TrimSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

std::wstring Join(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

std::wstring DirName(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

std::wstring BaseName(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

std::wstring ModulePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, &buf[0], DWORD(buf.size()));
        if (n == 0) return std::wstring();
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out = p;
    CoTaskMemFree(p);
    return out;
}

std::wstring FullPath(const std::wstring& p) {
    DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (!n) return p;
    std::wstring buf(n, L'\0');
    n = GetFullPathNameW(p.c_str(), n, &buf[0], nullptr);
    buf.resize(n);
    return buf;
}

std::wstring ExpandEnv(const std::wstring& s) {
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (!n) return s;
    std::wstring buf(n, L'\0');
    n = ExpandEnvironmentStringsW(s.c_str(), &buf[0], n);
    if (!n) return s;
    buf.resize(n - 1);
    return buf;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirIsEmpty(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool empty = true;
    do {
        if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { empty = false; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

std::wstring ErrorText(DWORD code) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring s = msg ? msg : L"";
    LocalFree(msg);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.')) s.pop_back();
    if (s.empty()) s = L"errore " + std::to_wstring(code);
    return s;
}

std::wstring RegReadString(HKEY root, const wchar_t* key, const wchar_t* value, DWORD flags = 0) {
    DWORD size = 0;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | flags, nullptr, nullptr, &size) != ERROR_SUCCESS || size < 2)
        return std::wstring();
    std::wstring buf(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | flags, nullptr, &buf[0], &size) != ERROR_SUCCESS)
        return std::wstring();
    buf.resize(wcslen(buf.c_str()));
    return buf;
}

bool RegKeyExists(HKEY root, const wchar_t* key) {
    HKEY k;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

int Message(HWND owner, const std::wstring& text, UINT flags) {
    if (g_silent) return (flags & 0xF) == MB_YESNO ? IDYES : IDOK;
    return MessageBoxW(owner, text.c_str(), kAppName, flags);
}

// ---------------------------------------------------------------------------------------------
// Processi e collegamenti
// ---------------------------------------------------------------------------------------------

std::vector<DWORD> FindRunning(const std::wstring& exePath) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (!IEq(pe.szExeFile, kAppExe)) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[32768];
        DWORD n = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(h, 0, path, &n) && IEq(std::wstring(path, n), exePath))
            pids.push_back(pe.th32ProcessID);
        CloseHandle(h);
    }
    CloseHandle(snap);
    return pids;
}

BOOL CALLBACK PostCloseProc(HWND hwnd, LPARAM lp) {
    const auto* pids = reinterpret_cast<const std::vector<DWORD>*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    for (DWORD p : *pids) {
        if (p == pid && !GetWindow(hwnd, GW_OWNER)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
    }
    return TRUE;
}

// Chiede la chiusura (WM_CLOSE) delle istanze avviate da exePath e aspetta che terminino.
bool CloseRunning(const std::wstring& exePath, DWORD timeoutMs) {
    std::vector<DWORD> pids = FindRunning(exePath);
    if (pids.empty()) return true;
    std::vector<HANDLE> handles;
    for (DWORD pid : pids)
        if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) handles.push_back(h);
    EnumWindows(PostCloseProc, reinterpret_cast<LPARAM>(&pids));
    if (!handles.empty())
        WaitForMultipleObjects(DWORD(handles.size() > MAXIMUM_WAIT_OBJECTS ? MAXIMUM_WAIT_OBJECTS : handles.size()),
                               handles.data(), TRUE, timeoutMs);
    for (HANDLE h : handles) CloseHandle(h);
    return FindRunning(exePath).empty();
}

// true se ReleaseForge (avviato da exePath) non è in esecuzione o è stato chiuso.
bool EnsureAppClosed(HWND owner, const std::wstring& exePath) {
    if (FindRunning(exePath).empty()) return true;
    if (g_silent) return CloseRunning(exePath, 15000);
    if (MessageBoxW(owner, L"ReleaseForge è aperto e deve essere chiuso per continuare.\n\nPremi OK per chiuderlo.",
                    kAppName, MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return false;
    for (;;) {
        if (CloseRunning(exePath, 15000)) return true;
        if (MessageBoxW(owner, L"Non è stato possibile chiudere ReleaseForge (forse sta lavorando a una release).\n\n"
                               L"Chiudilo a mano, poi premi Riprova.",
                        kAppName, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY)
            return false;
    }
}

HRESULT CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& workDir) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link));
    if (FAILED(hr)) return hr;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(workDir.c_str());
    link->SetIconLocation(target.c_str(), 0);
    link->SetDescription(L"ReleaseForge - preparazione release");
    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr)) {
        hr = file->Save(lnk.c_str(), TRUE);
        file->Release();
    }
    link->Release();
    return hr;
}

std::wstring ShortcutTarget(const std::wstring& lnk) {
    std::wstring out;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                reinterpret_cast<void**>(&link))))
        return out;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        if (SUCCEEDED(file->Load(lnk.c_str(), STGM_READ))) {
            wchar_t path[MAX_PATH] = L"";
            if (SUCCEEDED(link->GetPath(path, MAX_PATH, nullptr, SLGP_RAWPATH))) out = ExpandEnv(path);
        }
        file->Release();
    }
    link->Release();
    return out;
}

std::wstring StartMenuLink() { return Join(KnownFolder(FOLDERID_Programs), kLinkName); }
std::wstring DesktopLink() { return Join(KnownFolder(FOLDERID_Desktop), kLinkName); }

// Elimina il collegamento solo se punta all'eseguibile indicato.
void DeleteShortcutTo(const std::wstring& lnk, const std::wstring& exe) {
    if (FileExists(lnk) && IEq(ShortcutTarget(lnk), exe)) DeleteFileW(lnk.c_str());
}

// ---------------------------------------------------------------------------------------------
// MediaInfo e winget (stessa logica dell'app e del vecchio installer)
// ---------------------------------------------------------------------------------------------

bool MediaInfoInstalled() {
    std::wstring pf = KnownFolder(FOLDERID_ProgramFiles);
    if (!pf.empty() && FileExists(Join(pf, L"MediaInfo\\MediaInfo.dll"))) return true;
    const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\MediaInfo.exe";
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        std::wstring exe = RegReadString(root, key, nullptr, root == HKEY_LOCAL_MACHINE ? RRF_SUBKEY_WOW6464KEY : 0);
        if (exe.size() > 1 && exe.front() == L'"') exe = exe.substr(1, exe.find(L'"', 1) - 1);
        if (!exe.empty() && FileExists(Join(DirName(exe), L"MediaInfo.dll"))) return true;
    }
    return false;
}

std::wstring WingetExe() {
    wchar_t buf[MAX_PATH];
    DWORD n = SearchPathW(nullptr, L"winget.exe", nullptr, MAX_PATH, buf, nullptr);
    if (n && n < MAX_PATH) return buf;
    wchar_t local[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
        std::wstring alias = Join(local, L"Microsoft\\WindowsApps\\winget.exe");
        if (GetFileAttributesW(alias.c_str()) != INVALID_FILE_ATTRIBUTES) return alias;
    }
    return std::wstring();
}

// Esegue un comando senza finestra e ne restituisce il codice di uscita (-1 se non avviato).
long RunHidden(const std::wstring& exe, const std::wstring& args, bool wait) {
    std::wstring cmd = Quote(exe) + L" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    DWORD code = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return long(code);
}

bool LaunchApp(const std::wstring& dir) {
    std::wstring exe = Join(dir, kAppExe);
    std::wstring cmd = Quote(exe);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Installazione
// ---------------------------------------------------------------------------------------------

bool GetPayload(int id, const void** data, DWORD* size) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return false;
    HGLOBAL h = LoadResource(nullptr, res);
    if (!h) return false;
    *data = LockResource(h);
    *size = SizeofResource(nullptr, res);
    return *data != nullptr;
}

bool HasPayload() {
    const void* d;
    DWORD s;
    return GetPayload(IDR_PAY_APP, &d, &s);
}

// Scrive in un file temporaneo e poi sostituisce: un file a metà non resta mai al suo posto.
bool WriteFileAtomic(const std::wstring& path, const void* data, DWORD size, std::wstring& err) {
    std::wstring tmp = path + L".new";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = ErrorText(GetLastError());
        return false;
    }
    const BYTE* p = static_cast<const BYTE*>(data);
    DWORD left = size;
    bool ok = true;
    while (left && ok) {
        DWORD chunk = left > (1u << 20) ? (1u << 20) : left, written = 0;
        ok = WriteFile(h, p, chunk, &written, nullptr) && written == chunk;
        p += written;
        left -= written;
    }
    if (ok) ok = FlushFileBuffers(h) != FALSE;
    DWORD e = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (ok && !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        e = GetLastError();
    }
    if (!ok) {
        DeleteFileW(tmp.c_str());
        err = ErrorText(e);
    }
    return ok;
}

// Toglie i file noti di un'installazione precedente in un'altra cartella (mai file sconosciuti).
void RemoveKnownFiles(const std::wstring& dir) {
    if (dir.empty() || !DirExists(dir)) return;
    for (const auto& f : kFiles) DeleteFileW(Join(dir, f.name).c_str());
    for (const wchar_t* f : kLegacyFiles) DeleteFileW(Join(dir, f).c_str());
    RemoveDirectoryW(dir.c_str());
}

struct InstallOptions {
    std::wstring dir;
    bool desktop = false;
    bool mediainfo = false;
    bool launch = false;
};

struct InstallResult {
    bool ok = false;
    std::wstring error;
    std::wstring warning;
};

using ProgressFn = void (*)(void* ctx, int percent, const std::wstring& text);

void SetSz(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), DWORD((v.size() + 1) * sizeof(wchar_t)));
}

void SetDw(HKEY k, const wchar_t* name, DWORD v) {
    RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
}

InstallResult DoInstall(const InstallOptions& o, ProgressFn progress, void* ctx) {
    InstallResult r;
    auto step = [&](int pct, const std::wstring& text) { if (progress) progress(ctx, pct, text); };
    const std::wstring& dir = o.dir;

    // Installazioni precedenti (questo installer o Inno Setup), lette prima di scrivere il registro.
    std::wstring prevOwn = TrimSlash(RegReadString(HKEY_CURRENT_USER, kUninstKey, L"InstallLocation"));
    std::wstring prevInno = TrimSlash(RegReadString(HKEY_CURRENT_USER, kInnoKey, L"InstallLocation"));

    step(3, L"Creazione della cartella...");
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if ((rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) || !DirExists(dir)) {
        r.error = L"Impossibile creare la cartella\n" + dir + L"\n\n" + ErrorText(DWORD(rc)) +
                  L"\n\nScegli una cartella del tuo profilo utente: l'installazione non usa i diritti di amministratore.";
        return r;
    }

    ULONGLONG total = 0;
    const int count = int(ARRAYSIZE(kFiles));
    for (int i = 0; i < count; ++i) {
        const auto& f = kFiles[i];
        step(8 + 62 * i / count, std::wstring(L"Copia di ") + f.name + L"...");
        const void* data = nullptr;
        DWORD size = 0;
        if (!GetPayload(f.id, &data, &size)) {
            r.error = std::wstring(L"Il setup è danneggiato: manca ") + f.name + L".";
            return r;
        }
        std::wstring err;
        if (!WriteFileAtomic(Join(dir, f.name), data, size, err)) {
            r.error = std::wstring(L"Impossibile scrivere ") + Join(dir, f.name) + L"\n\n" + err;
            return r;
        }
        total += size;
    }

    step(74, L"Creazione dei collegamenti...");
    std::wstring exe = Join(dir, kAppExe);
    std::wstring startLnk = StartMenuLink(), deskLnk = DesktopLink();
    if (startLnk.empty() || FAILED(CreateShortcut(startLnk, exe, dir)))
        r.warning += L"Collegamento nel menu Start non creato.\n";
    if (o.desktop) {
        if (deskLnk.empty() || FAILED(CreateShortcut(deskLnk, exe, dir)))
            r.warning += L"Icona sul desktop non creata.\n";
    } else if (!deskLnk.empty()) {
        // Opzione tolta in un aggiornamento: rimuove l'icona creata in precedenza.
        DeleteShortcutTo(deskLnk, exe);
        if (!prevOwn.empty()) DeleteShortcutTo(deskLnk, Join(prevOwn, kAppExe));
        if (!prevInno.empty()) DeleteShortcutTo(deskLnk, Join(prevInno, kAppExe));
    }

    step(80, L"Registrazione in App installate...");
    HKEY k;
    LSTATUS ls = RegCreateKeyExW(HKEY_CURRENT_USER, kUninstKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (ls != ERROR_SUCCESS) {
        r.error = L"Impossibile registrare il programma di disinstallazione: " + ErrorText(DWORD(ls));
        return r;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t date[16];
    swprintf(date, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    std::wstring uninst = Join(dir, kUninstExe);
    SetSz(k, L"DisplayName", kAppName);
    SetSz(k, L"DisplayVersion", RF_VERSION_W);
    SetSz(k, L"Publisher", kPublisher);
    SetSz(k, L"URLInfoAbout", kUrl);
    SetSz(k, L"HelpLink", kUrl);
    SetSz(k, L"DisplayIcon", exe);
    SetSz(k, L"InstallLocation", dir);
    SetSz(k, L"UninstallString", Quote(uninst));
    SetSz(k, L"QuietUninstallString", Quote(uninst) + L" /S");
    SetSz(k, L"InstallDate", date);
    SetDw(k, L"NoModify", 1);
    SetDw(k, L"NoRepair", 1);
    SetDw(k, L"EstimatedSize", DWORD((total + 1023) / 1024));
    RegCloseKey(k);

    // Passaggio dal vecchio installer Inno Setup e cambi di cartella.
    for (const wchar_t* f : kLegacyFiles) DeleteFileW(Join(dir, f).c_str());
    if (!prevInno.empty() && !IEq(prevInno, dir)) RemoveKnownFiles(prevInno);
    if (!prevOwn.empty() && !IEq(prevOwn, dir)) RemoveKnownFiles(prevOwn);
    RegDeleteTreeW(HKEY_CURRENT_USER, kInnoKey);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (o.mediainfo && !MediaInfoInstalled()) {
        step(86, L"Installazione di MediaInfo completo con winget (Windows può chiedere l'autorizzazione)...");
        std::wstring winget = WingetExe();
        if (winget.empty()) {
            r.warning += L"winget non è disponibile: MediaInfo si può installare dall'app (Strumenti).\n";
        } else {
            RunHidden(winget, L"install --id MediaArea.MediaInfo.GUI -e --silent "
                              L"--accept-package-agreements --accept-source-agreements", true);
            if (!MediaInfoInstalled())
                r.warning += L"MediaInfo non risulta installato: riprova dall'app (Strumenti > Installa MediaInfo).\n";
        }
    }

    step(100, L"Installazione completata.");
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------------------------
// Finestra del setup
// ---------------------------------------------------------------------------------------------

constexpr int IDC_DIR = 1001, IDC_BROWSE = 1002, IDC_DESKTOP = 1003, IDC_MEDIAINFO = 1004, IDC_LAUNCH = 1005,
              IDC_STATUS = 1006, IDC_PROGRESS = 1007, IDC_TITLE = 1008, IDC_SUBTITLE = 1009, IDC_APPICON = 1010;

const UINT WM_APP_PROGRESS = WM_APP + 1;  // wParam = percento, lParam = std::wstring*
const UINT WM_APP_DONE = WM_APP + 2;      // lParam = InstallResult*

struct SetupUi {
    HWND hwnd = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    double scale = 1.0;
    bool busy = false, done = false;
    InstallOptions opts;
    std::wstring defaultDir, previousVersion;
    bool mediainfoInstalled = false;
};

SetupUi g_ui;

int Px(double dip) { return int(dip * g_ui.scale + 0.5); }

HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, Px(x), Px(y), Px(w), Px(h), g_ui.hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.font), FALSE);
    return c;
}

const int kClientW = 520, kClientH = 352;

void CreateControls(HWND hwnd) {
    g_ui.hwnd = hwnd;
    HWND icon = Ctl(L"STATIC", nullptr, SS_ICON | SS_REALSIZECONTROL, 20, 18, 40, 40, IDC_APPICON);
    HICON big = static_cast<HICON>(LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, Px(40), Px(40), 0));
    SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(big), 0);

    std::wstring title = std::wstring(L"Installazione di ReleaseForge ") + RF_VERSION_W;
    HWND t = Ctl(L"STATIC", title.c_str(), SS_LEFT | SS_NOPREFIX, 72, 16, kClientW - 92, 26, IDC_TITLE);
    SendMessageW(t, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.titleFont), FALSE);
    std::wstring sub = g_ui.previousVersion.empty()
                           ? std::wstring(L"Per l'utente corrente, senza diritti di amministratore.")
                           : L"Aggiornamento dalla versione " + g_ui.previousVersion + L". Impostazioni e template restano.";
    Ctl(L"STATIC", sub.c_str(), SS_LEFT | SS_NOPREFIX, 72, 44, kClientW - 92, 18, IDC_SUBTITLE);
    Ctl(L"STATIC", nullptr, SS_ETCHEDHORZ, 20, 76, kClientW - 40, 2, 0);

    Ctl(L"STATIC", L"Cartella di installazione:", SS_LEFT, 20, 92, kClientW - 40, 18, 0);
    HWND edit = Ctl(L"EDIT", g_ui.defaultDir.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 20, 113, kClientW - 40 - 104, 24,
                    IDC_DIR, WS_EX_CLIENTEDGE);
    SendMessageW(edit, EM_SETLIMITTEXT, 1024, 0);
    Ctl(L"BUTTON", L"Sfoglia...", WS_TABSTOP | BS_PUSHBUTTON, kClientW - 20 - 96, 112, 96, 26, IDC_BROWSE);

    HWND desk = Ctl(L"BUTTON", L"Crea un'icona sul desktop", WS_TABSTOP | BS_AUTOCHECKBOX, 20, 152, kClientW - 40, 20,
                    IDC_DESKTOP);
    if (g_ui.opts.desktop) SendMessageW(desk, BM_SETCHECK, BST_CHECKED, 0);
    HWND mi = Ctl(L"BUTTON",
                  g_ui.mediainfoInstalled ? L"MediaInfo completo: già installato"
                                          : L"Installa MediaInfo completo (versione con interfaccia) tramite winget",
                  WS_TABSTOP | BS_AUTOCHECKBOX, 20, 176, kClientW - 40, 20, IDC_MEDIAINFO);
    if (g_ui.mediainfoInstalled) EnableWindow(mi, FALSE);
    HWND launch = Ctl(L"BUTTON", L"Avvia ReleaseForge al termine", WS_TABSTOP | BS_AUTOCHECKBOX, 20, 200, kClientW - 40,
                      20, IDC_LAUNCH);
    SendMessageW(launch, BM_SETCHECK, BST_CHECKED, 0);

    Ctl(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 20, 238, kClientW - 40, 18, IDC_STATUS);
    HWND prog = Ctl(PROGRESS_CLASSW, nullptr, 0, 20, 260, kClientW - 40, 14, IDC_PROGRESS);
    SendMessageW(prog, PBM_SETRANGE32, 0, 100);

    Ctl(L"STATIC", nullptr, SS_ETCHEDHORZ, 20, 296, kClientW - 40, 2, 0);
    Ctl(L"BUTTON", g_ui.previousVersion.empty() ? L"Installa" : L"Aggiorna", WS_TABSTOP | BS_DEFPUSHBUTTON,
        kClientW - 20 - 96 - 8 - 96, 310, 96, 28, IDOK);
    Ctl(L"BUTTON", L"Annulla", WS_TABSTOP | BS_PUSHBUTTON, kClientW - 20 - 96, 310, 96, 28, IDCANCEL);
    SetFocus(GetDlgItem(hwnd, IDOK));
}

void SetBusy(bool busy) {
    g_ui.busy = busy;
    for (int id : {IDC_DIR, IDC_BROWSE, IDC_DESKTOP, IDC_LAUNCH, IDOK, IDCANCEL})
        EnableWindow(GetDlgItem(g_ui.hwnd, id), !busy);
    EnableWindow(GetDlgItem(g_ui.hwnd, IDC_MEDIAINFO), !busy && !g_ui.mediainfoInstalled);
}

bool Checked(int id) { return SendDlgItemMessageW(g_ui.hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }

void BrowseFolder() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Scegli dove installare ReleaseForge");
    if (SUCCEEDED(dlg->Show(g_ui.hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                std::wstring dir = TrimSlash(p);
                if (!IEq(BaseName(dir), kAppName)) dir = Join(dir, kAppName);  // come Inno: aggiunge "ReleaseForge"
                SetDlgItemTextW(g_ui.hwnd, IDC_DIR, dir.c_str());
            }
            CoTaskMemFree(p);
            item->Release();
        }
    }
    dlg->Release();
}

// Normalizza e controlla la cartella scelta. Restituisce una stringa vuota se non valida.
std::wstring ValidateDir(std::wstring dir, std::wstring& err) {
    size_t a = dir.find_first_not_of(L" \t\""), b = dir.find_last_not_of(L" \t\"");
    dir = a == std::wstring::npos ? std::wstring() : dir.substr(a, b - a + 1);
    dir = TrimSlash(FullPath(ExpandEnv(dir)));
    bool absolute = (dir.size() >= 3 && dir[1] == L':' && dir[2] == L'\\') || dir.rfind(L"\\\\", 0) == 0;
    if (dir.empty() || !absolute) {
        err = L"Indica il percorso completo della cartella, ad esempio\n" + g_ui.defaultDir;
        return std::wstring();
    }
    if (dir.size() <= 3) {
        err = L"Non è possibile installare nella radice di un'unità: scegli o crea una cartella.";
        return std::wstring();
    }
    if (dir.find_first_of(L"*?<>|") != std::wstring::npos || dir.find(L':', 2) != std::wstring::npos) {
        err = L"Il percorso contiene caratteri non validi.";
        return std::wstring();
    }
    return dir;
}

void ProgressToUi(void* ctx, int pct, const std::wstring& text) {
    PostMessageW(static_cast<HWND>(ctx), WM_APP_PROGRESS, WPARAM(pct), reinterpret_cast<LPARAM>(new std::wstring(text)));
}

DWORD WINAPI InstallThread(void* param) {
    HWND hwnd = static_cast<HWND>(param);
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto* r = new InstallResult(DoInstall(g_ui.opts, ProgressToUi, hwnd));
    if (SUCCEEDED(co)) CoUninitialize();
    PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(r));
    return 0;
}

void StartInstall() {
    wchar_t buf[1100] = L"";
    GetDlgItemTextW(g_ui.hwnd, IDC_DIR, buf, ARRAYSIZE(buf));
    std::wstring err;
    std::wstring dir = ValidateDir(buf, err);
    if (dir.empty()) {
        MessageBoxW(g_ui.hwnd, err.c_str(), kAppName, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(g_ui.hwnd, IDC_DIR));
        return;
    }
    SetDlgItemTextW(g_ui.hwnd, IDC_DIR, dir.c_str());
    if (DirExists(dir) && !DirIsEmpty(dir) && !FileExists(Join(dir, kAppExe)) &&
        MessageBoxW(g_ui.hwnd, (L"La cartella\n" + dir + L"\nesiste già e contiene altri file.\n\nInstallare comunque?").c_str(),
                    kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    if (!EnsureAppClosed(g_ui.hwnd, Join(dir, kAppExe))) return;
    std::wstring prevOwn = TrimSlash(RegReadString(HKEY_CURRENT_USER, kUninstKey, L"InstallLocation"));
    if (!prevOwn.empty() && !IEq(prevOwn, dir) && !EnsureAppClosed(g_ui.hwnd, Join(prevOwn, kAppExe))) return;

    g_ui.opts.dir = dir;
    g_ui.opts.desktop = Checked(IDC_DESKTOP);
    g_ui.opts.mediainfo = !g_ui.mediainfoInstalled && Checked(IDC_MEDIAINFO);
    g_ui.opts.launch = Checked(IDC_LAUNCH);
    SetBusy(true);
    HANDLE th = CreateThread(nullptr, 0, InstallThread, g_ui.hwnd, 0, nullptr);
    if (!th) {
        SetBusy(false);
        MessageBoxW(g_ui.hwnd, (L"Impossibile avviare l'installazione: " + ErrorText(GetLastError())).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(th);
}

void OnDone(InstallResult* r) {
    if (!r->ok) {
        SetBusy(false);
        SetDlgItemTextW(g_ui.hwnd, IDC_STATUS, L"Installazione non completata.");
        SendDlgItemMessageW(g_ui.hwnd, IDC_PROGRESS, PBM_SETSTATE, PBST_ERROR, 0);
        MessageBoxW(g_ui.hwnd, r->error.c_str(), kAppName, MB_OK | MB_ICONERROR);
        SendDlgItemMessageW(g_ui.hwnd, IDC_PROGRESS, PBM_SETSTATE, PBST_NORMAL, 0);
        SendDlgItemMessageW(g_ui.hwnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);
        return;
    }
    g_ui.busy = false;
    g_ui.done = true;
    SetDlgItemTextW(g_ui.hwnd, IDC_STATUS, (L"ReleaseForge è stato installato in " + g_ui.opts.dir).c_str());
    SetDlgItemTextW(g_ui.hwnd, IDOK, L"Fine");
    EnableWindow(GetDlgItem(g_ui.hwnd, IDOK), TRUE);
    EnableWindow(GetDlgItem(g_ui.hwnd, IDC_LAUNCH), TRUE);
    ShowWindow(GetDlgItem(g_ui.hwnd, IDCANCEL), SW_HIDE);
    SetFocus(GetDlgItem(g_ui.hwnd, IDOK));
    if (!r->warning.empty())
        MessageBoxW(g_ui.hwnd, (L"Installazione completata con avvisi:\n\n" + r->warning).c_str(), kAppName,
                    MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(hwnd);
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE:
                    BrowseFolder();
                    return 0;
                case IDOK:
                    if (g_ui.busy) return 0;
                    if (g_ui.done) {
                        if (Checked(IDC_LAUNCH)) LaunchApp(g_ui.opts.dir);
                        DestroyWindow(hwnd);
                    } else {
                        StartInstall();
                    }
                    return 0;
                case IDCANCEL:
                    if (!g_ui.busy) SendMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
        case WM_APP_PROGRESS: {
            auto* text = reinterpret_cast<std::wstring*>(lp);
            SetDlgItemTextW(hwnd, IDC_STATUS, text->c_str());
            SendDlgItemMessageW(hwnd, IDC_PROGRESS, PBM_SETPOS, wp, 0);
            delete text;
            return 0;
        }
        case WM_APP_DONE: {
            auto* r = reinterpret_cast<InstallResult*>(lp);
            OnDone(r);
            delete r;
            return 0;
        }
        case WM_CLOSE:
            if (g_ui.busy) {
                MessageBeep(MB_ICONWARNING);
                return 0;
            }
            if (!g_ui.done && MessageBoxW(hwnd, L"Annullare l'installazione di ReleaseForge?", kAppName,
                                          MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(g_ui.done ? kOk : kCancelled);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring DefaultInstallDir() {
    std::wstring prev = TrimSlash(RegReadString(HKEY_CURRENT_USER, kUninstKey, L"InstallLocation"));
    if (prev.empty()) prev = TrimSlash(RegReadString(HKEY_CURRENT_USER, kInnoKey, L"InstallLocation"));
    if (!prev.empty()) return prev;
    std::wstring programs = KnownFolder(FOLDERID_UserProgramFiles);  // %LOCALAPPDATA%\Programs
    if (programs.empty()) {
        wchar_t local[MAX_PATH] = L"";
        GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        programs = Join(local, L"Programs");
    }
    return Join(programs, kAppName);
}

int RunSetupUi() {
    g_ui.defaultDir = DefaultInstallDir();
    g_ui.previousVersion = RegReadString(HKEY_CURRENT_USER, kUninstKey, L"DisplayVersion");
    if (g_ui.previousVersion.empty()) g_ui.previousVersion = RegReadString(HKEY_CURRENT_USER, kInnoKey, L"DisplayVersion");
    g_ui.mediainfoInstalled = MediaInfoInstalled();
    std::wstring deskLnk = DesktopLink();
    g_ui.opts.desktop = !g_ui.previousVersion.empty() && FileExists(deskLnk);

    HDC dc = GetDC(nullptr);
    g_ui.scale = GetDeviceCaps(dc, LOGPIXELSY) / 96.0;
    ReleaseDC(nullptr, dc);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_ui.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW big = ncm.lfMessageFont;
    big.lfHeight = big.lfHeight * 3 / 2;
    big.lfWeight = FW_SEMIBOLD;
    g_ui.titleFont = CreateFontIndirectW(&big);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SetupWndProc;
    wc.hInstance = g_inst;
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"ReleaseForgeSetup";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, Px(kClientW), Px(kClientH)};
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    std::wstring caption = std::wstring(L"ReleaseForge ") + RF_VERSION_W + L" - Installazione";
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, caption.c_str(), style, x, y, w, h, nullptr, nullptr, g_inst, nullptr);
    if (!hwnd) return kError;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG m;
    int code = kCancelled;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (m.message == WM_QUIT) code = int(m.wParam);
    DeleteObject(g_ui.font);
    DeleteObject(g_ui.titleFont);
    return code;
}

int SetupMain(const std::vector<std::wstring>& args) {
    InstallOptions o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (IEq(a, L"/S") || IEq(a, L"/SILENT") || IEq(a, L"/VERYSILENT")) g_silent = true;
        else if (IEq(a, L"/DESKTOP")) o.desktop = true;
        else if (IEq(a, L"/MEDIAINFO")) o.mediainfo = true;
        else if (IEq(a, L"/LAUNCH")) o.launch = true;
        else if (a.size() > 5 && IEq(a.substr(0, 5), L"/DIR=")) o.dir = a.substr(5);
        else if (a.size() > 3 && IEq(a.substr(0, 3), L"/D=")) {
            o.dir = a.substr(3);  // stile NSIS: tutto il resto della riga, anche con spazi non virgolettati
            for (size_t j = i + 1; j < args.size(); ++j) o.dir += L" " + args[j];
            break;
        }
    }
    if (!g_silent) return RunSetupUi();

    std::wstring err;
    g_ui.defaultDir = DefaultInstallDir();
    o.dir = ValidateDir(o.dir.empty() ? g_ui.defaultDir : o.dir, err);
    if (o.dir.empty()) return kError;
    if (!EnsureAppClosed(nullptr, Join(o.dir, kAppExe))) return kError;
    std::wstring prevOwn = TrimSlash(RegReadString(HKEY_CURRENT_USER, kUninstKey, L"InstallLocation"));
    if (!prevOwn.empty() && !IEq(prevOwn, o.dir) && !EnsureAppClosed(nullptr, Join(prevOwn, kAppExe))) return kError;
    InstallResult r = DoInstall(o, nullptr, nullptr);
    if (!r.ok) return kError;
    if (o.launch) LaunchApp(o.dir);
    return kOk;
}

// ---------------------------------------------------------------------------------------------
// Disinstallazione
// ---------------------------------------------------------------------------------------------

// Cancella una cartella di dati con tutto il contenuto, senza seguire collegamenti/junction.
void DeleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = Join(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(p.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) DeleteTree(p);
                RemoveDirectoryW(p.c_str());
            } else {
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// Il file in uso (questo eseguibile) si cancella dopo l'uscita, tramite cmd.
void DeleteAfterExit(const std::wstring& file, const std::wstring& dirToRemove) {
    wchar_t sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return;
    std::wstring cmd = Join(sys, L"cmd.exe");
    std::wstring args = L"/d /c ping -n 3 127.0.0.1 >nul & del /f /q " + Quote(file);
    if (!dirToRemove.empty()) args += L" & rmdir " + Quote(dirToRemove);
    RunHidden(cmd, args, false);
}

int UninstallMain(const std::vector<std::wstring>& args) {
    bool run = false, purge = false;
    DWORD parentPid = 0;
    std::wstring dir;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (IEq(a, L"/S") || IEq(a, L"/SILENT") || IEq(a, L"/VERYSILENT")) g_silent = true;
        else if (IEq(a, L"/PURGE")) purge = true;
        else if (IEq(a, L"/_RUN") && i + 2 < args.size()) {
            run = true;
            parentPid = wcstoul(args[i + 1].c_str(), nullptr, 10);
            dir = args[i + 2];
            i += 2;
        }
    }
    const std::wstring self = ModulePath();

    if (!run) {
        // uninstall.exe non può cancellare sé stesso: si copia nella cartella temporanea e riparte da lì.
        dir = DirName(self);
        wchar_t tmp[MAX_PATH + 1];
        DWORD n = GetTempPathW(ARRAYSIZE(tmp), tmp);
        std::wstring copy = (n && n < ARRAYSIZE(tmp))
                                ? Join(tmp, L"ReleaseForge-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe")
                                : std::wstring();
        std::wstring cmdArgs = L"/_run " + std::to_wstring(GetCurrentProcessId()) + L" " + Quote(dir);
        if (g_silent) cmdArgs += L" /S";
        if (purge) cmdArgs += L" /PURGE";
        if (!copy.empty() && CopyFileW(self.c_str(), copy.c_str(), FALSE)) {
            std::wstring cmd = Quote(copy) + L" " + cmdArgs;
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(copy.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return kOk;
            }
            DeleteFileW(copy.c_str());
        }
        run = true;  // ripiego: disinstalla da qui, uninstall.exe verrà cancellato dopo l'uscita
    }

    if (parentPid) {
        if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, parentPid)) {
            WaitForSingleObject(p, 15000);
            CloseHandle(p);
        }
    }
    const bool fromTemp = !IEq(DirName(self), dir);
    auto finish = [&](int code) {
        if (fromTemp) DeleteAfterExit(self, std::wstring());
        return code;
    };

    dir = TrimSlash(dir);
    if (dir.empty() || !FileExists(Join(dir, kUninstExe))) {
        Message(nullptr, L"Cartella di installazione non valida:\n" + dir, MB_OK | MB_ICONERROR);
        return finish(kError);
    }
    if (Message(nullptr, L"Disinstallare ReleaseForge da\n" + dir + L" ?", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return finish(kCancelled);
    const std::wstring exe = Join(dir, kAppExe);
    if (!EnsureAppClosed(nullptr, exe)) return finish(kCancelled);

    std::wstring roaming = KnownFolder(FOLDERID_RoamingAppData), local = KnownFolder(FOLDERID_LocalAppData);
    std::wstring dataDir = roaming.empty() ? std::wstring() : Join(roaming, kAppName);
    std::wstring toolsDir = local.empty() ? std::wstring() : Join(local, kAppName);
    bool hasData = (!dataDir.empty() && DirExists(dataDir)) || (!toolsDir.empty() && DirExists(toolsDir));
    if (!g_silent && hasData) {
        std::wstring q = L"Rimuovere anche impostazioni, template, tracker e strumenti scaricati (FFmpeg)?\n\n";
        if (DirExists(dataDir)) q += dataDir + L"\n";
        if (DirExists(toolsDir)) q += toolsDir + L"\n";
        q += L"\nScegli No per conservarli in caso di reinstallazione. Le release in Documenti non vengono toccate.";
        purge = MessageBoxW(nullptr, q.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }

    // Solo i file installati: eventuali altri file dell'utente nella cartella restano.
    for (const auto& f : kFiles)
        if (fromTemp || !IEq(f.name, kUninstExe)) DeleteFileW(Join(dir, f.name).c_str());
    for (const wchar_t* f : kLegacyFiles) DeleteFileW(Join(dir, f).c_str());
    RemoveDirectoryW(dir.c_str());

    DeleteShortcutTo(StartMenuLink(), exe);
    DeleteShortcutTo(DesktopLink(), exe);
    for (const wchar_t* key : {kUninstKey, kInnoKey}) {
        std::wstring loc = TrimSlash(RegReadString(HKEY_CURRENT_USER, key, L"InstallLocation"));
        if (RegKeyExists(HKEY_CURRENT_USER, key) && (loc.empty() || IEq(loc, dir))) RegDeleteTreeW(HKEY_CURRENT_USER, key);
    }
    if (purge) {
        if (!dataDir.empty() && DirExists(dataDir)) DeleteTree(dataDir);
        if (!toolsDir.empty() && DirExists(toolsDir)) DeleteTree(toolsDir);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (!fromTemp) DeleteAfterExit(self, dir);
    if (FileExists(exe)) {
        Message(nullptr, L"Alcuni file non sono stati rimossi da\n" + dir + L"\n\nCancellali a mano.", MB_OK | MB_ICONWARNING);
        return finish(kError);
    }
    Message(nullptr, purge ? L"ReleaseForge è stato disinstallato."
                           : L"ReleaseForge è stato disinstallato.\nImpostazioni e template sono stati conservati.",
            MB_OK | MB_ICONINFORMATION);
    return finish(kOk);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;
    std::vector<std::wstring> args;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        LocalFree(argv);
    }
    for (const auto& a : args)
        if (IEq(a, L"/S") || IEq(a, L"/SILENT") || IEq(a, L"/VERYSILENT")) g_silent = true;

    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    int code = HasPayload() ? SetupMain(args) : UninstallMain(args);
    if (SUCCEEDED(co)) CoUninitialize();
    return code;
}
