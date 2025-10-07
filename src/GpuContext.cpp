#include "rawproc/GpuContext.h"

#if !defined(RAWPROC_USE_WGPU_NATIVE)
namespace rawproc {
GpuContext::GpuContext() {}
GpuContext::~GpuContext() {}
bool GpuContext::isAvailable() const { return false; }
bool GpuContext::processGrayAndGamma(const RawImage& /*tileRaw*/, int, int, int, int, int, int, int, int, float, float, RgbImageF&, float) { return false; }
} // namespace rawproc
#endif
