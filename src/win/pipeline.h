// ReleaseForge - flusso completo di una release (thread di lavoro).
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "online.h"
#include "settings.h"

namespace rf {

enum class ReleaseType { Auto = 0, Movie = 1, MovieFolder = 2, TvSingle = 3, TvMulti = 4 };
const char* ReleaseTypeLabel(ReleaseType t);
ReleaseType DetectReleaseType(const std::wstring& source);

struct JobInput {
    std::wstring source;
    ReleaseType type = ReleaseType::Auto;
    std::string tmdbRef;           // ID/URL opzionale
    std::string crew;
    std::wstring outputRoot;
    bool screenshots = true, torrent = true, upload = true, bitrateGraph = true, privateTorrent = false;
    bool resume = true;            // riusa .torrent, report_mediainfo.txt e bitrate.png se validi
    TorrentFormat torrentFormat = TorrentFormat::Hybrid;
    int pieceSizeKiB = 0;
    std::vector<std::string> trackers;
    std::vector<std::string> timestamps;
    Settings settings;             // chiavi, percorsi strumenti, host immagini
};

struct PostOutput {
    std::wstring templateName, file;
    std::string text;
};

struct JobResult {
    std::wstring outputDir;
    std::string forumTitle;
    std::string magnet;
    std::wstring torrentFile;
    std::vector<PostOutput> posts;
    std::vector<std::string> warnings;
};

struct TmdbChoice {
    bool cancelled = true;
    int index = -1;          // indice in hits
    std::string reference;   // oppure ID/URL digitato
};

class JobHost {
public:
    virtual ~JobHost() = default;
    virtual void Log(const std::string& line) = 0;
    virtual void Progress(double percent, const std::string& stage, const std::string& details) = 0;
    // Chiamata dal thread di lavoro: mostra la scelta sul thread UI e attende.
    virtual TmdbChoice ChooseTmdb(const std::vector<TmdbHit>& hits, const std::string& searched) = 0;
};

// Lancia std::exception con messaggio per l'utente, Cancelled se annullato.
JobResult RunRelease(const JobInput& in, JobHost& host, const std::atomic<bool>& cancel);

}  // namespace rf
