#pragma once

#include <array>
#include <string_view>

#include "shmscope/document_reader.hpp"

namespace shmscope {

    struct YamlReader {
        static constexpr std::string_view NAME = "yaml";
        static constexpr std::array<std::string_view, 3> EXTENSIONS = {
            ".yaml", ".yml", ".ksy"};

        [[nodiscard]] static ReadResult read(std::string_view text,
                                             std::string_view source);
    };

    static_assert(DocumentReader<YamlReader>);
}  // namespace shmscope
