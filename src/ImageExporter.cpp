#include "rawproc/ImageExporter.h"

#include <fstream>
#include <vector>
#include <cstring>

#if defined(RAWPROC_HAVE_STB)
#include "rawproc/stb_image_write.h"
#endif

#if defined(RAWPROC_HAVE_TINYEXR)
#include "rawproc/tinyexr.h"
#endif

namespace rawproc {

static inline unsigned char clamp8(float v) {
    if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f; return static_cast<unsigned char>(v * 255.0f + 0.5f);
}

// Very small PPM writer as a stand-in for PNG/JPG, to avoid external deps in scaffold.
static bool write_ppm(const std::filesystem::path& path, const RgbImageF& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << img.width << " " << img.height << "\n255\n";
    for (size_t i = 0; i < img.data.size(); i += 3) {
        unsigned char r = clamp8(img.data[i + 0]);
        unsigned char g = clamp8(img.data[i + 1]);
        unsigned char b = clamp8(img.data[i + 2]);
        f.write(reinterpret_cast<const char*>(&r), 1);
        f.write(reinterpret_cast<const char*>(&g), 1);
        f.write(reinterpret_cast<const char*>(&b), 1);
    }
    return true;
}

bool ImageExporter::exportPNG(const std::filesystem::path& path, const RgbImageF& img) {
#if defined(RAWPROC_HAVE_STB)
    // Convert to 8-bit interleaved
    std::vector<unsigned char> buf(static_cast<size_t>(img.width) * img.height * 3u);
    for (size_t i = 0; i < buf.size(); i += 3) {
        buf[i + 0] = clamp8(img.data[i + 0]);
        buf[i + 1] = clamp8(img.data[i + 1]);
        buf[i + 2] = clamp8(img.data[i + 2]);
    }
    int ok = stbi_write_png(path.string().c_str(), static_cast<int>(img.width), static_cast<int>(img.height), 3, buf.data(), static_cast<int>(img.width * 3));
    return ok != 0;
#else
    auto out = path;
    if (out.extension() != ".ppm") out.replace_extension(".ppm");
    return write_ppm(out, img);
#endif
}

bool ImageExporter::exportJPG(const std::filesystem::path& path, const RgbImageF& img, int quality) {
#if defined(RAWPROC_HAVE_STB)
    std::vector<unsigned char> buf(static_cast<size_t>(img.width) * img.height * 3u);
    for (size_t i = 0; i < buf.size(); i += 3) {
        buf[i + 0] = clamp8(img.data[i + 0]);
        buf[i + 1] = clamp8(img.data[i + 1]);
        buf[i + 2] = clamp8(img.data[i + 2]);
    }
    int ok = stbi_write_jpg(path.string().c_str(), static_cast<int>(img.width), static_cast<int>(img.height), 3, buf.data(), quality);
    return ok != 0;
#else
    auto out = path;
    if (out.extension() != ".ppm") out.replace_extension(".ppm");
    return write_ppm(out, img);
#endif
}

bool ImageExporter::exportEXR(const std::filesystem::path& path, const RgbImageF& img) {
#if defined(RAWPROC_HAVE_TINYEXR)
    const int w = static_cast<int>(img.width);
    const int h = static_cast<int>(img.height);
    // TinyEXR expects separate channel arrays (B,G,R,A) in this order.
    std::vector<float> R(w * h), G(w * h), B(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 3u;
            size_t j = static_cast<size_t>(y) * w + x;
            R[j] = img.data[i + 0];
            G[j] = img.data[i + 1];
            B[j] = img.data[i + 2];
        }
    }
    const float* channels[3];
    channels[0] = B.data();
    channels[1] = G.data();
    channels[2] = R.data();
    EXRHeader header; InitEXRHeader(&header);
    EXRImage image;  InitEXRImage(&image);
    image.num_channels = 3;
    image.images = (unsigned char**)channels;
    image.width = w;
    image.height = h;
    header.num_channels = 3;
    header.channels = (EXRChannelInfo*)malloc(sizeof(EXRChannelInfo) * header.num_channels);
    strncpy(header.channels[0].name, "B", 255);
    strncpy(header.channels[1].name, "G", 255);
    strncpy(header.channels[2].name, "R", 255);
    header.pixel_types = (int*)malloc(sizeof(int) * header.num_channels);
    header.requested_pixel_types = (int*)malloc(sizeof(int) * header.num_channels);
    for (int i = 0; i < header.num_channels; i++) {
        header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
        header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
    }
    const char* err = nullptr;
    int ret = SaveEXRImageToFile(&image, &header, path.string().c_str(), &err);
    free(header.channels);
    free(header.pixel_types);
    free(header.requested_pixel_types);
    if (ret != TINYEXR_SUCCESS) {
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    return true;
#else
    (void)path; (void)img;
    return false;
#endif
}

} // namespace rawproc
