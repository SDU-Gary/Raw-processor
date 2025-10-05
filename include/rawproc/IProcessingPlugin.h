#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "rawproc/ImageTypes.h"

namespace rawproc {

// Processing stages in the pipeline.
enum class ProcessingStage {
    PRE_DEMOSAIC = 0,
    DEMOSAIC,
    POST_DEMOSAIC_LINEAR,
    FINALIZE
};

// Supported parameter types for plugins.
enum class ParamType { Float, Int, Bool, Enum, String };

using ParamValue = std::variant<float, int, bool, std::string>;

struct ParameterDesc {
    std::string name;
    ParamType type = ParamType::Float;
    // UI hints
    float minF = 0.0f;
    float maxF = 1.0f;
    float stepF = 0.01f;
    // For Int
    int minI = 0;
    int maxI = 100;
    int stepI = 1;
    // For Enum
    std::vector<std::string> enumOptions; // e.g., {"fast", "quality"}
    // Default value
    ParamValue defaultValue = 0.0f;
};

class IProcessingPlugin {
public:
    virtual ~IProcessingPlugin() = default;

    virtual std::string_view getName() const = 0;
    virtual ProcessingStage getProcessingStage() const = 0;

    // Returns the parameter descriptors for UI auto-generation.
    virtual std::vector<ParameterDesc> getParameters() const = 0;

    // Set parameter by name.
    virtual bool setParameter(std::string_view name, const ParamValue& value) = 0;

    // Processing entry points (plugin implements one depending on stage)
    virtual void process_raw(RawImage& raw) { (void)raw; }
    virtual void process_rgb(RgbImageF& rgb) { (void)rgb; }
};

} // namespace rawproc

// Factory symbol exported by each plugin dynamic library.
#if defined(_WIN32)
  #if defined(RAWPROC_PLUGIN_EXPORTS)
    #define RAWPROC_PLUGIN_API __declspec(dllexport)
  #else
    #define RAWPROC_PLUGIN_API __declspec(dllimport)
  #endif
#else
  #define RAWPROC_PLUGIN_API __attribute__((visibility("default")))
#endif

extern "C" {
    rawproc::IProcessingPlugin* RAWPROC_PLUGIN_API create_plugin();
}
