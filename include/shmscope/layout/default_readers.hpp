#pragma once

#include "shmscope/layout/document_reader.hpp"
#include "shmscope/layout/readers/json_reader.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace shmscope {

    using DefaultReaders = ReaderSet<YamlReader, JsonReader>;
}
