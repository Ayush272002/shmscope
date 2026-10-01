#pragma once

#include "shmscope/core/format.hpp"
#include "shmscope/core/formatters.hpp"

namespace shmscope {
    using DefaultFormatters =
        FormatterSet<DecimalFormatter, HexFormatter, ScaledFormatter,
                     EnumFormatter, TimestampFormatter>;
}
