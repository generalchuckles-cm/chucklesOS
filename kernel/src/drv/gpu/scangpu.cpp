#include "scangpu.h"
#include "../../pci/pci.h"
#include "../../memory/vmm.h"
#include "../../cppstd/stdio.h"

// We will map the GPU MMIO to this arbitrary high virtual address for testing
#define GPU_MMIO_VIRT_BASE 0xFFFF900000000000

// Helper to read a register at an offset
static uint32_t gpu_read_reg(uint64_t base_virt, uint32_t offset) {
    volatile uint32_t* reg = (volatile uint32_t*)(base_virt + offset);
    return *reg;
}

void gpu_scan_intel() {
    printf("GPU: Scanning for Intel Graphics...\n");

    PCIDevice gpu;
    if (!pci_find_gpu(&gpu)) {
        printf("GPU: No Graphics Controller found.\n");
        return;
    }

    if (gpu.vendor_id != 0x8086) {
        printf("GPU: Found non-Intel GPU (Vendor 0x%x). Skipping Intel scan.\n", gpu.vendor_id);
        return;
    }

    printf("GPU: Intel Device 0x%x found.\n", gpu.device_id);
    
    // Get the physical address from BAR0
    uint64_t bar0_phys = gpu.bar0 & 0xFFFFFFF0; 
    printf("GPU: MMIO Physical Base: 0x%x\n", bar0_phys);

    // FIX: Map the first 16KB (4 pages) to cover offsets 0x0000 to 0x3FFF.
    // This ensures 0x2054 and 0x20D8 are valid.
    for (int i = 0; i < 4; i++) {
        uint64_t offset = i * 0x1000;
        vmm_map_page(GPU_MMIO_VIRT_BASE + offset, bar0_phys + offset, PTE_PRESENT | PTE_RW | PTE_PCD);
    }
    
    printf("GPU: Mapped 16KB to Virtual 0x%lx\n", GPU_MMIO_VIRT_BASE);

    // --- READ INTEL REGISTERS ---
    
    // 0x0000: GT_CORE_STATUS / ID
    uint32_t reg0 = gpu_read_reg(GPU_MMIO_VIRT_BASE, 0x0000);
    printf("  [0x0000] Head: 0x%x\n", reg0);

    // 0x2054: GEN6_PCODE_DATA (Power Control)
    // This caused the crash before because page 2 wasn't mapped.
    uint32_t pcode = gpu_read_reg(GPU_MMIO_VIRT_BASE, 0x2054);
    printf("  [0x2054] PCODE: 0x%x\n", pcode);

    // 0x20D8: Render Engine Mode / Control
    uint32_t render_ctrl = gpu_read_reg(GPU_MMIO_VIRT_BASE, 0x20D8); 
    printf("  [0x20D8] GPIO/Misc: 0x%x\n", render_ctrl);

    // 0x42000: Display Hotplug Detect (PCH)
    // This is far away, so we manually map its specific page.
    printf("GPU: Mapping Display Controller Page (Offset 0x42000)...\n");
    
    // Map physical address (BAR0 + 0x42000) to (Virtual Base + 0x10000)
    // We use a safe offset (0x10000 = 64KB) to avoid overlapping the first 16KB
    uint64_t display_virt_offset = 0x10000;
    vmm_map_page(GPU_MMIO_VIRT_BASE + display_virt_offset, bar0_phys + 0x42000, PTE_PRESENT | PTE_RW | PTE_PCD);
    
    uint32_t hotplug = gpu_read_reg(GPU_MMIO_VIRT_BASE + display_virt_offset, 0x00); 
    printf("  [0x42000] Hotplug Ctrl: 0x%x\n", hotplug);

    // 0x60000: Pipe A Status
    // Map physical (BAR0 + 0x60000) to (Virtual Base + 0x20000)
    uint64_t pipe_virt_offset = 0x20000;
    vmm_map_page(GPU_MMIO_VIRT_BASE + pipe_virt_offset, bar0_phys + 0x60000, PTE_PRESENT | PTE_RW | PTE_PCD);
    
    uint32_t pipe_a = gpu_read_reg(GPU_MMIO_VIRT_BASE + pipe_virt_offset, 0x00); 
    printf("  [0x60000] Pipe A Conf: 0x%x\n", pipe_a);

    printf("GPU: Scan Complete.\n");
}