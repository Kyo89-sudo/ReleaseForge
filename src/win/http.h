// ReleaseForge - client HTTPS minimale basato su WinHTTP (proxy di sistema, TLS del sistema operativo).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace rf {

struct HttpResponse {
    int status = 0;
    std::string body;
};

class HttpError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Operazione annullata") {}
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;  // "Nome: valore"
    std::string body;
    int timeoutSeconds = 60;
    const std::atomic<bool>* cancel = nullptr;
    // Download: se impostato, il corpo va su file (percorso UTF-8) invece che in memoria.
    std::wstring saveTo;
    std::function<void(uint64_t done, uint64_t total)> progress;
};

HttpResponse HttpSend(const HttpRequest& req);   // lancia HttpError / Cancelled

class Multipart {
public:
    Multipart();
    void AddField(const std::string& name, const std::string& value);
    void AddFile(const std::string& name, const std::string& fileName, const std::string& contentType, const std::string& data);
    std::string ContentTypeHeader() const;
    std::string Finish();

private:
    std::string boundary_, body_;
};

std::string Sha256Hex(const std::wstring& filePath);   // "" su errore

}  // namespace rf
