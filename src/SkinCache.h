// SkinCache.h - friend avatars: the player's real skin face (port of the C# SkinCache).
//
// Pipeline (same order as the original), run on one background thread:
//   1. UUID   : the agent's UUID when it is an ONLINE-mode (version 4) one; otherwise the Mojang API
//               GET api.mojang.com/users/profiles/minecraft/<name>   (offline/cracked players have v3 UUIDs)
//   2. cache  : %TEMP%\mc_esp_avatars\<uuid>.png
//   3. Crafatar: GET crafatar.com/avatars/<uuid>?size=16&overlay=true   -> face + hat layer, 16x16 PNG
//   4. fallback: GET sessionserver.mojang.com/session/minecraft/profile/<uuid> -> textures property (base64 JSON)
//               -> skin PNG URL -> crop the 8x8 face (8,8) + hat (40,8) and scale 2x
// Failures are retried after 60 s. The three base URLs can be overridden with the environment variables
// ESP_MOJANG_API / ESP_CRAFATAR / ESP_SESSION_API (mirrors, or the local test server used by the test-suite).
// Decoding uses WIC (PNG), downloading uses WinHTTP (HTTPS) - both part of Windows.
#pragma once
#include "Image.h"
#include <memory>
#include <string>

namespace SkinCache
{
    void Start();
    void Stop();
    // Idempotent and cheap: queues a fetch the first time a name is seen (and again 60 s after a failure).
    void Request(const std::string& name, const std::string& uuid);
    // The decoded avatar, or null while loading / unavailable.
    std::shared_ptr<const Image> Get(const std::string& name);
} // namespace SkinCache
