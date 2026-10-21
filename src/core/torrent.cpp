#include "torrent.h"

#include <algorithm>
#include <cstring>
#include <map>

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

TorrentResult BuildTorrent(const TorrentSpec& spec) {
    BDict info;
    info.Put("name", BencodeString(spec.name));
    info.Put("piece length", BencodeInt(spec.pieceLength));
    info.Put("pieces", BencodeString(spec.pieces));
    if (spec.isPrivate) info.Put("private", BencodeInt(1));
    if (!spec.source.empty()) info.Put("source", BencodeString(spec.source));
    if (spec.singleFile) {
        info.Put("length", BencodeInt(int64_t(spec.files.empty() ? 0 : spec.files[0].length)));
    } else {
        std::string list = "l";
        for (auto& f : spec.files) {
            BDict fd;
            fd.Put("length", BencodeInt(int64_t(f.length)));
            std::string path = "l";
            for (auto& part : f.path) path += BencodeString(part);
            fd.Put("path", path + "e");
            list += fd.Encode();
        }
        info.Put("files", list + "e");
    }
    std::string infoEncoded = info.Encode();
    unsigned char digest[20];
    Sha1::Hash(infoEncoded.data(), infoEncoded.size(), digest);

    BDict root;
    if (!spec.trackers.empty()) {
        root.Put("announce", BencodeString(spec.trackers[0]));
        std::string tiers = "l";
        for (auto& t : spec.trackers) tiers += "l" + BencodeString(t) + "e";  // un tracker per tier (come torf)
        root.Put("announce-list", tiers + "e");
    }
    root.Put("created by", BencodeString(spec.createdBy));
    root.Put("creation date", BencodeInt(spec.creationDate));
    root.Put("info", infoEncoded);

    TorrentResult r;
    r.bencoded = root.Encode();
    r.infoHash = HexLower(digest, 20);
    return r;
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
        try { n.i = std::stoll(d.substr(p + 1, e - p - 1)); } catch (...) { return false; }
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
                n.dict.emplace_back(key.s, std::move(val));
            }
        }
        if (p >= d.size()) return false;
        ++p;
    } else if (c >= '0' && c <= '9') {
        size_t colon = d.find(':', p);
        if (colon == std::string::npos) return false;
        uint64_t len = 0;
        try { len = std::stoull(d.substr(p, colon - p)); } catch (...) { return false; }
        if (colon + 1 + len > d.size()) return false;
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
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    BNode root;
    size_t p = 0;
    if (!Decode(data, p, root, 0) || root.k != BNode::Dict) return fail("file .torrent non valido");
    const BNode* info = root.Get("info");
    if (!info || info->k != BNode::Dict) return fail("dizionario info mancante");
    const BNode* name = info->Get("name");
    const BNode* pl = info->Get("piece length");
    const BNode* pieces = info->Get("pieces");
    if (!name || !pl || !pieces || pieces->s.size() % 20) return fail("campi info incompleti");
    out = ParsedTorrent();
    out.name = name->s;
    out.pieceLength = uint32_t(pl->i);
    out.pieces = pieces->s;
    if (const BNode* pr = info->Get("private")) out.isPrivate = pr->i == 1;
    if (const BNode* src = info->Get("source")) out.source = src->s;
    if (const BNode* cd = root.Get("creation date")) out.creationDate = cd->i;
    if (const BNode* len = info->Get("length")) {
        out.singleFile = true;
        TorrentEntry e;
        e.length = uint64_t(len->i);
        out.files.push_back(e);
    } else if (const BNode* files = info->Get("files")) {
        out.singleFile = false;
        for (auto& f : files->list) {
            const BNode* fl = f.Get("length");
            const BNode* fp = f.Get("path");
            if (!fl || !fp) return fail("voce file incompleta");
            TorrentEntry e;
            e.length = uint64_t(fl->i);
            for (auto& part : fp->list) e.path.push_back(part.s);
            out.files.push_back(e);
        }
    } else {
        return fail("né length né files");
    }
    unsigned char digest[20];
    Sha1::Hash(data.data() + info->begin, info->end - info->begin, digest);
    out.infoHash = HexLower(digest, 20);
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

}  // namespace rf
