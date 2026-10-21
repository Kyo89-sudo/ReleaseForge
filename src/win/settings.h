// ReleaseForge - impostazioni persistenti e cartella di configurazione personalizzabile.
//   %APPDATA%\ReleaseForge\settings.json   chiavi API, percorsi strumenti, preferenze
//   %APPDATA%\ReleaseForge\config\         template*.jinja, trackers.txt, screenshots.txt (come makerelease)
#pragma once
#include <string>
#include <vector>

namespace rf {

struct Settings {
    std::string tmdbKey;
    std::string imgbbKey;
    std::string imageHost = "imgbb";   // imgbb | pixhost | imgur | catbox | chevereto | none
    std::string imageHostFallback = "pixhost";  // usato se il primo fallisce ("none" = nessuno)
    std::string cheveretoUrl = "https://freeimage.host/api/1/upload";
    std::string cheveretoKey;
    std::wstring ffmpegPath;           // vuoto = rilevamento automatico
    std::wstring mediaInfoPath;        // vuoto = rilevamento automatico (MediaInfo completo)
    std::wstring avinapticPath;
    std::wstring outputDir;            // vuoto = Documenti\ReleaseForge\Output
    std::string crew;
    bool privateTorrent = false;
    int pieceSizeKiB = 0;              // 0 = automatico (tabella MIRCrew)
    int releaseType = 0;               // 0 auto, 1 film, 2 film+extra, 3 stagione, 4 serie completa
    bool screenshots = true;
    bool torrent = true;
    bool upload = true;
    bool resume = true;                // riusa .torrent/report/bitrate esistenti se validi
    bool bitrateGraph = true;          // solo se il template usa {{ BITRATE_GRAPH }}
    bool askBeforeOverwrite = true;
    int winX = 0, winY = 0, winW = 0, winH = 0;
    bool winMax = false;
    bool toolsPromptShown = false;
    bool trackerListsAuto = false;      // campo legacy: aggiornamento automatico disattivato
    int trackerListsMax = 8;          // massimo tracker presi dalle liste (0 = nessun limite)
    long long trackerListsLast = 0;    // epoch dell'ultimo aggiornamento riuscito

    static Settings Load();
    bool Save() const;
};

std::wstring SettingsPath();
std::wstring ConfigDir();
std::wstring DefaultOutputDir();
std::wstring EffectiveOutputDir(const Settings& s);

// Contenuti predefiniti incorporati nell'eseguibile.
enum class DefaultFile { Template, Trackers, Screenshots, Countries, TemplateClassic };
std::string DefaultContent(DefaultFile f);
std::string DefaultTrackerLists();

// Crea la cartella config con i file standard mancanti (non sovrascrive quelli personalizzati).
void EnsureConfig();
std::vector<std::wstring> ListTemplates();          // percorsi completi di config\template*.jinja
std::wstring PostNameForTemplate(const std::wstring& templatePath);   // template.jinja -> post.txt, template_a -> post_a.txt
std::string ReadConfigFile(const wchar_t* name);    // trackers.txt / screenshots.txt
bool WriteConfigFile(const wchar_t* name, const std::string& utf8);

}  // namespace rf
