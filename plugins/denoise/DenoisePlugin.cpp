#include "rawproc/IProcessingPlugin.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rawproc;

namespace {
class DenoisePlugin final : public IProcessingPlugin {
public:
    std::string_view getName() const override { return name_; }
    ProcessingStage getProcessingStage() const override { return ProcessingStage::PRE_DEMOSAIC; }

    std::vector<ParameterDesc> getParameters() const override {
        return {
            ParameterDesc{ "强度", ParamType::Float, 0.0f, 1.0f, 0.01f, 0, 0, 1, {}, ParamValue(0.25f) },
        };
    }

    bool setParameter(std::string_view name, const ParamValue& value) override {
        if (name == "强度") {
            if (auto pf = std::get_if<float>(&value)) { strength_ = std::clamp(*pf, 0.0f, 1.0f); return true; }
        }
        return false;
    }

    size_t kernelRadiusPx() const override { return (strength_ <= 0.001f ? 0u : (strength_ < 0.5f ? 1u : 2u)); }
    size_t stateHash() const override {
        // Simple float hash
        return std::hash<int>()(static_cast<int>(strength_ * 1000.0f + 0.5f));
    }

    void process_raw(RawImage& raw) override {
        if (raw.data.empty() || raw.width < 3 || raw.height < 3) return;
        // Very cheap box blur pass controlled by strength.
        const int w = static_cast<int>(raw.width);
        const int h = static_cast<int>(raw.height);
        std::vector<uint16_t> out(raw.data.size());
        const int radius = strength_ <= 0.001f ? 0 : (strength_ < 0.5f ? 1 : 2);
        if (radius == 0) return;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sum = 0; int cnt = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int yy = std::clamp(y + dy, 0, h - 1);
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int xx = std::clamp(x + dx, 0, w - 1);
                        sum += raw.data[yy * w + xx];
                        cnt++;
                    }
                }
                out[y * w + x] = static_cast<uint16_t>(sum / cnt);
            }
        }
        raw.data.swap(out);
    }

private:
    std::string name_ = "Denoise";
    float strength_ = 0.25f;
};
} // namespace

extern "C" rawproc::IProcessingPlugin* create_plugin() {
    return new DenoisePlugin();
}
