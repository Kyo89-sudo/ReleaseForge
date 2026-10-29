#include "core/torrent.h"
#include "core/strutil.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace rf;
static int failures=0;
static void Check(bool b,const char* msg) { if (!b) { ++failures;std::cerr<<"FAIL "<<msg<<'\n'; } }
template<class F> static void Reject(F f,const char* msg) {
    try { f();Check(false,msg); } catch (const std::invalid_argument&) {}
}
static std::string Hex(const std::string& s) { return HexLower(reinterpret_cast<const unsigned char*>(s.data()),s.size()); }

static TorrentSpec Make(TorrentFormat format,uint32_t piece,bool single,const std::vector<size_t>& sizes) {
    TorrentSpec s;s.format=format;s.name=single ? "sample.bin" : "release";s.singleFile=single;s.pieceLength=piece;
    s.creationDate=1700000000;s.isPrivate=true;s.source="test";
    s.trackers={"udp://tracker.example:80/announce","https://tracker.example/announce"};
    PieceHasher v1(piece);
    for (size_t i=0;i<sizes.size();++i) {
        TorrentEntry e;e.length=sizes[i];
        if (!single) e.path={"dir"+std::to_string(i),"file-\xc3\xa8.bin"};
        std::string data(sizes[i],char('a'+i));
        V2FileHasher v2(piece);
        // Deliberately cross block and piece boundaries using tiny/uneven reads.
        for (size_t pos=0;pos<data.size();) {
            size_t n=std::min<size_t>(7919,data.size()-pos);
            if (format!=TorrentFormat::V2) v1.Feed(data.data()+pos,n);
            if (format!=TorrentFormat::V1) v2.Feed(data.data()+pos,n);
            pos+=n;
        }
        if (format!=TorrentFormat::V1) v2.Finish(e.piecesRoot,e.pieceLayer);
        if (format==TorrentFormat::Hybrid && !single && i+1<sizes.size()) {
            std::string pad((piece-sizes[i]%piece)%piece,'\0');v1.Feed(pad.data(),pad.size());
        }
        s.files.push_back(e);
    }
    if (format!=TorrentFormat::V2) s.pieces=v1.Finish();
    return s;
}

int main(int argc,char** argv) {
    Check(Hex(Sha256::Hash("",0))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty");
    Check(Hex(Sha256::Hash("abc",3))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc");
    std::string million(1000000,'a');
    Check(Hex(Sha256::Hash(million.data(),million.size()))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 million");
    Check(TorrentSpec().format==TorrentFormat::Hybrid && ParseTorrentFormat("unknown")==TorrentFormat::Hybrid && ParseTorrentFormat("")==TorrentFormat::Hybrid,"default/migration hybrid");
    for (auto f:{TorrentFormat::V1,TorrentFormat::V2,TorrentFormat::Hybrid}) Check(ParseTorrentFormat(TorrentFormatName(f))==f,"format settings roundtrip");
    for (uint32_t p:{0u,1u,8192u,24576u,33554432u}) {
        Reject([&]{PieceHasher h(p);},"invalid v1 piece size");
        Reject([&]{V2FileHasher h(p);},"invalid v2 piece size");
    }
    size_t fixture=0;
    if (argc>1) std::filesystem::create_directories(argv[1]);
    for (uint32_t piece:{16384u,32768u,65536u}) {
        std::vector<size_t> edges={0,1,16383,16384,16385,piece-1,piece,piece+1,piece*3+7};
        for (auto format:{TorrentFormat::V1,TorrentFormat::V2,TorrentFormat::Hybrid}) {
            for (bool single:{true,false}) for (auto size:edges) {
                auto sizes=single ? std::vector<size_t>{size} : std::vector<size_t>{0,size,piece*3+7,1,0,piece};
                auto s=Make(format,piece,single,sizes);
                auto result=BuildTorrent(s);
                ParsedTorrent parsed;std::string error;
                Check(ParseTorrent(result.bencoded,parsed,&error),"parse generated torrent");
                if (!error.empty()) std::cerr<<error<<'\n';
                Check(parsed.format==format && parsed.files.size()==sizes.size() && parsed.infoHash==result.infoHash && parsed.infoHashV2==result.infoHashV2,"roundtrip mode/files/hashes");
                Check((format==TorrentFormat::V2)==result.infoHash.empty(),"SHA1 only for v1/hybrid");
                Check((format==TorrentFormat::V1)==result.infoHashV2.empty(),"SHA256 only for v2/hybrid");
                for (size_t i=0;i<parsed.files.size();++i) Check(parsed.files[i].piecesRoot==s.files[i].piecesRoot && parsed.files[i].pieceLayer==s.files[i].pieceLayer,"roundtrip layers");
                auto magnet=BuildMagnet(result,s.name,1,s.trackers);
                Check((magnet.find("urn:btih:")!=std::string::npos)==(format!=TorrentFormat::V2),"magnet v1");
                Check((magnet.find("urn:btmh:1220")!=std::string::npos)==(format!=TorrentFormat::V1),"magnet v2 multihash");
                Check(!ParseTorrent(result.bencoded+"junk",parsed),"reject trailing bytes");
                Check(!ParseTorrent(result.bencoded.substr(0,result.bencoded.size()-1),parsed),"reject truncated torrent");
                if (argc>1) {
                    auto stem=std::filesystem::path(argv[1])/std::to_string(fixture++);
                    std::ofstream(stem.string()+".torrent",std::ios::binary)<<result.bencoded;
                    std::ofstream(stem.string()+".txt")<<TorrentFormatName(format)<<'\n'<<piece<<'\n'<<single<<'\n';
                    std::ofstream hashes(stem.string()+".hashes");hashes<<result.infoHash<<'\n'<<result.infoHashV2<<'\n'<<magnet<<'\n';
                    std::ofstream payload(stem.string()+".sizes");for(auto n:sizes) payload<<n<<'\n';
                }
            }
        }
    }
    auto good=Make(TorrentFormat::Hybrid,32768,false,{1,0,100000,1});
    auto bad=good;bad.files[2].pieceLayer[0]^=1;
    Reject([&]{BuildTorrent(bad);},"reject corrupt layer");
    bad=good;bad.files[2].pieceLayer.pop_back();
    Reject([&]{BuildTorrent(bad);},"reject short layer");
    bad=good;bad.files[0].piecesRoot.clear();Reject([&]{BuildTorrent(bad);},"require nonempty file root");
    bad=good;bad.files[1].piecesRoot=std::string(32,'x');Reject([&]{BuildTorrent(bad);},"empty file has no root");
    bad=good;bad.files[0].pieceLayer=std::string(32,'x');Reject([&]{BuildTorrent(bad);},"no layer for small file");
    bad=good;bad.pieces.pop_back();Reject([&]{BuildTorrent(bad);},"reject wrong v1 hash count");
    bad=good;std::swap(bad.files[0],bad.files[2]);Reject([&]{BuildTorrent(bad);},"require v2 canonical order");
    bad=good;bad.files[1].path=bad.files[0].path;Reject([&]{BuildTorrent(bad);},"reject duplicate path");
    bad=good;bad.files[1].path={"dir0"};Reject([&]{BuildTorrent(bad);},"reject file/directory conflict");
    bad=good;bad.files[0].path={"..","bad"};Reject([&]{BuildTorrent(bad);},"reject traversal");
    bad=good;bad.files[0].path={std::string("x\0y",3)};Reject([&]{BuildTorrent(bad);},"reject NUL");
    bad=good;bad.files[0].path={".pad","real"};Reject([&]{BuildTorrent(bad);},"reject pad collision");
    // Duplicate contents share one piece-layer key.
    auto duplicate=Make(TorrentFormat::V2,32768,false,{100000,100000});
    duplicate.files[1].piecesRoot=duplicate.files[0].piecesRoot;
    duplicate.files[1].pieceLayer=duplicate.files[0].pieceLayer;
    ParsedTorrent pt;
    Check(ParseTorrent(BuildTorrent(duplicate).bencoded,pt),"deduplicate identical roots");
    // Resume compares the selected format and all payload metadata; trackers are mutable.
    for (auto format:{TorrentFormat::V1,TorrentFormat::V2,TorrentFormat::Hybrid}) {
        auto current=Make(format,32768,false,{1,0,100000,1});
        ParsedTorrent parsed;
        Check(ParseTorrent(BuildTorrent(current).bencoded,parsed),"resume fixture parses");
        Check(TorrentMatches(parsed,current),"unchanged payload resumes");
        current.trackers={"https://replacement.example/announce"};
        Check(TorrentMatches(parsed,current),"tracker update resumes");
        auto rebuilt=BuildTorrent(current);
        Check(rebuilt.infoHash==parsed.infoHash && rebuilt.infoHashV2==parsed.infoHashV2,"trackers preserve both infohashes");
        auto changed=current;changed.format=format==TorrentFormat::Hybrid ? TorrentFormat::V1 : TorrentFormat::Hybrid;
        Check(!TorrentMatches(parsed,changed),"format change requires rehash");
        changed=current;changed.pieceLength*=2;Check(!TorrentMatches(parsed,changed),"piece change requires rehash");
        changed=current;changed.isPrivate=false;Check(!TorrentMatches(parsed,changed),"private change requires rehash");
        changed=current;changed.files[0].length++;Check(!TorrentMatches(parsed,changed),"size change requires rehash");
        changed=current;changed.files[0].path={"other"};Check(!TorrentMatches(parsed,changed),"path change requires rehash");
        changed=current;changed.source="other";Check(!TorrentMatches(parsed,changed),"source change requires rehash");
    }
    // A pure v2 one-file tree has no separate single/multi flag.
    auto one=Make(TorrentFormat::V2,32768,true,{10});
    Check(ParseTorrent(BuildTorrent(one).bencoded,pt),"v2 single parses");
    one.singleFile=false;one.files[0].path={one.name};
    Check(TorrentMatches(pt,one),"v2 one-file directory resume");
    Check(ParseTorrent(BuildTorrent(one).bencoded,pt),"v2 one-file directory rebuild");
    V2FileHasher accelerated(32768, [](const unsigned char* d,size_t n){return Sha256::Hash(d,n);});
    accelerated.Feed("abc",3);std::string root,layer;accelerated.Finish(root,layer);
    Check(root==Sha256::Hash("abc",3) && layer.empty(),"injected SHA256 provider");
    std::string rootAgain,layerAgain;accelerated.Finish(rootAgain,layerAgain);
    Check(root==rootAgain && layer==layerAgain,"idempotent v2 finish");
    auto encoded=BuildTorrent(good).bencoded;
    auto corrupt=encoded;
    auto pos=corrupt.find("12:piece layersd32:");
    Check(pos!=std::string::npos,"piece layers exists");
    if(pos!=std::string::npos) { corrupt[pos+19]^=1;Check(!ParseTorrent(corrupt,pt),"reject corrupt piece layer in parser"); }
    auto replace=[&](std::string from,std::string to) {
        auto text=encoded;auto at=text.find(from);Check(at!=std::string::npos,"mutation found");
        if(at!=std::string::npos) text.replace(at,from.size(),to);
        Check(!ParseTorrent(text,pt),"reject malformed metadata");
    };
    replace("12:meta versioni2e","12:meta versioni3e");
    replace("12:meta versioni2e","12:meta versioni02e");
    replace("7:privatei1e","7:privatei-0e");
    replace("4:name7:release","04:name7:release");
    replace("4:name7:release","4:name7:release4:name7:release");
    replace("4:attr1:p","4:attr1:x");
    std::cout<<"Torrent tests: "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
