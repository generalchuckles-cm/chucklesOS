#ifndef INTEL_DEFS_H
#define INTEL_DEFS_H

#include <cstdint>

#define INTEL_GTT_OFFSET      0x800000

// --- Power Management ---
#define FORCEWAKE_MT          0xA188
#define FORCEWAKE_ACK_MT      0xA18C
#define GDRST                 0x9400

// --- Engine Base ---
#define RCS_BASE              0x2000

// --- Register Offsets (Relative to Ring Base) ---
#define RING_CTL              0x03C
#define RING_HEAD             0x034
#define RING_TAIL             0x030
#define RING_START            0x038
#define RING_ACTHD            0x074
#define RING_INSTDONE         0x06C 
#define RING_HWS_PGA          0x080 

// --- Absolute Registers ---
#define RCS_MODE_GEN8         0x229C
#define RCS_ELSP              0x2230 
#define RCS_ACTHD             0x2074 
#define RCS_INSTDONE          0x206C
#define RCS_EXECLIST_STATUS   0x2234 

// --- Gen9 Context Image Offsets (in DWORDS) ---
// These match the layout expected by the LRI instruction in the context
#define CTX_LRI_HEADER_0      0x01
#define CTX_CONTEXT_CONTROL   0x02
#define CTX_RING_HEAD         0x04
#define CTX_RING_TAIL         0x06
#define CTX_RING_START        0x08
#define CTX_RING_CTL          0x0A
#define CTX_PDP0_UDW          0x10
#define CTX_PDP0_LDW          0x12
#define CTX_PDP1_UDW          0x14
#define CTX_PDP1_LDW          0x16
#define CTX_PDP2_UDW          0x18
#define CTX_PDP2_LDW          0x1A
#define CTX_PDP3_UDW          0x1C
#define CTX_PDP3_LDW          0x1E

// --- Opcodes ---
#define MI_NOOP               0x00000000
#define MI_USER_INTERRUPT     (0x02 << 23)
#define MI_BATCH_BUFFER_END   (0x0A << 23)
#define MI_BATCH_BUFFER_START ((0x31 << 23) | (1 << 8) | (1 << 0)) 
#define MI_LOAD_REGISTER_IMM  (0x22 << 23)
#define MI_STORE_DATA_IMM     ((0x20 << 23) | (1 << 22)) 

#define CS_GPR(n)             (0x600 + (n * 8))

#endif