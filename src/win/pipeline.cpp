#include "pipeline.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <regex>
#include <stdexcept>

#include "../resource.h"
#include "../core/jinja.h"
#include "../core/naming.h"
#include "../core/strutil.h"
#include "../core/torrent.h"
#include "bitrate.h"
#include "http.h"
#include "mediainfo.h"
#include "process.h"
#include "tools.h"
#include "winutil.h"

#ifndef BCRYPT_HASH_REUSABLE_FLAG
#define BCRYPT_HASH_REUSABLE_FLAG 0x00000020
#endif

namespace rf {

const char* ReleaseTypeLabel(ReleaseType t) {
    switch (t) {
        case ReleaseType::Movie: return "Film (file singolo)";
        case ReleaseType::MovieFolder: return "Film + extra (cartella)";
        case ReleaseType::TvSingle: return "Serie TV - stagione singola";
        case ReleaseType::TvMulti: return "Serie TV - serie completa";
        default: return "Automatico";
    }
}

static std::vector<std::wstring> VideosIn(const std::wstring& dir) {
    std::vector<std::wstring> v;
    for (auto& f : ListFiles(dir)) if (IsVideoFile(f) && !IsJunkFile(PathFileName(f))) v.push_back(f);
    return v;
}

ReleaseType DetectReleaseType(const std::wstring& source) {
    if (FileExists(source)) return ReleaseType::Movie;
    if (!DirExists(source)) return ReleaseType::Auto;
    size_t direct = VideosIn(source).size();
    if (direct == 1) return ReleaseType::MovieFolder;  // film + cartelle Extra/Featurettes
    if (direct > 1) return ReleaseType::TvSingle;
    for (auto& sub : ListSubdirs(source)) if (!VideosIn(sub).empty()) return ReleaseType::TvMulti;
    return ReleaseType::MovieFolder;
}

namespace {

class Hasher {
public:
    Hasher() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA1_ALGORITHM, nullptr, BCRYPT_HASH_REUSABLE_FLAG) != 0) {
            alg_ = nullptr;
            return;
        }
        if (BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG) != 0) hash_ = nullptr;
    }
    ~Hasher() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    void Hash(const unsigned char* d, size_t n, unsigned char out[20]) {
        if (hash_ && BCryptHashData(hash_, const_cast<PUCHAR>(d), ULONG(n), 0) == 0 && BCryptFinishHash(hash_, out, 20, 0) == 0) return;
        Sha1::Hash(d, n, out);  // ripiego portabile
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

void CheckCancel(const std::atomic<bool>& c) {
    if (c.load()) throw Cancelled();
}

int64_t TimestampSeconds(const std::string& t) {
    static const std::regex re(R"(^\s*(\d{1,2}):(\d{2}):(\d{2})\s*$)");
    std::smatch m;
    if (!std::regex_match(t, m, re)) return -1;
    int mm = std::stoi(m[2]), ss = std::stoi(m[3]);
    if (mm > 59 || ss > 59) return -1;
    return std::stoll(m[1]) * 3600 + mm * 60 + ss;
}

std::string FormatEta(double seconds) {
    if (seconds < 0 || seconds > 360000) return "--:--";
    char b[32];
    int s = int(seconds);
    if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(b, sizeof b, "%02d:%02d", s / 60, s % 60);
    return b;
}

void TreeLines(const std::wstring& dir, int level, std::vector<std::string>& out, const std::function<std::string(const std::wstring&)>& info) {
    std::string indent(size_t(4 * level), ' ');
    out.push_back(indent + U8(PathFileName(dir)) + "/");
    std::string sub(size_t(4 * (level + 1)), ' ');
    for (auto& f : ListFiles(dir)) {
        if (IsJunkFile(PathFileName(f))) continue;
        out.push_back(sub + U8(PathFileName(f)) + " (" + info(f) + ")");
    }
    for (auto& d : ListSubdirs(dir)) TreeLines(d, level + 1, out, info);
}

}  // namespace

JobResult RunRelease(const JobInput& in, JobHost& host, const std::atomic<bool>& cancel) {
    JobResult res;
    auto log = [&](const std::string& s) { host.Log(s); };
    auto warn = [&](const std::string& s) { res.warnings.push_back(s); host.Log("ATTENZIONE: " + s); };

    std::wstring source = PathTrimSlashes(FullPath(in.source));
    bool isFile = FileExists(source);
    if (!isFile && !DirExists(source)) throw std::runtime_error("Sorgente inesistente: " + U8(source));
    ReleaseType type = in.type == ReleaseType::Auto ? DetectReleaseType(source) : in.type;
    if (isFile && type != ReleaseType::Movie) {
        warn("Il tipo selezionato richiede una cartella: uso \"Film (file singolo)\".");
        type = ReleaseType::Movie;
    }
    if (!isFile && type == ReleaseType::Movie) type = DetectReleaseType(source);
    log(std::string("Tipo release: ") + ReleaseTypeLabel(type) + (in.type == ReleaseType::Auto ? " (rilevato)" : ""));
    bool folderRelease = type != ReleaseType::Movie;
    bool tv = type == ReleaseType::TvSingle || type == ReleaseType::TvMulti;

    // File da analizzare (come makerelease get_file)
    std::wstring mainFile;
    if (type == ReleaseType::Movie) mainFile = source;
    else if (type == ReleaseType::TvMulti) {
        for (auto& sub : ListSubdirs(source)) {
            auto v = VideosIn(sub);
            if (!v.empty()) { mainFile = v[0]; break; }
        }
    } else {
        auto v = VideosIn(source);
        if (!v.empty()) mainFile = v[0];
    }
    if (mainFile.empty() || !IsVideoFile(mainFile)) throw std::runtime_error("Nessun file video (mkv, mp4, avi, ...) trovato nella sorgente per il tipo scelto.");
    std::wstring name = isFile ? PathStem(source) : PathFileName(source);
    log("Nome: " + U8(name));
    log("File analizzato: " + U8(PathFileName(mainFile)));

    // 1. TMDB
    host.Progress(2, "TheMovieDB", "");
    std::string apiKey = in.settings.tmdbKey;
    std::string mediaType = tv ? "tv" : "movie";
    int tmdbId = 0;
    std::string refError, refType;
    if (ParseTmdbReference(in.tmdbRef, tmdbId, refType, refError)) {
        if (!refType.empty() && refType != mediaType) {
            log("TMDB: il riferimento indica " + refType + ", uso quel tipo.");
            mediaType = refType;
        }
    } else if (!refError.empty()) {
        throw std::runtime_error(refError);
    }
    TitleYear parsed = ParseTitleYear(U8(name));
    while (tmdbId == 0) {
        CheckCancel(cancel);
        log("1. Ricerca TMDB (" + mediaType + "): \"" + parsed.title + "\"" + (parsed.year.empty() ? "" : " anno " + parsed.year));
        auto hits = TmdbSearch(apiKey, mediaType, parsed.title, parsed.year, &cancel);
        if (hits.size() == 1) {
            tmdbId = hits[0].id;
            log("TMDB: risultato unico " + hits[0].title + " (" + hits[0].year + ")");
            break;
        }
        TmdbChoice c = host.ChooseTmdb(hits, parsed.title + (parsed.year.empty() ? "" : " (" + parsed.year + ")"));
        if (c.cancelled) throw Cancelled();
        if (c.index >= 0 && c.index < int(hits.size())) { tmdbId = hits[size_t(c.index)].id; break; }
        if (!ParseTmdbReference(c.reference, tmdbId, refType, refError)) throw std::runtime_error(refError.empty() ? "Nessun titolo selezionato." : refError);
        if (!refType.empty()) mediaType = refType;
    }
    CheckCancel(cancel);
    TmdbData md = TmdbDetails(apiKey, mediaType, tmdbId, &cancel);
    log("TMDB: " + md.title + " (" + md.year + ") - " + md.tmdbUrl);
    if (!md.missing.empty()) warn("Da completare a mano, non trovati su TMDB: " + Join(md.missing, ", "));

    // 2. Analisi tecnica
    host.Progress(8, "Analisi MediaInfo", U8(PathFileName(mainFile)));
    CheckCancel(cancel);
    MediaInfoSetup mis{in.settings.mediaInfoPath, FindFfmpeg(in.settings.ffmpegPath)};
    MediaReport report = AnalyzeMedia(mainFile, mis, &cancel);
    switch (report.backend) {
        case MediaInfoBackend::Dll: log("2. Report generato con MediaInfo completo (MediaInfo.dll)."); break;
        case MediaInfoBackend::Cli: log("2. Report generato con MediaInfo CLI."); break;
        case MediaInfoBackend::Ffmpeg: warn("Report generato con FFmpeg: installa MediaInfo completo per il report standard."); break;
        default: warn("Report tecnico non disponibile: " + report.error);
    }
    if (!report.error.empty() && report.backend != MediaInfoBackend::None) log("Note analisi: " + report.error);

    std::string forumTitle = BuildReleaseTitle(md.title, md.year, report.tracks, in.crew);
    res.forumTitle = forumTitle;
    log("TITOLO: " + forumTitle);

    // Cartella di output
    std::wstring outDir = PathJoin(in.outputRoot, W(SanitizeFileName(U8(name))) + L"_files");
    if (DirExists(outDir)) log("Cartella esistente, i file verranno aggiornati: " + U8(outDir));
    if (!CreateDirs(outDir)) throw std::runtime_error("Impossibile creare la cartella di output " + U8(outDir) + ": " + U8(LastErrorMessage()));
    res.outputDir = outDir;
    std::wstring reportFile = PathJoin(outDir, L"report_mediainfo.txt");
    if (in.resume && FileExists(reportFile) && FileMtimeUnix(reportFile) >= FileMtimeUnix(mainFile)) {
        // Come makerelease: riusa il report già presente (eventuali modifiche manuali restano).
        std::string existing = Trim(NormalizeNewlines(ReadTextFile(reportFile)));
        if (!existing.empty()) {
            report.text = existing;
            log("2. report_mediainfo.txt già presente: riutilizzato (i dati tecnici per il titolo vengono comunque letti dal file).");
        }
    } else if (!report.text.empty()) {
        WriteFileBytes(reportFile, ToCrlf(report.text));
    }
    WriteFileBytes(PathJoin(outDir, L"titolo.txt"), forumTitle + "\r\n");

    // Template attivi e variabili richieste
    std::vector<std::wstring> templates = ListTemplates();
    if (templates.empty()) throw std::runtime_error("Nessun template in " + U8(ConfigDir()) + " (template*.jinja).");
    std::vector<std::pair<std::wstring, std::string>> sources;
    bool needBitrate = false, needAvinaptic = false;
    for (auto& t : templates) {
        std::string src = StripUtf8Bom(ReadTextFile(t));
        try {
            ValidateTemplate(src);
        } catch (TemplateError& e) {
            throw std::runtime_error(U8(PathFileName(t)) + ": " + e.what());
        }
        needBitrate = needBitrate || TemplateReferences(src, "BITRATE_GRAPH");
        needAvinaptic = needAvinaptic || TemplateReferences(src, "REPORT_AVINAPTIC");
        sources.emplace_back(t, src);
    }

    std::string avinaptic;
    if (needAvinaptic) {
        std::wstring av = FindAvinaptic(in.settings.avinapticPath);
        if (av.empty()) warn("Il template usa REPORT_AVINAPTIC ma avinaptic2-cli.exe non è stato trovato.");
        else {
            log("2b. Report AVInaptic...");
            std::wstring outFile = PathJoin(outDir, L"report_avinaptic.txt");
            ProcessOptions o;
            o.cancel = &cancel;
            RunProcess(av, {L"--drf", mainFile, L"--out=" + outFile}, o);
            avinaptic = NormalizeNewlines(ReadTextFile(outFile));
        }
    }

    // 3. Torrent
    uint64_t totalSize = 0;
    std::vector<DirEntry> files;
    if (isFile) files.push_back({source, PathFileName(source), FileSize(source)});
    else
        for (auto& e : ListFilesRecursive(source))
            if (!IsJunkFile(PathFileName(e.relPath))) files.push_back(e);
    for (auto& f : files) totalSize += f.size;
    if (files.empty() || totalSize == 0) throw std::runtime_error("La sorgente non contiene dati.");

    if (in.torrent) {
        CheckCancel(cancel);
        if (in.trackers.empty()) throw std::runtime_error("Nessun tracker valido: compila la scheda Tracker.");
        if (!isFile && (name.empty() || name.find(L':') != std::wstring::npos))
            throw std::runtime_error("Non si può creare un torrent dalla radice di un'unità: seleziona la cartella della release.");
        uint32_t piece = in.pieceSizeKiB > 0 ? uint32_t(in.pieceSizeKiB) * 1024u : PieceSizeForTotal(totalSize);
        TorrentSpec spec;
        spec.name = U8(isFile ? PathFileName(source) : name);
        spec.singleFile = isFile;
        for (auto& f : files) {
            TorrentEntry e;
            e.length = f.size;
            if (!isFile) for (auto& part : Split(U8(f.relPath), '\\')) e.path.push_back(part);
            spec.files.push_back(e);
        }
        spec.pieceLength = piece;
        spec.trackers = in.trackers;
        spec.isPrivate = in.privateTorrent;
        spec.createdBy = "ReleaseForge " RF_VERSION_STR;
        spec.creationDate = UnixNow();
        res.torrentFile = PathJoin(outDir, W(SanitizeFileName(U8(name))) + L".torrent");

        // Ripresa: riusa gli hash di un .torrent esistente se descrive esattamente gli stessi dati.
        bool reused = false;
        if (in.resume && FileExists(res.torrentFile)) {
            std::string old, why;
            ParsedTorrent pt;
            if (!ReadFileBytes(res.torrentFile, old) || !ParseTorrent(old, pt, &why)) why = why.empty() ? "illeggibile" : why;
            else if (pt.name != spec.name || pt.singleFile != spec.singleFile || pt.files.size() != spec.files.size()) why = "contenuto diverso";
            else if (pt.pieceLength != spec.pieceLength) why = "dimensione pezzi diversa";
            else if (pt.isPrivate != spec.isPrivate) why = "flag privato diverso";
            else if (pt.source != spec.source) why = "campo source diverso";
            else {
                for (size_t k = 0; k < files.size() && why.empty(); ++k) {
                    if (pt.files[k].length != spec.files[k].length || pt.files[k].path != spec.files[k].path) why = "file diversi";
                    else if (FileMtimeUnix(files[k].fullPath) > pt.creationDate) why = U8(files[k].relPath) + " modificato dopo la creazione";
                }
                uint64_t expectedPieces = (totalSize + piece - 1) / piece;
                if (why.empty() && pt.pieces.size() != expectedPieces * 20) why = "numero di pezzi incoerente";
            }
            if (why.empty()) {
                spec.pieces = pt.pieces;
                spec.creationDate = pt.creationDate;
                reused = true;
                log("3. Torrent esistente riutilizzato senza ricalcolare gli hash (stessi file); aggiornati i tracker.");
            } else {
                log("3. Torrent esistente non riutilizzabile (" + why + "): ricalcolo.");
            }
        }
        if (!reused) {
        log("3. Creazione torrent: " + std::to_string(files.size()) + " file, " + SizeOf(totalSize) + ", pezzi da " + std::to_string(piece / 1024) + " KiB");
        Hasher hasher;
        PieceHasher ph(piece, [&](const unsigned char* d, size_t n, unsigned char out[20]) { hasher.Hash(d, n, out); });
        std::vector<char> buf(4u << 20);
        ULONGLONG started = GetTickCount64(), lastReport = 0;
        uint64_t done = 0;
        for (auto& f : files) {
            // Condivisione completa: il file può essere aperto anche da un client torrent o da un player.
            HANDLE h = CreateFileW(LongPath(f.fullPath).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("Impossibile leggere " + U8(f.fullPath) + ": " + U8(LastErrorMessage()));
            uint64_t fileDone = 0;
            DWORD read = 0;
            for (;;) {
                if (!ReadFile(h, buf.data(), DWORD(buf.size()), &read, nullptr)) {
                    DWORD err = GetLastError();
                    CloseHandle(h);
                    throw std::runtime_error("Errore di lettura su " + U8(f.relPath) + ": " + U8(LastErrorMessage(err)));
                }
                if (read == 0) break;
                if (cancel.load()) { CloseHandle(h); throw Cancelled(); }
                ph.Feed(buf.data(), read);
                done += read;
                fileDone += read;
                ULONGLONG now = GetTickCount64();
                if (now - lastReport > 250) {
                    lastReport = now;
                    double secs = (now - started) / 1000.0;
                    double rate = secs > 0 ? done / 1048576.0 / secs : 0;
                    double eta = rate > 0 ? (totalSize - done) / 1048576.0 / rate : -1;
                    char d[160];
                    std::snprintf(d, sizeof d, "%.0f / %.0f MiB  -  %.1f MiB/s  -  ~%s", done / 1048576.0, totalSize / 1048576.0, rate, FormatEta(eta).c_str());
                    host.Progress(15 + 45.0 * double(done) / double(totalSize), "Hashing SHA-1", d);
                }
            }
            CloseHandle(h);
            if (fileDone != f.size) throw std::runtime_error("Il file è cambiato durante la lettura: " + U8(f.relPath));
        }
        spec.pieces = ph.Finish();
        }
        TorrentResult tr = BuildTorrent(spec);
        if (!WriteFileBytes(res.torrentFile, tr.bencoded)) throw std::runtime_error("Scrittura .torrent fallita: " + U8(res.torrentFile));
        res.magnet = BuildMagnet(tr.infoHash, spec.name, totalSize, in.trackers);
        log(std::string(reused ? "Torrent aggiornato: " : "Torrent creato: ") + U8(PathFileName(res.torrentFile)) + "  info-hash " + tr.infoHash);
    } else {
        log("3. Torrent: saltato.");
    }

    // 4. Screenshot
    std::vector<std::wstring> shots;
    std::wstring ffmpeg = FindFfmpeg(in.settings.ffmpegPath);
    if (in.screenshots) {
        if (ffmpeg.empty()) warn("FFmpeg non trovato: screenshot saltati (Strumenti > Scarica FFmpeg).");
        else {
            log("4. Estrazione screenshot...");
            int64_t durSec = report.tracks.durationMs / 1000;
            bool anamorphic = !report.tracks.video.empty() && std::fabs(report.tracks.video[0].pixelAspect - 1.0) > 0.01 && report.tracks.video[0].pixelAspect > 0;
            size_t i = 0;
            for (auto& t : in.timestamps) {
                CheckCancel(cancel);
                ++i;
                int64_t sec = TimestampSeconds(t);
                if (sec < 0) { warn("Timestamp non valido (HH:MM:SS): " + t); continue; }
                if (durSec > 0 && sec >= durSec) { log("  " + Trim(t) + " oltre la durata del video: ignorato."); continue; }
                std::wstring out = PathJoin(outDir, W(ReplaceAll(Trim(t), ":", ".")) + L".png");
                std::vector<std::wstring> args = {L"-hide_banner", L"-loglevel", L"error", L"-y", L"-ss", W(Trim(t)), L"-i", mainFile, L"-frames:v", L"1"};
                if (anamorphic) { args.push_back(L"-vf"); args.push_back(L"scale=iw*sar:ih,setsar=1"); }
                args.push_back(out);
                ProcessOptions o;
                o.cancel = &cancel;
                o.timeoutMs = 120000;
                ProcessResult r = RunProcess(ffmpeg, args, o);
                if (r.exitCode != 0 || !FileExists(out) || FileSize(out) == 0) {
                    warn("Screenshot " + Trim(t) + " non riuscito: " + Trim(AnsiToUtf8(r.err)) + r.startError);
                    continue;
                }
                shots.push_back(out);
                host.Progress(60 + 12.0 * double(i) / double(std::max<size_t>(1, in.timestamps.size())), "Screenshot", Trim(t));
            }
            log("  " + std::to_string(shots.size()) + " screenshot salvati.");
        }
    }

    // 5. Grafico bitrate (solo se il template lo usa, come makerelease)
    std::wstring bitratePng;
    if (needBitrate && in.bitrateGraph) {
        std::wstring ffprobe = FindFfprobe(ffmpeg);
        if (ffprobe.empty()) warn("ffprobe non trovato: grafico bitrate saltato.");
        else {
            bitratePng = PathJoin(outDir, L"bitrate.png");
            if (in.resume && FileExists(bitratePng) && FileMtimeUnix(bitratePng) >= FileMtimeUnix(mainFile)) {
                log("5. bitrate.png già presente: riutilizzato.");
            } else {
            log("5. Grafico bitrate (lettura completa del video, può richiedere qualche minuto)...");
            double dur = std::max<double>(1.0, report.tracks.durationMs / 1000.0);
            try {
                CreateBitrateGraph(ffprobe, mainFile, bitratePng, &cancel, [&](double s) {
                    host.Progress(72 + 10.0 * std::min(1.0, s / dur), "Grafico bitrate", FormatEta(s) + " / " + FormatEta(dur));
                });
                log("  bitrate.png creato.");
            } catch (Cancelled&) {
                throw;
            } catch (std::exception& e) {
                warn(std::string("Grafico bitrate non creato: ") + e.what());
                bitratePng.clear();
            }
            }
        }
    } else if (!needBitrate) {
        log("5. Grafico bitrate: il template non usa {{ BITRATE_GRAPH }}, saltato.");
    }

    // 6. Upload (host principale, poi host di riserva per le immagini che falliscono)
    std::string screenshotsCode, bitrateCode;
    std::string hostName = in.settings.imageHost;
    std::string fallback = in.settings.imageHostFallback == hostName ? "none" : in.settings.imageHostFallback;
    ImageHostConfig hostCfg{in.settings.imgbbKey, in.settings.cheveretoUrl, in.settings.cheveretoKey};
    auto upload = [&](const std::wstring& file) -> std::string {
        std::string firstError;
        for (const std::string& h : {hostName, fallback}) {
            if (h.empty() || h == "none") continue;
            CheckCancel(cancel);
            try {
                UploadedImage u = UploadImage(h, hostCfg, file, &cancel);
                if (!firstError.empty()) log("  " + U8(PathFileName(file)) + " caricato su " + ImageHostLabel(h) + " (riserva).");
                return "[url=" + u.full + "][img]" + u.thumb + "[/img][/url]";
            } catch (Cancelled&) {
                throw;
            } catch (std::exception& e) {
                if (firstError.empty()) firstError = e.what();
                log("  " + ImageHostLabel(h) + ": " + e.what());
            }
        }
        warn("Upload " + U8(PathFileName(file)) + " non riuscito: " + firstError);
        return "";
    };
    if (in.upload && hostName != "none" && (!shots.empty() || !bitratePng.empty())) {
        log("6. Caricamento immagini su " + ImageHostLabel(hostName) +
            (fallback != "none" ? " (riserva: " + ImageHostLabel(fallback) + ")" : "") + "...");
        std::vector<std::string> codes;
        size_t i = 0;
        for (auto& s : shots) {
            std::string code = upload(s);
            if (!code.empty()) codes.push_back(code);
            host.Progress(82 + 8.0 * double(++i) / double(shots.size()), "Upload immagini", U8(PathFileName(s)));
        }
        screenshotsCode = Join(codes, "\n");
        if (!bitratePng.empty()) bitrateCode = upload(bitratePng);
    } else if (!shots.empty()) {
        log("6. Upload disattivato: gli screenshot restano nella cartella di output.");
    }

    // 7. Contenuto (TREE) ed episodi
    host.Progress(92, "Generazione post", "");
    TValue::Map ctx;
    if (folderRelease) {
        MediaInfoSetup quick = mis;
        std::vector<std::string> tree;
        TreeLines(source, 0, tree, [&](const std::wstring& f) {
            std::string size = SizeOf(FileSize(f));
            if (!IsVideoFile(f)) return size;
            MediaReport r = (f == mainFile) ? report : AnalyzeMedia(f, quick, &cancel);
            return RuntimeFromMinutes(r.tracks.durationMs / 60000) + ", " + size;
        });
        ctx["TREE"] = TValue(Join(tree, "\n"));
    }
    if (tv) {
        long long eps = 0;
        for (auto& f : files) if (IsVideoFile(f.fullPath)) ++eps;
        ctx["EP_COUNT"] = TValue(std::to_string(eps));
    }

    // 8. Post (un file per template, come makerelease)
    int64_t minutes = report.tracks.durationMs / 60000;
    if (minutes <= 0) minutes = md.runtimeMinutes;
    TValue::List cast;
    for (auto& c : md.cast) {
        TValue::Map p;
        p["name"] = TValue(c.first);
        p["actor"] = TValue(c.first);
        p["character"] = TValue(c.second);
        cast.push_back(TValue(p));
    }
    ctx["TMDB_URL"] = TValue(md.tmdbUrl);
    ctx["TITLE"] = TValue(md.title);
    ctx["YEAR"] = TValue(md.year);
    ctx["SIZE"] = TValue(SizeOf(totalSize));
    ctx["POSTER_URL"] = TValue(md.posterUrl);
    ctx["ORIGINAL_TITLE"] = TValue(md.originalTitle);
    ctx["DIRECTOR"] = TValue(md.director);
    ctx["RUNTIME"] = TValue(RuntimeFromMinutes(minutes));
    ctx["COUNTRY"] = TValue(md.country);
    ctx["GENRE"] = TValue(md.genre);
    ctx["CAST"] = TValue(cast);
    ctx["PLOT"] = TValue(md.plot.empty() ? "<NON TROVATO>" : md.plot);
    ctx["TRAILER"] = TValue(md.trailer.empty() ? "<NON TROVATO>" : md.trailer);
    ctx["SCREENSHOTS"] = TValue(screenshotsCode);
    ctx["BITRATE_GRAPH"] = TValue(bitrateCode);
    ctx["REPORT_MEDIAINFO"] = TValue(report.text);
    ctx["REPORT_AVINAPTIC"] = TValue(avinaptic);
    ctx["MAGNET"] = TValue(res.magnet);
    // Variabili aggiuntive di ReleaseForge (non presenti in makerelease)
    ctx["FORUM_TITLE"] = TValue(forumTitle);
    ctx["RELEASE_NAME"] = TValue(U8(name));
    ctx["CREW"] = TValue(in.crew);
    ctx["TMDB_ID"] = TValue(std::to_string(md.id));
    ctx["MEDIA_TYPE"] = TValue(md.type);

    for (auto& [path, src] : sources) {
        CheckCancel(cancel);
        PostOutput p;
        p.templateName = PathFileName(path);
        p.file = PathJoin(outDir, PostNameForTemplate(path));
        try {
            p.text = RenderTemplate(src, ctx);
        } catch (TemplateError& e) {
            throw std::runtime_error(U8(p.templateName) + ": " + e.what());
        }
        if (!WriteFileBytes(p.file, ToCrlf(p.text))) throw std::runtime_error("Scrittura fallita: " + U8(p.file));
        log("7. Post generato: " + U8(PathFileName(p.file)) + " (da " + U8(p.templateName) + ")");
        res.posts.push_back(std::move(p));
    }
    host.Progress(100, "Completato", U8(outDir));
    return res;
}

}  // namespace rf
