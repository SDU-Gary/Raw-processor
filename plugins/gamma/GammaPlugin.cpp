#include "rawproc/IProcessingPlugin.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace rawproc;

namespace {
class GammaPlugin final : public IProcessingPlugin {
public:
    std::string_view getName() const override { return name_; }
    ProcessingStage getProcessingStage() const override { return ProcessingStage::FINALIZE; }

    std::vector<ParameterDesc> getParameters() const override {
        return {
            ParameterDesc{ "Gamma", ParamType::Float, 0.1f, 5.0f, 0.01f, 0,0,1, {}, ParamValue(2.2f) },
        };
    }

    bool setParameter(std::string_view name, const ParamValue& value) override {
        if (name == "Gamma") {
            if (auto pf = std::get_if<float>(&value)) { gamma_ = std::max(0.001f, *pf); return true; }
        }
        return false;
    }

    void process_rgb(RgbImageF& rgb) override {
        if (rgb.data.empty()) return;
        const float inv = 1.0f / gamma_;
        for (size_t i = 0; i < rgb.data.size(); ++i) {
            float v = std::max(0.0f, rgb.data[i]);
            rgb.data[i] = std::pow(v, inv);
        }
    }

private:
    std::string name_ = "Gamma";
    float gamma_ = 2.2f;
};
}

extern "C" rawproc::IProcessingPlugin* create_plugin() { return new GammaPlugin(); }

