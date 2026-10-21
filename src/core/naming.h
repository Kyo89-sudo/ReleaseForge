// ReleaseForge - riconoscimento titolo/anno dal nome e titolo release in formato consigliato
// (porting di makerelease tag.py).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rf {

struct TitleYear {
    std::string title;
    std::string year;
};

// "Amore.E.Altri.Rimedi.2010.1080p.ITA-ENG..." -> {"Amore E Altri Rimedi", "2010"}
TitleYear ParseTitleYear(const std::string& releaseName);

// Dati tecnici minimi per il titolo (da MediaInfo.dll, MediaInfo CLI o FFmpeg).
struct VideoTrack {
    int width = 0, height = 0;
    std::string format;     // nomi MediaInfo: AVC, HEVC, AV1, MPEG Video, MPEG-4 Visual, VP09...
    std::string scanType;   // "Interlaced" / "Progressive" / ""
    double pixelAspect = 1.0;
    std::string hdrFormat;      // MediaInfo HDR_Format (+ HDR_Format_Compatibility): "Dolby Vision, ... / SMPTE ST 2086, HDR10 compatible"
    std::string transfer;       // MediaInfo transfer_characteristics: "PQ", "HLG", "BT.709"
};
struct AudioTrack {
    std::string language;   // codice ISO 639-2 se noto (ita, eng), altrimenti come disponibile
    std::string format;     // AC-3, E-AC-3, AAC, DTS, MPEG Audio...
    std::string formatProfile;
    std::string channelLayout;  // "L R C LFE Ls Rs"
    int channels = 0;
    std::string commercial;     // MediaInfo Format_Commercial_IfAny: "Dolby Digital Plus with Dolby Atmos"
    std::string features;       // MediaInfo Format_AdditionalFeatures: "JOC", "16-ch", "XLL X"
};
struct TextTrack {
    std::string language;   // ISO 639-2
};
struct MediaTracks {
    bool valid = false;
    int64_t durationMs = 0;
    std::vector<VideoTrack> video;
    std::vector<AudioTrack> audio;
    std::vector<TextTrack> text;
};

// "Titolo (2010) 1080p H264 ITA ENG AC3 5.1 Sub Ita Eng [Crew]"
std::string BuildReleaseTitle(const std::string& title, const std::string& year, const MediaTracks& tracks,
                              const std::string& crew);

// Tag dinamica: "DV HDR10", "HDR10+", "HDR10", "HLG" o "" (SDR).
std::string HdrTag(const VideoTrack& v);
bool IsAtmos(const AudioTrack& a);
bool IsDtsX(const AudioTrack& a);

// Rimuove \ / * ? : " < > | (come makerelease) e spazi doppi.
std::string SanitizeFileName(const std::string& name);

// Interpreta l'output di "ffmpeg -hide_banner -i file" (ripiego quando MediaInfo manca).
MediaTracks ParseFfmpegProbe(const std::string& ffmpegStderr);

// "ita" / "it" / "Italian" -> "ita" (ISO 639-2) quando riconosciuto.
std::string NormalizeLanguage3(const std::string& lang);

}  // namespace rf
