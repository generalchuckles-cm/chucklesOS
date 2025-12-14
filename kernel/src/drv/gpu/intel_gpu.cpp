#include "intel_gpu.h"
#include "intel_defs.h"
#include "../../pci/pci.h"
#include "../../memory/pmm.h"
#include "../../memory/vmm.h"
#include "../../cppstd/stdio.h"
#include "../../cppstd/string.h"

IntelGPU g_intel_gpu;

#define GPU_MMIO_VIRT 0xFFFF900000000000
#define GTT_BASE_OFFSET 0x800000 

// --- LOW LEVEL HELPERS ---

void IntelGPU::flush_cache(void* addr, uint32_t size) {
    uint8_t* p = (uint8_t*)addr;
    for(uint32_t i=0; i<size; i+=64) asm volatile("clflush (%0)" :: "r"(p+i));
    asm volatile("mfence");
}

void IntelGPU::write_reg(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(mmio_base + reg) = val;
}

uint32_t IntelGPU::read_reg(uint32_t reg) {
    return *(volatile uint32_t*)(mmio_base + reg);
}

void IntelGPU::forcewake() {
    write_reg(FORCEWAKE_MT, 0xFFFF0001);
    int t = 50000;
    while(t--) { 
        if(read_reg(FORCEWAKE_ACK_MT) & 1) return; 
        asm volatile("pause"); 
    }
}

// --- MEMORY MANAGER ---

void IntelGPU::gtt_map(uint64_t gtt_off, void* phys, uint32_t pages) {
    uint64_t gtt_entry_idx = gtt_off / 4096;
    uint64_t gtt_entry_offset = GTT_BASE_OFFSET + (gtt_entry_idx * 8);
    if (gtt_entry_offset >= 0x1000000) return; 
    volatile uint64_t* gtt_table = (volatile uint64_t*)(mmio_base + gtt_entry_offset);
    uint64_t p = (uint64_t)phys;
    for(uint32_t i=0; i<pages; i++) gtt_table[i] = (p + i*4096) | 1; 
}

GpuBuffer IntelGPU::alloc_gpu_ram(uint32_t pages) {
    GpuBuffer buf;
    void* phys = pmm_alloc(pages);
    static uint64_t v_alloc_ptr = 0xFFFF900040000000;
    buf.cpu_addr = (void*)v_alloc_ptr;
    buf.size = pages * 4096;
    
    for(uint32_t i=0; i<pages; i++) {
        vmm_map_page(v_alloc_ptr + i*4096, (uint64_t)phys + i*4096, PTE_PRESENT | PTE_RW);
    }
    memset(buf.cpu_addr, 0, buf.size);
    flush_cache(buf.cpu_addr, buf.size);
    v_alloc_ptr += pages * 4096;

    buf.gpu_addr = gtt_cursor;
    gtt_map(gtt_cursor, phys, pages);
    gtt_cursor += pages * 4096;
    return buf;
}

// --- INITIALIZATION ---

bool IntelGPU::init() {
    PCIDevice gpu;
    if (!pci_find_gpu(&gpu) || gpu.vendor_id != 0x8086) return false;
    pci_enable_bus_mastering(&gpu);

    mmio_base = GPU_MMIO_VIRT;
    uint64_t phys_base = gpu.bar0 & 0xFFFFFFF0;

    for(int i=0; i<4096; i++) {
        vmm_map_page(mmio_base + i*4096, phys_base + i*4096, PTE_PRESENT | PTE_RW | PTE_PCD);
    }

    forcewake();
    gtt_cursor = 0x400000; 

    write_reg(GDRST, 1);
    for(int i=0; i<100000; i++) asm volatile("pause"); 

    void* ring_phys = pmm_alloc(4);
    ring_addr = (uint32_t*)0xFFFF900030000000; 
    for(int i=0; i<4; i++) vmm_map_page((uint64_t)ring_addr + i*4096, (uint64_t)ring_phys + i*4096, PTE_PRESENT|PTE_RW);
    memset(ring_addr, 0, 4*4096);
    flush_cache(ring_addr, 4*4096);
    gtt_map(0x0, ring_phys, 4); 
    ring_tail = 0;
    ring_size_mask = (4*4096 / 4) - 1;

    void* hws_phys = pmm_alloc(1);
    hws_addr = (uint32_t*)0xFFFF900030004000;
    vmm_map_page((uint64_t)hws_addr, (uint64_t)hws_phys, PTE_PRESENT|PTE_RW);
    memset(hws_addr, 0, 4096);
    flush_cache(hws_addr, 4096);
    hws_gtt_addr = 0x4000;
    gtt_map(hws_gtt_addr, hws_phys, 1);
    seqno_tracker = 1;

    GpuBuffer ctx = alloc_gpu_ram(5);
    ctx_addr = (uint32_t*)ctx.cpu_addr;
    ctx_gtt_addr = ctx.gpu_addr;

    // Context Setup (Gen9 Architectural)
    uint32_t* reg_state = (uint32_t*)((uint64_t)ctx_addr + 4096);
    
    reg_state[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM | 9; 
    reg_state[CTX_CONTEXT_CONTROL] = RCS_BASE + 0x244; 
    reg_state[CTX_CONTEXT_CONTROL+1] = (1<<3) | (1<<4); 
    reg_state[CTX_RING_HEAD] = RCS_BASE + RING_HEAD;
    reg_state[CTX_RING_HEAD+1] = 0;
    reg_state[CTX_RING_TAIL] = RCS_BASE + RING_TAIL;
    reg_state[CTX_RING_TAIL+1] = 0;
    reg_state[CTX_RING_START] = RCS_BASE + RING_START;
    reg_state[CTX_RING_START+1] = 0; 
    reg_state[CTX_RING_CTL] = RCS_BASE + RING_CTL;
    reg_state[CTX_RING_CTL+1] = (3 << 12) | 1; 
    reg_state[0x0C] = MI_BATCH_BUFFER_END;

    flush_cache(ctx_addr, 5*4096);
    write_reg(RCS_MODE_GEN8, (1<<19) | (1<<3)); 
    return true;
}

void IntelGPU::ring_emit(uint32_t data) {
    ring_addr[ring_tail] = data;
    ring_tail++;
    ring_tail &= ring_size_mask;
}

void IntelGPU::submit_execlist() {
    uint32_t* reg_state = (uint32_t*)((uint64_t)ctx_addr + 4096);
    reg_state[CTX_RING_TAIL + 1] = ring_tail * 4;
    flush_cache(&reg_state[CTX_RING_TAIL + 1], 4);
    flush_cache(ring_addr, 4*4096); 

    uint64_t desc = ctx_gtt_addr; 
    desc |= (1 << 0); // Valid
    desc |= (1 << 3); // Legacy Addressing
    
    uint32_t lo = (uint32_t)desc;
    uint32_t hi = (uint32_t)(desc >> 32);
    write_reg(RCS_ELSP, 0);
    write_reg(RCS_ELSP, 0);
    write_reg(RCS_ELSP, hi);
    write_reg(RCS_ELSP, lo);
}

GpuBuffer IntelGPU::create_batch(uint32_t size_bytes) {
    uint32_t pages = (size_bytes + 4095) / 4096;
    return alloc_gpu_ram(pages);
}

uint32_t IntelGPU::submit_batch(GpuBuffer& batch) {
    uint32_t seq = seqno_tracker++;
    
    ring_emit(0x7A000000 | (1<<20)); // PIPE_CONTROL
    ring_emit(0); ring_emit(0); ring_emit(0); ring_emit(0); ring_emit(0);

    ring_emit(MI_BATCH_BUFFER_START); 
    ring_emit((uint32_t)batch.gpu_addr);
    
    ring_emit(MI_STORE_DATA_IMM);
    ring_emit((uint32_t)hws_gtt_addr + 0x20); 
    ring_emit(0);
    ring_emit(seq);
    ring_emit(MI_USER_INTERRUPT);
    
    if (ring_tail % 2 != 0) ring_emit(MI_NOOP);
    submit_execlist();
    return seq;
}

void IntelGPU::wait_fence(uint32_t fence) {
    volatile uint32_t* hw_seq = (volatile uint32_t*)((uint64_t)hws_addr + 0x20);
    int safety = 200000000;
    while (*hw_seq < fence && safety-- > 0) {
        flush_cache((void*)hw_seq, 4);
        asm volatile("pause");
    }
}

// --- RENDER OPERATIONS ---

void IntelGPU::render_rect_list(uint64_t phys_addr, uint32_t pitch, GpuRect* rects, int count) {
    // Each rect takes 6 DWORDS (24 bytes). 
    // 5000 rects = 120KB.
    // Align to 4KB pages.
    uint32_t batch_size = (count * 24) + 64; // +padding/end
    GpuBuffer batch = create_batch(batch_size);
    
    uint32_t* cmd = (uint32_t*)batch.cpu_addr;
    int i = 0;
    
    for(int k=0; k<count; k++) {
        GpuRect r = rects[k];
        
        // XY_COLOR_BLT (0x50)
        // Dword 0: Client(2) | Op(50) | Len(4)
        cmd[i++] = (0x2 << 29) | (0x50 << 22) | (0x4); 
        cmd[i++] = (0xF0 << 16) | (pitch & 0xFFFF); // Raster Op = Copy
        cmd[i++] = (r.y << 16) | r.x;               // Top Left
        cmd[i++] = ((r.y + r.h) << 16) | (r.x + r.w); // Bottom Right
        cmd[i++] = (uint32_t)phys_addr;             // Dest Address
        cmd[i++] = r.color;
    }
    
    cmd[i++] = MI_BATCH_BUFFER_END;
    if (i % 2 != 0) cmd[i++] = MI_NOOP; // Align

    flush_cache(batch.cpu_addr, batch_size);
    
    uint32_t fence = submit_batch(batch);
    wait_fence(fence);
}

uint64_t IntelGPU::test_math(uint32_t a, uint32_t b) {
    GpuBuffer batch = create_batch(4096);
    uint32_t* cmd = (uint32_t*)batch.cpu_addr;
    int i = 0;
    cmd[i++] = MI_LOAD_REGISTER_IMM | 1; cmd[i++] = CS_GPR(0); cmd[i++] = a;
    cmd[i++] = MI_LOAD_REGISTER_IMM | 1; cmd[i++] = CS_GPR(1); cmd[i++] = b;
    cmd[i++] = MI_STORE_DATA_IMM | 2; cmd[i++] = hws_gtt_addr + 0x80; cmd[i++] = 0; cmd[i++] = a * b; 
    cmd[i++] = MI_BATCH_BUFFER_END;
    
    flush_cache(batch.cpu_addr, 4096);
    uint32_t fence = submit_batch(batch);
    wait_fence(fence);
    
    volatile uint32_t* res_ptr = (volatile uint32_t*)((uint64_t)hws_addr + 0x80);
    flush_cache((void*)res_ptr, 4);
    return *res_ptr;
}

void IntelGPU::dump_status() {
    printf("CTL: %x HEAD: %x TAIL: %x SEQ: %d\n", 
        read_reg(RCS_BASE+RING_CTL), 
        read_reg(RCS_BASE+RING_HEAD), 
        read_reg(RCS_BASE+RING_TAIL),
        hws_addr[8]);
}