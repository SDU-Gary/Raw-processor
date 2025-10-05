#include "rawproc/ProcessingPipeline.h"

#include <algorithm>

namespace rawproc {

RgbImageF ProcessingPipeline::apply(const UnifiedRawData& data, RenderMode mode) {
    // Start with copying RAW into a working buffer
    RawImage workingRaw = data.raw; // copy

    // 1) PRE_DEMOSAIC
    for (const auto& step : data.history) {
        auto inst = pm_.getInstance(step.instanceId);
        if (!inst) continue;
        if (inst->getProcessingStage() == ProcessingStage::PRE_DEMOSAIC) {
            inst->process_raw(workingRaw);
        }
    }

    // 2) DEMOSAIC
    RgbImageF rgb;
    rgb.width = workingRaw.width;
    rgb.height = workingRaw.height;
    rgb.data.resize(static_cast<size_t>(rgb.width) * rgb.height * 3u, 0.0f);
    if (mode == RenderMode::GrayscalePreview) {
        // Grayscale preview: replicate normalized RAW into RGB equally.
        // Use camera black/white levels when available for stable normalization.
        float black = data.meta.black_level;
        float white = data.meta.white_level;
        if (!(white > black + 1.0f)) {
            // Fallback to per-image min/max if meta is not valid
            uint16_t minv = 0xFFFF, maxv = 0;
            for (auto v : workingRaw.data) { if (v < minv) minv = v; if (v > maxv) maxv = v; }
            black = static_cast<float>(minv);
            white = static_cast<float>(maxv);
        }
        float denom = (white > black + 1.0f) ? (white - black) : 1.0f;
        float inv = 1.0f / denom;
        for (size_t i = 0, j = 0; i < workingRaw.data.size(); ++i, j += 3) {
            float g = (static_cast<float>(workingRaw.data[i]) - black) * inv;
            if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
            rgb.data[j + 0] = g;
            rgb.data[j + 1] = g;
            rgb.data[j + 2] = g;
        }
    } else {
        // FullColor mode would run a real demosaic (to be implemented).
        // For now, same grayscale fallback.
        uint16_t minv = 0xFFFF, maxv = 0;
        for (auto v : workingRaw.data) { if (v < minv) minv = v; if (v > maxv) maxv = v; }
        float black = static_cast<float>(minv);
        float white = static_cast<float>(maxv);
        float denom = (white > black + 1.0f) ? (white - black) : 1.0f;
        float inv = 1.0f / denom;
        for (size_t i = 0, j = 0; i < workingRaw.data.size(); ++i, j += 3) {
            float g = (static_cast<float>(workingRaw.data[i]) - black) * inv;
            if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
            rgb.data[j + 0] = g;
            rgb.data[j + 1] = g;
            rgb.data[j + 2] = g;
        }
    }

    // 3) POST_DEMOSAIC_LINEAR
    if (mode == RenderMode::FullColor) {
        for (const auto& step : data.history) {
            auto inst = pm_.getInstance(step.instanceId);
            if (!inst) continue;
            if (inst->getProcessingStage() == ProcessingStage::POST_DEMOSAIC_LINEAR) {
                inst->process_rgb(rgb);
            }
        }
    }

    // 4) FINALIZE (e.g., gamma) — always apply (works for grayscale and color)
    for (const auto& step : data.history) {
        auto inst = pm_.getInstance(step.instanceId);
        if (!inst) continue;
        if (inst->getProcessingStage() == ProcessingStage::FINALIZE) {
            inst->process_rgb(rgb);
        }
    }

    return rgb;
}

} // namespace rawproc
