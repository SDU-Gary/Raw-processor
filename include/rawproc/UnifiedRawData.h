#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "rawproc/ImageTypes.h"

namespace rawproc {

struct ProcessingStep {
    size_t instanceId = 0; // references PluginManager instance
};

struct CameraMeta {
    float wb[3] = {1.0f, 1.0f, 1.0f};
    // Sensor black/white levels for normalization in previews
    float black_level = 0.0f;
    float white_level = 65535.0f;
    // Add matrices, EXIF fields etc. later
};

struct UnifiedRawData {
    RawImage raw; // original sensor data
    CameraMeta meta;
    std::vector<ProcessingStep> history;
};

} // namespace rawproc
