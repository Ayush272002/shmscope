#include "shmscope/ui/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace shmscope {

    namespace {

        bool wanted(const std::filesystem::path& name,
                    const std::span<const std::string_view> extensions) {
            if (extensions.empty()) return true;

            const auto extension = name.extension().string();
            return std::ranges::find(extensions, extension) != extensions.end();
        }

    }  // namespace

    std::filesystem::path expandHome(const std::string_view text) {
        const char* home = std::getenv("HOME");
        if (home != nullptr && text == "~") return home;
        if (home != nullptr && text.starts_with("~/")) {
            return std::filesystem::path(home) / text.substr(2);
        }
        return std::filesystem::path(text);
    }

    std::vector<std::string> completePath(
        const std::string_view partial,
        const std::span<const std::string_view> extensions) {
        const auto slash = partial.rfind('/');
        const auto directory = slash == std::string_view::npos
                                   ? std::string_view{}
                                   : partial.substr(0, slash + 1);
        const auto prefix = slash == std::string_view::npos
                                ? partial
                                : partial.substr(slash + 1);

        std::vector<std::string> choices;
        if (prefix == "." || prefix == "..") {
            choices.push_back(std::string(directory) + "../");
            if (prefix == ".") choices.push_back(std::string(directory) + "./");
        }

        std::error_code error;
        const auto listed = directory.empty() ? std::filesystem::path(".")
                                              : expandHome(directory);
        std::filesystem::directory_iterator entries(listed, error);
        if (error) {
            std::ranges::sort(choices);
            return choices;
        }

        for (const auto& entry : entries) {
            const auto name = entry.path().filename().string();
            if (!name.starts_with(prefix)) continue;
            if (name.starts_with('.') && !prefix.starts_with('.')) continue;

            std::error_code kind;
            if (entry.is_directory(kind)) {
                choices.push_back(std::string(directory) + name + "/");
            } else if (wanted(entry.path(), extensions)) {
                choices.push_back(std::string(directory) + name);
            }
        }

        std::ranges::sort(choices);
        return choices;
    }

    std::string commonPrefix(const std::vector<std::string>& choices) {
        if (choices.empty()) return {};

        std::string prefix = choices.front();
        for (const auto& choice : choices) {
            const auto [mine, theirs] = std::ranges::mismatch(prefix, choice);
            prefix.erase(static_cast<std::size_t>(mine - prefix.begin()));
        }
        return prefix;
    }

}  // namespace shmscope
