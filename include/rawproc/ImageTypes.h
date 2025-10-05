#pragma once
#include <cstdint>
#include <vector>

namespace rawproc {

// Minimal image buffers to keep core independent from heavy libs.
struct RawImage {
    // Simple Bayer-like single channel buffer; 16-bit per pixel.
    std::vector<uint16_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    // CFA pattern, metadata placeholders can be added later.
};

struct RgbImageF {
    // Interleaved RGB, float per channel.
    std::vector<float> data; // size = width*height*3
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace rawproc
