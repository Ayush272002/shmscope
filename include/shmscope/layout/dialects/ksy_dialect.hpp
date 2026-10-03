#pragma once

#include <string_view>

#include "shmscope/layout/dialect.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"

namespace shmscope {

    struct KsyDialect {
        static constexpr std::string_view NAME = "ksy";
        static constexpr std::string_view FORMATS_KEY = "-shmscope-formats";
        static constexpr std::string_view FORMAT_KEY = "-shmscope-format";

        [[nodiscard]] static bool matches(const Node& root);
        [[nodiscard]] static LayoutResult load(const Node& root,
                                               std::string_view source);
    };

    static_assert(Dialect<KsyDialect>);
}  // namespace shmscope
