// Test del core portabile (compilabili anche su Linux/macOS):
//   g++ -std=c++17 -Isrc tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests [cartella-fixture]
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/jinja.h"
#include "core/json.h"
#include "core/naming.h"
#include "core/strutil.h"
#include "core/torrent.h"

using namespace rf;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("PASS %s\n", msg); else { std::printf("FAIL %s\n", msg); ++failures; } } while (0)
#define CHECK_EQ(a, b, msg) do { auto _a = (a); auto _b = (b); if (_a == _b) std::printf("PASS %s\n", msg); else { std::cout << "FAIL " << msg << "\n   atteso: [" << _b << "]\n   ottenuto: [" << _a << "]\n"; ++failures; } } while (0)

int main(int argc, char** argv) {
    // --- titolo/anno
    auto ty = ParseTitleYear("Amore.E.Altri.Rimedi.2010.1080p.ITA-ENG.MDST.WEB-DL.AAC.H.264-LucY.MIRCrew.mkv");
    CHECK_EQ(ty.title, std::string("Amore E Altri Rimedi"), "titolo pulito");
    CHECK_EQ(ty.year, std::string("2010"), "anno");
    CHECK_EQ(ParseTitleYear("1917.2019.1080p.mkv").title, std::string("1917"), "titolo numerico con anno");
    CHECK_EQ(ParseTitleYear("1917.1080p.mkv").title, std::string("1917"), "titolo numerico senza anno");
    CHECK_EQ(ParseTitleYear("The.Office.S01.1080p.WEB-DL.ITA").title, std::string("The Office"), "serie con S01");
    CHECK_EQ(ParseTitleYear("Film.Senza.Anno.1080p.ITA.WEB-DL").title, std::string("Film Senza Anno"), "senza anno");
    CHECK_EQ(ParseTitleYear("[MIRCrew] La Casa di Carta Stagione 2 (2017)").title, std::string("La Casa di Carta"), "stagione italiana");
    CHECK_EQ(ParseTitleYear("Blade.Runner.2049.2017.2160p").title, std::string("Blade Runner 2049"), "anno nel titolo");

    // --- titolo release (tag.py)
    MediaTracks mt;
    VideoTrack v; v.width = 1920; v.height = 800; v.format = "AVC"; v.scanType = "Progressive"; mt.video.push_back(v);
    AudioTrack a1; a1.language = "ita"; a1.format = "AC-3"; a1.channelLayout = "L R C LFE Ls Rs"; mt.audio.push_back(a1);
    AudioTrack a2; a2.language = "eng"; a2.format = "AC-3"; a2.channelLayout = "L R C LFE Ls Rs"; mt.audio.push_back(a2);
    AudioTrack a3; a3.language = "eng"; a3.format = "E-AC-3"; a3.channels = 2; mt.audio.push_back(a3);
    TextTrack s1; s1.language = "ita"; mt.text.push_back(s1);
    TextTrack s2; s2.language = "eng"; mt.text.push_back(s2);
    CHECK_EQ(BuildReleaseTitle("Amore e altri rimedi", "2010", mt, "LucY"),
             std::string("Amore e altri rimedi (2010) 1080p H264 ITA ENG AC3 5.1 ENG EAC3 2.0 Sub Ita Eng [LucY]"), "titolo release");
    MediaTracks sd; VideoTrack v2; v2.width = 720; v2.format = "MPEG-4 Visual"; sd.video.push_back(v2);
    CHECK_EQ(BuildReleaseTitle("X: Y?", "", sd, ""), std::string("X Y SD XviD"), "SD e caratteri non validi");

    MediaTracks fx; VideoTrack v3; v3.width = 1440; v3.height = 1080; v3.format = "AVC"; v3.scanType = "MBAFF"; fx.video.push_back(v3);
    AudioTrack m2; m2.language = "ita"; m2.format = "MPEG Audio"; m2.formatProfile = "Layer 2"; m2.channels = 2; fx.audio.push_back(m2);
    AudioTrack m3 = m2; fx.audio.push_back(m3);
    AudioTrack dt; dt.language = "eng"; dt.format = "DTS XLL"; dt.channelLayout = "C L R Ls Rs LFE"; fx.audio.push_back(dt);
    TextTrack nolang; fx.text.push_back(nolang);
    CHECK_EQ(BuildReleaseTitle("Film 4:3", "1999", fx, ""), std::string("Film 43 (1999) 1080i H264 ITA MP2 2.0 ENG DTS-HD MA 5.1"), "fix 4:3, MBAFF, MP2, DTS, lingue doppie");
    VideoTrack v4; v4.width = 3840; v4.height = 1606; v4.format = "HEVC"; MediaTracks uhd; uhd.video.push_back(v4);
    CHECK_EQ(BuildReleaseTitle("X", "2020", uhd, ""), std::string("X (2020) 2160p H265"), "2160p scope");

    // DTS-HD Master Audio: preserve the codec identity and keep DTS core separate.
    auto audioTitle = [](AudioTrack a) {
        a.language = "eng"; a.channels = 6; a.channelLayout = "L R C LFE Ls Rs";
        MediaTracks t; t.audio.push_back(a);
        return BuildReleaseTitle("X", "2020", t, "");
    };
    AudioTrack ma; ma.format = "DTS"; ma.commercial = "DTS-HD Master Audio";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS-HD MA 5.1"), "DTS-HD MA commercial name");
    ma.commercial.clear(); ma.formatProfile = "MA / Core";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS-HD MA 5.1"), "DTS-HD MA legacy profile");
    ma.formatProfile.clear(); ma.features = "XLL";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS-HD MA 5.1"), "DTS-HD MA XLL feature");
    ma.features.clear(); ma.format = "DTS XLL";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS-HD MA 5.1"), "DTS-HD MA XLL format");
    ma.format = "DTS-HD MA";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS-HD MA 5.1"), "DTS-HD MA explicit format");
    ma.format = "DTS";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS 5.1"), "DTS core remains DTS");
    ma.commercial = "DTS-HD High Resolution Audio"; ma.features = "XBR";
    CHECK_EQ(audioTitle(ma), std::string("X (2020) ENG DTS 5.1"), "DTS HRA is not MA");
    MediaTracks mixedDts;
    AudioTrack core; core.format = "DTS"; core.language = "ita"; core.channels = 6; core.channelLayout = "L R C LFE Ls Rs";
    AudioTrack master = core; master.language = "eng"; master.features = "XLL";
    mixedDts.audio = {core, master};
    CHECK_EQ(BuildReleaseTitle("X", "2020", mixedDts, ""),
             std::string("X (2020) ITA DTS 5.1 ENG DTS-HD MA 5.1"), "DTS core and MA are not grouped");
    const auto ffma = ParseFfmpegProbe("  Stream #0:1(eng): Audio: dts (DTS-HD MA), 48000 Hz, 5.1(side), s32p (24 bit)\n");
    CHECK_EQ(BuildReleaseTitle("X", "2020", ffma, ""), std::string("X (2020) ENG DTS-HD MA 5.1"), "DTS-HD MA from FFmpeg");
    master.features = "XLL X";
    CHECK_EQ(audioTitle(master), std::string("X (2020) ENG DTS-X 5.1"), "DTS-X keeps priority over MA");

    // Group identical language sets without assigning a codec to the wrong language.
    MediaTracks languageAudio;
    AudioTrack itEac; itEac.language = "ita"; itEac.format = "E-AC-3"; itEac.channelLayout = "L R C LFE Ls Rs";
    AudioTrack itAc = itEac; itAc.format = "AC-3"; itAc.channelLayout = "L R";
    languageAudio.audio = {itEac, itAc, itAc};
    CHECK_EQ(BuildReleaseTitle("X", "2020", languageAudio, ""),
             std::string("X (2020) ITA EAC3 5.1 AC3 2.0"), "same language codecs combined without duplicate tracks");
    AudioTrack enEac = itEac; enEac.language = "eng";
    languageAudio.audio = {itEac, enEac, itAc};
    CHECK_EQ(BuildReleaseTitle("X", "2020", languageAudio, ""),
             std::string("X (2020) ITA ENG EAC3 5.1 ITA AC3 2.0"), "partially overlapping languages remain separate");
    AudioTrack enAc = itAc; enAc.language = "eng";
    languageAudio.audio = {itEac, enEac, enAc, itAc};
    CHECK_EQ(BuildReleaseTitle("X", "2020", languageAudio, ""),
             std::string("X (2020) ITA ENG EAC3 5.1 AC3 2.0"), "same language sets combined regardless of track order");
    AudioTrack itMa = itEac; itMa.format = "DTS"; itMa.features = "XLL";
    AudioTrack itDts = itMa; itDts.features.clear();
    languageAudio.audio = {itMa, itDts};
    CHECK_EQ(BuildReleaseTitle("X", "2020", languageAudio, ""),
             std::string("X (2020) ITA DTS-HD MA 5.1 DTS 5.1"), "same language keeps both DTS codec identities");

    // --- HDR / Dolby Vision / Atmos
    MediaTracks hdr;
    VideoTrack hv; hv.width = 3840; hv.height = 2160; hv.format = "HEVC";
    hv.hdrFormat = "Dolby Vision, Version 1.0, Profile 8.1, dvhe.08.06, BL+RPU, HDR10 compatible / SMPTE ST 2086, HDR10 compatible";
    hv.transfer = "PQ"; hdr.video.push_back(hv);
    AudioTrack ea; ea.language = "eng"; ea.format = "E-AC-3"; ea.features = "JOC"; ea.commercial = "Dolby Digital Plus with Dolby Atmos"; ea.channelLayout = "L R C LFE Ls Rs"; hdr.audio.push_back(ea);
    AudioTrack ia; ia.language = "ita"; ia.format = "E-AC-3"; ia.channelLayout = "L R C LFE Ls Rs"; hdr.audio.push_back(ia);
    AudioTrack th; th.language = "eng"; th.format = "MLP FBA"; th.features = "16-ch"; th.commercial = "Dolby TrueHD with Dolby Atmos"; th.channelLayout = "L R C LFE Ls Rs Lb Rb"; hdr.audio.push_back(th);
    CHECK_EQ(BuildReleaseTitle("Dune", "2021", hdr, ""), std::string("Dune (2021) 2160p H265 DV HDR10 ENG EAC3 5.1 Atmos TrueHD 7.1 Atmos ITA EAC3 5.1"), "DV HDR10 + Atmos");
    VideoTrack p10; p10.hdrFormat = "SMPTE ST 2094 App 4, Version 1, HDR10+ Profile B compatible"; CHECK_EQ(HdrTag(p10), std::string("HDR10+"), "HDR10+");
    VideoTrack p5; p5.hdrFormat = "Dolby Vision, Version 1.0, Profile 5, dvhe.05.06, BL+RPU / "; p5.transfer = "PQ";
    CHECK_EQ(HdrTag(p5), std::string("DV"), "DV profilo 5 senza HDR10");
    VideoTrack hl; hl.transfer = "HLG"; CHECK_EQ(HdrTag(hl), std::string("HLG"), "HLG");
    VideoTrack sdr; sdr.transfer = "BT.709"; CHECK_EQ(HdrTag(sdr), std::string(""), "SDR senza tag");
    AudioTrack dx; dx.language = "eng"; dx.format = "DTS XLL"; dx.commercial = "DTS-HD Master Audio + DTS:X"; dx.channelLayout = "C L R Ls Rs Lb Rb LFE";
    MediaTracks dxt; dxt.audio.push_back(dx);
    CHECK_EQ(BuildReleaseTitle("X", "2020", dxt, ""), std::string("X (2020) ENG DTS-X 7.1"), "DTS:X");
    auto fhdr = ParseFfmpegProbe("  Stream #0:0: Video: hevc (Main 10), yuv420p10le(tv, bt2020nc/bt2020/smpte2084), 3840x2160\n"
                                 "    Side data:\n      DOVI configuration record: version: 1.0, profile: 8\n"
                                 "  Stream #0:1(eng): Audio: eac3 (Dolby Digital Plus + Dolby Atmos), 48000 Hz, 5.1(side), fltp\n");
    auto f5 = ParseFfmpegProbe("  Stream #0:0: Video: hevc (Main 10), yuv420p10le(tv, bt2020nc/bt2020/smpte2084), 3840x2160\n"
                               "    Side data:\n      DOVI configuration record: version: 1.0, profile: 5, level: 6, rpu flag: 1, el flag: 0, bl flag: 1, compatibility id: 0\n");
    CHECK_EQ(HdrTag(f5.video[0]), std::string("DV"), "ffmpeg DV profilo 5");
    auto f84 = ParseFfmpegProbe("  Stream #0:0: Video: hevc (Main 10), yuv420p10le(tv, bt2020nc/bt2020/arib-std-b67), 3840x2160\n"
                                "    Side data:\n      DOVI configuration record: version: 1.0, profile: 8, level: 6, rpu flag: 1, el flag: 0, bl flag: 1, compatibility id: 4\n");
    CHECK_EQ(HdrTag(f84.video[0]), std::string("DV HLG"), "ffmpeg DV 8.4 HLG");
    CHECK_EQ(BuildReleaseTitle("F", "2022", fhdr, ""), std::string("F (2022) 2160p H265 DV HDR10 ENG EAC3 5.1 Atmos"), "HDR/Atmos da ffmpeg");

    // --- ffmpeg fallback
    std::string probe =
        "Input #0, matroska,webm, from 'x.mkv':\n  Duration: 01:52:10.24, start: 0.000000, bitrate: 5000 kb/s\n"
        "  Stream #0:0(ita): Video: h264 (High), yuv420p(progressive), 1920x800 [SAR 1:1 DAR 12:5], 23.98 fps\n"
        "  Stream #0:1(ita): Audio: ac3, 48000 Hz, 5.1(side), fltp, 640 kb/s (default)\n"
        "  Stream #0:2(eng): Audio: aac (LC), 48000 Hz, stereo, fltp\n"
        "  Stream #0:3(ita): Subtitle: subrip\n";
    auto pt = ParseFfmpegProbe(probe);
    CHECK_EQ(pt.durationMs, (int64_t)6730240, "durata ffmpeg");
    CHECK_EQ(BuildReleaseTitle("T", "2000", pt, ""), std::string("T (2000) 1080p H264 ITA AC3 5.1 ENG AAC 2.0 Sub Ita"), "titolo da ffmpeg");

    // --- utilità
    CHECK_EQ(SizeOf(4724464025ull), std::string("4.4 GiB"), "sizeof_fmt");
    CHECK_EQ(SizeOf(512), std::string("512.0 B"), "sizeof_fmt byte");
    CHECK_EQ(RuntimeFromMinutes(112), std::string("1h 52m"), "runtime");
    CHECK_EQ(RuntimeFromMinutes(45), std::string("45m"), "runtime breve");
    CHECK_EQ(PieceSizeForTotal(4724464025ull), (uint32_t)(4096 * 1024), "piece size 4.4 GiB");
    CHECK_EQ(PieceSizeForTotal(40ull * 1024 * 1024), (uint32_t)(32 * 1024), "piece size piccolo");
    auto tl = ParseTrackerList("udp://a.b:1/announce\r\n\r\n# commento\nhttp://x.y/announce\nnon-un-url\nudp://a.b:1/announce\n");
    CHECK_EQ(tl.size(), (size_t)2, "lista tracker deduplicata");

    // --- SHA-1
    unsigned char d[20];
    Sha1::Hash("abc", 3, d);
    CHECK_EQ(HexLower(d, 20), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"), "sha1 abc");
    std::string million(1000000, 'a');
    Sha1::Hash(million.data(), million.size(), d);
    CHECK_EQ(HexLower(d, 20), std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"), "sha1 milione di a");

    // --- JSON
    auto j = Json::Parse(R"({"a":[1,2.5,"xè😀"],"b":{"c":null,"d":true},"id":43347})");
    CHECK_EQ(j["a"][2].Str(), std::string("x\xC3\xA8\xF0\x9F\x98\x80"), "json unicode");
    CHECK_EQ(j["id"].Str(), std::string("43347"), "json intero");
    CHECK(j["b"]["c"].IsNull() && j["b"]["d"].Bool() && j["zzz"]["y"].IsNull(), "json accesso tollerante");

    // --- template: errori con riga
    try { RenderTemplate("ok\n{% if X %}\nmanca fine", {}); CHECK(false, "errore blocco non chiuso"); }
    catch (TemplateError& e) { CHECK(std::string(e.what()).find("endif") != std::string::npos, "errore blocco non chiuso"); }
    try { RenderTemplate("a\nb\n{% foo %}", {}); CHECK(false, "istruzione sconosciuta"); }
    catch (TemplateError& e) { CHECK_EQ(e.line(), 3, "riga errore"); }
    CHECK(TemplateReferences("x {{ BITRATE_GRAPH }}", "BITRATE_GRAPH") && !TemplateReferences("x {{ A }}", "BITRATE_GRAPH"), "rilevamento variabili");

    // --- torrent da fixture (confrontato con riferimento Python nel test di build)
    if (argc > 1) {
        std::ifstream list(std::string(argv[1]) + "/files.txt");
        std::string line;
        TorrentSpec spec;
        spec.name = "fixture";
        spec.singleFile = false;
        std::vector<std::string> paths;
        uint64_t total = 0;
        while (std::getline(list, line)) {
            if (line.empty()) continue;
            TorrentEntry e;
            e.path = Split(line, '/');
            std::ifstream f(std::string(argv[1]) + "/fixture/" + line, std::ios::binary | std::ios::ate);
            e.length = uint64_t(f.tellg());
            total += e.length;
            spec.files.push_back(e);
            paths.push_back(line);
        }
        spec.pieceLength = 32768;
        PieceHasher h(spec.pieceLength);
        std::vector<char> buf(100000);
        for (auto& p : paths) {
            std::ifstream f(std::string(argv[1]) + "/fixture/" + p, std::ios::binary);
            while (f) { f.read(buf.data(), buf.size()); h.Feed(buf.data(), size_t(f.gcount())); }
        }
        spec.pieces = h.Finish();
        spec.trackers = {"udp://tracker.example:1337/announce", "http://t2.example/announce"};
        spec.isPrivate = true;
        spec.creationDate = 1700000000;
        auto r = BuildTorrent(spec);
        std::ofstream(std::string(argv[1]) + "/out.torrent", std::ios::binary) << r.bencoded;
        ParsedTorrent pt2;
        std::string perr;
        CHECK(ParseTorrent(r.bencoded, pt2, &perr) && pt2.infoHash == r.infoHash && pt2.files.size() == spec.files.size() &&
              pt2.pieces == spec.pieces && pt2.isPrivate && pt2.creationDate == 1700000000 && pt2.files[1].path == spec.files[1].path,
              "rilettura .torrent (ripresa)");
        std::printf("INFOHASH %s\nMAGNET %s\n", r.infoHash.c_str(), BuildMagnet(r.infoHash, spec.name, total, spec.trackers).c_str());
    }
    std::printf(failures ? "\n%d TEST FALLITI\n" : "\nTUTTI I TEST SUPERATI\n", failures);
    return failures ? 1 : 0;
}
