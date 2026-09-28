#pragma once

#include <array>
#include <string_view>

#include "shmscope/document_reader.hpp"

namespace shmscope {

    struct JsonReader {
        static constexpr std::string_view NAME = "json";
        static constexpr std::array<std::string_view, 1> EXTENSIONS = {".json"};

        [[nodiscard]] static ReadResult read(std::string_view text,
                                             std::string_view source);
    };

    static_assert(DocumentReader<JsonReader>);
}  // namespace shmscope
