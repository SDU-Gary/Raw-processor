#include "rawproc/IProcessingPlugin.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace rawproc;

namespace {
class WhiteBalancePlugin final : public IProcessingPlugin {
public:
    std::string_view getName() const override { return name_; }
    ProcessingStage getProcessingStage() const override { return ProcessingStage::POST_DEMOSAIC_LINEAR; }

    std::vector<ParameterDesc> getParameters() const override {
        return {
            ParameterDesc{ "R", ParamType::Float, 0.0f, 8.0f, 0.01f, 0,0,1, {}, ParamValue(1.0f) },
            ParameterDesc{ "G", ParamType::Float, 0.0f, 8.0f, 0.01f, 0,0,1, {}, ParamValue(1.0f) },
            ParameterDesc{ "B", ParamType::Float, 0.0f, 8.0f, 0.01f, 0,0,1, {}, ParamValue(1.0f) },
        };
    }

    bool setParameter(std::string_view name, const ParamValue& value) override {
        if (auto pf = std::get_if<float>(&value)) {
            if (name == "R") { r_ = std::max(0.0f, *pf); return true; }
            if (name == "G") { g_ = std::max(0.0f, *pf); return true; }
            if (name == "B") { b_ = std::max(0.0f, *pf); return true; }
        }
        return false;
    }

    void process_rgb(RgbImageF& rgb) override {
        if (rgb.data.empty()) return;
        for (size_t i = 0; i < rgb.data.size(); i += 3) {
            rgb.data[i + 0] *= r_;
            rgb.data[i + 1] *= g_;
            rgb.data[i + 2] *= b_;
        }
    }

private:
    std::string name_ = "WhiteBalance";
    float r_ = 1.0f, g_ = 1.0f, b_ = 1.0f;
};
} // namespace

extern "C" rawproc::IProcessingPlugin* create_plugin() { return new WhiteBalancePlugin(); }

