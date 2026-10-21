#include "trackerlists.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <algorithm>
#include <stdexcept>

#include "../core/strutil.h"
#include "../core/torrent.h"
#include "http.h"
#include "settings.h"

namespace rf {

std::vector<std::string> TrackerListSources() {
    std::vector<std::string> out;
    for (auto& line : SplitLines(ReadConfigFile(L"tracker_lists.txt"))) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        if ((StartsWith(ToLower(t), "https://") || StartsWith(ToLower(t), "http://")) && std::find(out.begin(), out.end(), t) == out.end())
            out.push_back(t);
    }
    return out;
}

std::vector<std::string> CachedRemoteTrackers() { return ParseTrackerList(ReadConfigFile(L"trackers_remote.txt")); }

std::vector<std::string> MergeTrackers(const std::vector<std::string>& manual, const std::vector<std::string>& remote) {
    std::vector<std::string> out = manual;
    for (auto& t : remote) {
        bool dup = false;
        for (auto& m : out) if (IEquals(m, t)) { dup = true; break; }
        if (!dup) out.push_back(t);
    }
    return out;
}

std::vector<std::string> UpdateRemoteTrackers(int maxCount, const std::atomic<bool>* cancel,
                                              const std::function<void(const std::string&)>& log) {
    auto sources = TrackerListSources();
    if (sources.empty()) throw std::runtime_error("nessuna lista configurata (Tracker > Liste remote)");
    std::vector<std::string> merged;
    int ok = 0;
    for (auto& url : sources) {
        if (cancel && cancel->load()) throw Cancelled();
        try {
            HttpRequest req;
            req.url = url;
            req.cancel = cancel;
            req.timeoutSeconds = 30;
            HttpResponse r = HttpSend(req);
            if (r.status != 200) throw std::runtime_error("HTTP " + std::to_string(r.status));
            auto list = ParseTrackerList(r.body);
            if (list.empty()) throw std::runtime_error("nessun tracker valido nel contenuto");
            merged = MergeTrackers(merged, list);
            ++ok;
            log("Lista tracker: " + std::to_string(list.size()) + " da " + url);
        } catch (Cancelled&) {
            throw;
        } catch (std::exception& e) {
            log("Lista tracker non scaricata (" + url + "): " + e.what());
        }
    }
    if (!ok) throw std::runtime_error("nessuna lista scaricata: tengo la cache precedente");
    if (maxCount > 0 && int(merged.size()) > maxCount) merged.resize(size_t(maxCount));
    std::string text = "# Generato da ReleaseForge dalle liste in tracker_lists.txt - non modificare a mano\n";
    for (auto& t : merged) text += t + "\n";
    WriteConfigFile(L"trackers_remote.txt", text);
    return merged;
}

}  // namespace rf
