#pragma once

#include "shmscope/layout/dialect.hpp"
#include "shmscope/layout/dialects/ksy_dialect.hpp"

namespace shmscope {

    using DefaultDialects = DialectSet<KsyDialect>;
}
