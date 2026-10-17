// Json.h - tiny JSON reader/writer, just enough for esp_config.json, friends.json and profiles/*.json
// (flat objects of string/number/bool values, and arrays of strings). No external dependency.
// Unknown keys are kept in the map and simply ignored by callers, like System.Text.Json does.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace json
{
    struct Value
    {
        enum class Type { Null, Bool, Number, String } type = Type::Null;

        bool b = false;
        double n = 0.0;
        std::string s;
    };

    using Object = std::map<std::string, Value>;

    // Parses a top-level object. Nested objects/arrays inside values are skipped (value -> Null).
    std::optional<Object> ParseObject(std::string_view text, std::string* error = nullptr);
    // Parses a top-level array of strings (non-string elements are skipped).
    std::optional<std::vector<std::string>> ParseStringArray(std::string_view text, std::string* error = nullptr);

    std::string Escape(std::string_view s);
    std::string Number(double v); // 2 -> "2", 0.75 -> "0.75" (no trailing zeros)

    // Convenience getters with fallbacks (missing key / wrong type -> fallback).
    std::string GetString(const Object& o, const char* key, const std::string& fallback);
    double GetNumber(const Object& o, const char* key, double fallback);
    bool GetBool(const Object& o, const char* key, bool fallback);
} // namespace json
