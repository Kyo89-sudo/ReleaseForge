// ReleaseForge - creazione metainfo BitTorrent v1/v2/hybrid (bencode, info-hash, magnet).
// L'hashing dei pezzi è fornito dal chiamante (PieceHasher) così il core resta portabile.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rf {

enum class TorrentFormat { Hybrid = 0, V1 = 1, V2 = 2 };
TorrentFormat ParseTorrentFormat(const std::string& value);
const char* TorrentFormatName(TorrentFormat format);
bool ValidPieceLength(uint32_t length);

class Sha256 {
public:
    Sha256();
    void Update(const void* data, size_t len);
    void Final(unsigned char out[32]);
    static std::string Hash(const void* data, size_t len); // 32 binary bytes
private:
    void Block(const unsigned char* data);
    uint32_t h_[8];
    unsigned char buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

// Streaming BEP 52: one 16 KiB block plus hashes, never a whole file in memory.
class V2FileHasher {
public:
    using HashFn = std::function<std::string(const unsigned char*, size_t)>;
    explicit V2FileHasher(uint32_t pieceLength, HashFn fn = nullptr);
    void Feed(const void* data, size_t len);
    void Finish(std::string& root, std::string& layer);
private:
    uint32_t pieceLength_;
    HashFn fn_;
    std::vector<unsigned char> block_;
    size_t used_ = 0;
    uint64_t fed_ = 0;
    std::vector<std::string> leaves_, pieces_;
    bool finished_ = false;
    std::string root_, layer_;
    void Flush();
};


class Sha1 {
public:
    Sha1();
    void Update(const void* data, size_t len);
    void Final(unsigned char out[20]);
    static void Hash(const void* data, size_t len, unsigned char out[20]);

private:
    void Block(const unsigned char* p);
    uint32_t h_[5];
    unsigned char buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

// Accumula i dati in pezzi e calcola gli SHA-1. HashFn permette di usare BCrypt su Windows.
class PieceHasher {
public:
    using HashFn = std::function<void(const unsigned char* data, size_t len, unsigned char out[20])>;
    PieceHasher(uint32_t pieceLength, HashFn fn = nullptr);
    void Feed(const void* data, size_t len);
    std::string Finish();  // pezzi concatenati
    uint64_t BytesFed() const { return fed_; }

private:
    uint32_t pieceLength_;
    HashFn fn_;
    std::vector<unsigned char> buf_;
    size_t used_ = 0;
    uint64_t fed_ = 0;
    std::string pieces_;
    void Flush();
};

struct TorrentEntry {
    std::vector<std::string> path;  // componenti relativi alla radice (UTF-8); vuoto = torrent a file singolo
    uint64_t length = 0;
    std::string piecesRoot; // v2, absent for empty files
    std::string pieceLayer; // v2, present only for files larger than a piece
};

struct TorrentSpec {
    TorrentFormat format = TorrentFormat::Hybrid;
    std::string name;                 // nome radice (UTF-8)
    bool singleFile = true;
    std::vector<TorrentEntry> files;  // nello stesso ordine usato per l'hashing
    uint32_t pieceLength = 0;
    std::string pieces;               // concatenazione SHA-1 (20 byte per pezzo)
    std::vector<std::string> trackers;
    bool isPrivate = false;
    std::string createdBy = "ReleaseForge";
    int64_t creationDate = 0;         // epoch UTC
    std::string source;               // campo "source" opzionale (alcuni tracker privati)
};

struct TorrentResult {
    std::string bencoded;  // contenuto del file .torrent
    std::string infoHash;  // v1 SHA-1, empty for pure v2
    std::string infoHashV2; // v2 SHA-256, empty for pure v1
};

TorrentResult BuildTorrent(const TorrentSpec& spec);

// Lettura di un .torrent esistente (per riprendere senza ricalcolare gli hash).
struct ParsedTorrent {
    TorrentFormat format = TorrentFormat::V1;
    std::string name;
    bool singleFile = true;
    std::vector<TorrentEntry> files;
    uint32_t pieceLength = 0;
    std::string pieces;
    bool isPrivate = false;
    std::string source;
    int64_t creationDate = 0;
    std::string infoHash;
    std::string infoHashV2;
};
bool ParseTorrent(const std::string& data, ParsedTorrent& out, std::string* error = nullptr);

// Metadata comparison for resume; the caller also checks source timestamps.
bool TorrentMatches(const ParsedTorrent& parsed, const TorrentSpec& expected, std::string* reason = nullptr);

// Tabella makerelease/MIRCrew: 32 KiB fino a 50 MB ... 16 MiB oltre 16 GB.
uint32_t PieceSizeForTotal(uint64_t totalBytes);

// magnet:?xt=urn:btih:<hash>&dn=<nome>&xl=<dim>&tr=<tracker>...
std::string BuildMagnet(const std::string& infoHash, const std::string& name, uint64_t totalBytes,
                        const std::vector<std::string>& trackers);

std::string BuildMagnet(const TorrentResult& torrent, const std::string& name, uint64_t totalBytes,
                        const std::vector<std::string>& trackers);

// Validazione URL tracker (http, https, udp, wss).
bool IsValidTrackerUrl(const std::string& url);
std::vector<std::string> ParseTrackerList(const std::string& text);  // una riga per tracker, deduplica

// Unisce i tracker importati all'elenco: quelli già presenti restano tutti e per primi, poi i nuovi
// (senza duplicati, maiuscole/minuscole ignorate) finché l'elenco arriva a maxTotal (0 = nessun limite).
// Esempio: 6 nell'elenco, limite 8 -> si aggiungono 2 tracker.
std::vector<std::string> MergeTrackerLists(const std::vector<std::string>& current, const std::vector<std::string>& imported,
                                           size_t maxTotal);

// Testo dell'elenco: un tracker per riga, con una riga vuota tra un tracker e l'altro.
// Le righe vuote servono solo alla lettura: nel torrent ogni tracker resta in un tier proprio.
std::string FormatTrackerList(const std::vector<std::string>& trackers);

// Bencode generico (per test e lettura).
std::string BencodeString(const std::string& s);
std::string BencodeInt(int64_t v);

}  // namespace rf
