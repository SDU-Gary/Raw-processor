#pragma once
#include <memory>
#include "rawproc/ImageTypes.h"

namespace rawproc {

// Stub GPU context; will use CPU fallback until WGPU is enabled.
class GpuContext {
public:
    enum class DebugMode : uint32_t { Real = 0, Coords = 1, Raw = 2 };

    GpuContext();
    ~GpuContext();

    bool isAvailable() const;

    // Process grayscale normalization and optional gamma on inner region of a tile.
    // Returns true if executed on GPU, false if not available (caller should fallback to CPU).
    bool processGrayAndGamma(const RawImage& tileRaw,
                             int x0, int y0, int tw, int th,
                             int sx0, int sy0, int sw, int sh,
                             float blackN, float invNorm, RgbImageF& outRgb,
                             float gamma = 2.2f);

    // Debug/diagnostic controls
    void setDebugMode(DebugMode m) { debugMode_ = m; }
    void setSyntheticInput(bool on) { synthInput_ = on; }
private:
    bool available_ = false;
    struct Impl; // pimpl to hide WGPU types
    std::unique_ptr<Impl> impl_;
    DebugMode debugMode_ = DebugMode::Real;
    bool synthInput_ = false;
};

} // namespace rawproc
