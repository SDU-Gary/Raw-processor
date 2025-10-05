#include <filesystem>
#include <iostream>
#include <string>

#include "rawproc/ImageExporter.h"
#include "rawproc/PluginManager.h"
#include "rawproc/ProcessingPipeline.h"
#include "rawproc/RawLoader.h"
#include "rawproc/UnifiedRawData.h"

#ifndef RAWPROC_RUNTIME_PLUGIN_DIR
#define RAWPROC_RUNTIME_PLUGIN_DIR "./plugins"
#endif

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
    if (argc > 1) {
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
    auto rgb = pipeline.apply(data, rawproc::RenderMode::GrayscalePreview);

    ImageExporter ex;
#if defined(RAWPROC_HAVE_STB)
    std::filesystem::path out = "preview.png";
#else
    std::filesystem::path out = "preview.ppm";
#endif
    if (ex.exportPNG(out, rgb)) {
        std::cout << "Wrote preview image to " << out << "\n";
    } else {
        std::cerr << "Failed to write preview image" << "\n";
    }

    return 0;
}
