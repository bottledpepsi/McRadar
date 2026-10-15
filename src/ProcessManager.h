// ProcessManager.h - locate the Minecraft JVM and own the process handle.
#pragma once
#include "Platform.h"
#include "RaiiHandle.h"
#include <optional>
#include <string>

class ProcessManager {
public:
    ProcessManager() = default;
    ~ProcessManager() = default;                       // UniqueHandle closes the handle
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    // Find the Minecraft JVM and open it with PROCESS_VM_READ | PROCESS_QUERY_INFORMATION.
    // Returns false (quietly) if Minecraft isn't running, so it can be polled safely.
    bool Attach();
    void Detach();

    // Use this PID instead of auto-detecting (needed with two Minecraft clients on one PC).
    // 0 = auto-detect.
    void SetForcedPid(DWORD pid) { forcedPid_ = pid; }

    bool IsAttached() const { return handle_.valid(); }
    // False once the target has exited (handle still valid but process is dead).
    bool IsAlive() const;

    HANDLE Handle() const { return handle_.get(); }
    DWORD Pid() const { return pid_; }

    // Base address of a loaded module in the target (e.g. "jvm.dll").
    std::optional<uintptr_t> ModuleBase(const std::string& moduleName) const;

    // Strategy 1: visible top-level window whose title starts with "Minecraft" AND whose
    //             owning process is java.exe/javaw.exe (this excludes the launcher).
    // Strategy 2: fall back to any java.exe / javaw.exe process.
    static std::optional<DWORD> FindMinecraftPid();

private:
    UniqueHandle handle_;
    DWORD pid_ = 0;
    DWORD lastFailedPid_ = 0;
    DWORD forcedPid_ = 0;  // avoid spamming the same OpenProcess error every second
};
