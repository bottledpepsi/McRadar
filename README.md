# external-esp (C++ port)

External Direct2D overlay for a **local Minecraft Java world** (single-player / LAN with two accounts on your own PC).
It loads the version-independent Java agent into the game's JVM, receives player positions over loopback UDP, reconstructs
the camera, and draws boxes / skeletons / tracers / names / health / minimap in a transparent click-through window, with
the original tool's 7-tab config window, friends, and profiles.

> Scope: written for reverse-engineering practice on worlds you control. Do not use it on servers you do not own.

```
Minecraft JVM ──(JVM Attach API)── esp-agent.jar ──UDP 127.0.0.1:47615──▶ UdpListener thread
   (agent finds players by structure,                                         │ immutable snapshot, pointer swap
    exports esp_layout.json)                                                  ▼
                                      EspOverlay (UI thread): Smoother → Camera → EspRenderer + ConfigUi → Gfx (D2D) → UpdateLayeredWindow
```

## Build (MinGW-w64, C++20)
Requirements: **JDK 8+** (builds the agent; also what runs the attach step) and **MinGW-w64 g++ 11+ with POSIX threads** (CLion's bundled one, MSYS2 `ucrt64`/`mingw64`).

| Way | Command |
|---|---|
| One step | `build-all.bat` → `dist\mcesp.exe` + `dist\esp-agent.jar` (finds CLion's/MSYS2's g++ automatically) |
| CLion / CMake | open the folder; first run `agent\build.bat`; build target `mcesp` (jar is copied next to the exe) |
| Manual | `agent\build.bat`, then `g++ -std=c++20 -O2 src/*.cpp -o mcesp.exe -ld2d1 -ldwrite -ldwmapi -lwinmm -lgdi32 -luser32 -lws2_32 -static` and put `esp-agent.jar` next to it |

Linker libs: `d2d1 dwrite` (Direct2D/DirectWrite), `dwmapi` (DwmFlush = VSync), `winmm` (1 ms timer), `gdi32 user32`, `ws2_32` (UDP).
**Do not pass `-mwindows`** - the console subsystem is intentional so logs stay visible (they also go to `esp_log.txt`).
`-static` bundles the runtime + winpthread (single exe). Without it you must ship `libwinpthread-1.dll` (`-DESP_STATIC=OFF` in CMake copies it).

## Run
```
mcesp                      # normal: finds Minecraft, attaches the agent, draws the ESP
mcesp --demo               # synthetic players, no Minecraft needed (try the UI / friends / profiles)
mcesp --list               # list java processes (two clients on one PC?)  then:  mcesp --pid 12345
mcesp --screen             # overlay the whole monitor (borderless-fullscreen setups) instead of tracking the window
mcesp --no-inject          # don't auto-attach; start the game with -javaagent:esp-agent.jar=port=47615
mcesp --java "C:\...\jdk-21\bin\java.exe"   # JDK used for the attach helper (default: the game's own JRE, JAVA_HOME, PATH)
mcesp --open-config --port 47616 --jar C:\path\esp-agent.jar
```
Hotkeys (rebindable in HOTKEYS): **Insert** config window · **Delete** toggle ESP · **hold End 3 s** exit. They are polled globally but act only while the game or overlay has focus.
While the config window is open the overlay becomes interactive (`WS_EX_TRANSPARENT` removed and keyboard focus taken); closing it - or clicking back into the game - restores click-through and returns focus to Minecraft.

**Calibration:** set *Camera FOV* (GENERAL) to Minecraft's FOV option (default 70; that slider is the *vertical* FOV). Boxes that drift away from the players mean the FOV differs (sprinting/speed effects change it temporarily).

## How the pieces work
* **Agent attach** (`AgentInjector.h`): `java -cp esp-agent.jar mcradar.Attach <pid> ...` uses the official JVM Attach API (same as `jcmd`) to make the game JVM load the jar; its worker thread discovers player objects by structure (a class with a `com.mojang.authlib.GameProfile` + a position, found in a container holding only players) - no Minecraft names/offsets. It exports the field paths and in-object offsets it found to **`esp_layout.json`** (next to the exe, plus `esp_agent.log`). Helper JVM candidates: the game's JRE → `--java` → `JAVA_HOME` → `PATH` (those without `jdk.attach` are skipped).
* **UDP wire format v2** (`Wire.h`): `MCRADAR 2 / S status / L name x y z yaw pitch health / P ... / I name count x y z` (`-` = unknown). Parsed on the UDP thread into an immutable snapshot, published by a pointer swap under a mutex held only for the copy; the UI thread never blocks on I/O.
* **Camera** (`Camera.h`): the original read GL matrices; this agent reads game objects, so the camera is reconstructed: eye = local feet + 1.62, forward `(-sin y·cos p, -sin p, cos y·cos p)`, right `(-cos y, 0, -sin y)`, up = right × forward, perspective divide with `k = 1/tan(fovY/2)` and the window's aspect ratio. Boxes project the 8 corners of the 0.6×1.8 hitbox; skeletons project joints rotated by the player's yaw.
* **Smoother** (`Smoother.h`): agent updates arrive at 20-60 Hz; each frame extrapolates players and the camera (≤ 100 ms) from their last two samples so boxes don't stutter or lag head turns. Toggle: *Motion smoothing*.
* **Rendering** (`Gfx.h`): Direct2D `DCRenderTarget` + DirectWrite into a 32-bit premultiplied DIB, published with `UpdateLayeredWindow` (per-pixel alpha; *Overlay opacity* = global blend alpha). All drawing happens off-screen, then one atomic present. **VSync:** layered windows have no swap chain, so each frame ends with `DwmFlush()` (blocks until the compositor's next refresh-aligned pass); FPS cap 60/120/Unlimited adds a limiter, and when VSync is off with "Unlimited" a 240 fps limit prevents pegging a core.

## Config & persistence (same schema as the C# tool)
`esp_config.json`, `friends.json`, `profiles/<name>.json` next to the exe; existing files from the C# tool load unchanged (unknown keys ignored, missing keys default). Added keys: `Fov`, `VSync`, `Smoothing`, `ShowHealth`.
Tabs: **GENERAL** · **PLAYERS** (+ friends list) · **TRACERS** · **ITEMS** · **MINIMAP** · **PROFILES** · **HOTKEYS**. Left-click cycles a preset, right-click goes back, toggles are switches, the title bar drags the panel, the mouse wheel scrolls long tabs. Friends render green (box, tracer, name) with an avatar *placeholder*.

## Tests performed (and what was NOT)
* Native unit tests (`tests/run-tests.sh`): C# config JSON loads, Settings↔config round trip, friends/profiles on disk, wire parser (incl. a real agent datagram), projection orientation at 4 yaws/pitch/FOV edge, smoother. All pass.
* Agent vs a fake game with Minecraft-like structure (obfuscated names, Vec3 and plain-double layouts, decoy classes) on JDK 17 and 21: players, local player, yaw, pitch, health discovered.
* Full chain under **Wine + Xvfb**: real agent datagrams → `mcesp.exe` → rendered overlay; a player aimed at (yaw −45°) landed at the predicted pixel (x≈640, y≈430 vs 429 computed). Config UI exercised with synthetic mouse/keyboard: tabs, cycling, text input, friends, profile save/list.
* **Not tested:** a real Minecraft install, real-Windows behavior (click-through/focus handling, `DwmFlush` pacing, Attach on Windows), and discovery against real Minecraft classes. Treat the first run as a test; read `esp_log.txt`, `esp_agent.log`, `esp_layout.json`.

## Known limits
* **Camera is reconstructed, not read:** wrong FOV, third-person view (F5), or a modded camera misaligns boxes; a zoom/FOV effect changes alignment briefly. Head-turn lag is reduced by the smoother but not zero (agent sampling + reflection read).
* **Items:** the protocol and renderer support them (`I` lines, junk filter, names/count) but the current agent only discovers players - the ITEMS tab says so. **Health** shows only on mappings where the method is literally named `getHealth` (Mojang/Yarn/MCP); otherwise it is hidden. **Skins:** friend avatars are placeholders (real heads need WinHTTP + PNG decoding).
* Exclusive fullscreen can't be overlaid; use windowed / borderless (use `--screen` if window tracking misbehaves).
* Newer JDKs warn about dynamically loaded agents (JDK 21+) and restrict `sun.misc.Unsafe`; if the HUD says *Unsafe unavailable* add `--sun-misc-unsafe-memory-access=allow` to the game's JVM args; if dynamic loading is refused add `-XX:+EnableDynamicAgentLoading` (or use `--no-inject` + `-javaagent`).
* Two clients on one PC are two `javaw.exe`: the first window wins unless you pass `--pid`.

## Troubleshooting
| HUD / log says | Fix |
|---|---|
| `waiting for minecraft` | game not running, or window title doesn't start with "Minecraft" - try `--list`, `--pid`, `--screen` |
| `agent attach failed: ... no Attach API` | the game's runtime lacks `jdk.attach` and no JDK was found: install JDK 17+ or pass `--java` |
| `attach failed ... AttachNotSupported` | run `mcesp` and the game at the same privilege level; no `-XX:+DisableAttachMechanism` |
| `agent ok` but nothing drawn | check FOV / that another player is within range & in front; `heading unavailable` = look-vector method not found (see `esp_layout.json`) |
| `Cannot listen on 127.0.0.1:47615` | another instance is running; use `--port` |
| `agent jar not found` | run `agent\build.bat` and keep `esp-agent.jar` next to `mcesp.exe` |

## Layout
`src/` C++ (Logger, Json, Presets, EspConfig+Settings, FriendList, ProfileManager, Wire, Camera, Smoother, ProcessManager, AgentInjector, AgentSupervisor, UdpListener, Gfx, EspRenderer, ConfigUi, EspOverlay, main) ·
`agent/` Java agent (`src/mcradar/`: Agent, Attach, Probe, Mem/Acc/FieldPath, Snapshot) + `build.bat|sh` + `test/` fake game ·
`tests/` host unit tests.
