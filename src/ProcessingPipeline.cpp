#include "rawproc/ProcessingPipeline.h"

#include <algorithm>
#include <future>
#include <unordered_map>
#include <functional>
#include <list>
#include <mutex>
#include "rawproc/GpuContext.h"

namespace rawproc {

// (Optional) could create GpuContext here in future

RgbImageF ProcessingPipeline::apply(const UnifiedRawData& data, RenderMode mode) {
    // Build a full-frame request and forward
    RenderRequest req;
    req.outWidth = static_cast<int>(data.raw.width);
    req.outHeight = static_cast<int>(data.raw.height);
    req.tileSize = 256;
    const int tilesX = (req.outWidth + req.tileSize - 1) / req.tileSize;
    const int tilesY = (req.outHeight + req.tileSize - 1) / req.tileSize;
    req.tiles.reserve(static_cast<size_t>(tilesX) * tilesY);
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) req.tiles.push_back({tx, ty, 0});
    }
    return apply(data, req, mode);
}

RgbImageF ProcessingPipeline::apply(const UnifiedRawData& data, const RenderRequest& reqIn, RenderMode mode) {
    RenderRequest req = reqIn;
    if (useGpu_ && !gpu_) {
        gpu_ = std::make_unique<GpuContext>();
        gpu_->setDebugMode(static_cast<GpuContext::DebugMode>(gpuDebugMode_));
        gpu_->setSyntheticInput(gpuSynth_);
    }
    // Build or reuse RAW mips for requested LOD
    ensureRawMips(data, req.lod);
    const RawImage& fullRaw = (req.lod >= 0 && req.lod < static_cast<int>(rawMips_.size())) ? rawMips_[req.lod] : data.raw;
    if (req.outWidth == 0 || req.outHeight == 0) {
        req.outWidth = static_cast<int>(fullRaw.width);
        req.outHeight = static_cast<int>(fullRaw.height);
    }
    if (req.tileSize <= 0) req.tileSize = 256;
    if (req.tiles.empty()) {
        const int tilesX = (req.outWidth + req.tileSize - 1) / req.tileSize;
        const int tilesY = (req.outHeight + req.tileSize - 1) / req.tileSize;
        req.tiles.reserve(static_cast<size_t>(tilesX) * tilesY);
        for (int ty = 0; ty < tilesY; ++ty)
            for (int tx = 0; tx < tilesX; ++tx) req.tiles.push_back({tx, ty, req.lod});
    }

    // Determine PRE_DEMOSAIC max radius to compute apron
    size_t preRadius = 0;
    for (const auto& step : data.history) {
        auto inst = pm_.getInstance(step.instanceId);
        if (!inst) continue;
        if (inst->getProcessingStage() == ProcessingStage::PRE_DEMOSAIC) {
            preRadius = std::max(preRadius, inst->kernelRadiusPx());
        }
    }
    // Scale radius for LOD (approximate): radius at LOD = max(0, floor(radius / 2^lod))
    int scaledRadius = static_cast<int>(preRadius);
    for (int i = 0; i < req.lod && scaledRadius > 0; ++i) scaledRadius >>= 1;
    const int apron = scaledRadius;

    // Prepare output image
    RgbImageF rgb;
    rgb.width = fullRaw.width;
    rgb.height = fullRaw.height;
    rgb.data.resize(static_cast<size_t>(rgb.width) * rgb.height * 3u, 0.0f);

    // Normalization params (grayscale)
    float blackN = data.meta.black_level;
    float whiteN = data.meta.white_level;
    if (!(whiteN > blackN + 1.0f)) {
        uint16_t minv = 0xFFFF, maxv = 0;
        for (auto v : fullRaw.data) { if (v < minv) minv = v; if (v > maxv) maxv = v; }
        blackN = static_cast<float>(minv);
        whiteN = static_cast<float>(maxv);
    }
    const float denom = (whiteN > blackN + 1.0f) ? (whiteN - blackN) : 1.0f;
    const float invNorm = 1.0f / denom;

    // Process tiles in parallel with simple caching
    const auto hashes = computeHashes(data, mode, req.tileSize, req.lod);
    const size_t pipelineHash = combineHashes(hashes);
    std::vector<std::future<void>> futs;
    futs.reserve(req.tiles.size());
    for (const auto& tc : req.tiles) {
        futs.push_back(pool_.enqueue([&, tc]{
            // Compute inner tile rect
            const int x0 = tc.x * req.tileSize;
            const int y0 = tc.y * req.tileSize;
            const int tw = std::min(req.tileSize, req.outWidth - x0);
            const int th = std::min(req.tileSize, req.outHeight - y0);
            if (tw <= 0 || th <= 0) return;

            // Cache key
            size_t key = hashCombine(pipelineHash, static_cast<size_t>((tc.lod << 28) ^ (tc.y << 14) ^ tc.x));
            // Check cache
            if (auto cached = cacheLookup(key, tw, th)) {
                // Blit cached tile to output
                const auto& buf = *cached;
                for (int yy = 0; yy < th; ++yy) {
                    for (int xx = 0; xx < tw; ++xx) {
                        size_t si = (static_cast<size_t>(yy) * tw + xx) * 3u;
                        size_t di = (static_cast<size_t>(y0 + yy) * rgb.width + (x0 + xx)) * 3u;
                        rgb.data[di + 0] = buf[si + 0];
                        rgb.data[di + 1] = buf[si + 1];
                        rgb.data[di + 2] = buf[si + 2];
                    }
                }
                return;
            }

            // Compute source rect with apron, clamped to image bounds
            const int sx0 = std::max(0, x0 - apron);
            const int sy0 = std::max(0, y0 - apron);
            const int sx1 = std::min<int>(req.outWidth, x0 + tw + apron);
            const int sy1 = std::min<int>(req.outHeight, y0 + th + apron);
            const int sw = sx1 - sx0;
            const int sh = sy1 - sy0;

            // Extract raw tile with apron (from selected LOD)
            RawImage tileRaw;
            tileRaw.width = sw;
            tileRaw.height = sh;
            tileRaw.data.resize(static_cast<size_t>(sw) * sh);
            for (int y = 0; y < sh; ++y) {
                const uint16_t* src = &fullRaw.data[(sy0 + y) * fullRaw.width + sx0];
                uint16_t* dst = &tileRaw.data[y * sw];
                std::copy(src, src + sw, dst);
            }

            // Apply PRE_DEMOSAIC plugins to tileRaw (with apron)
            for (const auto& step : data.history) {
                auto inst = pm_.getInstance(step.instanceId);
                if (!inst) continue;
                if (inst->getProcessingStage() == ProcessingStage::PRE_DEMOSAIC) {
                    inst->process_raw(tileRaw);
                }
            }

            // GPU/CPU processing of inner region
            auto cpuProcess = [&](){
                // grayscale into rgb main buffer
                for (int yy = 0; yy < th; ++yy) {
                    const int srcY = yy + (y0 - sy0);
                    const int dstY = y0 + yy;
                    for (int xx = 0; xx < tw; ++xx) {
                        const int srcX = xx + (x0 - sx0);
                        const uint16_t v = tileRaw.data[srcY * sw + srcX];
                        float g = (static_cast<float>(v) - blackN) * invNorm;
                        if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
                        const size_t di = (static_cast<size_t>(dstY) * rgb.width + (x0 + xx)) * 3u;
                        rgb.data[di + 0] = g; rgb.data[di + 1] = g; rgb.data[di + 2] = g;
                    }
                }
                // build tileRgb view
                RgbImageF tileRgb; tileRgb.width = tw; tileRgb.height = th; tileRgb.data.resize(static_cast<size_t>(tw) * th * 3u);
                for (int yy = 0; yy < th; ++yy) {
                    for (int xx = 0; xx < tw; ++xx) {
                        size_t si = (static_cast<size_t>(y0 + yy) * rgb.width + (x0 + xx)) * 3u;
                        size_t di = (static_cast<size_t>(yy) * tw + xx) * 3u;
                        tileRgb.data[di + 0] = rgb.data[si + 0];
                        tileRgb.data[di + 1] = rgb.data[si + 1];
                        tileRgb.data[di + 2] = rgb.data[si + 2];
                    }
                }
                for (const auto& step : data.history) {
                    auto inst = pm_.getInstance(step.instanceId);
                    if (!inst) continue;
                    if (inst->getProcessingStage() == ProcessingStage::FINALIZE) inst->process_rgb(tileRgb);
                }
                // write back
                for (int yy = 0; yy < th; ++yy) {
                    for (int xx = 0; xx < tw; ++xx) {
                        size_t si = (static_cast<size_t>(yy) * tw + xx) * 3u;
                        size_t di = (static_cast<size_t>(y0 + yy) * rgb.width + (x0 + xx)) * 3u;
                        rgb.data[di + 0] = tileRgb.data[si + 0];
                        rgb.data[di + 1] = tileRgb.data[si + 1];
                        rgb.data[di + 2] = tileRgb.data[si + 2];
                    }
                }
                // copy buffer for caching
                auto sp = std::make_shared<std::vector<float>>(static_cast<size_t>(tw) * th * 3u);
                for (int yy = 0; yy < th; ++yy) {
                    for (int xx = 0; xx < tw; ++xx) {
                        size_t si = (static_cast<size_t>(y0 + yy) * rgb.width + (x0 + xx)) * 3u;
                        size_t di = (static_cast<size_t>(yy) * tw + xx) * 3u;
                        (*sp)[di+0] = rgb.data[si+0];
                        (*sp)[di+1] = rgb.data[si+1];
                        (*sp)[di+2] = rgb.data[si+2];
                    }
                }
                return sp;
            };

            std::shared_ptr<std::vector<float>> buf;
            bool gpuDone = false;
            if (useGpu_ && gpu_ && gpu_->isAvailable()) {
                gpuDone = gpu_->processGrayAndGamma(tileRaw, x0, y0, tw, th, sx0, sy0, sw, sh, blackN, invNorm, rgb, 2.2f);
            }
            if (!gpuDone) {
                buf = cpuProcess();
            } else {
                // Build cache buffer from rgb (inner region)
                buf = std::make_shared<std::vector<float>>(static_cast<size_t>(tw) * th * 3u);
                for (int yy = 0; yy < th; ++yy) {
                    for (int xx = 0; xx < tw; ++xx) {
                        size_t si = (static_cast<size_t>(y0 + yy) * rgb.width + (x0 + xx)) * 3u;
                        size_t di = (static_cast<size_t>(yy) * tw + xx) * 3u;
                        (*buf)[di+0] = rgb.data[si+0];
                        (*buf)[di+1] = rgb.data[si+1];
                        (*buf)[di+2] = rgb.data[si+2];
                    }
                }
            }
            cacheInsert(key, tw, th, buf);
        }));
    }

    for (auto& f : futs) f.get();

    return rgb;
}

void ProcessingPipeline::clearCache() {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    tileCache_.clear();
    lru_.clear();
    cacheBytes_ = 0;
}

size_t ProcessingPipeline::hashCombine(size_t a, size_t b) {
    // 64-bit mix
    a ^= b + 0x9e3779b97f4a7c15ULL + (a<<6) + (a>>2);
    return a;
}

size_t ProcessingPipeline::computePipelineHash(const UnifiedRawData& data, RenderMode mode, int tileSize, int lod) {
    // Deprecated: kept for ABI compatibility; use computeHashes+combineHashes
    auto ph = computeHashes(data, mode, tileSize, lod);
    return combineHashes(ph);
}

void ProcessingPipeline::ensureRawMips(const UnifiedRawData& data, int lodNeeded) {
    if (lodNeeded <= 0) { rawMips_.clear(); return; }
    // Rebuild if base image changed
    if (rawMips_.empty() || mipsBaseW_ != data.raw.width || mipsBaseH_ != data.raw.height) {
        rawMips_.clear();
        rawMips_.push_back(data.raw);
        mipsBaseW_ = data.raw.width;
        mipsBaseH_ = data.raw.height;
    }
    while (static_cast<int>(rawMips_.size()) <= lodNeeded) {
        rawMips_.push_back(downsample2x(rawMips_.back()));
        if (rawMips_.back().width <= 1 || rawMips_.back().height <= 1) break;
    }
}

void ProcessingPipeline::setGpuDebugMode(int mode) {
    gpuDebugMode_ = mode;
    if (gpu_) gpu_->setDebugMode(static_cast<GpuContext::DebugMode>(gpuDebugMode_));
}

void ProcessingPipeline::setGpuSynthetic(bool on) {
    gpuSynth_ = on;
    if (gpu_) gpu_->setSyntheticInput(gpuSynth_);
}

RawImage ProcessingPipeline::downsample2x(const RawImage& in) {
    RawImage out;
    const uint32_t w = in.width;
    const uint32_t h = in.height;
    const uint32_t ow = std::max<uint32_t>(1, w / 2);
    const uint32_t oh = std::max<uint32_t>(1, h / 2);
    out.width = ow; out.height = oh;
    out.data.resize(static_cast<size_t>(ow) * oh);
    for (uint32_t y = 0; y < oh; ++y) {
        uint32_t sy = y * 2;
        for (uint32_t x = 0; x < ow; ++x) {
            uint32_t sx = x * 2;
            // average 2x2, clamp edges if odd
            uint64_t sum = 0; int cnt = 0;
            for (uint32_t dy = 0; dy < 2 && (sy+dy) < h; ++dy) {
                for (uint32_t dx = 0; dx < 2 && (sx+dx) < w; ++dx) {
                    sum += in.data[(sy+dy)*w + (sx+dx)]; cnt++;
                }
            }
            out.data[y*ow + x] = static_cast<uint16_t>(sum / cnt);
        }
    }
    return out;
}

ProcessingPipeline::PipelineHashes ProcessingPipeline::computeHashes(const UnifiedRawData& data, RenderMode mode, int tileSize, int lod) {
    std::hash<int> Hi; std::hash<float> Hf; std::hash<std::string_view> Hsv; std::hash<size_t> Hs;
    PipelineHashes ph;
    // sourceHash: input dimensions + black/white + wb (acts as source characteristics for preview)
    ph.source = 0;
    ph.source = hashCombine(ph.source, Hi(static_cast<int>(data.raw.width)));
    ph.source = hashCombine(ph.source, Hi(static_cast<int>(data.raw.height)));
    ph.source = hashCombine(ph.source, Hf(data.meta.black_level));
    ph.source = hashCombine(ph.source, Hf(data.meta.white_level));
    ph.source = hashCombine(ph.source, Hf(data.meta.wb[0]));
    ph.source = hashCombine(ph.source, Hf(data.meta.wb[1]));
    ph.source = hashCombine(ph.source, Hf(data.meta.wb[2]));

    // paramsHash: sequence of plugin identities + their stateHash
    ph.params = 0;
    for (const auto& step : data.history) {
        auto inst = pm_.getInstance(step.instanceId);
        if (!inst) continue;
        ph.params = hashCombine(ph.params, Hsv(inst->getName()));
        ph.params = hashCombine(ph.params, Hi(static_cast<int>(inst->getProcessingStage())));
        ph.params = hashCombine(ph.params, Hs(inst->stateHash()));
    }

    // geomHash: tiling, lod, rendermode
    ph.geom = 0;
    ph.geom = hashCombine(ph.geom, Hi(tileSize));
    ph.geom = hashCombine(ph.geom, Hi(lod));
    ph.geom = hashCombine(ph.geom, Hi(static_cast<int>(mode)));

    return ph;
}

std::shared_ptr<std::vector<float>> ProcessingPipeline::cacheLookup(size_t key, int w, int h) {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    auto it = tileCache_.find(key);
    if (it == tileCache_.end()) return {};
    auto& e = it->second;
    if (!(e.tile.w == w && e.tile.h == h && e.tile.data && e.tile.data->size() == static_cast<size_t>(w) * h * 3u)) return {};
    // Move to front in LRU
    lru_.erase(e.lruIt);
    lru_.push_front(key);
    e.lruIt = lru_.begin();
    return e.tile.data;
}

void ProcessingPipeline::cacheInsert(size_t key, int w, int h, std::shared_ptr<std::vector<float>> data) {
    const size_t bytes = static_cast<size_t>(w) * h * 3u * sizeof(float);
    std::lock_guard<std::mutex> lk(cacheMutex_);
    // If exists, update and adjust bytes
    auto it = tileCache_.find(key);
    if (it != tileCache_.end()) {
        cacheBytes_ -= it->second.bytes;
        lru_.erase(it->second.lruIt);
        tileCache_.erase(it);
    }
    lru_.push_front(key);
    CacheEntry e;
    e.tile.w = w; e.tile.h = h; e.tile.data = std::move(data);
    e.bytes = bytes;
    e.lruIt = lru_.begin();
    tileCache_[key] = std::move(e);
    cacheBytes_ += bytes;
    cacheEvictIfNeeded();
}

void ProcessingPipeline::cacheEvictIfNeeded() {
    while (cacheBytes_ > cacheCapacityBytes_ && !lru_.empty()) {
        size_t victimKey = lru_.back();
        lru_.pop_back();
        auto it = tileCache_.find(victimKey);
        if (it != tileCache_.end()) {
            cacheBytes_ -= it->second.bytes;
            tileCache_.erase(it);
        }
    }
}

} // namespace rawproc
