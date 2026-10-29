// SHA-256 and BEP 52 Merkle hashing. No platform-specific dependencies.
#include "torrent.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace rf {
TorrentFormat ParseTorrentFormat(const std::string& v) {
    if (v == "v1") return TorrentFormat::V1;
    if (v == "v2") return TorrentFormat::V2;
    return TorrentFormat::Hybrid;
}
const char* TorrentFormatName(TorrentFormat f) {
    switch (f) {
    case TorrentFormat::V1: return "v1";
    case TorrentFormat::V2: return "v2";
    default: return "hybrid";
    }
}
bool ValidPieceLength(uint32_t n) { return n >= 16384 && n <= 16777216 && std::has_single_bit(n); }

Sha256::Sha256() : h_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} {}
void Sha256::Block(const unsigned char* p) {
    static constexpr uint32_t k[] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for (int i=0;i<16;++i) w[i]=(uint32_t(p[i*4])<<24)|(uint32_t(p[i*4+1])<<16)|(uint32_t(p[i*4+2])<<8)|p[i*4+3];
    for (int i=16;i<64;++i) {
        auto x=w[i-15], y=w[i-2];
        w[i]=w[i-16]+(std::rotr(x,7)^std::rotr(x,18)^(x>>3))+w[i-7]+(std::rotr(y,17)^std::rotr(y,19)^(y>>10));
    }
    auto a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],h=h_[7];
    for (int i=0;i<64;++i) {
        auto t1=h+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h_[0]+=a;h_[1]+=b;h_[2]+=c;h_[3]+=d;h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=h;
}
void Sha256::Update(const void* data, size_t len) {
    auto p=static_cast<const unsigned char*>(data);
    total_+=len;
    while (len) {
        auto n=std::min(len,64-used_);
        std::memcpy(buf_+used_,p,n);used_+=n;p+=n;len-=n;
        if (used_==64) { Block(buf_);used_=0; }
    }
}
void Sha256::Final(unsigned char out[32]) {
    uint64_t bits=total_*8;
    unsigned char pad=0x80, zero=0;
    Update(&pad,1);
    while (used_!=56) Update(&zero,1);
    unsigned char n[8];
    for (int i=0;i<8;++i) n[i]=static_cast<unsigned char>(bits>>(56-8*i));
    Update(n,8);
    for (int i=0;i<8;++i) for (int j=0;j<4;++j) out[i*4+j]=static_cast<unsigned char>(h_[i]>>(24-8*j));
}
std::string Sha256::Hash(const void* d,size_t n) {
    Sha256 h;h.Update(d,n);unsigned char out[32];h.Final(out);
    return {reinterpret_cast<const char*>(out),32};
}

// Missing leaves are zero digests, not hashes of zero-filled data blocks.
static std::string Merkle(std::vector<std::string> nodes, size_t width, const std::string& zero) {
    if (nodes.empty()) return {};
    nodes.resize(width,zero);
    while (nodes.size()>1) {
        for (size_t i=0;i<nodes.size()/2;++i) {
            auto pair=nodes[i*2]+nodes[i*2+1];
            nodes[i]=Sha256::Hash(pair.data(),pair.size());
        }
        nodes.resize(nodes.size()/2);
    }
    return nodes[0];
}
std::string V2LayerRoot(const std::string& layer, uint32_t pieceLength) {
    std::vector<std::string> nodes;
    for (size_t i=0;i<layer.size();i+=32) nodes.push_back(layer.substr(i,32));
    std::string zero(32,'\0');
    for (uint32_t n=16384;n<pieceLength;n*=2) {
        auto pair=zero+zero; zero=Sha256::Hash(pair.data(),pair.size());
    }
    return Merkle(nodes,std::bit_ceil(nodes.size()),zero);
}
V2FileHasher::V2FileHasher(uint32_t p, HashFn fn) : pieceLength_(p),fn_(std::move(fn)),block_(16384) {
    if (!ValidPieceLength(p)) throw std::invalid_argument("Dimensione pezzi non valida (16 KiB..16 MiB, potenza di due)");
}
void V2FileHasher::Flush() {
    auto digest=fn_ ? fn_(block_.data(),used_) : Sha256::Hash(block_.data(),used_);
    if (digest.size()!=32) throw std::runtime_error("SHA-256 non valido");
    leaves_.push_back(std::move(digest));used_=0;
    if (leaves_.size()==pieceLength_/16384) {
        pieces_.push_back(Merkle(leaves_,leaves_.size(),std::string(32,'\0')));leaves_.clear();
    }
}
void V2FileHasher::Feed(const void* d,size_t n) {
    if (finished_) throw std::logic_error("Hash gia finalizzato");
    auto p=static_cast<const unsigned char*>(d);fed_+=n;
    while (n) {
        auto take=std::min(n,block_.size()-used_);
        std::memcpy(block_.data()+used_,p,take);used_+=take;p+=take;n-=take;
        if (used_==block_.size()) Flush();
    }
}
void V2FileHasher::Finish(std::string& root,std::string& layer) {
    if (!finished_) {
        if (used_) Flush();
        if (!leaves_.empty()) {
            size_t width=fed_>pieceLength_ ? pieceLength_/16384 : std::bit_ceil(leaves_.size());
            pieces_.push_back(Merkle(leaves_,width,std::string(32,'\0')));leaves_.clear();
        }
        if (fed_>pieceLength_) {
            for (auto& p:pieces_) layer_+=p;
            root_=V2LayerRoot(layer_,pieceLength_);
        } else if (fed_) root_=pieces_.at(0);
        finished_=true;
    }
    root=root_;layer=layer_;
}
} // namespace rf
