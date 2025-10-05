#pragma once
#include <filesystem>
#include <optional>

#include "rawproc/UnifiedRawData.h"

namespace rawproc {

class RawLoader {
public:
    std::optional<UnifiedRawData> load(const std::filesystem::path& path);
};

} // namespace rawproc
