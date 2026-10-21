#include "winutil.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>

namespace rf {

std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

std::string U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static std::string CodepageToUtf8(const std::string& s, UINT cp) {
    if (s.empty()) return "";
    int n = MultiByteToWideChar(cp, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(cp, 0, s.data(), int(s.size()), w.data(), n);
    return U8(w);
}

static bool IsValidUtf8(const std::string& s) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0) > 0 || s.empty();
}

std::string AnsiToUtf8(const std::string& s) {
    if (IsValidUtf8(s)) return s;
    return CodepageToUtf8(s, CP_ACP);
}

std::string BytesToUtf8Text(const std::string& b) {
    if (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) return b.substr(3);
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE) {
        std::wstring w((b.size() - 2) / 2, L'\0');
        memcpy(w.data(), b.data() + 2, w.size() * 2);
        return U8(w);
    }
    return AnsiToUtf8(b);
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1ll << 30)) { CloseHandle(h); return false; }
    out.resize(size_t(size.QuadPart));
    DWORD read = 0;
    size_t done = 0;
    while (done < out.size()) {
        DWORD chunk = DWORD(std::min<size_t>(out.size() - done, 1 << 24));
        if (!ReadFile(h, out.data() + done, chunk, &read, nullptr) || read == 0) break;
        done += read;
    }
    CloseHandle(h);
    out.resize(done);
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".rf-tmp";
    HANDLE h = CreateFileW(LongPath(tmp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t done = 0;
    bool ok = true;
    while (done < data.size()) {
        DWORD written = 0;
        DWORD chunk = DWORD(std::min<size_t>(data.size() - done, 1 << 24));
        if (!WriteFile(h, data.data() + done, chunk, &written, nullptr) || written == 0) { ok = false; break; }
        done += written;
    }
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    if (!ok) { DeleteFileW(LongPath(tmp).c_str()); return false; }
    if (!MoveFileExW(LongPath(tmp).c_str(), LongPath(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(LongPath(tmp).c_str());
        return false;
    }
    return true;
}

std::string ReadTextFile(const std::wstring& path) {
    std::string b;
    if (!ReadFileBytes(path, b)) return "";
    return BytesToUtf8Text(b);
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(LongPath(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(LongPath(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool CreateDirs(const std::wstring& path) {
    if (path.empty()) return false;
    if (DirExists(path)) return true;
    int r = SHCreateDirectoryExW(nullptr, FullPath(path).c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS || DirExists(path);
}

uint64_t FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(LongPath(p).c_str(), GetFileExInfoStandard, &d)) return 0;
    return (uint64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

int64_t FileMtimeUnix(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(LongPath(p).c_str(), GetFileExInfoStandard, &d)) return 0;
    uint64_t t = (uint64_t(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    return int64_t((t - 116444736000000000ull) / 10000000ull);
}

std::wstring PathTrimSlashes(const std::wstring& p) {
    std::wstring s = p;
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring PathFileName(const std::wstring& path) {
    std::wstring p = PathTrimSlashes(path);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

std::wstring PathExtension(const std::wstring& path) {
    std::wstring name = PathFileName(path);
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0) return L"";
    std::wstring e = name.substr(dot);
    for (auto& c : e) c = wchar_t(towlower(c));
    return e;
}

std::wstring PathStem(const std::wstring& path) {
    std::wstring name = PathFileName(path);
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0) return name;
    return name.substr(0, dot);
}

std::wstring PathParent(const std::wstring& path) {
    std::wstring p = PathTrimSlashes(path);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    if (pos == 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, pos);
}

std::wstring FullPath(const std::wstring& path) {
    DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!n) return path;
    std::wstring out(n, L'\0');
    n = GetFullPathNameW(path.c_str(), n, out.data(), nullptr);
    out.resize(n);
    return out;
}

std::wstring LongPath(const std::wstring& path) {
    if (path.size() < 240 || path.rfind(L"\\\\?\\", 0) == 0) return path;
    std::wstring full = FullPath(path);
    if (full.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + full.substr(2);
    return L"\\\\?\\" + full;
}

static bool LessCI(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_LESS_THAN;
}

static void Walk(const std::wstring& root, const std::wstring& rel, std::vector<DirEntry>& out) {
    WIN32_FIND_DATAW fd;
    std::wstring dir = rel.empty() ? root : PathJoin(root, rel);
    HANDLE h = FindFirstFileExW(LongPath(PathJoin(dir, L"*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> subdirs;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring r = rel.empty() ? name : rel + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Niente junction/symlink di cartelle (possibili cicli o dati esterni alla release).
            // I file con reparse point (OneDrive, deduplicazione, ecc.) sono file reali: vanno inclusi.
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) subdirs.push_back(r);
        }
        else out.push_back({PathJoin(root, r), r, (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (auto& s : subdirs) Walk(root, s, out);
}

std::vector<DirEntry> ListFilesRecursive(const std::wstring& root) {
    std::vector<DirEntry> out;
    Walk(PathTrimSlashes(root), L"", out);
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return LessCI(a.relPath, b.relPath); });
    return out;
}

static std::vector<std::wstring> ListLevel(const std::wstring& dir, bool wantDirs) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(LongPath(PathJoin(dir, L"*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir == wantDirs) out.push_back(PathJoin(dir, name));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), LessCI);
    return out;
}

std::vector<std::wstring> ListSubdirs(const std::wstring& dir) { return ListLevel(dir, true); }
std::vector<std::wstring> ListFiles(const std::wstring& dir) { return ListLevel(dir, false); }

bool IsVideoFile(const std::wstring& path) {
    static const wchar_t* exts[] = {L".mkv", L".mp4", L".avi", L".m2ts", L".ts", L".mov", L".webm", L".wmv", L".m4v", L".mpg", L".mpeg"};
    std::wstring e = PathExtension(path);
    for (auto x : exts) if (e == x) return true;
    return false;
}

bool IsJunkFile(const std::wstring& name) {
    if (name.rfind(L"._", 0) == 0) return true;
    if (_wcsicmp(name.c_str(), L".DS_Store") == 0 || _wcsicmp(name.c_str(), L"Thumbs.db") == 0 || _wcsicmp(name.c_str(), L"desktop.ini") == 0) return true;
    return PathExtension(name) == L".tmp" || PathExtension(name) == L".part" || PathExtension(name) == L".!qb";
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return PathParent(std::wstring(buf, n));
}

std::wstring KnownFolder(const GUID& id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring AppDataDir() { return PathJoin(KnownFolder(FOLDERID_RoamingAppData), L"ReleaseForge"); }
std::wstring LocalAppDataDir() { return PathJoin(KnownFolder(FOLDERID_LocalAppData), L"ReleaseForge"); }
std::wstring DocumentsDir() { return KnownFolder(FOLDERID_Documents); }

std::wstring EnvVar(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return L"";
    std::wstring v(n, L'\0');
    n = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(n);
    return v;
}

static std::wstring RegString(HKEY root, const wchar_t* sub, const wchar_t* value) {
    wchar_t buf[8192];
    DWORD size = sizeof buf;
    if (RegGetValueW(root, sub, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return L"";
    return buf;
}

std::wstring FindInPath(const std::wstring& exe) {
    // PATH del processo + PATH aggiornato da registro (es. dopo winget senza riavvio dell'app).
    std::wstring paths = EnvVar(L"PATH") + L";" + RegString(HKEY_CURRENT_USER, L"Environment", L"Path") + L";" +
                         RegString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", L"Path");
    size_t start = 0;
    while (start <= paths.size()) {
        size_t end = paths.find(L';', start);
        if (end == std::wstring::npos) end = paths.size();
        std::wstring dir = paths.substr(start, end - start);
        start = end + 1;
        if (dir.empty()) continue;
        if (dir.front() == L'"' && dir.size() > 1) dir = dir.substr(1, dir.size() - 2);
        wchar_t expanded[MAX_PATH * 2];
        if (ExpandEnvironmentStringsW(dir.c_str(), expanded, DWORD(std::size(expanded)))) dir = expanded;
        std::wstring candidate = PathJoin(dir, exe);
        if (FileExists(candidate)) return candidate;
    }
    return L"";
}

std::wstring LastErrorMessage(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
                   reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = buf ? buf : L"";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r' || msg.back() == L' ')) msg.pop_back();
    return msg + L" (" + std::to_wstring(err) + L")";
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        void* p = GlobalLock(mem);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

void OpenInExplorer(const std::wstring& path, bool select) {
    if (select) {
        std::wstring args = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void OpenUrl(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::string NowTimeHMS() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    wsprintfA(buf, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

int64_t UnixNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return int64_t((t - 116444736000000000ull) / 10000000ull);
}

}  // namespace rf
