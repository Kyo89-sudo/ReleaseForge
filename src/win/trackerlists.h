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

// Scarica tutte le liste, unisce e deduplica (nell'ordine delle fonti) e aggiorna la cache.
// Il limite si applica dopo, all'elenco finale (MergeTrackerLists in core/torrent.h).
// Le liste che falliscono vengono saltate (riportate nel log). Lancia se nessuna lista è valida.
std::vector<std::string> UpdateRemoteTrackers(const std::atomic<bool>* cancel,
                                              const std::function<void(const std::string&)>& log);

}  // namespace rf
