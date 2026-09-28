#pragma once

#include "shmscope/document_reader.hpp"
#include "shmscope/json_reader.hpp"
#include "shmscope/yaml_reader.hpp"

namespace shmscope {

    using DefaultReaders = ReaderSet<YamlReader, JsonReader>;
}
