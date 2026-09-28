#include "shmscope/ui/recent.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

namespace shmscope {

    RecentList::RecentList(std::filesystem::path file)
        : file_(std::move(file)) {
        load();
    }

    std::filesystem::path RecentList::defaultPath() {
        if (const char* state = std::getenv("XDG_STATE_HOME");
            state != nullptr && *state != '\0') {
            return std::filesystem::path(state) / "shmscope" / "recent";
        }
        if (const char* home = std::getenv("HOME");
            home != nullptr && *home != '\0') {
            return std::filesystem::path(home) / ".local" / "state" /
                   "shmscope" / "recent";
        }
        return {};
    }

    void RecentList::touch(std::string_view name) {
        std::erase(entries_, name);
        entries_.insert(entries_.begin(), std::string(name));
        if (entries_.size() > LIMIT) {
            entries_.resize(LIMIT);
        }
        save();
    }

    void RecentList::load() {
        if (file_.empty()) return;

        std::ifstream in(file_);
        for (std::string line;
             entries_.size() < LIMIT && std::getline(in, line);) {
            if (!line.empty() &&
                std::ranges::find(entries_, line) == entries_.end()) {
                entries_.push_back(std::move(line));
            }
        }
    }

    void RecentList::save() const {
        if (file_.empty()) return;

        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(), ec);
        std::ofstream out(file_, std::ios::trunc);
        for (const auto& entry : entries_) {
            out << entry << "\n";
        }
    }

}  // namespace shmscope
