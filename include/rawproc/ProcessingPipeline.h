#pragma once
#include <vector>

#include "rawproc/IProcessingPlugin.h"
#include "rawproc/PluginManager.h"
#include "rawproc/UnifiedRawData.h"

namespace rawproc {

enum class RenderMode { GrayscalePreview, FullColor };

class ProcessingPipeline {
public:
    explicit ProcessingPipeline(PluginManager& pm) : pm_(pm) {}

    // Applies the pipeline and returns a simple RGB image for preview/export.
    RgbImageF apply(const UnifiedRawData& data, RenderMode mode = RenderMode::GrayscalePreview);

private:
    PluginManager& pm_;
};

} // namespace rawproc
