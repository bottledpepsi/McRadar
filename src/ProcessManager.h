// ProcessManager.h - finds the Minecraft JVM (port of ProcessScanner.cs + the PID logic of the radar).
//
// A Minecraft client is a java.exe / javaw.exe process that owns a visible window titled "Minecraft...".
// With two clients on one PC (LAN test with 2 accounts) there are two such processes: Scan() lists
// them all and --pid <PID> picks one.
#pragma once
#include "Platform.h"
#include "RaiiHandle.h"
#include <filesystem>
#include <string>
#include <vector>

struct JavaProcess
{
    DWORD pid = 0;
    HWND hwnd = nullptr; // main (game) window, may be null while the game is starting
    std::string title;
    std::filesystem::path exe; // ...\bin\javaw.exe
    std::filesystem::path javaExe; // ...\bin\java.exe next to it (what runs the attach helper)
    std::string versionHint; // from the window title, e.g. "1.21.1" (may be empty)
    std::string DisplayName() const;
};

class ProcessManager
{
public:
    // Every java(w).exe process, those with a "Minecraft" window first.
    static std::vector<JavaProcess> Scan();

    void SetForcedPid(DWORD pid) { forcedPid_ = pid; }
    bool Attach(); // pick target (forced PID, else first Minecraft-looking one)
    void Detach();
    bool IsAttached() const { return handle_.valid(); }
    bool IsAlive() const; // false once the process exited
    const JavaProcess& Target() const { return target_; }
    // Re-resolves the game window (cheap, throttled). Returns it (may be null).
    HWND RefreshWindow();

private:
    UniqueHandle handle_; // SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION only: we never read its memory
    JavaProcess target_;
    DWORD forcedPid_ = 0;
    DWORD lastFailedPid_ = 0;
    ULONGLONG lastWindowRefresh_ = 0;
};
