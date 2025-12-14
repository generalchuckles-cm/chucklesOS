#ifndef INTEL_GPU_H
#define INTEL_GPU_H

#include <cstdint>

struct GpuBuffer {
    void*    cpu_addr;    
    uint64_t gpu_addr;    
    uint64_t size;
};

struct GpuRect {
    int x, y;
    int w, h;
    uint32_t color;
};

class IntelGPU {
public:
    bool init();
    
    // --- Advanced Batch API ---
    // Allocates a batch buffer large enough for 'count' commands
    GpuBuffer create_batch(uint32_t size_bytes);
    
    // Submits a raw batch
    uint32_t  submit_batch(GpuBuffer& batch);
    
    // Waits for the GPU to finish a specific batch (Seqno)
    void      wait_fence(uint32_t fence);

    // --- Rendering Operations ---
    // Fills a list of rectangles using Hardware Acceleration (Blitter)
    // This replaces the CPU drawing loop.
    void      render_rect_list(uint64_t dst_phys, uint32_t dst_pitch, GpuRect* rects, int count);

    uint64_t  test_math(uint32_t a, uint32_t b);
    void      dump_status();

private:
    uint64_t mmio_base;
    
    uint64_t gtt_cursor; 
    void gtt_map(uint64_t gtt_addr, void* phys_addr, uint32_t pages);
    GpuBuffer alloc_gpu_ram(uint32_t pages);

    uint32_t* ring_addr;
    uint32_t  ring_tail;
    uint32_t  ring_size_mask;
    
    uint32_t* hws_addr; 
    uint64_t  hws_gtt_addr;
    uint32_t  seqno_tracker; 

    uint32_t* ctx_addr;
    uint64_t  ctx_gtt_addr;

    void      write_reg(uint32_t reg, uint32_t val);
    uint32_t  read_reg(uint32_t reg);
    void      forcewake();
    void      flush_cache(void* addr, uint32_t size);
    
    void      ring_emit(uint32_t data);
    void      submit_execlist();
};

extern IntelGPU g_intel_gpu;

#endif