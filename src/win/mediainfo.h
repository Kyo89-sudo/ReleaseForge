// ReleaseForge - analisi file multimediali.
// Ordine di preferenza:
//   1. MediaInfo completo (GUI installata): MediaInfo.dll caricata direttamente, nessuna console;
//   2. MediaInfo.dll fornita a parte (pacchetto DLL) o MediaInfo CLI;
//   3. FFmpeg ("ffmpeg -i"), report ridotto.
#pragma once
#include <atomic>
#include <string>

#include "../core/naming.h"

namespace rf {

enum class MediaInfoBackend { None, Dll, Cli, Ffmpeg };

struct MediaInfoSetup {
    std::wstring mediaInfoPath;   // cartella, MediaInfo.dll, MediaInfo.exe (GUI o CLI) oppure vuoto = rilevamento
    std::wstring ffmpegPath;      // ffmpeg.exe per il ripiego
};

struct MediaReport {
    MediaInfoBackend backend = MediaInfoBackend::None;
    std::string text;             // report testuale (percorso completo sostituito dal nome file)
    MediaTracks tracks;
    std::string error;
};

// Stato del rilevamento per la finestra Strumenti.
struct MediaInfoStatus {
    MediaInfoBackend backend = MediaInfoBackend::None;
    std::wstring location;        // DLL o EXE effettivamente usato
    std::string version;          // es. "MediaInfoLib - v26.10"
    std::string note;             // spiegazione per l'utente
};

MediaInfoStatus DetectMediaInfo(const MediaInfoSetup& setup);
MediaReport AnalyzeMedia(const std::wstring& file, const MediaInfoSetup& setup, const std::atomic<bool>* cancel);

// Cartella dell'installazione completa di MediaInfo (registro App Paths / Program Files), "" se assente.
std::wstring FindInstalledMediaInfoDir();

}  // namespace rf
