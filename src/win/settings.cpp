#include "settings.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <algorithm>

#include "../core/json.h"
#include "../core/strutil.h"
#include "../resource.h"
#include "winutil.h"

namespace rf {

std::wstring SettingsPath() { return PathJoin(AppDataDir(), L"settings.json"); }
std::wstring ConfigDir() { return PathJoin(AppDataDir(), L"config"); }
std::wstring DefaultOutputDir() { return PathJoin(PathJoin(DocumentsDir(), L"ReleaseForge"), L"Output"); }
std::wstring EffectiveOutputDir(const Settings& s) {
    std::wstring o = s.outputDir;
    while (!o.empty() && (o.back() == L' ')) o.pop_back();
    return o.empty() ? DefaultOutputDir() : o;
}

static std::string Str(const Json& j, const char* k, const std::string& def) { return j.Has(k) ? j[k].Str() : def; }
static bool Bool(const Json& j, const char* k, bool def) { return j.Has(k) ? j[k].Bool() : def; }
static int Int(const Json& j, const char* k, int def) { return j.Has(k) ? int(j[k].Num()) : def; }

Settings Settings::Load() {
    Settings s;
    std::string text = ReadTextFile(SettingsPath());
    if (text.empty()) {
        // Importazione delle impostazioni legacy: %APPDATA%\MakeReleaseWin\settings.json
        std::string old = ReadTextFile(PathJoin(PathParent(AppDataDir()), L"MakeReleaseWin\\settings.json"));
        if (!old.empty()) {
            try {
                Json j = Json::Parse(old);
                s.tmdbKey = j["TmdbApiKey"].Str();
                s.imgbbKey = j["ImgBbkApiKey"].Str();
                s.outputDir = W(j["OutputDirectory"].Str());
            } catch (...) {}
        }
        return s;
    }
    try {
        Json j = Json::Parse(text);
        s.tmdbKey = Str(j, "tmdbKey", "");
        s.imgbbKey = Str(j, "imgbbKey", "");
        s.imageHost = Str(j, "imageHost", "imgbb");
        s.imageHostFallback = Str(j, "imageHostFallback", "pixhost");
        s.cheveretoUrl = Str(j, "cheveretoUrl", s.cheveretoUrl);
        s.cheveretoKey = Str(j, "cheveretoKey", "");
        s.ffmpegPath = W(Str(j, "ffmpegPath", ""));
        s.mediaInfoPath = W(Str(j, "mediaInfoPath", ""));
        s.avinapticPath = W(Str(j, "avinapticPath", ""));
        s.outputDir = W(Str(j, "outputDir", ""));
        s.crew = Str(j, "crew", "");
        s.privateTorrent = Bool(j, "privateTorrent", false);
        s.pieceSizeKiB = Int(j, "pieceSizeKiB", 0);
        s.releaseType = std::clamp(Int(j, "releaseType", 0), 0, 4);
        s.screenshots = Bool(j, "screenshots", true);
        s.torrent = Bool(j, "torrent", true);
        s.upload = Bool(j, "upload", true);
        s.bitrateGraph = Bool(j, "bitrateGraph", true);
        s.resume = Bool(j, "resume", true);
        s.askBeforeOverwrite = Bool(j, "askBeforeOverwrite", true);
        s.winX = Int(j, "winX", 0);
        s.winY = Int(j, "winY", 0);
        s.winW = Int(j, "winW", 0);
        s.winH = Int(j, "winH", 0);
        s.winMax = Bool(j, "winMax", false);
        s.toolsPromptShown = Bool(j, "toolsPromptShown", false);
        s.trackerListsAuto = false;
        s.trackerListsMax = Int(j, "trackerListsMax", 8);
        s.trackerListsLast = j.Has("trackerListsLast") ? (long long)j["trackerListsLast"].Num() : 0;
    } catch (...) {
        // File corrotto: si riparte dai valori predefiniti (il file verrà riscritto al salvataggio).
    }
    return s;
}

bool Settings::Save() const {
    auto q = [](const std::string& v) { return Json::Quote(v); };
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    std::string j = "{\n";
    j += "  \"tmdbKey\": " + q(tmdbKey) + ",\n";
    j += "  \"imgbbKey\": " + q(imgbbKey) + ",\n";
    j += "  \"imageHost\": " + q(imageHost) + ",\n";
    j += "  \"imageHostFallback\": " + q(imageHostFallback) + ",\n";
    j += "  \"cheveretoUrl\": " + q(cheveretoUrl) + ",\n";
    j += "  \"cheveretoKey\": " + q(cheveretoKey) + ",\n";
    j += "  \"ffmpegPath\": " + q(U8(ffmpegPath)) + ",\n";
    j += "  \"mediaInfoPath\": " + q(U8(mediaInfoPath)) + ",\n";
    j += "  \"avinapticPath\": " + q(U8(avinapticPath)) + ",\n";
    j += "  \"outputDir\": " + q(U8(outputDir)) + ",\n";
    j += "  \"crew\": " + q(crew) + ",\n";
    j += "  \"privateTorrent\": " + b(privateTorrent) + ",\n";
    j += "  \"pieceSizeKiB\": " + std::to_string(pieceSizeKiB) + ",\n";
    j += "  \"releaseType\": " + std::to_string(releaseType) + ",\n";
    j += "  \"screenshots\": " + b(screenshots) + ",\n";
    j += "  \"torrent\": " + b(torrent) + ",\n";
    j += "  \"upload\": " + b(upload) + ",\n";
    j += "  \"bitrateGraph\": " + b(bitrateGraph) + ",\n";
    j += "  \"resume\": " + b(resume) + ",\n";
    j += "  \"askBeforeOverwrite\": " + b(askBeforeOverwrite) + ",\n";
    j += "  \"winX\": " + std::to_string(winX) + ",\n";
    j += "  \"winY\": " + std::to_string(winY) + ",\n";
    j += "  \"winW\": " + std::to_string(winW) + ",\n";
    j += "  \"winH\": " + std::to_string(winH) + ",\n";
    j += "  \"winMax\": " + b(winMax) + ",\n";
    j += "  \"toolsPromptShown\": " + b(toolsPromptShown) + ",\n";
    j += "  \"trackerListsAuto\": " + b(trackerListsAuto) + ",\n";
    j += "  \"trackerListsMax\": " + std::to_string(trackerListsMax) + ",\n";
    j += "  \"trackerListsLast\": " + std::to_string(trackerListsLast) + "\n";
    j += "}\n";
    CreateDirs(AppDataDir());
    return WriteFileBytes(SettingsPath(), j);
}

std::string DefaultTrackerLists() {
    return "# Fonti per importare tracker nell’elenco visibile (un URL per riga).\n"
           "# Le righe con # sono disattivate: togli # davanti all’URL per attivare la fonte.\n"
           "# NewTrackon stable: tracker con uptime >95% e almeno 10 giorni di storico.\n"
           "# https://newtrackon.com/api/stable\n"
           "# https://ngosang.github.io/trackerslist/trackers_best.txt\n"
           "# https://ngosang.github.io/trackerslist/trackers_best_ip.txt\n"
           "# https://raw.githubusercontent.com/XIU2/TrackersListCollection/master/best.txt\n";
}

std::string DefaultContent(DefaultFile f) {
    int id = 0;
    switch (f) {
        case DefaultFile::Template: id = IDR_DEFAULT_TEMPLATE; break;
        case DefaultFile::Trackers: id = IDR_DEFAULT_TRACKERS; break;
        case DefaultFile::Screenshots: id = IDR_DEFAULT_SCREENSHOTS; break;
        case DefaultFile::Countries: id = IDR_COUNTRIES; break;
        case DefaultFile::TemplateClassic: id = IDR_TEMPLATE_CLASSIC; break;
    }
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!res) return "";
    HGLOBAL g = LoadResource(nullptr, res);
    DWORD size = SizeofResource(nullptr, res);
    const char* p = g ? static_cast<const char*>(LockResource(g)) : nullptr;
    return p ? StripUtf8Bom(std::string(p, size)) : "";
}

void EnsureConfig() {
    std::wstring dir = ConfigDir();
    CreateDirs(dir);
    auto ensure = [&](const wchar_t* name, DefaultFile f) {
        std::wstring p = PathJoin(dir, name);
        if (!FileExists(p)) WriteFileBytes(p, NormalizeNewlines(DefaultContent(f)));
    };
    bool firstRun = ListTemplates().empty();
    if (firstRun) ensure(L"template.jinja", DefaultFile::Template);
    ensure(L"trackers.txt", DefaultFile::Trackers);
    ensure(L"screenshots.txt", DefaultFile::Screenshots);
    std::wstring lists = PathJoin(dir, L"tracker_lists.txt");
    if (!FileExists(lists)) WriteFileBytes(lists, DefaultTrackerLists());
    else {
        // Add the new example to existing configurations without enabling it.
        std::string text;
        if (ReadFileBytes(lists, text) &&
            ToLower(text).find("https://newtrackon.com/api/stable") == std::string::npos) {
            if (!text.empty() && text.back() != '\n') text += "\n";
            text += "# NewTrackon stable: tracker con uptime >95% e almeno 10 giorni di storico.\n"
                    "# https://newtrackon.com/api/stable\n";
            WriteFileBytes(lists, text);
        }
    }
    // Esempio dello stile classico: fuori dal ciclo multi-template (non inizia con "template").
    std::wstring examples = PathJoin(dir, L"esempi");
    CreateDirs(examples);
    std::wstring classic = PathJoin(examples, L"template_releaseforge-classico.jinja");
    if (!FileExists(classic)) WriteFileBytes(classic, NormalizeNewlines(DefaultContent(DefaultFile::TemplateClassic)));
    std::wstring standard = PathJoin(examples, L"template_makerelease-standard.jinja");
    if (!FileExists(standard)) WriteFileBytes(standard, NormalizeNewlines(DefaultContent(DefaultFile::Template)));
}

std::vector<std::wstring> ListTemplates() {
    std::vector<std::wstring> out;
    for (auto& f : ListFiles(ConfigDir())) {
        std::wstring name = PathFileName(f);
        std::wstring lower = name;
        for (auto& c : lower) c = wchar_t(towlower(c));
        if (lower.rfind(L"template", 0) == 0 && PathExtension(lower) == L".jinja") out.push_back(f);
    }
    // template.jinja sempre per primo
    std::stable_sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        bool ma = _wcsicmp(PathFileName(a).c_str(), L"template.jinja") == 0;
        bool mb = _wcsicmp(PathFileName(b).c_str(), L"template.jinja") == 0;
        return ma && !mb;
    });
    return out;
}

std::wstring PostNameForTemplate(const std::wstring& templatePath) {
    std::wstring name = PathFileName(templatePath);
    if (_wcsicmp(name.c_str(), L"template.jinja") == 0) return L"post.txt";
    std::wstring stem = PathStem(name);  // template_a
    std::wstring suffix = stem.size() > 8 ? stem.substr(8) : L"";
    while (!suffix.empty() && (suffix[0] == L'_' || suffix[0] == L'-')) suffix.erase(0, 1);
    return suffix.empty() ? L"post.txt" : L"post_" + suffix + L".txt";
}

std::string ReadConfigFile(const wchar_t* name) { return ReadTextFile(PathJoin(ConfigDir(), name)); }

bool WriteConfigFile(const wchar_t* name, const std::string& utf8) {
    CreateDirs(ConfigDir());
    return WriteFileBytes(PathJoin(ConfigDir(), name), NormalizeNewlines(utf8));
}

}  // namespace rf
