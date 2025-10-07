#include <filesystem>
#include <iostream>
#include <string>
#include <cstring>

#include "rawproc/ImageExporter.h"
#include "rawproc/PluginManager.h"
#include "rawproc/ProcessingPipeline.h"
#include "rawproc/RawLoader.h"
#include "rawproc/UnifiedRawData.h"
#include "rawproc/Tiling.h"

#ifndef RAWPROC_RUNTIME_PLUGIN_DIR
#define RAWPROC_RUNTIME_PLUGIN_DIR "./plugins"
#endif

static bool parseInt(const char* s, int& out) {
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

int main(int argc, char** argv) {
    using namespace rawproc;

    std::filesystem::path pluginDir = RAWPROC_RUNTIME_PLUGIN_DIR;
    PluginManager pm;

    std::cout << "Scanning plugins in: " << pluginDir << "\n";
    pm.scanDirectory(pluginDir);

    const auto& protos = pm.prototypes();
    std::cout << "Found " << protos.size() << " plugin(s)\n";
    for (size_t i = 0; i < protos.size(); ++i) {
        std::cout << "  [" << i << "] " << protos[i].name << " stage=" << (int)protos[i].stage << "\n";
    }

    RawLoader loader;
    UnifiedRawData data;
    if (argc > 1 && argv[1][0] != '-') {
        auto loaded = loader.load(argv[1]);
        if (!loaded) {
            std::cerr << "Failed to load RAW: " << argv[1] << "\n";
            return 1;
        }
        data = std::move(*loaded);
    } else {
        // No file provided; synthesize a dummy RAW frame locally.
        data.raw.width = 640;
        data.raw.height = 480;
        data.raw.data.resize(static_cast<size_t>(data.raw.width) * data.raw.height, 512);
    }

    // Optional: parse viewport / tile size / LOD
    bool hasViewport = false;
    int vx = 0, vy = 0, vw = 0, vh = 0;
    int tileSize = 256;
    int lod = 0;
    bool useGpu = false;
    int gpuDebug = 0; // 0=real,1=coords,2=raw
    bool gpuSynth = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--viewport") == 0 && i + 4 < argc) {
            int x, y, w, h;
            if (parseInt(argv[i+1], x) && parseInt(argv[i+2], y) && parseInt(argv[i+3], w) && parseInt(argv[i+4], h)) {
                vx = x; vy = y; vw = w; vh = h; hasViewport = true; i += 4; continue;
            } else {
                std::cerr << "Invalid --viewport args. Usage: --viewport x y w h\n";
                return 2;
            }
        } else if (std::strcmp(argv[i], "--tile") == 0 && i + 1 < argc) {
            int ts;
            if (parseInt(argv[i+1], ts) && ts > 0) { tileSize = ts; i += 1; continue; }
            std::cerr << "Invalid --tile N\n"; return 2;
        } else if (std::strcmp(argv[i], "--lod") == 0 && i + 1 < argc) {
            int L;
            if (parseInt(argv[i+1], L) && L >= 0) { lod = L; i += 1; continue; }
            std::cerr << "Invalid --lod N (>=0)\n"; return 2;
        } else if (std::strcmp(argv[i], "--gpu") == 0) {
            useGpu = true; continue;
        } else if (std::strcmp(argv[i], "--gpu-debug") == 0 && i + 1 < argc) {
            if (std::strcmp(argv[i+1], "coords") == 0) gpuDebug = 1;
            else if (std::strcmp(argv[i+1], "raw") == 0) gpuDebug = 2;
            else gpuDebug = 0;
            i += 1; continue;
        } else if (std::strcmp(argv[i], "--gpu-synth") == 0) {
            gpuSynth = true; continue;
        }
    }

    // Add an optional PRE_DEMOSAIC plugin (e.g., denoise)
    for (size_t i = 0; i < protos.size(); ++i) {
        if (protos[i].stage == ProcessingStage::PRE_DEMOSAIC) {
            auto id = pm.createInstance(i);
            if (id) {
                data.history.push_back({id});
                std::cout << "Added plugin instance: " << protos[i].name << " id=" << id << "\n";
            }
            break;
        }
    }

    // Add WhiteBalance (POST_DEMOSAIC_LINEAR) if available, set from meta if present
    for (size_t i = 0; i < protos.size(); ++i) {
        if (protos[i].name == "WhiteBalance") {
            auto id = pm.createInstance(i);
            if (id) {
                auto inst = pm.getInstance(id);
                if (inst) {
                    inst->setParameter("R", data.meta.wb[0]);
                    inst->setParameter("G", data.meta.wb[1]);
                    inst->setParameter("B", data.meta.wb[2]);
                }
                data.history.push_back({id});
                std::cout << "Added WhiteBalance id=" << id << " (from meta)\n";
            }
            break;
        }
    }

    // Add Gamma (FINALIZE) if available
    for (size_t i = 0; i < protos.size(); ++i) {
        if (protos[i].name == "Gamma") {
            auto id = pm.createInstance(i);
            if (id) {
                data.history.push_back({id});
                std::cout << "Added Gamma id=" << id << "\n";
            }
            break;
        }
    }

    ProcessingPipeline pipeline(pm);

    rawproc::RenderRequest req;
    req.outWidth = static_cast<int>(data.raw.width);
    req.outHeight = static_cast<int>(data.raw.height);
    req.tileSize = tileSize;
    req.lod = lod;

    if (hasViewport) {
        // Compute tiles covering viewport
        // Viewport is interpreted at the selected LOD
        int x0 = std::max(0, vx);
        int y0 = std::max(0, vy);
        int x1 = std::min<int>(req.outWidth, vx + vw);
        int y1 = std::min<int>(req.outHeight, vy + vh);
        if (x1 <= x0 || y1 <= y0) {
            std::cerr << "Viewport out of bounds or empty\n"; return 3;
        }
        int tx0 = x0 / tileSize;
        int ty0 = y0 / tileSize;
        int tx1 = (x1 - 1) / tileSize;
        int ty1 = (y1 - 1) / tileSize;
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) req.tiles.push_back({tx, ty, 0});
        }
    }

    pipeline.setUseGpu(useGpu);
    pipeline.setGpuDebugMode(gpuDebug);
    pipeline.setGpuSynthetic(gpuSynth);
    auto rgb = pipeline.apply(data, req, rawproc::RenderMode::GrayscalePreview);

    ImageExporter ex;
    // Output: if viewport specified, write cropped image
    std::filesystem::path out = hasViewport ? std::filesystem::path("preview_viewport.png") : std::filesystem::path("preview.png");
    if (hasViewport) {
        RgbImageF crop;
        crop.width = static_cast<uint32_t>(vw);
        crop.height = static_cast<uint32_t>(vh);
        crop.data.resize(static_cast<size_t>(vw) * vh * 3u);
        int x0 = std::max(0, vx);
        int y0 = std::max(0, vy);
        for (int yy = 0; yy < vh; ++yy) {
            int sy = y0 + yy;
            if (sy < 0 || sy >= static_cast<int>(rgb.height)) continue;
            for (int xx = 0; xx < vw; ++xx) {
                int sx = x0 + xx;
                if (sx < 0 || sx >= static_cast<int>(rgb.width)) continue;
                size_t si = (static_cast<size_t>(sy) * rgb.width + sx) * 3u;
                size_t di = (static_cast<size_t>(yy) * vw + xx) * 3u;
                crop.data[di+0] = rgb.data[si+0];
                crop.data[di+1] = rgb.data[si+1];
                crop.data[di+2] = rgb.data[si+2];
            }
        }
        if (ex.exportPNG(out, crop)) {
            std::cout << "Wrote viewport image to " << out << "\n";
        } else {
            std::cerr << "Failed to write viewport image\n";
        }
    } else {
        if (ex.exportPNG(out, rgb)) {
            std::cout << "Wrote preview image to " << out << "\n";
        } else {
            std::cerr << "Failed to write preview image" << "\n";
        }
    }

    return 0;
}
