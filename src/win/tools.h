// ReleaseForge - individuazione e installazione delle dipendenze esterne.
//   FFmpeg (+ ffprobe): screenshot, grafico bitrate, report di ripiego
//   MediaInfo completo: MediaInfo.dll dell'installazione GUI (vedi mediainfo.h)
//   AVInaptic (opzionale): solo se il template usa {{ REPORT_AVINAPTIC }}
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace rf {

std::wstring FindFfmpeg(const std::wstring& configured);     // "" se assente
std::wstring FindFfprobe(const std::wstring& ffmpegExe);       // accanto a ffmpeg o nel PATH
std::wstring FindAvinaptic(const std::wstring& configured);
std::string ToolVersion(const std::wstring& exe);              // prima riga di "-version"

using ToolLog = std::function<void(const std::string&)>;
using ToolProgress = std::function<void(uint64_t done, uint64_t total)>;

// Scarica FFmpeg essentials (gyan.dev) in %LOCALAPPDATA%\ReleaseForge\tools\ffmpeg con verifica SHA-256.
// Restituisce il percorso di ffmpeg.exe; lancia std::runtime_error.
std::wstring DownloadFfmpeg(const std::atomic<bool>* cancel, const ToolLog& log, const ToolProgress& progress);

// Installa MediaInfo completo (GUI) con winget, che verifica l'hash dal proprio manifest.
// Restituisce true se dopo l'installazione la DLL risulta presente.
bool InstallMediaInfoWithWinget(const ToolLog& log, std::string& error);
bool WingetAvailable();

}  // namespace rf
