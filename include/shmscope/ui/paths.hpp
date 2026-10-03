#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shmscope {

    inline constexpr std::string_view LAYOUT_EXTENSIONS[] = {".ksy", ".yaml",
                                                             ".yml", ".json"};

    [[nodiscard]] std::filesystem::path expandHome(std::string_view text);

    [[nodiscard]] std::vector<std::string> completePath(
        std::string_view partial,
        std::span<const std::string_view> extensions = {});

    [[nodiscard]] std::string commonPrefix(
        const std::vector<std::string>& choices);

}  // namespace shmscope
