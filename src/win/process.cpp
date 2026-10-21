#include "process.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include "http.h"
#include "winutil.h"

namespace rf {

std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) { out.append(backslashes * 2, L'\\'); break; }
        if (arg[i] == L'"') { out.append(backslashes * 2 + 1, L'\\'); out += L'"'; }
        else { out.append(backslashes, L'\\'); out += arg[i]; }
    }
    return out + L"\"";
}

namespace {

struct PipeReader {
    HANDLE read = nullptr;
    std::string data;
    std::function<void(const std::string&)> sink;
    HANDLE thread = nullptr;
    static DWORD WINAPI Run(LPVOID p) {
        auto* self = static_cast<PipeReader*>(p);
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(self->read, buf, sizeof buf, &n, nullptr) && n) {
            if (self->sink) self->sink(std::string(buf, n));
            if (self->data.size() < (256u << 20)) self->data.append(buf, n);
        }
        return 0;
    }
};

}  // namespace

ProcessResult RunProcess(const std::wstring& exe, const std::vector<std::wstring>& args, const ProcessOptions& opt) {
    ProcessResult r;
    std::wstring cmd = QuoteArg(exe);
    for (auto& a : args) cmd += L" " + QuoteArg(a);

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0) || !CreatePipe(&inR, &inW, &sa, 0)) {
        r.startError = U8(LastErrorMessage());
        return r;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    CloseHandle(inW);  // stdin vuoto: evita blocchi su richieste interattive

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = opt.showWindow ? SW_SHOWNORMAL : SW_HIDE;
    si.StartupInfo.hStdOutput = outW;
    si.StartupInfo.hStdError = errW;
    si.StartupInfo.hStdInput = inR;

    // Eredita solo le tre handle delle pipe.
    HANDLE inherit[3] = {outW, errW, inR};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool attrs = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize) &&
                 UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }

    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | (attrs ? EXTENDED_STARTUPINFO_PRESENT : 0) |
                  (opt.showWindow ? 0 : CREATE_NO_WINDOW) | (opt.lowPriority ? BELOW_NORMAL_PRIORITY_CLASS : 0);
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);
    BOOL ok = CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE, flags, nullptr,
                             opt.workingDir.empty() ? nullptr : opt.workingDir.c_str(), &si.StartupInfo, &pi);
    DWORD startErr = GetLastError();
    if (attrs) DeleteProcThreadAttributeList(si.lpAttributeList);
    CloseHandle(outW);
    CloseHandle(errW);
    CloseHandle(inR);
    if (!ok) {
        CloseHandle(outR);
        CloseHandle(errR);
        if (job) CloseHandle(job);
        r.startError = U8(LastErrorMessage(startErr));
        return r;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    r.started = true;

    PipeReader out{outR, {}, opt.onStdout, nullptr}, err{errR, {}, nullptr, nullptr};
    out.thread = CreateThread(nullptr, 0, PipeReader::Run, &out, 0, nullptr);
    err.thread = CreateThread(nullptr, 0, PipeReader::Run, &err, 0, nullptr);

    ULONGLONG start = GetTickCount64();
    bool cancelled = false, timedOut = false;
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (opt.cancel && opt.cancel->load()) { cancelled = true; break; }
        if (opt.timeoutMs && GetTickCount64() - start > opt.timeoutMs) { timedOut = true; break; }
    }
    if (cancelled || timedOut) {
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    HANDLE threads[2] = {out.thread, err.thread};
    WaitForMultipleObjects(2, threads, TRUE, 10000);
    CloseHandle(out.thread);
    CloseHandle(err.thread);
    CloseHandle(outR);
    CloseHandle(errR);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    r.exitCode = int(code);
    r.out = std::move(out.data);
    r.err = std::move(err.data);
    if (cancelled) throw Cancelled();
    if (timedOut) { r.exitCode = -2; r.startError = "tempo massimo superato"; }
    return r;
}

}  // namespace rf
