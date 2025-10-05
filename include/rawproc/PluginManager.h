#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "rawproc/IProcessingPlugin.h"
#include "rawproc/PAL/DynamicLibrary.h"

namespace rawproc {

struct PluginPrototype {
    std::string name;
    ProcessingStage stage;
    std::vector<ParameterDesc> params;
    std::filesystem::path libraryPath;
};

class PluginManager {
public:
    using InstanceId = size_t;

    bool scanDirectory(const std::filesystem::path& dir);

    const std::vector<PluginPrototype>& prototypes() const { return prototypes_; }

    // Create a new instance from a prototype index
    InstanceId createInstance(size_t protoIndex);
    std::shared_ptr<IProcessingPlugin> getInstance(InstanceId id);

    bool destroyInstance(InstanceId id);

private:
    struct LoadedLib {
        pal::DynamicLibrary lib;
        std::filesystem::path path;
    };

    std::vector<PluginPrototype> prototypes_;
    std::vector<LoadedLib> loadedLibs_;
    std::map<InstanceId, std::shared_ptr<IProcessingPlugin>> instances_;
    InstanceId nextId_ = 1;
};

} // namespace rawproc
