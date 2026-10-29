#include "torrent.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <limits>
#include <stdexcept>
#include <set>

#include "strutil.h"

namespace rf {

// ---------------------------------------------------------------- SHA-1 (FIPS 180-4)

static inline uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

Sha1::Sha1() {
    h_[0] = 0x67452301; h_[1] = 0xEFCDAB89; h_[2] = 0x98BADCFE; h_[3] = 0x10325476; h_[4] = 0xC3D2E1F0;
}

void Sha1::Block(const unsigned char* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = Rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = Rol(b, 30); b = a; a = t;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
}

void Sha1::Update(const void* data, size_t len) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    total_ += len;
    if (used_) {
        size_t take = std::min(len, 64 - used_);
        std::memcpy(buf_ + used_, p, take);
        used_ += take; p += take; len -= take;
        if (used_ == 64) { Block(buf_); used_ = 0; }
    }
    while (len >= 64) { Block(p); p += 64; len -= 64; }
    if (len) { std::memcpy(buf_, p, len); used_ = len; }
}

void Sha1::Final(unsigned char out[20]) {
    uint64_t bits = total_ * 8;
    unsigned char pad = 0x80;
    Update(&pad, 1);
    unsigned char zero = 0;
    while (used_ != 56) Update(&zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; ++i) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    Update(len, 8);
    for (int i = 0; i < 5; ++i) {
        out[4 * i] = (unsigned char)(h_[i] >> 24);
        out[4 * i + 1] = (unsigned char)(h_[i] >> 16);
        out[4 * i + 2] = (unsigned char)(h_[i] >> 8);
        out[4 * i + 3] = (unsigned char)h_[i];
    }
}

void Sha1::Hash(const void* data, size_t len, unsigned char out[20]) {
    Sha1 s;
    s.Update(data, len);
    s.Final(out);
}

// ---------------------------------------------------------------- PieceHasher

PieceHasher::PieceHasher(uint32_t pieceLength, HashFn fn)
    : pieceLength_(pieceLength), fn_(std::move(fn)), buf_(pieceLength) {
    if (!ValidPieceLength(pieceLength)) throw std::invalid_argument("Dimensione pezzi non valida");
    if (!fn_) fn_ = [](const unsigned char* d, size_t n, unsigned char out[20]) { Sha1::Hash(d, n, out); };
}

void PieceHasher::Flush() {
    unsigned char digest[20];
    fn_(buf_.data(), used_, digest);
    pieces_.append(reinterpret_cast<const char*>(digest), 20);
    used_ = 0;
}

void PieceHasher::Feed(const void* data, size_t len) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    fed_ += len;
    while (len) {
        size_t take = std::min<size_t>(len, pieceLength_ - used_);
        std::memcpy(buf_.data() + used_, p, take);
        used_ += take; p += take; len -= take;
        if (used_ == pieceLength_) Flush();
    }
}

std::string PieceHasher::Finish() {
    if (used_) Flush();
    return pieces_;
}

// ---------------------------------------------------------------- Bencode

std::string BencodeString(const std::string& s) { return std::to_string(s.size()) + ":" + s; }
std::string BencodeInt(int64_t v) { return "i" + std::to_string(v) + "e"; }

// Dizionario con chiavi ordinate per byte (requisito BEP 3).
class BDict {
public:
    void Put(const std::string& key, std::string encodedValue) { items_[key] = std::move(encodedValue); }
    std::string Encode() const {
        std::string out = "d";
        for (auto& kv : items_) out += BencodeString(kv.first) + kv.second;  // std::map<string> ordina per byte
        return out + "e";
    }

private:
    std::map<std::string, std::string> items_;
};


std::string V2LayerRoot(const std::string& layer, uint32_t pieceLength);

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
bool SafePart(const std::string& s) {
    return !s.empty() && s != "." && s != ".." && s.find_first_of("/\\:") == std::string::npos && s.find('\0') == std::string::npos;
}
struct FileTree {
    std::map<std::string, FileTree> children;
    const TorrentEntry* file = nullptr;
    std::string Encode() const {
        BDict d;
        if (file) {
            BDict props;
            props.Put("length", BencodeInt(int64_t(file->length)));
            if (file->length) props.Put("pieces root", BencodeString(file->piecesRoot));
            d.Put("", props.Encode());
        }
        for (auto& [name,child] : children) d.Put(name,child.Encode());
        return d.Encode();
    }
};
std::string EncodedFile(const std::vector<std::string>& parts, uint64_t length, bool pad = false) {
    BDict d;
    d.Put("length", BencodeInt(int64_t(length)));
    if (pad) d.Put("attr", BencodeString("p"));
    std::string path="l";
    for (auto& part : parts) path+=BencodeString(part);
    d.Put("path",path+"e");
    return d.Encode();
}
uint64_t Padding(uint64_t length,uint32_t piece) { return (piece-length%piece)%piece; }
}

TorrentResult BuildTorrent(const TorrentSpec& spec) {
    Require(spec.format==TorrentFormat::V1 || spec.format==TorrentFormat::V2 || spec.format==TorrentFormat::Hybrid,"Formato torrent non valido");
    Require(ValidPieceLength(spec.pieceLength),"Dimensione pezzi non valida");
    Require(SafePart(spec.name),"Nome torrent non valido");
    Require(!spec.files.empty() && (!spec.singleFile || spec.files.size()==1),"Elenco file non valido");
    const bool v1=spec.format!=TorrentFormat::V2, v2=spec.format!=TorrentFormat::V1;
    FileTree tree;
    BDict layers;
    std::map<std::string,std::string> uniqueLayers;
    std::set<std::vector<std::string>> paths;
    uint64_t streamLength=0;
    std::string fileList="l";
    std::vector<std::string> previous;
    for (size_t i=0;i<spec.files.size();++i) {
        auto& f=spec.files[i];
        Require(f.length<=uint64_t(std::numeric_limits<int64_t>::max()),"File troppo grande");
        auto path=spec.singleFile ? std::vector<std::string>{spec.name} : f.path;
        Require(!path.empty() && path.size()<=60,"Percorso file non valido");
        for (auto& part:path) Require(SafePart(part),"Componente percorso non valido");
        Require(paths.insert(path).second,"Percorso duplicato");
        if (v2 && i) Require(previous<path,"File v2 non ordinati per percorso UTF-8");
        previous=path;
        auto node=&tree;
        for (auto& part:path) {
            Require(!node->file,"Conflitto file/cartella");
            node=&node->children[part];
        }
        Require(node->children.empty(),"Conflitto file/cartella");
        node->file=&f;
        Require(streamLength<=uint64_t(std::numeric_limits<int64_t>::max())-f.length,"Torrent troppo grande");
        streamLength+=f.length;
        if (v2) {
            Require(f.length ? f.piecesRoot.size()==32 : f.piecesRoot.empty(),"Pieces root non valido");
            uint64_t count=f.length/spec.pieceLength+(f.length%spec.pieceLength!=0);
            if (f.length>spec.pieceLength) {
                Require(count==f.pieceLayer.size()/32 && f.pieceLayer.size()%32==0,"Numero hash piece layer non valido");
                Require(V2LayerRoot(f.pieceLayer,spec.pieceLength)==f.piecesRoot,"Piece layer non corrisponde alla root");
                auto [it,inserted]=uniqueLayers.emplace(f.piecesRoot,f.pieceLayer);
                Require(inserted || it->second==f.pieceLayer,"Piece layer in conflitto");
                layers.Put(f.piecesRoot,BencodeString(f.pieceLayer));
            } else Require(f.pieceLayer.empty(),"Piece layer superfluo");
        }
        if (v1 && !spec.singleFile) {
            fileList+=EncodedFile(f.path,f.length);
            if (v2 && i+1<spec.files.size() && Padding(f.length,spec.pieceLength)) {
                auto pad=Padding(f.length,spec.pieceLength);
                // Reserve .pad for virtual files, never touch disk.
                for (auto& real:spec.files) Require(real.path.empty() || real.path[0]!=".pad","Cartella .pad riservata ai file di padding hybrid");
                fileList+=EncodedFile({".pad",std::to_string(i)+"_"+std::to_string(pad)},pad,true);
                Require(streamLength<=uint64_t(std::numeric_limits<int64_t>::max())-pad,"Torrent troppo grande");
                streamLength+=pad;
            }
        }
    }
    BDict info;
    info.Put("name",BencodeString(spec.name));
    info.Put("piece length",BencodeInt(spec.pieceLength));
    if (spec.isPrivate) info.Put("private",BencodeInt(1));
    if (!spec.source.empty()) info.Put("source",BencodeString(spec.source));
    if (v1) {
        uint64_t count=streamLength/spec.pieceLength+(streamLength%spec.pieceLength!=0);
        Require(spec.pieces.size()%20==0 && count==spec.pieces.size()/20,"Numero hash SHA-1 non valido");
        info.Put("pieces",BencodeString(spec.pieces));
        if (spec.singleFile) info.Put("length",BencodeInt(int64_t(spec.files[0].length)));
        else info.Put("files",fileList+"e");
    } else Require(spec.pieces.empty(),"SHA-1 superfluo in torrent v2");
    if (v2) {
        info.Put("meta version",BencodeInt(2));
        info.Put("file tree",tree.Encode());
    }
    auto encoded=info.Encode();
    BDict root;
    if (!spec.trackers.empty()) {
        root.Put("announce",BencodeString(spec.trackers[0]));
        std::string tiers="l";
        for (auto& t:spec.trackers) tiers+="l"+BencodeString(t)+"e";
        root.Put("announce-list",tiers+"e");
    }
    root.Put("created by",BencodeString(spec.createdBy));
    root.Put("creation date",BencodeInt(spec.creationDate));
    root.Put("info",encoded);
    if (v2) root.Put("piece layers",layers.Encode());
    TorrentResult result;
    result.bencoded=root.Encode();
    if (v1) {
        unsigned char hash[20];Sha1::Hash(encoded.data(),encoded.size(),hash);
        result.infoHash=HexLower(hash,20);
    }
    if (v2) {
        auto hash=Sha256::Hash(encoded.data(),encoded.size());
        result.infoHashV2=HexLower(reinterpret_cast<const unsigned char*>(hash.data()),hash.size());
    }
    return result;
}

// ---------------------------------------------------------------- Lettura bencode

namespace {

struct BNode {
    enum K { Int, Str, List, Dict } k = Str;
    int64_t i = 0;
    std::string s;
    std::vector<BNode> list;
    std::vector<std::pair<std::string, BNode>> dict;
    size_t begin = 0, end = 0;  // posizione nel buffer (serve per l'info-hash)
    const BNode* Get(const std::string& key) const {
        for (auto& kv : dict) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

bool Decode(const std::string& d, size_t& p, BNode& n, int depth) {
    if (depth > 64 || p >= d.size()) return false;
    n.begin = p;
    char c = d[p];
    if (c == 'i') {
        size_t e = d.find('e', p);
        if (e == std::string::npos) return false;
        auto number=d.substr(p+1,e-p-1);
        size_t start=(!number.empty() && number[0]=='-') ? 1 : 0;
        if (start==number.size() || (number[start]=='0' && (number.size()-start!=1 || start)) ||
            number.find_first_not_of("0123456789",start)!=std::string::npos) return false;
        try { n.i=std::stoll(number); } catch (...) { return false; }
        n.k = BNode::Int;
        p = e + 1;
    } else if (c == 'l' || c == 'd') {
        n.k = c == 'l' ? BNode::List : BNode::Dict;
        ++p;
        while (p < d.size() && d[p] != 'e') {
            if (n.k == BNode::List) {
                BNode child;
                if (!Decode(d, p, child, depth + 1)) return false;
                n.list.push_back(std::move(child));
            } else {
                BNode key, val;
                if (!Decode(d, p, key, depth + 1) || key.k != BNode::Str) return false;
                if (!Decode(d, p, val, depth + 1)) return false;
                if (!n.dict.empty() && !(n.dict.back().first < key.s)) return false;
                n.dict.emplace_back(key.s, std::move(val));
            }
        }
        if (p >= d.size()) return false;
        ++p;
    } else if (c >= '0' && c <= '9') {
        size_t colon = d.find(':', p);
        if (colon == std::string::npos) return false;
        uint64_t len = 0;
        auto number=d.substr(p,colon-p);
        if (number.empty() || (number.size()>1 && number[0]=='0') || number.find_first_not_of("0123456789")!=std::string::npos) return false;
        try { len=std::stoull(number); } catch (...) { return false; }
        if (len > d.size() - colon - 1) return false;
        n.k = BNode::Str;
        n.s = d.substr(colon + 1, size_t(len));
        p = colon + 1 + size_t(len);
    } else {
        return false;
    }
    n.end = p;
    return true;
}

}  // namespace


bool ParseTorrent(const std::string& data, ParsedTorrent& out, std::string* error) {
    out=ParsedTorrent();
    if (error) error->clear();
    try {
        BNode root;size_t p=0;
        Require(Decode(data,p,root,0) && p==data.size() && root.k==BNode::Dict,"Bencode non valido o non canonico");
        auto info=root.Get("info");
        Require(info && info->k==BNode::Dict,"Dizionario info mancante");
        auto version=info->Get("meta version");
        if (version) Require(version->k==BNode::Int && version->i==2,"Meta version non supportata");
        auto text=[](const BNode* n)->std::string {
            Require(n && n->k==BNode::Str,"Stringa mancante o non valida");return n->s;
        };
        auto integer=[](const BNode* n)->int64_t {
            Require(n && n->k==BNode::Int && n->i>=0,"Intero mancante o negativo");return n->i;
        };
        TorrentSpec spec;
        auto pieces=info->Get("pieces");
        spec.format=version ? (pieces ? TorrentFormat::Hybrid : TorrentFormat::V2) : TorrentFormat::V1;
        spec.name=text(info->Get("name"));
        auto pl=integer(info->Get("piece length"));
        Require(pl<=16777216 && ValidPieceLength(uint32_t(pl)),"Dimensione pezzi non valida");
        spec.pieceLength=uint32_t(pl);
        if (auto pr=info->Get("private")) { Require(integer(pr)<=1,"Flag private non valido");spec.isPrivate=pr->i==1; }
        if (auto src=info->Get("source")) spec.source=text(src);
        if (auto date=root.Get("creation date")) spec.creationDate=integer(date);
        auto files=info->Get("files"), length=info->Get("length");
        if (spec.format!=TorrentFormat::V2) {
            spec.pieces=text(pieces);
            Require((files!=nullptr)!=(length!=nullptr),"Servono files oppure length");
            spec.singleFile=length!=nullptr;
            if (length) { TorrentEntry f;f.length=uint64_t(integer(length));spec.files.push_back(f); }
            else {
                Require(files->k==BNode::List && !files->list.empty(),"Elenco file non valido");
                for (auto& n:files->list) {
                    Require(n.k==BNode::Dict,"Voce file non valida");
                    TorrentEntry f;f.length=uint64_t(integer(n.Get("length")));
                    auto path=n.Get("path");Require(path && path->k==BNode::List && !path->list.empty(),"Percorso mancante");
                    for (auto& part:path->list) f.path.push_back(text(&part));
                    auto attr=n.Get("attr");
                    bool pad=attr && text(attr).find('p')!=std::string::npos;
                    Require(!pad || spec.format==TorrentFormat::Hybrid,"Padding inatteso");
                    if (!pad) spec.files.push_back(f);
                }
            }
        }
        if (version) {
            auto tree=info->Get("file tree"), layers=root.Get("piece layers");
            Require(tree && tree->k==BNode::Dict && !tree->Get(""),"File tree non valido");
            Require(layers && layers->k==BNode::Dict,"Piece layers mancante");
            std::vector<TorrentEntry> v2files;
            std::function<void(const BNode&,std::vector<std::string>)> walk;
            walk=[&](const BNode& node,std::vector<std::string> path) {
                Require(node.k==BNode::Dict && !node.dict.empty(),"Nodo file tree non valido");
                if (auto props=node.Get("")) {
                    Require(node.dict.size()==1 && props->k==BNode::Dict && !path.empty(),"Foglia file tree non valida");
                    TorrentEntry f;f.path=path;f.length=uint64_t(integer(props->Get("length")));
                    if (auto hash=props->Get("pieces root")) f.piecesRoot=text(hash);
                    if (f.length>spec.pieceLength) f.pieceLayer=text(layers->Get(f.piecesRoot));
                    v2files.push_back(f);
                } else for (auto& [part,child]:node.dict) {
                    Require(SafePart(part),"Percorso file tree non valido");
                    auto next=path;next.push_back(part);walk(child,std::move(next));
                }
            };
            walk(*tree,{});
            if (spec.format==TorrentFormat::V2) {
                Require(!files && !length,"Campi v1 inattesi");
                spec.singleFile=v2files.size()==1 && v2files[0].path==std::vector<std::string>{spec.name};
                spec.files=v2files;
                if (spec.singleFile) spec.files[0].path.clear();
            } else {
                Require(v2files.size()==spec.files.size(),"Numero file v1/v2 diverso");
                for (size_t i=0;i<v2files.size();++i) {
                    auto& f=spec.files[i];auto& v=v2files[i];
                    Require(f.length==v.length && (spec.singleFile ? std::vector<std::string>{spec.name} : f.path)==v.path,"File v1/v2 diversi");
                    f.piecesRoot=v.piecesRoot;f.pieceLayer=v.pieceLayer;
                }
            }
            std::set<std::string> requiredLayers;
            for (auto& f:spec.files) if (f.length>spec.pieceLength) requiredLayers.insert(f.piecesRoot);
            Require(requiredLayers.size()==layers->dict.size(),"Piece layers superflui");
        }
        // Rebuilding validates roots, piece counts, path collisions and the exact
        // hybrid padding layout. Unknown info fields trigger safe rehash on resume.
        auto built=BuildTorrent(spec);
        BNode rebuilt;size_t q=0;
        Require(Decode(built.bencoded,q,rebuilt,0),"Errore interno bencode");
        auto rebuiltInfo=rebuilt.Get("info");
        Require(data.substr(info->begin,info->end-info->begin)==built.bencoded.substr(rebuiltInfo->begin,rebuiltInfo->end-rebuiltInfo->begin),"Metadati info non supportati o layout incoerente");
        ParsedTorrent parsed;
        parsed.format=spec.format;parsed.name=spec.name;parsed.singleFile=spec.singleFile;
        parsed.files=spec.files;parsed.pieceLength=spec.pieceLength;parsed.pieces=spec.pieces;
        parsed.isPrivate=spec.isPrivate;parsed.source=spec.source;parsed.creationDate=spec.creationDate;
        parsed.infoHash=built.infoHash;parsed.infoHashV2=built.infoHashV2;
        out=std::move(parsed);return true;
    } catch (const std::exception& e) {
        if (error) *error=e.what();
        return false;
    }
}

bool TorrentMatches(const ParsedTorrent& parsed, const TorrentSpec& expected, std::string* reason) {
    auto fail=[&](const char* why) { if (reason) *reason=why;return false; };
    if (reason) reason->clear();
    if (parsed.format!=expected.format) return fail("formato torrent diverso");
    if (parsed.name!=expected.name || parsed.files.size()!=expected.files.size()) return fail("contenuto diverso");
    if (parsed.format!=TorrentFormat::V2 && parsed.singleFile!=expected.singleFile) return fail("struttura torrent diversa");
    if (parsed.pieceLength!=expected.pieceLength) return fail("dimensione pezzi diversa");
    if (parsed.isPrivate!=expected.isPrivate) return fail("flag privato diverso");
    if (parsed.source!=expected.source) return fail("campo source diverso");
    for (size_t i=0;i<expected.files.size();++i) {
        auto actualPath=parsed.singleFile ? std::vector<std::string>{parsed.name} : parsed.files[i].path;
        auto expectedPath=expected.singleFile ? std::vector<std::string>{expected.name} : expected.files[i].path;
        if (parsed.files[i].length!=expected.files[i].length || actualPath!=expectedPath) return fail("file diversi");
    }
    return true;
}

uint32_t PieceSizeForTotal(uint64_t s) {
    const uint64_t MB = 1024ull * 1024ull;
    if (s <= 50 * MB) return 32 * 1024;
    if (s <= 150 * MB) return 64 * 1024;
    if (s <= 350 * MB) return 128 * 1024;
    if (s <= 512 * MB) return 256 * 1024;
    if (s <= 1024 * MB) return 512 * 1024;
    if (s <= 2048 * MB) return 1024 * 1024;
    if (s <= 4096 * MB) return 2048 * 1024;
    if (s <= 8192 * MB) return 4096 * 1024;
    if (s <= 16384 * MB) return 8192 * 1024;
    return 16384 * 1024;
}

std::string BuildMagnet(const std::string& infoHash, const std::string& name, uint64_t totalBytes,
                        const std::vector<std::string>& trackers) {
    std::string m = "magnet:?xt=urn:btih:" + infoHash + "&dn=" + UrlEncode(name) + "&xl=" + std::to_string(totalBytes);
    for (auto& t : trackers) m += "&tr=" + UrlEncode(t);
    return m;
}

std::string BuildMagnet(const TorrentResult& torrent, const std::string& name, uint64_t totalBytes,
                        const std::vector<std::string>& trackers) {
    std::string m="magnet:?";
    if (!torrent.infoHash.empty()) m+="xt=urn:btih:"+torrent.infoHash;
    if (!torrent.infoHashV2.empty()) {
        if (!torrent.infoHash.empty()) m+='&';
        m+="xt=urn:btmh:1220"+torrent.infoHashV2;
    }
    m+="&dn="+UrlEncode(name)+"&xl="+std::to_string(totalBytes);
    for (auto& t:trackers) m+="&tr="+UrlEncode(t);
    return m;
}

bool IsValidTrackerUrl(const std::string& url) {
    std::string u = ToLower(url);
    for (const char* scheme : {"http://", "https://", "udp://", "wss://"}) {
        if (StartsWith(u, scheme) && u.size() > std::strlen(scheme) + 2 && u.find(' ') == std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> ParseTrackerList(const std::string& text) {
    std::vector<std::string> out;
    for (auto& line : SplitLines(text)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (!IsValidTrackerUrl(t)) continue;
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    return out;
}

std::vector<std::string> MergeTrackerLists(const std::vector<std::string>& current, const std::vector<std::string>& imported,
                                           size_t maxTotal) {
    std::vector<std::string> out = current;
    for (auto& t : imported) {
        if (maxTotal && out.size() >= maxTotal) break;
        bool dup = false;
        for (auto& m : out) if (IEquals(m, t)) { dup = true; break; }
        if (!dup) out.push_back(t);
    }
    return out;
}

std::string FormatTrackerList(const std::vector<std::string>& trackers) {
    std::string out;
    for (size_t i = 0; i < trackers.size(); ++i) {
        if (i) out += "\r\n\r\n";
        out += trackers[i];
    }
    if (!out.empty()) out += "\r\n";
    return out;
}

}  // namespace rf
