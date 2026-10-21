#include "http.h"
#include "../resource.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <bcrypt.h>
#include <winhttp.h>

#include "../core/strutil.h"
#include "winutil.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace rf {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
};

std::string WinHttpErrorText(DWORD err) {
    switch (err) {
        case ERROR_WINHTTP_TIMEOUT: return "timeout della connessione";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "nome host non risolto (rete assente o DNS)";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "impossibile connettersi al server";
        case ERROR_WINHTTP_SECURE_FAILURE: return "errore TLS/certificato";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "connessione interrotta";
        default: return U8(LastErrorMessage(err));
    }
}

[[noreturn]] void FailE(DWORD err, const std::string& what) { throw HttpError(what + ": " + WinHttpErrorText(err)); }
#define Fail(what) do { DWORD e_ = GetLastError(); FailE(e_, (what)); } while (0)

void CheckCancel(const std::atomic<bool>* c) {
    if (c && c->load()) throw Cancelled();
}

}  // namespace

HttpResponse HttpSend(const HttpRequest& req) {
    std::wstring url = W(req.url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = DWORD(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = DWORD(std::size(path));
    wchar_t extra[8192];
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = DWORD(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &uc)) Fail("URL non valido");
    std::wstring object = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) + std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

    Handle session(WinHttpOpen(L"ReleaseForge/" RF_VERSION_W L" (+Windows)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        session.h = WinHttpOpen(L"ReleaseForge/" RF_VERSION_W L" (+Windows)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) Fail("WinHttpOpen");
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // sistemi senza TLS 1.3 in WinHTTP
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    int t = req.timeoutSeconds * 1000;
    WinHttpSetTimeouts(session, 15000, 15000, t, t);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);

    Handle connect(WinHttpConnect(session, std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(), uc.nPort, 0));
    if (!connect) Fail("connessione");
    Handle request(WinHttpOpenRequest(connect, W(req.method).c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0));
    if (!request) Fail("richiesta");
    DWORD decompression = 3;  // WINHTTP_DECOMPRESSION_FLAG_ALL (Windows 8.1+)
    WinHttpSetOption(request, 118 /*WINHTTP_OPTION_DECOMPRESSION*/, &decompression, sizeof decompression);

    std::wstring headers;
    for (auto& h : req.headers) headers += W(h) + L"\r\n";
    CheckCancel(req.cancel);
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), DWORD(-1),
                            req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data(), DWORD(req.body.size()),
                            DWORD(req.body.size()), 0))
        Fail("invio richiesta a " + U8(std::wstring(uc.lpszHostName, uc.dwHostNameLength)));
    if (!WinHttpReceiveResponse(request, nullptr)) Fail("risposta da " + U8(std::wstring(uc.lpszHostName, uc.dwHostNameLength)));

    HttpResponse resp;
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    resp.status = int(status);
    uint64_t total = 0;
    wchar_t lenBuf[32];
    DWORD lenSize = sizeof lenBuf;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(lenBuf, nullptr, 10);

    HANDLE file = INVALID_HANDLE_VALUE;
    bool toFile = !req.saveTo.empty() && status >= 200 && status < 300;
    if (toFile) {
        file = CreateFileW(LongPath(req.saveTo).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw HttpError("impossibile creare " + U8(req.saveTo) + ": " + U8(LastErrorMessage()));
    }
    uint64_t done = 0;
    std::string chunk;
    try {
        for (;;) {
            CheckCancel(req.cancel);
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) Fail("lettura risposta");
            if (avail == 0) break;
            chunk.resize(avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), avail, &read)) Fail("lettura risposta");
            if (read == 0) break;
            if (toFile) {
                DWORD written = 0;
                if (!WriteFile(file, chunk.data(), read, &written, nullptr) || written != read) throw HttpError("scrittura file di download fallita");
            } else {
                resp.body.append(chunk.data(), read);
                if (resp.body.size() > (64u << 20)) throw HttpError("risposta troppo grande");
            }
            done += read;
            if (req.progress) req.progress(done, total);
        }
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); DeleteFileW(LongPath(req.saveTo).c_str()); }
        throw;
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return resp;
}

Multipart::Multipart() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    boundary_ = "----ReleaseForge" + std::to_string(c.QuadPart) + std::to_string(GetCurrentThreadId());
}

void Multipart::AddField(const std::string& name, const std::string& value) {
    body_ += "--" + boundary_ + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
}

void Multipart::AddFile(const std::string& name, const std::string& fileName, const std::string& contentType, const std::string& data) {
    std::string safe = ReplaceAll(fileName, "\"", "");
    body_ += "--" + boundary_ + "\r\nContent-Disposition: form-data; name=\"" + name + "\"; filename=\"" + safe +
             "\"\r\nContent-Type: " + contentType + "\r\n\r\n";
    body_ += data;
    body_ += "\r\n";
}

std::string Multipart::ContentTypeHeader() const { return "Content-Type: multipart/form-data; boundary=" + boundary_; }

std::string Multipart::Finish() { return body_ + "--" + boundary_ + "--\r\n"; }

std::string Sha256Hex(const std::wstring& filePath) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) return "";
    std::string result;
    HANDLE f = CreateFileW(LongPath(filePath).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f != INVALID_HANDLE_VALUE && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == STATUS_SUCCESS) {
        std::vector<unsigned char> buf(1 << 20);
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(f, buf.data(), DWORD(buf.size()), &read, nullptr) && read) {
            if (BCryptHashData(hash, buf.data(), read, 0) != STATUS_SUCCESS) { ok = false; break; }
        }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == STATUS_SUCCESS) result = HexLower(digest, 32);
        BCryptDestroyHash(hash);
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

}  // namespace rf
