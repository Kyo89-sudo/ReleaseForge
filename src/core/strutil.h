// ReleaseForge - utility di stringa portabili (UTF-8).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rf {

std::string Trim(const std::string& s);
std::string ToLower(std::string s);   // solo ASCII
std::string ToUpper(std::string s);   // solo ASCII
bool StartsWith(const std::string& s, const std::string& prefix);
bool EndsWith(const std::string& s, const std::string& suffix);
bool IEndsWith(const std::string& s, const std::string& suffix);
bool IEquals(const std::string& a, const std::string& b);
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to);
std::vector<std::string> Split(const std::string& s, char sep);
std::vector<std::string> SplitLines(const std::string& s);   // gestisce \r\n, \n, \r
std::string Join(const std::vector<std::string>& v, const std::string& sep);
std::string NormalizeNewlines(const std::string& s);          // -> \n
std::string ToCrlf(const std::string& s);                     // \n -> \r\n
std::string StripUtf8Bom(const std::string& s);

// Dimensione stile makerelease: "%3.1f %sB" con prefissi binari (4.4 GiB).
std::string SizeOf(uint64_t bytes);
// Durata stile makerelease: "48m" oppure "1h 48m".
std::string RuntimeFromMinutes(int64_t minutes);

std::string UrlEncode(const std::string& s);   // percent-encoding RFC 3986 (unreserved esclusi)
std::string HexLower(const unsigned char* data, size_t len);

// Titolo in stile Python str.title() limitato ad ASCII ("ita" -> "Ita").
std::string TitleCase(const std::string& s);

}  // namespace rf
