#include "AgentLauncher.h"
#include "Logger.h"
#include "RaiiHandle.h"
#include <chrono>
#include <string>

namespace AgentLauncher {
namespace {

std::string Narrow(const std::wstring& w) {   // good enough for log output
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}
std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::optional<std::wstring> FindJava(const std::string& configured) {
    if (!configured.empty()) {
        std::wstring p = Widen(configured);
        if (FileExists(p)) return p;
        LOG_ERROR("[agent] java_path '%s' does not exist.", configured.c_str());
        return std::nullopt;
    }
    wchar_t home[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"JAVA_HOME", home, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring p = std::wstring(home, n) + L"\\bin\\java.exe";
        if (FileExists(p)) return p;
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"java.exe", nullptr, MAX_PATH, found, nullptr) > 0) return std::wstring(found);
    return std::nullopt;
}

std::wstring ResolveJar(const std::string& configured) {
    std::wstring p = Widen(configured);
    const bool absolute = p.size() > 1 && (p[1] == L':' || p[0] == L'\\');
    return absolute ? p : ExeDir() + L"\\" + p;
}

bool Inject(DWORD pid, const std::wstring& javaExe, const std::wstring& jar, int udpPort, std::stop_token st) {
    if (!FileExists(jar)) {
        LOG_ERROR("Agent jar not found: %s (build it with agent\\build.bat and put it next to mcradar.exe)",
                  Narrow(jar).c_str());
        return false;
    }

    // Agent options. Forward slashes avoid any backslash-before-quote problems on the command line.
    std::wstring exportDir = ExeDir();
    for (auto& c : exportDir) if (c == L'\\') c = L'/';
    const std::wstring opts = L"port=" + std::to_wstring(udpPort) + L";export=" + exportDir;

    std::wstring cmd = L"\"" + javaExe + L"\" -jar \"" + jar + L"\" " + std::to_wstring(pid) + L" \"" + jar +
                       L"\" \"" + opts + L"\"";
    LOG_INFO("Injecting agent into PID %lu using %s", static_cast<unsigned long>(pid), Narrow(javaExe).c_str());

    // Capture the helper's stdout+stderr through an inheritable pipe.
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { LOG_ERROR("CreatePipe failed (%lu)", static_cast<unsigned long>(GetLastError())); return false; }
    UniqueHandle readEnd(rd), writeEnd(wr);
    SetHandleInformation(readEnd.get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd.get();
    si.hStdError = writeEnd.get();
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        LOG_ERROR("Could not start java (%lu).", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    UniqueHandle proc(pi.hProcess), thread(pi.hThread);
    writeEnd.reset();   // parent must close its copy or ReadFile would never see EOF

    std::string pending;
    auto drain = [&]() {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(readEnd.get(), nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            char buf[512];
            DWORD got = 0;
            if (!ReadFile(readEnd.get(), buf, sizeof buf, &got, nullptr) || got == 0) break;
            pending.append(buf, got);
        }
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            while (!line.empty() && (line.back() == '\r')) line.pop_back();
            if (!line.empty()) LOG_INFO("[attach] %s", line.c_str());
        }
    };

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    for (;;) {
        DWORD w = WaitForSingleObject(proc.get(), 100);
        drain();
        if (w == WAIT_OBJECT_0) break;
        if (st.stop_requested() || std::chrono::steady_clock::now() > deadline) {
            TerminateProcess(proc.get(), 1);
            LOG_WARN("Attach helper %s.", st.stop_requested() ? "aborted" : "timed out");
            return false;
        }
    }
    drain();
    if (!pending.empty()) LOG_INFO("[attach] %s", pending.c_str());

    DWORD code = 1;
    GetExitCodeProcess(proc.get(), &code);
    if (code == 0) return true;
    if (code == 3) LOG_ERROR("That Java has no Attach API. Set [agent] java_path to a full JDK 17+ java.exe.");
    else LOG_ERROR("Attach helper failed (exit code %lu).", static_cast<unsigned long>(code));
    return false;
}

}  // namespace AgentLauncher
