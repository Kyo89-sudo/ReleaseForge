// ReleaseForge - servizi online: TheMovieDB (metadati) e host immagini (ImgBB, Imgur).
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "../core/jinja.h"

namespace rf {

struct TmdbHit {
    int id = 0;
    std::string type;    // movie | tv
    std::string title, originalTitle, year, overview;
};

struct TmdbData {
    std::string type;
    int id = 0;
    std::string title, originalTitle, year, posterUrl, tmdbUrl, director, country, genre, plot, trailer;
    int runtimeMinutes = 0;
    std::vector<std::pair<std::string, std::string>> cast;  // nome, personaggio
    std::vector<std::string> missing;                       // campi assenti su TMDB
};

// Cerca per tipo (movie/tv) con anno; senza risultati riprova senza anno e con la parte prima di " - ".
std::vector<TmdbHit> TmdbSearch(const std::string& apiKey, const std::string& type, const std::string& title,
                                const std::string& year, const std::atomic<bool>* cancel);
TmdbData TmdbDetails(const std::string& apiKey, const std::string& type, int id, const std::atomic<bool>* cancel);

// "https://www.themoviedb.org/movie/123-titolo", "tv/123", "123": restituisce id e (se presente) tipo.
bool ParseTmdbReference(const std::string& text, int& id, std::string& type, std::string& error);

struct UploadedImage {
    std::string full, thumb;   // BBCode: [url=full][img]thumb[/img][/url]
};

struct ImageHostConfig {
    std::string imgbbKey;
    std::string cheveretoUrl;   // es. https://freeimage.host/api/1/upload
    std::string cheveretoKey;
};

// Host supportati (id, etichetta). "none" = nessun upload.
//   imgbb (chiave), imgur (anonimo), pixhost (anonimo), catbox (anonimo, senza miniature),
//   chevereto (qualsiasi sito Chevereto API v1: freeimage.host, lensdump, ptscreens, onlyimage...)
const std::vector<std::pair<std::string, std::string>>& ImageHosts();
std::string ImageHostLabel(const std::string& id);

// Lancia std::runtime_error con messaggio leggibile.
UploadedImage UploadImage(const std::string& host, const ImageHostConfig& cfg, const std::wstring& file,
                          const std::atomic<bool>* cancel);

}  // namespace rf
