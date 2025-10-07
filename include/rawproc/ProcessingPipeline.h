#pragma once
#include <vector>

#include "rawproc/IProcessingPlugin.h"
#include "rawproc/PluginManager.h"
#include "rawproc/UnifiedRawData.h"
#include "rawproc/Tiling.h"
#include "rawproc/ThreadPool.h"
#include "rawproc/GpuContext.h"
#include <list>
#include <mutex>

namespace rawproc {

enum class RenderMode { GrayscalePreview, FullColor };

class ProcessingPipeline {
public:
    explicit ProcessingPipeline(PluginManager& pm) : pm_(pm) {}

    // Applies the pipeline and returns a simple RGB image for preview/export.
    RgbImageF apply(const UnifiedRawData& data, RenderMode mode = RenderMode::GrayscalePreview);
    RgbImageF apply(const UnifiedRawData& data, const RenderRequest& request, RenderMode mode = RenderMode::GrayscalePreview);

    // Clear any internal tile caches (call when parameters/history/source change significantly).
    void clearCache();
    void setCacheCapacityMB(size_t mb) { setCacheCapacityBytes(mb * 1024ull * 1024ull); }

    // Toggle GPU path (if available); currently falls back to CPU when unavailable.
    void setUseGpu(bool on) { useGpu_ = on; }
    void setGpuDebugMode(int mode);
    void setGpuSynthetic(bool on);

private:
    PluginManager& pm_;
    // Simple thread pool for parallel tile processing
    ThreadPool pool_;

    // Very simple tile cache keyed by a combined hash.
    struct CachedTile {
        int w = 0, h = 0;
        std::shared_ptr<std::vector<float>> data; // interleaved RGB
    };
    struct CacheEntry {
        CachedTile tile;
        size_t bytes = 0;
        std::list<size_t>::iterator lruIt; // points into lru_ list
    };
    std::unordered_map<size_t, CacheEntry> tileCache_;
    std::list<size_t> lru_; // most recently used at front
    size_t cacheCapacityBytes_ = 128ull * 1024ull * 1024ull; // 128MB default
    size_t cacheBytes_ = 0;
    std::mutex cacheMutex_;

    // Simple RAW mip cache for LOD (grayscale preview only)
    std::vector<RawImage> rawMips_;
    uint32_t mipsBaseW_ = 0, mipsBaseH_ = 0;

    size_t computePipelineHash(const UnifiedRawData& data, RenderMode mode, int tileSize, int lod);
    static size_t hashCombine(size_t a, size_t b);
    void ensureRawMips(const UnifiedRawData& data, int lodNeeded);
    static RawImage downsample2x(const RawImage& in);

    // cache helpers
    std::shared_ptr<std::vector<float>> cacheLookup(size_t key, int w, int h);
    void cacheInsert(size_t key, int w, int h, std::shared_ptr<std::vector<float>> data);
    void cacheEvictIfNeeded();
    void setCacheCapacityBytes(size_t bytes) { std::lock_guard<std::mutex> lk(cacheMutex_); cacheCapacityBytes_ = bytes; cacheEvictIfNeeded(); }

    struct PipelineHashes { size_t source=0, params=0, geom=0; };
    PipelineHashes computeHashes(const UnifiedRawData& data, RenderMode mode, int tileSize, int lod);
    static size_t combineHashes(const PipelineHashes& h) {
        return hashCombine(hashCombine(h.source, h.params), h.geom);
    }

    // GPU (stub/fallback for now)
    std::unique_ptr<GpuContext> gpu_;
    bool useGpu_ = false;
    int gpuDebugMode_ = 0; // 0=real,1=coords,2=raw
    bool gpuSynth_ = false;
};

} // namespace rawproc
