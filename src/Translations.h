#pragma once

#include <format>
#include <string>

namespace AQT::Translations
{
    void Load();
    const char* Get(const char* key);
    const char* Label(const char* key);
    const char* English(const char* key);

    template <class... Args>
    std::string Format(const char* key, Args&&... args)
    {
        try {
            return std::vformat(Get(key), std::make_format_args(args...));
        } catch (const std::format_error&) {
            return std::vformat(English(key), std::make_format_args(args...));
        }
    }
}
