// AgentLauncher.h - loads mcradar-agent.jar into the running Minecraft JVM.
//
// Mechanism: we run   java -jar mcradar-agent.jar <pid> <agent.jar> <options>
// The jar's main class uses the OFFICIAL JVM Attach API (the same thing jcmd, VisualVM and IDE
// debuggers use) to make the target JVM load the jar as a Java agent. We write no code into the
// game's memory ourselves: the JDK's attach implementation talks to the JVM's own, documented
// attach listener and the JVM loads the agent. (On Windows the JDK uses a short-lived remote
// thread internally for that handshake; the target must run at the same privilege level.)
#pragma once
#include "Platform.h"
#include <optional>
#include <stop_token>
#include <string>

namespace AgentLauncher {

// Folder containing mcradar.exe (with trailing backslash removed).
std::wstring ExeDir();

// `configured` (radar.ini) > %JAVA_HOME%\bin\java.exe > first java.exe on PATH.
// Must be a full JDK (needs the jdk.attach module); a plain JRE will fail with exit code 3.
std::optional<std::wstring> FindJava(const std::string& configured);

// Relative jar paths are resolved against the exe folder.
std::wstring ResolveJar(const std::string& configured);

// Runs the attach helper, relays its output to the log and waits for it (abortable via `st`).
// Returns true if the agent was loaded (helper exit code 0).
bool Inject(DWORD pid, const std::wstring& javaExe, const std::wstring& jar, int udpPort, std::stop_token st);

}  // namespace AgentLauncher
