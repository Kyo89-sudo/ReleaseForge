// ReleaseForge - liste di tracker remote (es. ngosang/trackerslist) da unire a trackers.txt.
//   config\tracker_lists.txt   un URL per riga (# = commento)
//   config\trackers_remote.txt cache dell'ultimo aggiornamento
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace rf {

std::vector<std::string> TrackerListSources();   // URL validi da tracker_lists.txt
std::vector<std::string> CachedRemoteTrackers();  // da trackers_remote.txt

// Scarica tutte le liste, unisce e deduplica, tiene al massimo maxCount tracker e aggiorna la cache.
// Le liste che falliscono vengono saltate (riportate nel log). Lancia se nessuna lista è valida.
std::vector<std::string> UpdateRemoteTrackers(int maxCount, const std::atomic<bool>* cancel,
                                              const std::function<void(const std::string&)>& log);

// Tracker manuali per primi, poi quelli remoti non duplicati.
std::vector<std::string> MergeTrackers(const std::vector<std::string>& manual, const std::vector<std::string>& remote);

}  // namespace rf
