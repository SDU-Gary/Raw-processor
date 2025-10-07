#include "rawproc/GpuContext.h"

#if defined(RAWPROC_USE_WGPU_NATIVE)

#include <wgpu.h>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>

namespace rawproc {

namespace {

static const char* kWGSLGrayGamma = R"(// WGSL: grayscale normalize + gamma on inner tile (flat params)
// params layout (f32 array):
// [0]=sw, [1]=sh, [2]=xoff, [3]=yoff, [4]=tw, [5]=th, [6]=black, [7]=invNorm, [8]=gamma
@group(0) @binding(0) var<storage, read> rawBuf: array<u32>;
@group(0) @binding(1) var<storage, read> params: array<f32>;
@group(0) @binding(2) var<storage, read_write> outBuf: array<f32>;

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let sw: u32 = u32(params[0]);
  let sh: u32 = u32(params[1]);
  let xoff: u32 = u32(params[2]);
  let yoff: u32 = u32(params[3]);
  let tw: u32 = u32(params[4]);
  let th: u32 = u32(params[5]);
  let black: f32 = params[6];
  let invNorm: f32 = params[7];
  let gamma: f32 = params[8];
  let mode: u32 = u32(params[9]);

  if (gid.x >= tw || gid.y >= th) { return; }
  // Debug mode 1: visualize coordinates
  if (mode == 1u) {
    let r = f32(gid.x) / max(1.0, f32(tw - 1u));
    let g = f32(gid.y) / max(1.0, f32(th - 1u));
    let oidx: u32 = (gid.y * tw + gid.x) * 3u;
    outBuf[oidx+0u] = r;
    outBuf[oidx+1u] = g;
    outBuf[oidx+2u] = 0.0;
    return;
  }
  let sx: u32 = gid.x + xoff;
  let sy: u32 = gid.y + yoff;
  if (sx >= sw || sy >= sh) { return; }
  let idx: u32 = sy * sw + sx;
  // RAW 16-bit stored in u32 array (lower 16 bits used)
  let rv: u32 = rawBuf[idx] & 0xFFFFu;
  var g: f32;
  if (mode == 2u) {
    // Debug mode 2: visualize raw value directly
    g = f32(rv) / 65535.0;
  } else {
    g = (f32(rv) - black) * invNorm;
    g = clamp(g, 0.0, 1.0);
    g = pow(g, 1.0 / gamma);
  }
  let oidx: u32 = (gid.y * tw + gid.x) * 3u;
  outBuf[oidx + 0u] = g;
  outBuf[oidx + 1u] = g;
  outBuf[oidx + 2u] = g;
}
)";

struct CallbackData {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
};

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
    auto* d = reinterpret_cast<CallbackData*>(userdata);
    if (status == WGPURequestAdapterStatus_Success) {
        d->adapter = adapter;
    } else {
        std::cerr << "wgpu: adapter request failed: " << (message ? message : "") << "\n";
    }
    std::lock_guard<std::mutex> lk(d->m);
    d->done = true;
    d->cv.notify_all();
}

static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
    auto* d = reinterpret_cast<CallbackData*>(userdata);
    if (status == WGPURequestDeviceStatus_Success) {
        d->device = device;
    } else {
        std::cerr << "wgpu: device request failed: " << (message ? message : "") << "\n";
    }
    std::lock_guard<std::mutex> lk(d->m);
    d->done = true;
    d->cv.notify_all();
}

} // namespace

class WgpuHolder {
public:
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUShaderModule shader = nullptr;
    WGPUComputePipeline pipeline = nullptr;
    WGPUBindGroupLayout bgl = nullptr;

    ~WgpuHolder() {
        if (pipeline) wgpuComputePipelineRelease(pipeline);
        if (bgl) wgpuBindGroupLayoutRelease(bgl);
        if (shader) wgpuShaderModuleRelease(shader);
        if (queue) wgpuQueueRelease(queue);
        if (device) wgpuDeviceRelease(device);
        if (adapter) wgpuAdapterRelease(adapter);
        if (instance) wgpuInstanceRelease(instance);
    }
};

static WGPUShaderModule createWGSL(WGPUDevice dev, const char* src) {
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = src;
    wgsl.code.length = WGPU_STRLEN;
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl.chain;
    desc.label = WGPU_STRING_VIEW_INIT;
    return wgpuDeviceCreateShaderModule(dev, &desc);
}

struct GpuContext::Impl {
    std::unique_ptr<WgpuHolder> w;
};

GpuContext::GpuContext() {
    impl_ = std::make_unique<Impl>();
    // Create instance
    auto holder = std::make_unique<WgpuHolder>();
    WGPUInstanceDescriptor id{};
    holder->instance = wgpuCreateInstance(&id);
    if (!holder->instance) { available_ = false; return; }

    // Request adapter
    CallbackData cbA;
    WGPURequestAdapterOptions opts{};
    // opts.compatibleSurface = nullptr;
    WGPURequestAdapterCallbackInfo cai{};
    cai.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView /*message*/, void* userdata1, void* /*userdata2*/){ onAdapterRequestEnded(status, adapter, nullptr, userdata1); };
    cai.userdata1 = &cbA;
    (void)wgpuInstanceRequestAdapter(holder->instance, &opts, cai);
    {
        std::unique_lock<std::mutex> lk(cbA.m);
        cbA.cv.wait(lk, [&]{return cbA.done;});
    }
    if (!cbA.adapter) { available_ = false; return; }
    holder->adapter = cbA.adapter;

    // Request device
    CallbackData cbD;
    WGPUDeviceDescriptor dd{};
    WGPURequestDeviceCallbackInfo cdi{};
    cdi.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView /*message*/, void* userdata1, void* /*userdata2*/){ onDeviceRequestEnded(status, device, nullptr, userdata1); };
    cdi.userdata1 = &cbD;
    (void)wgpuAdapterRequestDevice(holder->adapter, &dd, cdi);
    {
        std::unique_lock<std::mutex> lk(cbD.m);
        cbD.cv.wait(lk, [&]{return cbD.done;});
    }
    if (!cbD.device) { available_ = false; return; }
    holder->device = cbD.device;
    holder->queue = wgpuDeviceGetQueue(holder->device);

    // Create shader module
    holder->shader = createWGSL(holder->device, kWGSLGrayGamma);
    if (!holder->shader) { available_ = false; return; }

    // Bind group layout: binding0 storage read, binding1 uniform, binding2 storage rw
    WGPUBindGroupLayoutEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[0].buffer.minBindingSize = 0;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[1].buffer.minBindingSize = 0;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].buffer.type = WGPUBufferBindingType_Storage;
    entries[2].buffer.minBindingSize = 0;

    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 3;
    bglDesc.entries = entries;
    holder->bgl = wgpuDeviceCreateBindGroupLayout(holder->device, &bglDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pld{};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &holder->bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(holder->device, &pld);

    // Compute pipeline
    WGPUProgrammableStageDescriptor cs{};
    cs.module = holder->shader;
    cs.entryPoint = {"main", WGPU_STRLEN};
    WGPUComputePipelineDescriptor cpd{};
    cpd.compute = cs;
    cpd.layout = pl;
    holder->pipeline = wgpuDeviceCreateComputePipeline(holder->device, &cpd);
    wgpuPipelineLayoutRelease(pl);

    available_ = holder->pipeline != nullptr;
    if (available_) {
        impl_->w = std::move(holder);
    }
}

GpuContext::~GpuContext() {}

bool GpuContext::isAvailable() const { return available_; }

bool GpuContext::processGrayAndGamma(const RawImage& tileRaw,
                                     int x0, int y0, int tw, int th,
                                     int sx0, int sy0, int sw, int sh,
                                     float blackN, float invNorm, RgbImageF& outRgb,
                                     float gamma) {
    if (!available_ || !impl_ || !impl_->w) return false;
    auto& w = *impl_->w;

    // Prepare buffers
    const size_t inCount = static_cast<size_t>(sw) * sh;
    const size_t inBytes = inCount * sizeof(uint32_t); // store 16-bit in lower 16 bits
    const size_t outCount = static_cast<size_t>(tw) * th * 3u;
    const size_t outBytes = outCount * sizeof(float);

    WGPUBufferDescriptor bd{};
    bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    bd.size = inBytes;
    WGPUBuffer inBuf = wgpuDeviceCreateBuffer(w.device, &bd);

    // Storage output buffer (GPU write), then copy to a MAP_READ buffer for CPU readback
    bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    bd.size = outBytes;
    WGPUBuffer outStorage = wgpuDeviceCreateBuffer(w.device, &bd);

    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    bd.size = outBytes;
    WGPUBuffer readBuf = wgpuDeviceCreateBuffer(w.device, &bd);

    // Flattened params buffer (add debug mode at index 9)
    std::vector<float> fparams(10);
    fparams[0] = static_cast<float>(sw);
    fparams[1] = static_cast<float>(sh);
    fparams[2] = static_cast<float>(x0 - sx0);
    fparams[3] = static_cast<float>(y0 - sy0);
    fparams[4] = static_cast<float>(tw);
    fparams[5] = static_cast<float>(th);
    fparams[6] = blackN;
    fparams[7] = invNorm;
    fparams[8] = gamma;
    fparams[9] = static_cast<float>(static_cast<uint32_t>(debugMode_));
    bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    bd.size = fparams.size() * sizeof(float);
    WGPUBuffer uBuf = wgpuDeviceCreateBuffer(w.device, &bd);

    // Upload data (optionally synthesize a test pattern)
    std::vector<uint32_t> inData(inCount);
    static int dbgPrints = 0;
    if (synthInput_) {
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                uint16_t v = static_cast<uint16_t>((static_cast<float>(x) / std::max(1, sw - 1)) * 65535.0f);
                inData[static_cast<size_t>(y) * sw + x] = static_cast<uint32_t>(v);
            }
        }
    } else {
        // Copy u16 -> u32 explicitly
        uint16_t tmin = 0xFFFF, tmax = 0;
        for (size_t i = 0; i < inCount; ++i) {
            uint16_t v = tileRaw.data[i];
            if (v < tmin) tmin = v; if (v > tmax) tmax = v;
            inData[i] = static_cast<uint32_t>(v);
        }
        if (dbgPrints < 4) {
            std::cerr << "GPU DBG tile sw=" << sw << " sh=" << sh << " min=" << tmin << " max=" << tmax
                      << " x0=" << x0 << " y0=" << y0 << " tw=" << tw << " th=" << th
                      << " xoff=" << (x0 - sx0) << " yoff=" << (y0 - sy0) << "\n";
            dbgPrints++;
        }
    }
    wgpuQueueWriteBuffer(w.queue, inBuf, 0, inData.data(), inBytes);
    wgpuQueueWriteBuffer(w.queue, uBuf, 0, fparams.data(), fparams.size() * sizeof(float));

    // Bind group
    WGPUBindGroupEntry bge[3]{};
    bge[0].binding = 0; bge[0].buffer = inBuf; bge[0].offset = 0; bge[0].size = inBytes;
    bge[1].binding = 1; bge[1].buffer = uBuf; bge[1].offset = 0; bge[1].size = fparams.size() * sizeof(float);
    bge[2].binding = 2; bge[2].buffer = outStorage; bge[2].offset = 0; bge[2].size = outBytes;
    WGPUBindGroupDescriptor bgd{}; bgd.layout = w.bgl; bgd.entryCount = 3; bgd.entries = bge;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(w.device, &bgd);

    // Encode compute pass
    WGPUCommandEncoderDescriptor ced{};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(w.device, &ced);
    WGPUComputePassDescriptor cpd{};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(enc, &cpd);
    wgpuComputePassEncoderSetPipeline(pass, w.pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    const uint32_t gx = (tw + 15u) / 16u;
    const uint32_t gy = (th + 15u) / 16u;
    wgpuComputePassEncoderDispatchWorkgroups(pass, gx, gy, 1);
    wgpuComputePassEncoderEnd(pass);
    // Copy storage to readback buffer
    wgpuCommandEncoderCopyBufferToBuffer(enc, outStorage, 0, readBuf, 0, outBytes);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuCommandEncoderRelease(enc);

    wgpuQueueSubmit(w.queue, 1, &cb);
    wgpuCommandBufferRelease(cb);

    // Map read out buffer
    struct MapData { std::mutex m; std::condition_variable cv; bool done=false; } md;
    WGPUBufferMapCallbackInfo mci{};
    mci.callback = [](WGPUMapAsyncStatus /*status*/, WGPUStringView /*message*/, void* userdata1, void* /*userdata2*/){ auto* m = reinterpret_cast<MapData*>(userdata1); std::lock_guard<std::mutex> lk(m->m); m->done = true; m->cv.notify_all(); };
    mci.userdata1 = &md;
    (void)wgpuBufferMapAsync(readBuf, WGPUMapMode_Read, 0, outBytes, mci);
    wgpuDevicePoll(w.device, true, nullptr);
    {
        std::unique_lock<std::mutex> lk(md.m);
        md.cv.wait(lk, [&]{return md.done;});
    }
    const void* mapped = wgpuBufferGetConstMappedRange(readBuf, 0, outBytes);
    if (!mapped) {
        wgpuBufferUnmap(readBuf);
        wgpuBindGroupRelease(bg);
        wgpuBufferRelease(inBuf); wgpuBufferRelease(outStorage); wgpuBufferRelease(readBuf); wgpuBufferRelease(uBuf);
        return false;
    }
    // Copy to outRgb
    const float* fsrc = reinterpret_cast<const float*>(mapped);
    for (int yy = 0; yy < th; ++yy) {
        for (int xx = 0; xx < tw; ++xx) {
            size_t si = (static_cast<size_t>(yy) * tw + xx) * 3u;
            size_t di = (static_cast<size_t>(y0 + yy) * outRgb.width + (x0 + xx)) * 3u;
            outRgb.data[di+0] = fsrc[si+0];
            outRgb.data[di+1] = fsrc[si+1];
            outRgb.data[di+2] = fsrc[si+2];
        }
    }
    wgpuBufferUnmap(readBuf);

    wgpuBindGroupRelease(bg);
    wgpuBufferRelease(inBuf); wgpuBufferRelease(outStorage); wgpuBufferRelease(readBuf); wgpuBufferRelease(uBuf);
    return true;
}

} // namespace rawproc

#endif // RAWPROC_USE_WGPU_NATIVE
