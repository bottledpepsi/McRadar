// Avatar.h - friends get a face avatar in the C# tool (downloaded skin heads). In this port the
// avatar is a placeholder: a coloured rounded square with the first letter of the name, with a
// stable colour derived from the name. (Downloading real skins needs WinHTTP + a PNG decoder.)
#pragma once
#include <string>

struct AvatarColor
{
    float r, g, b;
};

inline AvatarColor AvatarColorFor(const std::string& name)
{
    unsigned h = 2166136261u; // FNV-1a
    for (unsigned char c : name)
    {
        h ^= c;
        h *= 16777619u;
    }
    // keep it green-ish for "friend" but vary hue a little per name
    const float t = (h % 1000) / 1000.0f;
    return {0.10f + 0.20f * t, 0.45f + 0.30f * (1.0f - t), 0.25f + 0.25f * t};
}

inline std::string AvatarInitial(const std::string& name)
{
    if (name.empty()) return "?";
    unsigned char c = static_cast<unsigned char>(name[0]);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 32);
    return std::string(1, static_cast<char>(c));
}
