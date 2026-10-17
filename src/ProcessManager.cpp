#include "ProcessManager.h"
#include "Logger.h"
#include <tlhelp32.h>
#include <algorithm>
#include <cwchar>
#include <map>

namespace
{
    bool IsJavaExe(const std::wstring& n)
    {
        return _wcsicmp(n.c_str(), L"javaw.exe") == 0 || _wcsicmp(n.c_str(), L"java.exe") == 0;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::map<DWORD, std::wstring> JavaProcesses()
    {
        std::map<DWORD, std::wstring> out;
        UniqueHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snap.valid()) return out;
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        for (BOOL ok = Process32FirstW(snap.get(), &pe); ok; ok = Process32NextW(snap.get(), &pe))
            if (IsJavaExe(pe.szExeFile)) out[pe.th32ProcessID] = pe.szExeFile;
        return out;
    }

    struct WinSearch
    {
        DWORD pid = 0;
        HWND best = nullptr;
        long bestScore = -1;
        std::wstring bestTitle;
    };

    // Best window of a process: title starting with "Minecraft" beats everything, then largest area.
BOOL CALLBACK EnumProc(HWND h, LPARAM lp)
    {
        auto* ws = reinterpret_cast<WinSearch*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != ws->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
        wchar_t title[256] = {};
        GetWindowTextW(h, title, 256);
        RECT rc{};
        GetClientRect(h, &rc);
        long score = static_cast<long>(rc.right) * rc.bottom / 1000;
        if (std::wcsncmp(title, L"Minecraft", 9) == 0) score += 1'000'000;
        else if (title[0] == 0) score -= 500;
        if (score > ws->bestScore)
        {
            ws->bestScore = score;
            ws->best = h;
            ws->bestTitle = title;
        }
        return TRUE;
    }

    std::string VersionFromTitle(const std::string& t)
    {
        // "Minecraft* 1.20.4" / "Minecraft 26.1" -> first d+.d+(.d+)
        for (size_t i = 0; i < t.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(t[i])) || (i > 0 && (std::isdigit(
                static_cast<unsigned char>(t[i - 1])) || t[i - 1] == '.'))) continue;
            size_t j = i;
            int dots = 0;
            while (j < t.size() && (std::isdigit(static_cast<unsigned char>(t[j])) || (t[j] == '.' && dots < 2 && j + 1
                < t.size() && std::isdigit(static_cast<unsigned char>(t[j + 1])))))
            {
                if (t[j] == '.') ++dots;
                ++j;
            }
            if (dots >= 1) return t.substr(i, j - i);
        }
        return {};
    }

    JavaProcess Describe(DWORD pid, const std::wstring& exeName)
    {
        JavaProcess jp;
        jp.pid = pid;
        WinSearch ws;
        ws.pid = pid;
        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ws));
        jp.hwnd = ws.best;
        jp.title = Narrow(ws.bestTitle);
        jp.versionHint = VersionFromTitle(jp.title);

        UniqueHandle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (h.valid())
        {
            wchar_t buf[MAX_PATH * 2] = {};
            DWORD n = static_cast<DWORD>(sizeof buf / sizeof buf[0]);
            if (QueryFullProcessImageNameW(h.get(), 0, buf, &n)) jp.exe = buf;
        }
        if (!jp.exe.empty())
        {
            auto cand = jp.exe.parent_path() / L"java.exe";
            std::error_code ec;
            jp.javaExe = std::filesystem::exists(cand, ec) ? cand : jp.exe;
        }
        (void)exeName;
        return jp;
    }
} // namespace

std::string JavaProcess::DisplayName() const
{
    return (versionHint.empty() ? std::string("Minecraft") : "Minecraft " + versionHint) + " (PID " +
        std::to_string(pid) + ")";
}

std::vector<JavaProcess> ProcessManager::Scan()
{
    std::vector<JavaProcess> out;
    for (const auto& [pid, name] : JavaProcesses()) out.push_back(Describe(pid, name));
    std::stable_sort(out.begin(), out.end(), [](const JavaProcess& a, const JavaProcess& b)
    {
        return (a.title.rfind("Minecraft", 0) == 0) > (b.title.rfind("Minecraft", 0) == 0);
    });
    return out;
}

bool ProcessManager::Attach()
{
    Detach();
    auto list = Scan();
    const JavaProcess* pick = nullptr;
    if (forcedPid_ != 0)
    {
        for (const auto& p : list) if (p.pid == forcedPid_) pick = &p;
        if (!pick)
        {
            if (lastFailedPid_ != forcedPid_)
            {
                lastFailedPid_ = forcedPid_;
                LOG_WARN("PID %lu is not a java process.", static_cast<unsigned long>(forcedPid_));
            }
            return false;
        }
    }
    else
    {
        std::vector<const JavaProcess*> mc;
        for (const auto& p : list) if (p.title.rfind("Minecraft", 0) == 0) mc.push_back(&p);
        if (mc.size() > 1)
        {
            std::string l;
            for (auto* p : mc) l += (l.empty() ? "" : ", ") + p->DisplayName();
            LOG_WARN("%llu Minecraft windows found: %s. Using the first; choose another with --pid <PID>.",
                     static_cast<unsigned long long>(mc.size()), l.c_str());
        }
        if (!mc.empty()) pick = mc.front();
        else if (!list.empty()) pick = &list.front(); // java without a titled window yet (game still starting)
    }
    if (!pick) return false;

    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pick->pid);
    if (!h)
    {
        if (lastFailedPid_ != pick->pid)
        {
            lastFailedPid_ = pick->pid;
            LOG_ERROR("OpenProcess(PID %lu) failed (error %lu). Run this tool at the same privilege level as the game.",
                      static_cast<unsigned long>(pick->pid), static_cast<unsigned long>(GetLastError()));
        }
        return false;
    }
    handle_.reset(h);
    target_ = *pick;
    lastFailedPid_ = 0;
    LOG_INFO("Target: %s | window '%s' | JRE %s", target_.DisplayName().c_str(), target_.title.c_str(),
             target_.javaExe.string().c_str());
    return true;
}

void ProcessManager::Detach()
{
    if (handle_.valid()) LOG_INFO("Detached from PID %lu.", static_cast<unsigned long>(target_.pid));
    handle_.reset();
    target_ = {};
}

bool ProcessManager::IsAlive() const
{
    return handle_.valid() && WaitForSingleObject(handle_.get(), 0) == WAIT_TIMEOUT;
}

HWND ProcessManager::RefreshWindow()
{
    if (!handle_.valid()) return nullptr;
    const ULONGLONG now = GetTickCount64();
    if (target_.hwnd && IsWindow(target_.hwnd) && now - lastWindowRefresh_ < 2000) return target_.hwnd;
    lastWindowRefresh_ = now;
    WinSearch ws;
    ws.pid = target_.pid;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ws));
    target_.hwnd = ws.best;
    if (ws.best) target_.title = Narrow(ws.bestTitle);
    return target_.hwnd;
}
