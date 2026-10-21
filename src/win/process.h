// ReleaseForge - esecuzione di strumenti esterni (ffmpeg, ffprobe, MediaInfo CLI, avinaptic, winget)
// senza finestra console, con annullamento e terminazione dell'albero dei processi (Job Object).
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace rf {

struct ProcessResult {
    int exitCode = -1;
    std::string out;   // stdout (byte grezzi)
    std::string err;   // stderr (byte grezzi)
    bool started = false;
    std::string startError;
};

struct ProcessOptions {
    std::wstring workingDir;
    const std::atomic<bool>* cancel = nullptr;
    unsigned timeoutMs = 0;           // 0 = nessun limite
    bool showWindow = false;          // winget/installer interattivi
    bool lowPriority = false;         // hashing/analisi lunghe in background
    std::function<void(const std::string& chunk)> onStdout;  // streaming opzionale
};

std::wstring QuoteArg(const std::wstring& arg);   // regole CommandLineToArgvW
ProcessResult RunProcess(const std::wstring& exe, const std::vector<std::wstring>& args, const ProcessOptions& opt = {});

}  // namespace rf
