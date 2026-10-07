#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace AQT
{
    class ConfigFile
    {
    public:
        void Load(const std::filesystem::path& path)
        {
            std::ifstream input(path);
            if (!input) {
                if (std::filesystem::exists(path)) {
                    throw std::runtime_error("Cannot read " + path.string());
                }
                return;
            }
            std::string section;
            std::string line;
            while (std::getline(input, line)) {
                if (line.starts_with("\xEF\xBB\xBF")) {
                    line.erase(0, 3);
                }
                auto text = Trim(line);
                if (text.empty() || text.front() == ';' || text.front() == '#') {
                    continue;
                }
                if (text.front() == '[' && text.back() == ']') {
                    section = Key(Trim(text.substr(1, text.size() - 2)));
                } else if (const auto equal = text.find('='); equal != std::string_view::npos) {
                    values[{section, Key(Trim(text.substr(0, equal)))}] = Trim(text.substr(equal + 1));
                }
            }
            if (input.bad()) {
                throw std::runtime_error("Cannot finish reading " + path.string());
            }
        }

        float Number(std::string_view section, std::string_view key, float fallback) const
        {
            const auto found = values.find({Key(section), Key(key)});
            if (found == values.end()) {
                return fallback;
            }
            std::istringstream input(found->second);
            input.imbue(std::locale::classic());
            float value;
            if (!(input >> value) || !std::isfinite(value)) {
                return fallback;
            }
            input >> std::ws;
            const auto next = input.peek();
            return next == std::char_traits<char>::eof() || next == ';' || next == '#' ? value : fallback;
        }

    private:
        static std::string_view Trim(std::string_view text)
        {
            const auto begin = text.find_first_not_of(" \t\r\n");
            return begin == std::string_view::npos ? std::string_view{} :
                text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
        }

        static std::string Key(std::string_view text)
        {
            std::string result(text);
            std::ranges::transform(result, result.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            });
            return result;
        }

        std::map<std::pair<std::string, std::string>, std::string> values;
    };
}
