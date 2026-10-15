#include "ProcessManager.h"
#include "Logger.h"
#include <tlhelp32.h>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace {

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // ASCII names

bool IsJavaExe(const std::wstring& name) {
    return _wcsicmp(name.c_str(), L"javaw.exe") == 0 || _wcsicmp(name.c_str(), L"java.exe") == 0;
}

// One toolhelp snapshot of all processes -> (pid, exe name) pairs.
std::vector<std::pair<DWORD, std::wstring>> ListProcesses() {
    std::vector<std::pair<DWORD, std::wstring>> out;
    UniqueHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap.valid()) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap.get(), &pe); ok; ok = Process32NextW(snap.get(), &pe))
        out.emplace_back(pe.th32ProcessID, pe.szExeFile);
    return out;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lp) {
    auto* pids = reinterpret_cast<std::vector<DWORD>*>(lp);
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, 256) <= 0) return TRUE;
    if (std::wcsncmp(title, L"Minecraft", 9) != 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) pids->push_back(pid);
    return TRUE;  // keep enumerating
}

}  // namespace

std::optional<DWORD> ProcessManager::FindMinecraftPid() {
    const auto procs = ListProcesses();
    auto isJava = [&procs](DWORD pid) {
        for (const auto& [p, name] : procs)
            if (p == pid) return IsJavaExe(name);
        return false;
    };

    // Strategy 1: window title.
    std::vector<DWORD> windowPids;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windowPids));
    std::vector<DWORD> mcPids;
    for (DWORD pid : windowPids)
        if (isJava(pid)) mcPids.push_back(pid);
    if (!mcPids.empty()) {
        if (mcPids.size() > 1) {
            std::string list;
            for (DWORD p : mcPids) list += (list.empty() ? "" : ", ") + std::to_string(p);
            LOG_WARN("%llu Minecraft windows found (PIDs %s); using %lu. Pick one with --pid <PID>.",
                     static_cast<unsigned long long>(mcPids.size()), list.c_str(),
                     static_cast<unsigned long>(mcPids.front()));
        }
        return mcPids.front();
    }

    // Strategy 2: executable name (launchers that retitle the window, minimised windows, ...).
    std::vector<DWORD> javaPids;
    for (const auto& [pid, name] : procs)
        if (IsJavaExe(name)) javaPids.push_back(pid);
    if (javaPids.empty()) return std::nullopt;
    if (javaPids.size() > 1)
        LOG_WARN("%llu java processes found and no Minecraft window title matched; using PID %lu.",
                 static_cast<unsigned long long>(javaPids.size()), static_cast<unsigned long>(javaPids.front()));
    return javaPids.front();
}

bool ProcessManager::Attach() {
    Detach();
    const auto pid = forcedPid_ != 0 ? std::optional<DWORD>(forcedPid_) : FindMinecraftPid();
    if (!pid) return false;

    // Least privilege: we only ever read. PROCESS_QUERY_INFORMATION lets us call
    // GetExitCodeProcess/IsWow64Process; PROCESS_VM_READ is required by ReadProcessMemory.
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, *pid);
    if (!h) {
        if (lastFailedPid_ != *pid) {
            lastFailedPid_ = *pid;
            const DWORD err = GetLastError();
            LOG_ERROR("OpenProcess(PID %lu) failed, error %lu%s", static_cast<unsigned long>(*pid),
                      static_cast<unsigned long>(err),
                      err == ERROR_ACCESS_DENIED
                          ? " (access denied - run this tool at the same integrity level as the game, "
                            "e.g. as Administrator if the game was started elevated)"
                          : "");
        }
        return false;
    }
    handle_.reset(h);
    pid_ = *pid;
    lastFailedPid_ = 0;

    BOOL wow64 = FALSE;
    if (IsWow64Process(h, &wow64) && wow64)
        LOG_WARN("Target is a 32-bit process; this x64 build cannot read it correctly.");
    LOG_INFO("Attached to Minecraft JVM, PID %lu.", static_cast<unsigned long>(pid_));
    return true;
}

void ProcessManager::Detach() {
    if (handle_.valid()) LOG_INFO("Detached from PID %lu.", static_cast<unsigned long>(pid_));
    handle_.reset();
    pid_ = 0;
}

bool ProcessManager::IsAlive() const {
    if (!handle_.valid()) return false;
    DWORD code = 0;
    return GetExitCodeProcess(handle_.get(), &code) && code == STILL_ACTIVE;
}

std::optional<uintptr_t> ProcessManager::ModuleBase(const std::string& moduleName) const {
    if (!handle_.valid()) return std::nullopt;
    const std::wstring want = Widen(moduleName);

    // TH32CS_SNAPMODULE can transiently fail with ERROR_BAD_LENGTH while the target is loading
    // modules; retrying a few times is the documented workaround.
    UniqueHandle snap;
    for (int attempt = 0; attempt < 5; ++attempt) {
        snap.reset(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_));
        if (snap.valid() || GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(10);
    }
    if (!snap.valid()) return std::nullopt;

    MODULEENTRY32W me{};
    me.dwSize = sizeof me;
    for (BOOL ok = Module32FirstW(snap.get(), &me); ok; ok = Module32NextW(snap.get(), &me))
        if (_wcsicmp(me.szModule, want.c_str()) == 0)
            return reinterpret_cast<uintptr_t>(me.modBaseAddr);
    return std::nullopt;
}
