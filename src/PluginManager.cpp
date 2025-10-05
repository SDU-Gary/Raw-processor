#include "rawproc/PluginManager.h"

#include <iostream>

namespace fs = std::filesystem;

namespace rawproc {

namespace {
#ifdef _WIN32
    const char* kExt = ".dll";
#elif __APPLE__
    const char* kExt = ".dylib";
#else
    const char* kExt = ".so";
#endif
}

using CreateFn = IProcessingPlugin* (*)();

bool PluginManager::scanDirectory(const fs::path& dir) {
    prototypes_.clear();
    loadedLibs_.clear();

    if (!fs::exists(dir)) return false;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != kExt) continue;

        LoadedLib ll;
        if (!ll.lib.open(entry.path().string())) {
            std::cerr << "Failed to open plugin: " << entry.path().string() << "\n";
            continue;
        }

        auto sym = reinterpret_cast<CreateFn>(ll.lib.symbol("create_plugin"));
        if (!sym) {
            std::cerr << "create_plugin not found in: " << entry.path().string() << "\n";
            continue;
        }

        std::unique_ptr<IProcessingPlugin> proto(sym());
        if (!proto) continue;

        PluginPrototype p;
        p.name = std::string(proto->getName());
        p.stage = proto->getProcessingStage();
        p.params = proto->getParameters();
        p.libraryPath = entry.path();

        prototypes_.push_back(p);
        loadedLibs_.push_back(std::move(ll));
    }

    return !prototypes_.empty();
}

PluginManager::InstanceId PluginManager::createInstance(size_t protoIndex) {
    if (protoIndex >= loadedLibs_.size()) return 0;

    auto sym = reinterpret_cast<CreateFn>(loadedLibs_[protoIndex].lib.symbol("create_plugin"));
    if (!sym) return 0;

    std::shared_ptr<IProcessingPlugin> inst(sym());
    if (!inst) return 0;

    InstanceId id = nextId_++;
    instances_[id] = inst;
    return id;
}

std::shared_ptr<IProcessingPlugin> PluginManager::getInstance(InstanceId id) {
    auto it = instances_.find(id);
    if (it == instances_.end()) return nullptr;
    return it->second;
}

bool PluginManager::destroyInstance(InstanceId id) {
    return instances_.erase(id) > 0;
}

} // namespace rawproc
