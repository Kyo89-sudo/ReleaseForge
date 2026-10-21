// ReleaseForge - grafico del bitrate video (come makerelease BitrateViewer): ffprobe + GDI+ -> PNG.
#pragma once
#include <atomic>
#include <functional>
#include <string>

namespace rf {

// Crea outputPng (1600x900). Lancia std::runtime_error.
void CreateBitrateGraph(const std::wstring& ffprobe, const std::wstring& video, const std::wstring& outputPng,
                        const std::atomic<bool>* cancel, const std::function<void(double seconds)>& progress);

}  // namespace rf
