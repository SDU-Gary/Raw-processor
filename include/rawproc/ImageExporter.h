#pragma once
#include <filesystem>
#include <string>

#include "rawproc/ImageTypes.h"

namespace rawproc {

class ImageExporter {
public:
    // Export 8-bit PNG/JPG using stb if available; otherwise falls back to PPM.
    bool exportPNG(const std::filesystem::path& path, const RgbImageF& img);
    bool exportJPG(const std::filesystem::path& path, const RgbImageF& img, int quality = 90);

    // Export EXR using TinyEXR if available; returns false if unsupported.
    bool exportEXR(const std::filesystem::path& path, const RgbImageF& img);
};

} // namespace rawproc
