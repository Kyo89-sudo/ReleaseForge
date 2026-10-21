#include "strutil.h"

#include <cstdio>

namespace rf {

static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpace(s[b])) ++b;
    while (e > b && IsSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string ToLower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::string ToUpper(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}

bool StartsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool EndsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool IEndsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && ToLower(s.substr(s.size() - p.size())) == ToLower(p); }
bool IEquals(const std::string& a, const std::string& b) { return a.size() == b.size() && ToLower(a) == ToLower(b); }

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::string NormalizeNewlines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else out += s[i];
    }
    return out;
}

std::vector<std::string> SplitLines(const std::string& s) {
    auto v = Split(NormalizeNewlines(s), '\n');
    if (!v.empty() && v.back().empty()) v.pop_back();
    return v;
}

std::string Join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::string ToCrlf(const std::string& s) {
    std::string n = NormalizeNewlines(s), out;
    out.reserve(n.size() + n.size() / 20);
    for (char c : n) {
        if (c == '\n') out += "\r\n";
        else out += c;
    }
    return out;
}

std::string StripUtf8Bom(const std::string& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) return s.substr(3);
    return s;
}

std::string SizeOf(uint64_t bytes) {
    static const char* units[] = {"", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi"};
    double num = double(bytes);
    char buf[64];
    for (const char* u : units) {
        if (num < 1024.0) {
            std::snprintf(buf, sizeof buf, "%3.1f %sB", num, u);
            return buf;
        }
        num /= 1024.0;
    }
    std::snprintf(buf, sizeof buf, "%.1f YiB", num);
    return buf;
}

std::string RuntimeFromMinutes(int64_t minutes) {
    if (minutes < 0) minutes = 0;
    if (minutes < 60) return std::to_string(minutes) + "m";
    return std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m";
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string HexLower(const unsigned char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) { out += hex[data[i] >> 4]; out += hex[data[i] & 15]; }
    return out;
}

std::string TitleCase(const std::string& s) {
    std::string out = s;
    bool start = true;
    for (auto& c : out) {
        bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (alpha) {
            if (start && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            else if (!start && c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            start = false;
        } else start = !((unsigned char)c >= 0x80);
    }
    return out;
}

}  // namespace rf
