// FriendList.h - port of FriendList.cs: friends.json next to the exe, case-insensitive, saved on every change.
#pragma once
#include <string>
#include <vector>

namespace FriendList
{
    void Load(); // missing/empty/malformed file -> empty list (file is never overwritten on error)
    void Save(); // atomic: temp file + rename
    bool Add(const std::string& name); // false if blank or already present
    bool Remove(const std::string& name); // false if not present
    bool Contains(const std::string& name); // case-insensitive
    std::vector<std::string> Names(); // snapshot, sorted case-insensitively
} // namespace FriendList
