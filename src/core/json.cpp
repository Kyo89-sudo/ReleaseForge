#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rf {

static const Json kNull;

class JsonParser {
public:
    explicit JsonParser(const std::string& t) : s(t) {}

    Json ParseDocument() {
        SkipWs();
        Json v = ParseValue(0);
        SkipWs();
        if (p != s.size()) Fail("contenuto inatteso dopo il valore");
        return v;
    }

private:
    const std::string& s;
    size_t p = 0;

    [[noreturn]] void Fail(const char* what) {
        throw JsonError(std::string("JSON non valido (") + what + ") alla posizione " + std::to_string(p));
    }
    void SkipWs() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
    }
    bool Lit(const char* lit) {
        size_t n = 0;
        while (lit[n]) ++n;
        if (s.compare(p, n, lit) == 0) { p += n; return true; }
        return false;
    }

    Json ParseValue(int depth) {
        if (depth > 200) Fail("annidamento eccessivo");
        if (p >= s.size()) Fail("fine inattesa");
        Json v;
        char c = s[p];
        if (c == '{') {
            v.type_ = Json::Type::Object;
            ++p; SkipWs();
            if (p < s.size() && s[p] == '}') { ++p; return v; }
            for (;;) {
                SkipWs();
                if (p >= s.size() || s[p] != '"') Fail("attesa chiave");
                std::string key = ParseString();
                SkipWs();
                if (p >= s.size() || s[p] != ':') Fail("atteso ':'");
                ++p; SkipWs();
                v.obj_.emplace_back(std::move(key), ParseValue(depth + 1));
                SkipWs();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; break; }
                Fail("atteso ',' o '}'");
            }
        } else if (c == '[') {
            v.type_ = Json::Type::Array;
            ++p; SkipWs();
            if (p < s.size() && s[p] == ']') { ++p; return v; }
            for (;;) {
                SkipWs();
                v.arr_.push_back(ParseValue(depth + 1));
                SkipWs();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; break; }
                Fail("atteso ',' o ']'");
            }
        } else if (c == '"') {
            v.type_ = Json::Type::String;
            v.str_ = ParseString();
        } else if (Lit("true")) { v.type_ = Json::Type::Bool; v.bool_ = true; }
        else if (Lit("false")) { v.type_ = Json::Type::Bool; v.bool_ = false; }
        else if (Lit("null")) { v.type_ = Json::Type::Null; }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t b = p;
            if (s[p] == '-') ++p;
            while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '.' || s[p] == 'e' || s[p] == 'E' || s[p] == '+' || s[p] == '-')) ++p;
            std::string num = s.substr(b, p - b);
            char* end = nullptr;
            v.type_ = Json::Type::Number;
            v.num_ = std::strtod(num.c_str(), &end);
            if (!end || *end) Fail("numero");
        } else Fail("valore");
        return v;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }

    unsigned Hex4() {
        if (p + 4 > s.size()) Fail("escape \\u troncato");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else Fail("escape \\u");
        }
        return v;
    }

    std::string ParseString() {
        ++p;  // "
        std::string out;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (p >= s.size()) break;
            char e = s[p++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = Hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        p += 2;
                        unsigned lo = Hex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { AppendUtf8(out, 0xFFFD); cp = lo; }
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                    AppendUtf8(out, cp);
                    break;
                }
                default: Fail("escape sconosciuto");
            }
        }
        Fail("stringa non terminata");
    }
};

Json Json::Parse(const std::string& text) {
    std::string t = text;
    if (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t.erase(0, 3);
    JsonParser parser(t);
    return parser.ParseDocument();
}

std::string Json::Quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else out += char(c);
        }
    }
    return out + "\"";
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ != Type::Object) return kNull;
    for (auto& kv : obj_) if (kv.first == key) return kv.second;
    return kNull;
}

const Json& Json::operator[](size_t index) const {
    if (type_ != Type::Array || index >= arr_.size()) return kNull;
    return arr_[index];
}

bool Json::Has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (auto& kv : obj_) if (kv.first == key) return true;
    return false;
}

size_t Json::Size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

std::string Json::Str() const {
    switch (type_) {
        case Type::String: return str_;
        case Type::Bool: return bool_ ? "true" : "false";
        case Type::Number: {
            if (std::isfinite(num_) && std::floor(num_) == num_ && std::fabs(num_) < 1e15) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%lld", (long long)num_);
                return buf;
            }
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", num_);
            return buf;
        }
        default: return "";
    }
}

}  // namespace rf
