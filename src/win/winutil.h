// ReleaseForge - utilità Windows (UTF-8 <-> UTF-16, file, percorsi, appunti).
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rf {

std::wstring W(const std::string& utf8);
std::string U8(const std::wstring& wide);
std::string AnsiToUtf8(const std::string& ansi);   // output di programmi console (codepage OEM/ANSI)
std::string BytesToUtf8Text(const std::string& bytes);  // rileva UTF-8/UTF-16 BOM, altrimenti ANSI

bool ReadFileBytes(const std::wstring& path, std::string& out);
bool WriteFileBytes(const std::wstring& path, const std::string& data);   // scrittura atomica (tmp + replace)
std::string ReadTextFile(const std::wstring& path);   // UTF-8 senza BOM; "" se mancante

bool FileExists(const std::wstring& path);
bool DirExists(const std::wstring& path);
bool CreateDirs(const std::wstring& path);
uint64_t FileSize(const std::wstring& path);
int64_t FileMtimeUnix(const std::wstring& path);   // ultima modifica (epoch UTC), 0 se assente
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring PathFileName(const std::wstring& path);   // ultimo componente
std::wstring PathStem(const std::wstring& path);       // senza estensione
std::wstring PathExtension(const std::wstring& path);  // ".mkv" (minuscolo)
std::wstring PathParent(const std::wstring& path);
std::wstring PathTrimSlashes(const std::wstring& path);
std::wstring FullPath(const std::wstring& path);
std::wstring LongPath(const std::wstring& path);       // prefisso \\?\ per percorsi > MAX_PATH

struct DirEntry {
    std::wstring fullPath;
    std::wstring relPath;  // relativo alla radice, separatore '\'
    uint64_t size = 0;
};
// Elenca ricorsivamente i file (ordinati per percorso relativo, case-insensitive).
std::vector<DirEntry> ListFilesRecursive(const std::wstring& root);
std::vector<std::wstring> ListSubdirs(const std::wstring& dir);   // ordinati
std::vector<std::wstring> ListFiles(const std::wstring& dir);     // solo primo livello, ordinati

bool IsVideoFile(const std::wstring& path);
bool IsJunkFile(const std::wstring& name);   // ._*, .DS_Store, Thumbs.db, desktop.ini, *.tmp

std::wstring ExeDir();
std::wstring KnownFolder(const GUID& id);
std::wstring AppDataDir();        // %APPDATA%\ReleaseForge
std::wstring LocalAppDataDir();   // %LOCALAPPDATA%\ReleaseForge
std::wstring DocumentsDir();
std::wstring EnvVar(const wchar_t* name);
std::wstring FindInPath(const std::wstring& exeName);   // ricerca nel PATH (anche da registro, aggiornato)

std::wstring LastErrorMessage(DWORD err = GetLastError());
bool SetClipboardText(HWND owner, const std::wstring& text);
void OpenInExplorer(const std::wstring& path, bool select = false);
void OpenUrl(const std::wstring& url);

std::string NowTimeHMS();
int64_t UnixNow();

}  // namespace rf
