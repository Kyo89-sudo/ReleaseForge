#include "naming.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <regex>

#include "strutil.h"

namespace rf {

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

static std::string CollapseSpaces(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

static std::string TrimChars(const std::string& s, const std::string& chars) {
    size_t b = 0, e = s.size();
    while (b < e && chars.find(s[b]) != std::string::npos) ++b;
    while (e > b && chars.find(s[e - 1]) != std::string::npos) --e;
    return s.substr(b, e - b);
}

TitleYear ParseTitleYear(const std::string& releaseName) {
    std::string name = releaseName;
    static const char* exts[] = {".mkv", ".mp4", ".avi", ".m2ts", ".ts", ".mov", ".webm", ".wmv", ".m4v", ".mpg"};
    for (const char* e : exts) {
        if (IEndsWith(name, e)) { name.resize(name.size() - std::string(e).size()); break; }
    }
    for (auto& c : name) if (c == '.' || c == '_') c = ' ';
    // Gruppi tra parentesi quadre all'inizio ([MIRCrew] Titolo ...)
    while (!name.empty() && name[0] == '[') {
        size_t close = name.find(']');
        if (close == std::string::npos) break;
        name = Trim(name.substr(close + 1));
    }
    name = CollapseSpaces(name);

    // Anni candidati 19xx/20xx non attaccati ad altre cifre.
    std::vector<size_t> years;
    for (size_t i = 0; i + 4 <= name.size(); ++i) {
        if (!(name.compare(i, 2, "19") == 0 || name.compare(i, 2, "20") == 0)) continue;
        if (!IsDigit(name[i + 2]) || !IsDigit(name[i + 3])) continue;
        if (i > 0 && IsDigit(name[i - 1])) continue;
        if (i + 4 < name.size() && IsDigit(name[i + 4])) continue;
        // "1080p", "2160p": già esclusi dal controllo cifre; scarta anche 4 cifre seguite da 'p'
        if (i + 4 < name.size() && (name[i + 4] == 'p' || name[i + 4] == 'P')) continue;
        years.push_back(i);
    }
    // Tag tecnici/stagione: tutto ciò che segue viene scartato.
    static const std::regex junk(
        R"((^|\s)(S\d{1,2}(E\d{1,3})?|Stagion[ei]|Season|Complet[ae]|2160p|1080p|1080i|720p|576p|480p|4K|UHD|iTA|ITA|ENG|MULTI|WEB[- ]?DL|WEBRip|WEB|BluRay|BDRip|BRRip|BDMux|DLMux|HDTV|DVDRip|DVD|x264|x265|H ?26[45]|HEVC|AVC|AAC|AC3|EAC3|DTS|HDR|10bit)(\s|$|-))",
        std::regex::icase);
    std::smatch m;
    size_t junkPos = std::regex_search(name, m, junk) ? size_t(m.position(0)) : name.size();

    // Anno = ultimo candidato prima dei tag tecnici ("Blade Runner 2049 2017 2160p" -> 2017).
    TitleYear r;
    size_t cut = std::string::npos;
    for (size_t pos : years) {
        if (pos == 0 || pos > junkPos) continue;  // pos 0: titolo numerico (1917, 2012)
        cut = pos;
        r.year = name.substr(pos, 4);
    }
    std::string title = cut == std::string::npos ? name : name.substr(0, cut);

    if (std::regex_search(title, m, junk)) title = title.substr(0, size_t(m.position(0)));
    title = TrimChars(Trim(title), " -([{");
    title = TrimChars(title, " -)]}");
    if (title.empty()) title = Trim(name);
    // Titolo tutto numerico all'inizio senza altri anni (es. "1917 1080p")
    r.title = CollapseSpaces(title);
    return r;
}

std::string NormalizeLanguage3(const std::string& langIn) {
    std::string lang = ToLower(Trim(langIn));
    size_t dash = lang.find_first_of("-_");
    if (dash != std::string::npos && dash == 2) lang = lang.substr(0, 2);
    static const char* table[][3] = {
        {"it", "ita", "italian"}, {"en", "eng", "english"}, {"fr", "fre", "french"}, {"de", "ger", "german"},
        {"es", "spa", "spanish"}, {"pt", "por", "portuguese"}, {"ja", "jpn", "japanese"}, {"ko", "kor", "korean"},
        {"zh", "chi", "chinese"}, {"ru", "rus", "russian"}, {"nl", "dut", "dutch"}, {"sv", "swe", "swedish"},
        {"da", "dan", "danish"}, {"no", "nor", "norwegian"}, {"fi", "fin", "finnish"}, {"pl", "pol", "polish"},
        {"cs", "cze", "czech"}, {"hu", "hun", "hungarian"}, {"el", "gre", "greek"}, {"tr", "tur", "turkish"},
        {"ar", "ara", "arabic"}, {"he", "heb", "hebrew"}, {"hi", "hin", "hindi"}, {"ro", "rum", "romanian"},
    };
    for (auto& row : table) {
        if (lang == row[0] || lang == row[1] || lang == row[2]) return row[1];
    }
    if (lang == "fra") return "fre";
    if (lang == "deu") return "ger";
    if (lang == "zho") return "chi";
    if (lang == "ces") return "cze";
    if (lang == "nld") return "dut";
    if (lang == "ell") return "gre";
    if (lang == "ron") return "rum";
    return lang.size() == 3 ? lang : "";
}

static std::string VideoFormatTag(const std::string& f) {
    if (f == "MPEG Video") return "MPEG2";
    if (f == "MPEG-4 Visual") return "XviD";
    if (f == "AVC") return "H264";
    if (f == "HEVC") return "H265";
    if (f == "VP08") return "VP8";
    if (f == "VP09") return "VP9";
    if (f == "VC-1") return "VC1";
    return f;
}

// Nome formato audio per il titolo. Fix rispetto a makerelease: MP2, varianti DTS/TrueHD con spazi.
static std::string AudioFormatTag(const AudioTrack& a) {
    std::string fmt = a.format;
    if (fmt == "MPEG Audio") {
        if (a.formatProfile == "Layer 3") fmt = "MP3";
        else if (a.formatProfile == "Layer 2") fmt = "MP2";
    }
    if (StartsWith(fmt, "DTS")) fmt = "DTS";
    if (StartsWith(fmt, "MLP")) fmt = "TrueHD";
    size_t sp = fmt.find(' ');
    if (sp != std::string::npos) fmt = fmt.substr(0, sp);
    return ReplaceAll(fmt, "-", "");
}

static std::string ChannelsTag(const AudioTrack& a) {
    std::string layout = Trim(a.channelLayout);
    if (!layout.empty()) {
        std::vector<std::string> parts;
        for (auto& p : Split(layout, ' ')) if (!p.empty()) parts.push_back(p);
        int lfe = 0;
        for (auto& p : parts) if (p.find("LFE") != std::string::npos) { lfe = 1; break; }
        return std::to_string(int(parts.size()) - lfe) + "." + std::to_string(lfe);
    }
    if (a.channels <= 0) return "";  // fix: makerelease scriveva "None.0"
    return std::to_string(a.channels) + ".0";
}

std::string HdrTag(const VideoTrack& v) {
    std::string h = v.hdrFormat, low = ToLower(h), tr = ToLower(v.transfer);
    bool dv = low.find("dolby vision") != std::string::npos;
    bool plus = h.find("HDR10+") != std::string::npos || h.find("ST 2094 App 4") != std::string::npos;
    bool hdr10Meta = h.find("ST 2086") != std::string::npos || h.find("HDR10") != std::string::npos;
    bool pq = tr == "pq" || tr.find("2084") != std::string::npos;
    // Dolby Vision profilo 5 usa PQ ma non ha layer HDR10: niente "HDR10" se manca la compatibilità dichiarata.
    bool hdr10 = hdr10Meta || (pq && !dv);
    bool hlg = tr.find("hlg") != std::string::npos || low.find("hlg") != std::string::npos || tr.find("b67") != std::string::npos;
    std::vector<std::string> tags;
    if (dv) tags.push_back("DV");
    if (plus) tags.push_back("HDR10+");
    else if (hdr10) tags.push_back("HDR10");
    else if (hlg) tags.push_back("HLG");
    return Join(tags, " ");
}

bool IsAtmos(const AudioTrack& a) {
    return a.commercial.find("Atmos") != std::string::npos || a.features.find("JOC") != std::string::npos ||
           (StartsWith(a.format, "MLP") || a.format == "TrueHD" ? a.features.find("16-ch") != std::string::npos : false);
}

bool IsDtsX(const AudioTrack& a) {
    return a.commercial.find("DTS:X") != std::string::npos || a.features.find("XLL X") != std::string::npos;
}

std::string BuildReleaseTitle(const std::string& title, const std::string& year, const MediaTracks& tracks,
                              const std::string& crew) {
    std::string v;
    if (!tracks.video.empty()) {
        const VideoTrack& t = tracks.video[0];
        // makerelease ricava l'altezza "16:9 equivalente" dalla larghezza (1920x800 -> 1080p).
        // Fix: per contenuti 4:3/pillarbox (1440x1080) si usa l'altezza reale se maggiore.
        int fake = int(double(t.width) / (16.0 / 9.0));
        if (t.height > fake) fake = t.height;
        for (int res : {576, 720, 1080, 2160, 4320}) if (std::abs(res - fake) < 10) fake = res;
        std::string fmt = VideoFormatTag(t.format);
        bool interlaced = t.scanType == "Interlaced" || t.scanType == "MBAFF" || t.scanType == "PAFF";
        if (fake <= 576) v = "SD " + fmt;
        else v = std::to_string(fake) + (interlaced ? "i" : "p") + " " + fmt;
        std::string hdr = HdrTag(t);   // aggiunta ReleaseForge: "2160p H265 DV HDR10"
        if (!hdr.empty()) v += " " + hdr;
    }

    // Raggruppa le lingue per formato/canali: "ITA AC3 5.1 ENG AC3 5.1" -> "ITA ENG AC3 5.1"
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    for (auto& a : tracks.audio) {
        std::string lang = a.language.empty() ? "UND" : ToUpper(a.language);
        std::string fmt = AudioFormatTag(a);
        if (IsDtsX(a)) fmt = "DTS-X";
        std::string key = Trim(fmt + " " + ChannelsTag(a) + (IsAtmos(a) ? " Atmos" : ""));
        if (key.empty()) continue;
        auto it = std::find_if(groups.begin(), groups.end(), [&](auto& g) { return g.first == key; });
        if (it == groups.end()) groups.push_back({key, {lang}});
        else if (std::find(it->second.begin(), it->second.end(), lang) == it->second.end()) it->second.push_back(lang);
    }
    std::vector<std::string> audio;
    for (auto& g : groups) {
        std::string s = Join(g.second, " ") + " " + g.first;
        if (std::find(audio.begin(), audio.end(), s) == audio.end()) audio.push_back(s);
    }

    std::vector<std::string> subs;
    for (auto& t : tracks.text) {
        if (t.language.empty()) continue;
        std::string s = TitleCase(t.language);
        if (std::find(subs.begin(), subs.end(), s) == subs.end()) subs.push_back(s);
    }
    std::string s = subs.size() > 3 ? "MultiSub" : (subs.empty() ? "" : "Sub " + Join(subs, " "));

    std::string tag = v + " " + Join(audio, " ") + " " + s;
    if (!Trim(crew).empty()) tag += " [" + Trim(crew) + "]";
    std::string out = title + (year.empty() ? "" : " (" + year + ")") + " " + tag;
    return SanitizeFileName(out);
}

std::string SanitizeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::string("\\/*?:\"<>|").find(c) != std::string::npos) continue;
        if ((unsigned char)c < 0x20) continue;
        out += c;
    }
    return Trim(CollapseSpaces(out));
}

static std::string MapFfmpegVideo(const std::string& codec) {
    std::string c = ToLower(codec);
    if (c == "h264") return "AVC";
    if (c == "hevc" || c == "h265") return "HEVC";
    if (c == "av1") return "AV1";
    if (c == "mpeg2video" || c == "mpeg1video") return "MPEG Video";
    if (c == "mpeg4") return "MPEG-4 Visual";
    if (c == "vp9") return "VP09";
    if (c == "vp8") return "VP08";
    return ToUpper(codec);
}

static std::string MapFfmpegAudio(const std::string& codec) {
    std::string c = ToLower(codec);
    if (c == "ac3") return "AC-3";
    if (c == "eac3") return "E-AC-3";
    if (c == "aac") return "AAC";
    if (c == "dts") return "DTS";
    if (c == "truehd") return "TrueHD";
    if (c == "flac") return "FLAC";
    if (c == "mp3") return "MP3";
    if (c == "opus") return "Opus";
    if (c == "vorbis") return "Vorbis";
    if (c.rfind("pcm", 0) == 0) return "PCM";
    return ToUpper(codec);
}

MediaTracks ParseFfmpegProbe(const std::string& text) {
    MediaTracks t;
    static const std::regex dur(R"(Duration:\s*(\d+):(\d{2}):(\d{2})(?:\.(\d+))?)");
    static const std::regex stream(R"(Stream #\d+:\d+(?:\[[^\]]*\])?(?:\(([A-Za-z]{2,3})\))?(?:\[[^\]]*\])?:\s*(Video|Audio|Subtitle):\s*([A-Za-z0-9_]+)(.*))");
    static const std::regex res(R"((\d{2,5})x(\d{2,5}))");
    static const std::regex sar(R"(SAR (\d+):(\d+))");
    static const std::regex chans(R"(Hz,\s*([^,]+),)");
    for (auto& line : SplitLines(text)) {
        std::smatch m;
        if (t.durationMs == 0 && std::regex_search(line, m, dur)) {
            int64_t ms = (std::stoll(m[1]) * 3600 + std::stoll(m[2]) * 60 + std::stoll(m[3])) * 1000;
            if (m[4].matched) {
                std::string frac = m[4].str() + "000";
                ms += std::stoll(frac.substr(0, 3));
            }
            t.durationMs = ms;
            t.valid = true;
            continue;
        }
        // Side data della traccia video precedente (righe successive a "Stream #")
        if (!t.video.empty()) {
            if (line.find("DOVI configuration record") != std::string::npos) {
                // compatibility id: 1/6 = layer HDR10, 4 = HLG, 2 = SDR, 0 = nessuno (profilo 5)
                std::string& hf = t.video.back().hdrFormat;
                hf += " Dolby Vision";
                static const std::regex compat(R"(compatibility id:\s*(\d+))"), profile(R"(profile:\s*(\d+))");
                std::smatch cm;
                if (std::regex_search(line, cm, compat)) {
                    int id = std::stoi(cm[1]);
                    if (id == 1 || id == 6) hf += " HDR10 compatible";
                    else if (id == 4) hf += " HLG compatible";
                } else if (std::regex_search(line, cm, profile) && (cm[1] == "7" || cm[1] == "8")) {
                    hf += " HDR10 compatible";  // ffmpeg datati senza compatibility id: profili 7/8 di norma HDR10
                }
            }
            if (line.find("Mastering Display Metadata") != std::string::npos) t.video.back().hdrFormat += " SMPTE ST 2086";
            if (line.find("HDR10+") != std::string::npos || line.find("HDR Dynamic Metadata SMPTE2094-40") != std::string::npos)
                t.video.back().hdrFormat += " HDR10+";
        }
        if (!std::regex_search(line, m, stream)) continue;
        std::string lang = m[1].matched ? ToLower(m[1].str()) : "";
        if (lang == "und") lang.clear();
        std::string kind = m[2], codec = m[3], rest = m[4];
        t.valid = true;
        if (kind == "Video") {
            if (rest.find("attached pic") != std::string::npos) continue;
            VideoTrack v;
            v.format = MapFfmpegVideo(codec);
            std::smatch r;
            if (std::regex_search(rest, r, res)) { v.width = std::stoi(r[1]); v.height = std::stoi(r[2]); }
            if (std::regex_search(rest, r, sar)) {
                double a = std::stod(r[1]), b = std::stod(r[2]);
                if (a > 0 && b > 0) v.pixelAspect = a / b;
            }
            if (rest.find("smpte2084") != std::string::npos) v.transfer = "PQ";
            else if (rest.find("arib-std-b67") != std::string::npos) v.transfer = "HLG";
            if (rest.find("top first") != std::string::npos || rest.find("bottom first") != std::string::npos ||
                rest.find("top coded first") != std::string::npos)
                v.scanType = "Interlaced";
            t.video.push_back(v);
        } else if (kind == "Audio") {
            AudioTrack a;
            a.language = NormalizeLanguage3(lang);
            a.format = MapFfmpegAudio(codec);
            if (rest.find("Atmos") != std::string::npos) a.commercial = "Dolby Atmos";
            if (rest.find("DTS:X") != std::string::npos) a.commercial = "DTS:X";
            std::smatch c;
            if (std::regex_search(rest, c, chans)) {
                std::string ch = Trim(c[1]);
                size_t paren = ch.find('(');
                if (paren != std::string::npos) ch = ch.substr(0, paren);
                if (ch == "mono") a.channels = 1;
                else if (ch == "stereo") a.channels = 2;
                else if (ch.find('.') != std::string::npos) {
                    // "5.1" -> layout sintetico con LFE
                    int main = std::atoi(ch.c_str());
                    int lfe = std::atoi(ch.substr(ch.find('.') + 1).c_str());
                    std::string layout;
                    for (int i = 0; i < main; ++i) layout += (i ? " C" : "C");
                    for (int i = 0; i < lfe; ++i) layout += " LFE";
                    a.channelLayout = layout;
                    a.channels = main + lfe;
                } else {
                    a.channels = std::atoi(ch.c_str());
                }
            }
            t.audio.push_back(a);
        } else {
            TextTrack s;
            s.language = NormalizeLanguage3(lang);
            t.text.push_back(s);
        }
    }
    return t;
}

}  // namespace rf
