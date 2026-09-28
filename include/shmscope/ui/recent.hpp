#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shmscope {

    class RecentList {
    public:
        explicit RecentList(std::filesystem::path file);

        [[nodiscard]] static std::filesystem::path defaultPath();

        [[nodiscard]] std::span<const std::string> entries() const noexcept {
            return entries_;
        }

        void touch(std::string_view name);

    private:
        static constexpr std::size_t LIMIT = 10;

        void load();
        void save() const;

        std::filesystem::path file_;
        std::vector<std::string> entries_;
    };

}  // namespace shmscope
