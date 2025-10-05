#include "rawproc/RawLoader.h"

#if defined(RAWPROC_HAVE_LIBRAW)

#include <libraw/libraw.h>
#include <iostream>
#include <vector>
#include <cstring>

namespace rawproc {

static bool load_raw_with_libraw(const std::filesystem::path& path, UnifiedRawData& out) {
    LibRaw proc;
    if (proc.open_file(path.string().c_str()) != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw: open_file failed: " << path << "\n";
        return false;
    }
    if (proc.unpack() != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw: unpack failed\n";
        proc.recycle();
        return false;
    }

    auto& sizes = proc.imgdata.sizes;
    auto* rawp = proc.imgdata.rawdata.raw_image; // 16-bit buffer
    if (!rawp) {
        std::cerr << "LibRaw: raw_image is null (possibly non-Bayer sensor)\n";
        proc.recycle();
        return false;
    }

    const uint32_t rw = sizes.raw_width;
    const uint32_t rh = sizes.raw_height;
    const uint32_t left = sizes.left_margin;
    const uint32_t top  = sizes.top_margin;
    const uint32_t w = sizes.width;   // active area
    const uint32_t h = sizes.height;  // active area
    out.raw.width = w;
    out.raw.height = h;
    out.raw.data.resize(static_cast<size_t>(w) * h);

    // Copy only active area, skipping masked margins to avoid black edges
    for (uint32_t y = 0; y < h; ++y) {
        const uint16_t* src = rawp + (top + y) * rw + left;
        uint16_t* dst = out.raw.data.data() + static_cast<size_t>(y) * w;
        std::memcpy(dst, src, static_cast<size_t>(w) * sizeof(uint16_t));
    }

    // White balance estimates
    out.meta.wb[0] = proc.imgdata.color.cam_mul[0] != 0 ? proc.imgdata.color.cam_mul[0] : 1.0f;
    out.meta.wb[1] = proc.imgdata.color.cam_mul[1] != 0 ? proc.imgdata.color.cam_mul[1] : 1.0f;
    out.meta.wb[2] = proc.imgdata.color.cam_mul[2] != 0 ? proc.imgdata.color.cam_mul[2] : 1.0f;

    // Black/white levels for normalization
    out.meta.black_level = static_cast<float>(proc.imgdata.color.black);
    int maximum = proc.imgdata.color.maximum;
    if (maximum <= 0) maximum = 65535;
    out.meta.white_level = static_cast<float>(maximum);

    proc.recycle();
    return true;
}

std::optional<UnifiedRawData> RawLoader::load(const std::filesystem::path& path) {
    UnifiedRawData out;
    if (load_raw_with_libraw(path, out)) return out;
    return std::nullopt;
}

} // namespace rawproc

#endif // RAWPROC_HAVE_LIBRAW
