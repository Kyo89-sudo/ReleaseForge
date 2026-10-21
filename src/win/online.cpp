#include "online.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <algorithm>
#include <regex>
#include <stdexcept>

#include "../core/json.h"
#include "../core/strutil.h"
#include "http.h"
#include "settings.h"
#include "winutil.h"

namespace rf {

namespace {

bool IsBearer(const std::string& key) { return StartsWith(key, "eyJ") && std::count(key.begin(), key.end(), '.') >= 2; }

Json TmdbGet(const std::string& key, const std::string& pathAndQuery, const std::atomic<bool>* cancel) {
    std::string k = Trim(key);
    if (k.empty()) throw std::runtime_error("Chiave TMDB mancante: inseriscila in Impostazioni (API key v3 o API Read Access Token).");
    HttpRequest req;
    req.url = "https://api.themoviedb.org/3" + pathAndQuery;
    req.cancel = cancel;
    req.timeoutSeconds = 30;
    req.headers.push_back("Accept: application/json");
    if (IsBearer(k)) req.headers.push_back("Authorization: Bearer " + k);
    else req.url += (req.url.find('?') == std::string::npos ? "?" : "&") + std::string("api_key=") + UrlEncode(k);
    HttpResponse r = HttpSend(req);
    Json j;
    try { j = Json::Parse(r.body); } catch (...) {}
    if (r.status == 401) throw std::runtime_error("TMDB: chiave non valida o scaduta (401). Controlla Impostazioni.");
    if (r.status == 404) throw std::runtime_error("TMDB: titolo non trovato (404). Controlla ID e tipo (film/serie).");
    if (r.status < 200 || r.status >= 300) {
        std::string msg = j["status_message"].Str();
        throw std::runtime_error("TMDB ha risposto " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg));
    }
    return j;
}

std::string CountryName(const std::string& iso, const std::string& fallback) {
    static Json countries;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try { countries = Json::Parse(DefaultContent(DefaultFile::Countries)); } catch (...) {}
    }
    std::string n = countries[iso].Str();
    return n.empty() ? fallback : n;
}

std::vector<TmdbHit> DoSearch(const std::string& key, const std::string& type, const std::string& title, const std::string& year,
                              const std::atomic<bool>* cancel) {
    std::string q = "/search/" + type + "?language=it-IT&include_adult=false&query=" + UrlEncode(title);
    if (!year.empty()) q += (type == "tv" ? "&first_air_date_year=" : "&year=") + year;
    Json j = TmdbGet(key, q, cancel);
    std::vector<TmdbHit> out;
    for (auto& item : j["results"].Items()) {
        TmdbHit h;
        h.id = int(item["id"].Num());
        h.type = type;
        h.title = item[type == "tv" ? "name" : "title"].Str();
        h.originalTitle = item[type == "tv" ? "original_name" : "original_title"].Str();
        std::string date = item[type == "tv" ? "first_air_date" : "release_date"].Str();
        h.year = date.size() >= 4 ? date.substr(0, 4) : "";
        h.overview = item["overview"].Str();
        if (h.id > 0) out.push_back(h);
    }
    return out;
}

}  // namespace

std::vector<TmdbHit> TmdbSearch(const std::string& apiKey, const std::string& type, const std::string& title, const std::string& year,
                                const std::atomic<bool>* cancel) {
    auto r = DoSearch(apiKey, type, title, year, cancel);
    if (r.empty() && !year.empty()) r = DoSearch(apiKey, type, title, "", cancel);
    // makerelease: alcuni releaser aggiungono il titolo originale dopo "-"
    if (r.empty() && title.find('-') != std::string::npos) r = DoSearch(apiKey, type, Trim(title.substr(0, title.find('-'))), year, cancel);
    // Titolo con anno coincidente in testa
    if (!year.empty()) std::stable_sort(r.begin(), r.end(), [&](const TmdbHit& a, const TmdbHit& b) { return a.year == year && b.year != year; });
    return r;
}

TmdbData TmdbDetails(const std::string& key, const std::string& type, int id, const std::atomic<bool>* cancel) {
    std::string base = "/" + type + "/" + std::to_string(id);
    Json j = TmdbGet(key, base + "?language=it-IT&append_to_response=credits,videos&include_video_language=it,en,null", cancel);
    bool tv = type == "tv";
    TmdbData d;
    d.type = type;
    d.id = id;
    d.title = j[tv ? "name" : "title"].Str();
    d.originalTitle = j[tv ? "original_name" : "original_title"].Str();
    std::string date = j[tv ? "first_air_date" : "release_date"].Str();
    d.year = date.size() >= 4 ? date.substr(0, 4) : "";
    d.tmdbUrl = "https://www.themoviedb.org/" + type + "/" + std::to_string(id);
    std::string poster = j["poster_path"].Str();
    d.posterUrl = poster.empty() ? "https://i.imgur.com/iDhevbK.jpg" : "https://image.tmdb.org/t/p/w500" + poster;
    if (poster.empty()) d.missing.push_back("POSTER_URL");

    std::vector<std::string> countries;
    for (auto& c : j["production_countries"].Items()) countries.push_back(CountryName(c["iso_3166_1"].Str(), c["name"].Str()));
    if (countries.empty() && tv) for (auto& c : j["origin_country"].Items()) countries.push_back(CountryName(c.Str(), c.Str()));
    d.country = Join(countries, ", ");

    std::vector<std::string> genres;
    for (auto& g : j["genres"].Items()) genres.push_back(g["name"].Str());
    d.genre = Join(genres, ", ");

    std::vector<std::string> directors;
    for (auto& c : j["credits"]["crew"].Items())
        if (c["job"].Str() == "Director" && std::find(directors.begin(), directors.end(), c["name"].Str()) == directors.end())
            directors.push_back(c["name"].Str());
    if (directors.empty() && tv) for (auto& c : j["created_by"].Items()) directors.push_back(c["name"].Str());
    d.director = Join(directors, ", ");

    size_t n = 0;
    for (auto& c : j["credits"]["cast"].Items()) {
        if (n++ >= 10) break;  // makerelease: primi 10
        d.cast.emplace_back(c["name"].Str(), c["character"].Str());
    }

    auto pickTrailer = [](const Json& videos, const std::string& lang) -> std::string {
        for (auto& v : videos["results"].Items()) {
            if (v["site"].Str() == "YouTube" && v["type"].Str() == "Trailer" && !v["key"].Str().empty() &&
                (lang.empty() || v["iso_639_1"].Str() == lang))
                return "https://www.youtube.com/watch?v=" + v["key"].Str();
        }
        return "";
    };
    d.trailer = pickTrailer(j["videos"], "it");
    if (d.trailer.empty()) d.trailer = pickTrailer(j["videos"], "");
    d.runtimeMinutes = tv ? int(j["episode_run_time"][0].Num()) : int(j["runtime"].Num());
    d.plot = j["overview"].Str();
    if (d.plot.empty()) {
        try {
            Json en = TmdbGet(key, base + "?language=en-US", cancel);
            d.plot = en["overview"].Str();
        } catch (Cancelled&) {
            throw;
        } catch (...) {
        }
    }
    if (d.title.empty()) throw std::runtime_error("Risposta TMDB senza titolo.");
    if (d.plot.empty()) d.missing.push_back("PLOT");
    if (d.trailer.empty()) d.missing.push_back("TRAILER");
    if (d.director.empty()) d.missing.push_back("DIRECTOR");
    if (d.cast.empty()) d.missing.push_back("CAST");
    if (d.country.empty()) d.missing.push_back("COUNTRY");
    if (d.genre.empty()) d.missing.push_back("GENRE");
    return d;
}

bool ParseTmdbReference(const std::string& textIn, int& id, std::string& type, std::string& error) {
    std::string text = Trim(textIn);
    id = 0;
    type.clear();
    if (text.empty()) return false;
    static const std::regex url(R"(^(?:https?://)?(?:www\.)?themoviedb\.org/(movie|tv)/(\d+)(?:[-/?#].*)?$)", std::regex::icase);
    static const std::regex shortRef(R"(^(movie|tv|film|serie)[/: ]+(\d+)$)", std::regex::icase);
    static const std::regex num(R"(^(?:id:)?(\d+)$)", std::regex::icase);
    std::smatch m;
    if (std::regex_match(text, m, url)) { type = ToLower(m[1]); id = std::stoi(m[2]); }
    else if (std::regex_match(text, m, shortRef)) {
        std::string t = ToLower(m[1]);
        type = (t == "tv" || t == "serie") ? "tv" : "movie";
        id = std::stoi(m[2]);
    } else if (std::regex_match(text, m, num)) id = std::stoi(m[1]);
    else {
        error = "Riferimento TMDB non valido: usa un ID numerico o un URL themoviedb.org/movie/ID o /tv/ID.";
        return false;
    }
    if (id <= 0) { error = "ID TMDB non valido."; return false; }
    return true;
}

const std::vector<std::pair<std::string, std::string>>& ImageHosts() {
    static const std::vector<std::pair<std::string, std::string>> hosts = {
        {"imgbb", "ImgBB (chiave API)"},
        {"pixhost", "Pixhost (anonimo)"},
        {"imgur", "Imgur (anonimo)"},
        {"catbox", "Catbox (anonimo, senza miniature)"},
        {"chevereto", "Chevereto: freeimage.host, lensdump\u2026 (URL + chiave)"},
        {"none", "Nessun upload"},
    };
    return hosts;
}

std::string ImageHostLabel(const std::string& id) {
    for (auto& h : ImageHosts()) if (h.first == id) return h.second;
    return id;
}

UploadedImage UploadImage(const std::string& host, const ImageHostConfig& cfg, const std::wstring& file, const std::atomic<bool>* cancel) {
    const std::string& imgbbKey = cfg.imgbbKey;
    std::string data;
    if (!ReadFileBytes(file, data)) throw std::runtime_error("impossibile leggere " + U8(PathFileName(file)));
    double mb = data.size() / 1048576.0;
    std::string ext = U8(PathExtension(file));
    std::string mime = ext == ".jpg" || ext == ".jpeg" ? "image/jpeg" : "image/png";
    Multipart mp;
    HttpRequest req;
    req.method = "POST";
    req.cancel = cancel;
    req.timeoutSeconds = 180;
    UploadedImage out;
    if (host == "imgbb") {
        if (Trim(imgbbKey).empty()) throw std::runtime_error("chiave ImgBB mancante (Impostazioni)");
        if (mb > 32) throw std::runtime_error("immagine oltre 32 MB, limite ImgBB");
        req.url = "https://api.imgbb.com/1/upload?key=" + UrlEncode(Trim(imgbbKey));
        mp.AddFile("image", U8(PathFileName(file)), mime, data);
        req.headers.push_back(mp.ContentTypeHeader());
        req.body = mp.Finish();
        HttpResponse r = HttpSend(req);
        Json j;
        try { j = Json::Parse(r.body); } catch (...) {}
        if (r.status != 200 || j["data"]["url"].Str().empty()) {
            std::string msg = j["error"]["message"].Str();
            throw std::runtime_error("ImgBB " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg));
        }
        out.full = j["data"]["url"].Str();
        out.thumb = j["data"]["display_url"].Str();
        if (out.thumb.empty()) out.thumb = out.full;
    } else if (host == "imgur") {
        if (mb > 10) throw std::runtime_error("immagine oltre 10 MB, limite Imgur");
        req.url = "https://api.imgur.com/3/image";
        req.headers.push_back("Authorization: Client-ID bcad66c82e13805");  // client id anonimo di makerelease
        mp.AddFile("image", U8(PathFileName(file)), mime, data);
        mp.AddField("type", "file");
        req.headers.push_back(mp.ContentTypeHeader());
        req.body = mp.Finish();
        HttpResponse r = HttpSend(req);
        Json j;
        try { j = Json::Parse(r.body); } catch (...) {}
        if (r.status != 200 || !j["success"].Bool()) {
            std::string msg = j["data"]["error"].Str();
            throw std::runtime_error("Imgur " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg));
        }
        out.full = j["data"]["link"].Str();
        size_t dot = out.full.rfind('.');
        out.thumb = dot == std::string::npos ? out.full : out.full.substr(0, dot) + "l" + out.full.substr(dot);
    } else if (host == "pixhost") {
        // API pubblica di pixhost.to: content_type 0 = contenuto per tutti, miniatura max 350 px.
        if (mb > 10) throw std::runtime_error("immagine oltre 10 MB, limite Pixhost");
        req.url = "https://api.pixhost.to/images";
        mp.AddFile("img", U8(PathFileName(file)), mime, data);
        mp.AddField("content_type", "0");
        mp.AddField("max_th_size", "350");
        req.headers.push_back("Accept: application/json");
        req.headers.push_back(mp.ContentTypeHeader());
        req.body = mp.Finish();
        HttpResponse r = HttpSend(req);
        Json j;
        try { j = Json::Parse(r.body); } catch (...) {}
        if (r.status != 200 || j["th_url"].Str().empty()) throw std::runtime_error("Pixhost " + std::to_string(r.status));
        out.full = j["show_url"].Str();   // pagina dell'immagine (link standard per Pixhost)
        out.thumb = j["th_url"].Str();
        if (out.full.empty()) out.full = out.thumb;
    } else if (host == "catbox") {
        if (mb > 200) throw std::runtime_error("file oltre 200 MB, limite Catbox");
        req.url = "https://catbox.moe/user/api.php";
        mp.AddField("reqtype", "fileupload");
        mp.AddFile("fileToUpload", U8(PathFileName(file)), mime, data);
        req.headers.push_back(mp.ContentTypeHeader());
        req.body = mp.Finish();
        HttpResponse r = HttpSend(req);
        std::string url = Trim(r.body);
        if (r.status != 200 || !StartsWith(url, "https://")) throw std::runtime_error("Catbox " + std::to_string(r.status) + (url.size() < 200 ? ": " + url : ""));
        out.full = out.thumb = url;
    } else if (host == "chevereto") {
        std::string endpoint = Trim(cfg.cheveretoUrl), key = Trim(cfg.cheveretoKey);
        if (endpoint.empty()) throw std::runtime_error("URL API Chevereto mancante (Impostazioni)");
        if (!StartsWith(ToLower(endpoint), "https://")) throw std::runtime_error("l'URL API Chevereto deve iniziare con https://");
        req.url = endpoint;
        if (!key.empty()) {
            req.headers.push_back("X-API-Key: " + key);  // Chevereto v4
            mp.AddField("key", key);                      // Chevereto v3 / freeimage.host
        }
        mp.AddField("format", "json");
        mp.AddFile("source", U8(PathFileName(file)), mime, data);
        req.headers.push_back(mp.ContentTypeHeader());
        req.body = mp.Finish();
        HttpResponse r = HttpSend(req);
        Json j;
        try { j = Json::Parse(r.body); } catch (...) {}
        const Json& img = j["image"].IsObject() ? j["image"] : j["data"];
        out.full = img["url"].Str();
        if (out.full.empty()) out.full = img["image"]["url"].Str();
        out.thumb = img["medium"]["url"].Str();
        if (out.thumb.empty()) out.thumb = img["thumb"]["url"].Str();
        if (out.thumb.empty()) out.thumb = out.full;
        if (r.status != 200 || out.full.empty()) {
            std::string msg = j["error"]["message"].Str();
            if (msg.empty()) msg = j["status_txt"].Str();
            throw std::runtime_error("Chevereto " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg));
        }
    } else {
        throw std::runtime_error("host immagini non configurato");
    }
    return out;
}

}  // namespace rf
