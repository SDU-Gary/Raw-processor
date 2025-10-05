#include "rawproc/RawLoader.h"

#include <fstream>

namespace rawproc {

#if !defined(RAWPROC_HAVE_LIBRAW)
std::optional<UnifiedRawData> RawLoader::load(const std::filesystem::path& path) {
    // Placeholder loader: creates a dummy RAW image when LibRaw is not enabled.
    UnifiedRawData out;
    out.raw.width = 640;
    out.raw.height = 480;
    out.raw.data.resize(static_cast<size_t>(out.raw.width) * out.raw.height, 512);
    return out;
}
#endif

} // namespace rawproc
