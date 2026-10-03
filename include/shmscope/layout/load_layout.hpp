#pragma once

#include <filesystem>
#include <utility>

#include "shmscope/layout/default_dialects.hpp"
#include "shmscope/layout/default_readers.hpp"
#include "shmscope/layout/model.hpp"

namespace shmscope {

    template <typename Readers = DefaultReaders,
              typename Dialects = DefaultDialects>
    [[nodiscard]] LayoutResult loadLayout(const std::filesystem::path& file) {
        auto document = Readers::readFile(file);

        if (!document) return std::unexpected(std::move(document.error()));

        return Dialects::load(*document, file.string());
    }
}  // namespace shmscope
