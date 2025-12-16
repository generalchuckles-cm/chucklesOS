#include "nes.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../cppstd/stdlib.h"
#include "../input.h"
#include "../timer.h"
#include "../globals.h"
#include "../game_data.h"

// --- Configuration ---
#define NES_WIDTH  256
#define NES_HEIGHT 240
#define PRG_ROM_PAGE_SIZE 16384
#define CHR_ROM_PAGE_SIZE 8192

// --- 2C02 Palette (RGB) ---
static uint32_t nes_palette[64] = {
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400,
    0x503000, 0x007800, 0x006800, 0x005800, 0x004058, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
    0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0x000000, 0x000000, 0x000000,
    0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898, 0xF87858, 0xFCA044,
    0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA4E4FC, 0xB8B8F8, 0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8,
    0xF8D878, 0xD8F878, 0xB8F8B8, 0xD8F878, 0xB8F8B8, 0xB8F8B8, 0x000000, 0x000000
};

// Flags
#define C_FLAG 0x01
#define Z_FLAG 0x02
#define I_FLAG 0x04
#define D_FLAG 0x08
#define B_FLAG 0x10
#define U_FLAG 0x20
#define V_FLAG 0x40
#define N_FLAG 0x80

static void set_flag(CPU6502& cpu, uint8_t flag, bool v) { if (v) cpu.p |= flag; else cpu.p &= ~flag; }
static bool get_flag(CPU6502& cpu, uint8_t flag) { return (cpu.p & flag) != 0; }
static void update_nz(CPU6502& cpu, uint8_t val) { set_flag(cpu, Z_FLAG, val == 0); set_flag(cpu, N_FLAG, val & 0x80); }

static uint32_t get_nes_color(uint8_t palette_idx) {
    return nes_palette[palette_idx & 0x3F];
}

// --- ACTUAL CLASS IMPLEMENTATION ---

NESApp::NESApp(const char* filename) {
    if (filename) strcpy(rom_file, filename);
    else rom_file[0] = 0;
    
    // Clear State
    memset(&cpu, 0, sizeof(CPU6502));
    memset(&ppu, 0, sizeof(PPU2C02));
    memset(&cart, 0, sizeof(Cartridge));
    memset(&m4, 0, sizeof(Mapper4State));
    
    controller_state = 0;
    controller_latch = 0;
    controller_strobe = false;
    
    cart.loaded = false; 
    next_frame_ticks = 0;
    ticks_per_frame = 0;
}

NESApp::~NESApp() {
    // Robust cleanup
    if (cart.prg_rom) { free(cart.prg_rom); cart.prg_rom = nullptr; }
    if (cart.chr_rom) { free(cart.chr_rom); cart.chr_rom = nullptr; }
    if (cart.prg_ram) { free(cart.prg_ram); cart.prg_ram = nullptr; }
    if (ppu.frame_buffer) { free(ppu.frame_buffer); ppu.frame_buffer = nullptr; }
}

void NESApp::on_init(Window* win) {
    my_window = win;
    
    ppu.frame_buffer = (uint32_t*)malloc(NES_WIDTH * NES_HEIGHT * 4);
    if (ppu.frame_buffer) {
        for(int i=0; i<NES_WIDTH*NES_HEIGHT; i++) ppu.frame_buffer[i] = 0x202020;
    }
    
    for(int i=0; i<32; i++) ppu.palette_ram[i] = 0x00; 
    
    init_emulation();
    
    win->renderer->clear(0x000000);

    uint64_t freq = get_cpu_frequency();
    if (freq == 0) freq = 2000000000; 
    ticks_per_frame = freq / 60;
    next_frame_ticks = rdtsc_serialized();
}

void NESApp::on_input(char c) { (void)c; }

void NESApp::init_emulation() {
    uint8_t* file_buf = NULL;

    printf("NES: Initializing...\n");

    if (rom_file[0] != '\0') {
        printf("NES: Loading from %s\n", rom_file);
        uint32_t max_size = 512 * 1024; 
        file_buf = (uint8_t*)malloc(max_size);
        if (!Fat32::getInstance().read_file(rom_file, file_buf, max_size)) {
            printf("NES: Load failed. Falling back to embedded.\n");
            if (file_buf) { free(file_buf); file_buf = NULL; }
        }
    } 
    
    if (!file_buf) {
        printf("NES: Using embedded ROM (%d bytes)...\n", _mnt_smb_nes_len);
        file_buf = (uint8_t*)malloc(_mnt_smb_nes_len);
        if(file_buf) memcpy(file_buf, _mnt_smb_nes, _mnt_smb_nes_len);
    }
    
    if (!file_buf || memcmp(file_buf, "NES\x1A", 4) != 0) { 
        printf("NES: Invalid ROM header.\n");
        if (file_buf) free(file_buf); 
        return; 
    }
    
    cart.prg_banks = file_buf[4]; cart.chr_banks = file_buf[5];
    cart.mapper = (file_buf[6] >> 4) | (file_buf[7] & 0xF0);
    cart.mirroring = (file_buf[6] & 1); 
    
    printf("NES: Mapper %d, PRG %d, CHR %d\n", cart.mapper, cart.prg_banks, cart.chr_banks);

    cart.prg_rom = (uint8_t*)malloc(cart.prg_banks * PRG_ROM_PAGE_SIZE);
    int chr_size = (cart.chr_banks > 0) ? (cart.chr_banks * 8192) : 8192;
    cart.chr_rom = (uint8_t*)malloc(chr_size); 
    cart.prg_ram = (uint8_t*)malloc(8192);
    memset(cart.prg_ram, 0, 8192);

    uint8_t* ptr = file_buf + 16;
    if (file_buf[6] & 4) ptr += 512; 

    memcpy(cart.prg_rom, ptr, cart.prg_banks * PRG_ROM_PAGE_SIZE);
    ptr += cart.prg_banks * PRG_ROM_PAGE_SIZE;
    
    if (cart.chr_banks > 0) memcpy(cart.chr_rom, ptr, chr_size);
    else memset(cart.chr_rom, 0, chr_size); 

    free(file_buf);
    
    memset(&cpu, 0, sizeof(CPU6502)); 
    ppu.ctrl = 0; ppu.mask = 0; ppu.status = 0; 
    ppu.oam_addr = 0; ppu.fine_x = 0; ppu.v = 0; ppu.t = 0; 
    ppu.data_buf = 0; ppu.w = 0; ppu.scanline = 0; ppu.cycle = 0;
    
    if (cart.mapper == 4) {
        memset(&m4, 0, sizeof(Mapper4State));
        m4.prg_mode = 0;
        m4.chr_mode = 0;
        mapper4_update_offsets();
    }

    cpu.pc = cpu_read(0xFFFC) | (cpu_read(0xFFFD) << 8);
    cpu.p = 0x24; cpu.s = 0xFD;
    cart.loaded = true;
}

void NESApp::mapper4_irq_clock() {
    if (m4.irq_counter == 0 || m4.irq_reload) {
        m4.irq_counter = m4.irq_latch;
        m4.irq_reload = false;
    } else {
        m4.irq_counter--;
    }
    if (m4.irq_counter == 0 && m4.irq_enabled) {
        cpu.irq_pending = true;
    }
}

void NESApp::mapper4_update_offsets() {
    int prg_len = cart.prg_banks * 16384; 
    int last_8k = prg_len - 8192;
    int second_last_8k = prg_len - 16384;

    int r6_bank = m4.regs[6] * 8192;
    int r7_bank = m4.regs[7] * 8192;
    
    if (prg_len > 0) {
        r6_bank %= prg_len; 
        r7_bank %= prg_len;
        if (second_last_8k < 0) second_last_8k = 0;
        if (last_8k < 0) last_8k = 0;
    }

    if (m4.prg_mode == 0) {
        m4.prg_offsets[0] = r6_bank;
        m4.prg_offsets[1] = r7_bank;
        m4.prg_offsets[2] = second_last_8k;
        m4.prg_offsets[3] = last_8k;
    } else {
        m4.prg_offsets[0] = second_last_8k;
        m4.prg_offsets[1] = r7_bank;
        m4.prg_offsets[2] = r6_bank;
        m4.prg_offsets[3] = last_8k;
    }

    int chr_len = cart.chr_banks * 8192;
    if (chr_len == 0) return; 

    int r0 = (m4.regs[0] & 0xFE) * 1024;
    int r1 = (m4.regs[1] & 0xFE) * 1024;
    int r2 = m4.regs[2] * 1024;
    int r3 = m4.regs[3] * 1024;
    int r4 = m4.regs[4] * 1024;
    int r5 = m4.regs[5] * 1024;

    r0 %= chr_len; r1 %= chr_len; r2 %= chr_len;
    r3 %= chr_len; r4 %= chr_len; r5 %= chr_len;

    if (m4.chr_mode == 0) {
        m4.chr_offsets[0] = r0; m4.chr_offsets[1] = r0 + 1024;
        m4.chr_offsets[2] = r1; m4.chr_offsets[3] = r1 + 1024;
        m4.chr_offsets[4] = r2; m4.chr_offsets[5] = r3;
        m4.chr_offsets[6] = r4; m4.chr_offsets[7] = r5;
    } else {
        m4.chr_offsets[0] = r2; m4.chr_offsets[1] = r3;
        m4.chr_offsets[2] = r4; m4.chr_offsets[3] = r5;
        m4.chr_offsets[4] = r0; m4.chr_offsets[5] = r0 + 1024;
        m4.chr_offsets[6] = r1; m4.chr_offsets[7] = r1 + 1024;
    }
}

uint8_t NESApp::get_chr_byte(uint16_t addr) {
    if (cart.mapper == 4 && cart.chr_banks > 0) {
        int bank = addr / 1024;
        int off = addr % 1024;
        return cart.chr_rom[m4.chr_offsets[bank] + off];
    }
    return cart.chr_rom[addr & 0x1FFF]; 
}

uint8_t NESApp::cpu_read(uint16_t addr) {
    if (addr < 0x2000) return cpu.ram[addr & 0x07FF];
    else if (addr < 0x4000) {
        uint16_t reg = 0x2000 + (addr & 0x0007);
        if (reg == 0x2002) {
            uint8_t data = ppu.status;
            ppu.status &= ~0x80; 
            ppu.w = false;
            return data;
        }
        if (reg == 0x2004) return ppu.oam[ppu.oam_addr];
        if (reg == 0x2007) {
            uint8_t data = ppu.data_buf;
            ppu.data_buf = ppu_read(ppu.v);
            if (ppu.v >= 0x3F00) data = ppu.data_buf;
            ppu.v += (ppu.ctrl & 0x04) ? 32 : 1;
            return data;
        }
        return 0;
    }
    else if (addr == 0x4016) {
        uint8_t data = (controller_latch & 1) ? 1 : 0;
        controller_latch >>= 1;
        if (controller_strobe) controller_latch = controller_state; 
        return data | 0x40; 
    }
    else if (addr >= 0x6000 && addr < 0x8000) {
        return cart.prg_ram[addr - 0x6000];
    }
    else if (addr >= 0x8000) {
        if (cart.mapper == 4) {
            int bank_idx = (addr - 0x8000) / 0x2000;
            int offset = (addr - 0x8000) % 0x2000;
            return cart.prg_rom[m4.prg_offsets[bank_idx] + offset];
        } else {
            return cart.prg_rom[(cart.prg_banks == 1) ? (addr & 0x3FFF) : (addr & 0x7FFF)];
        }
    }
    return 0;
}

void NESApp::cpu_write(uint16_t addr, uint8_t data) {
    if (addr < 0x2000) cpu.ram[addr & 0x07FF] = data;
    else if (addr < 0x4000) {
        uint16_t reg = 0x2000 + (addr & 0x0007);
        switch(reg) {
            case 0x2000: 
                ppu.ctrl = data;
                ppu.t = (ppu.t & 0xF3FF) | ((data & 0x03) << 10);
                if ((ppu.ctrl & 0x80) && (ppu.status & 0x80)) cpu.nmi_pending = true;
                break;
            case 0x2001: ppu.mask = data; break;
            case 0x2003: ppu.oam_addr = data; break;
            case 0x2004: ppu.oam[ppu.oam_addr++] = data; break;
            case 0x2005: 
                if (!ppu.w) {
                    ppu.t = (ppu.t & 0xFFE0) | (data >> 3);
                    ppu.fine_x = data & 0x07;
                } else {
                    ppu.t = (ppu.t & 0x8FFF) | ((data & 0x07) << 12);
                    ppu.t = (ppu.t & 0xFC1F) | ((data & 0xF8) << 2);
                }
                ppu.w = !ppu.w;
                break;
            case 0x2006:
                if (!ppu.w) {
                    ppu.t = (ppu.t & 0x80FF) | ((data & 0x3F) << 8);
                } else {
                    ppu.t = (ppu.t & 0xFF00) | data;
                    ppu.v = ppu.t; 
                }
                ppu.w = !ppu.w;
                break;
            case 0x2007:
                ppu_write(ppu.v, data);
                ppu.v += (ppu.ctrl & 0x04) ? 32 : 1;
                break;
        }
    }
    else if (addr == 0x4014) { 
        uint16_t base = (uint16_t)data << 8;
        for (int i=0; i<256; i++) ppu.oam[(ppu.oam_addr + i) & 0xFF] = cpu_read(base + i);
        cpu.cycles += 513; 
        if (cpu.cycles % 2 == 1) cpu.cycles++; 
    }
    else if (addr == 0x4016) {
        if (data & 1) { controller_strobe = true; controller_latch = controller_state; }
        else controller_strobe = false;
    }
    else if (addr >= 0x6000 && addr < 0x8000) {
        cart.prg_ram[addr - 0x6000] = data;
    }
    else if (addr >= 0x8000 && cart.mapper == 4) {
        if (addr <= 0x9FFF) {
            if ((addr & 1) == 0) {
                m4.bank_select = data & 7;
                m4.prg_mode = (data >> 6) & 1;
                m4.chr_mode = (data >> 7) & 1;
                mapper4_update_offsets();
            } else {
                m4.regs[m4.bank_select] = data;
                mapper4_update_offsets();
            }
        } else if (addr <= 0xBFFF) {
            if ((addr & 1) == 0) {
                if (cart.mirroring != 4) cart.mirroring = (data & 1); 
            }
        } else if (addr <= 0xDFFF) {
            if ((addr & 1) == 0) m4.irq_latch = data; 
            else m4.irq_reload = true;                
        } else if (addr <= 0xFFFF) {
             if ((addr & 1) == 0) {
                m4.irq_enabled = false;
                cpu.irq_pending = false; 
            }
            else m4.irq_enabled = true;
        }
    }
}

uint8_t NESApp::ppu_read(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return get_chr_byte(addr);
    }
    else if (addr < 0x3F00) {
        uint16_t off = addr & 0x0FFF;
        int nt = (cart.mirroring == 1) ? ((off & 0x400) ? 1 : 0) : ((off & 0x800) ? 1 : 0);
        return ppu.name_tables[nt][off & 0x3FF]; 
    }
    else if (addr >= 0x3F00) {
        addr &= 0x1F;
        if (addr >= 0x10 && (addr & 3) == 0) addr -= 0x10;
        return ppu.palette_ram[addr];
    }
    return 0;
}

void NESApp::ppu_write(uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;
    if (addr < 0x2000) { 
        if(cart.chr_banks == 0) cart.chr_rom[addr] = data; 
    }
    else if (addr < 0x3F00) {
         uint16_t off = addr & 0x0FFF;
         int nt = (cart.mirroring == 1) ? ((off & 0x400) ? 1 : 0) : ((off & 0x800) ? 1 : 0);
         ppu.name_tables[nt][off & 0x3FF] = data; 
    }
    else if (addr >= 0x3F00) {
        addr &= 0x1F;
        if (addr >= 0x10 && (addr & 3) == 0) addr -= 0x10;
        ppu.palette_ram[addr] = data;
    }
}

void NESApp::cpu_step() {
    if (cpu.nmi_pending) {
        cpu.nmi_pending = false;
        cpu.ram[0x0100 + cpu.s--] = (cpu.pc >> 8) & 0xFF;
        cpu.ram[0x0100 + cpu.s--] = cpu.pc & 0xFF;
        cpu.ram[0x0100 + cpu.s--] = cpu.p;
        cpu.p |= I_FLAG;
        cpu.pc = cpu_read(0xFFFA) | (cpu_read(0xFFFB) << 8);
        cpu.cycles += 7;
        return;
    }
    if (cpu.irq_pending && !get_flag(cpu, I_FLAG)) {
        cpu.irq_pending = false;
        cpu.ram[0x0100 + cpu.s--] = (cpu.pc >> 8) & 0xFF;
        cpu.ram[0x0100 + cpu.s--] = cpu.pc & 0xFF;
        cpu.ram[0x0100 + cpu.s--] = cpu.p;
        cpu.p |= I_FLAG;
        cpu.pc = cpu_read(0xFFFE) | (cpu_read(0xFFFF) << 8);
        cpu.cycles += 7;
        return;
    }

    uint8_t op = cpu_read(cpu.pc++);
    
    // Lambdas capturing this (via &) to access member functions
    auto IMM = [&]() { return cpu.pc++; };
    auto ZP  = [&]() { return (uint16_t)cpu_read(cpu.pc++); };
    auto ZPX = [&]() { return (uint16_t)((cpu_read(cpu.pc++) + cpu.x) & 0xFF); };
    auto ZPY = [&]() { return (uint16_t)((cpu_read(cpu.pc++) + cpu.y) & 0xFF); };
    auto ABS = [&]() { uint16_t l=cpu_read(cpu.pc++); return (uint16_t)((cpu_read(cpu.pc++)<<8)|l); };
    auto ABX = [&]() { uint16_t l=cpu_read(cpu.pc++); uint16_t h=cpu_read(cpu.pc++); return (uint16_t)(((h<<8)|l)+cpu.x); };
    auto ABY = [&]() { uint16_t l=cpu_read(cpu.pc++); uint16_t h=cpu_read(cpu.pc++); return (uint16_t)(((h<<8)|l)+cpu.y); };
    auto IND = [&]() { 
        uint16_t l=cpu_read(cpu.pc++); uint16_t h=cpu_read(cpu.pc++); uint16_t ptr=(h<<8)|l;
        if ((ptr&0xFF)==0xFF) return (uint16_t)((cpu_read(ptr&0xFF00)<<8)|cpu_read(ptr));
        else return (uint16_t)((cpu_read(ptr+1)<<8)|cpu_read(ptr));
    };
    auto IX = [&]() { uint8_t t=cpu_read(cpu.pc++); return (uint16_t)(cpu_read((t+cpu.x)&0xFF)|(cpu_read((t+cpu.x+1)&0xFF)<<8)); };
    auto IY = [&]() { uint8_t t=cpu_read(cpu.pc++); return (uint16_t)((cpu_read(t)|(cpu_read((t+1)&0xFF)<<8))+cpu.y); };
    auto REL = [&]() { int8_t o=(int8_t)cpu_read(cpu.pc++); return (uint16_t)(cpu.pc+o); };
    
    auto PUSH = [&](uint8_t v) { cpu.ram[0x0100 + cpu.s--] = v; };
    auto POP = [&]() { return cpu.ram[0x0100 + ++cpu.s]; };
    
    uint16_t addr = 0;

    // Same opcode switch table, using 'cpu' member access
    switch(op) {
        case 0xA9: cpu.a = cpu_read(IMM()); update_nz(cpu, cpu.a); cpu.cycles+=2; break; 
        case 0xA5: cpu.a = cpu_read(ZP());  update_nz(cpu, cpu.a); cpu.cycles+=3; break;
        case 0xB5: cpu.a = cpu_read(ZPX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0xAD: cpu.a = cpu_read(ABS()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0xBD: cpu.a = cpu_read(ABX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0xB9: cpu.a = cpu_read(ABY()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0xA1: cpu.a = cpu_read(IX());  update_nz(cpu, cpu.a); cpu.cycles+=6; break;
        case 0xB1: cpu.a = cpu_read(IY());  update_nz(cpu, cpu.a); cpu.cycles+=5; break;

        case 0xA2: cpu.x = cpu_read(IMM()); update_nz(cpu, cpu.x); cpu.cycles+=2; break; 
        case 0xA6: cpu.x = cpu_read(ZP());  update_nz(cpu, cpu.x); cpu.cycles+=3; break;
        case 0xB6: cpu.x = cpu_read(ZPY()); update_nz(cpu, cpu.x); cpu.cycles+=4; break;
        case 0xAE: cpu.x = cpu_read(ABS()); update_nz(cpu, cpu.x); cpu.cycles+=4; break;
        case 0xBE: cpu.x = cpu_read(ABY()); update_nz(cpu, cpu.x); cpu.cycles+=4; break;

        case 0xA0: cpu.y = cpu_read(IMM()); update_nz(cpu, cpu.y); cpu.cycles+=2; break; 
        case 0xA4: cpu.y = cpu_read(ZP());  update_nz(cpu, cpu.y); cpu.cycles+=3; break;
        case 0xB4: cpu.y = cpu_read(ZPX()); update_nz(cpu, cpu.y); cpu.cycles+=4; break;
        case 0xAC: cpu.y = cpu_read(ABS()); update_nz(cpu, cpu.y); cpu.cycles+=4; break;
        case 0xBC: cpu.y = cpu_read(ABX()); update_nz(cpu, cpu.y); cpu.cycles+=4; break;

        case 0x85: cpu_write(ZP(), cpu.a);  cpu.cycles+=3; break; 
        case 0x95: cpu_write(ZPX(), cpu.a); cpu.cycles+=4; break;
        case 0x8D: cpu_write(ABS(), cpu.a); cpu.cycles+=4; break;
        case 0x9D: cpu_write(ABX(), cpu.a); cpu.cycles+=5; break;
        case 0x99: cpu_write(ABY(), cpu.a); cpu.cycles+=5; break;
        case 0x81: cpu_write(IX(), cpu.a);  cpu.cycles+=6; break;
        case 0x91: cpu_write(IY(), cpu.a);  cpu.cycles+=6; break;

        case 0x86: cpu_write(ZP(), cpu.x);  cpu.cycles+=3; break; 
        case 0x96: cpu_write(ZPY(), cpu.x); cpu.cycles+=4; break;
        case 0x8E: cpu_write(ABS(), cpu.x); cpu.cycles+=4; break;
        case 0x84: cpu_write(ZP(), cpu.y);  cpu.cycles+=3; break; 
        case 0x94: cpu_write(ZPX(), cpu.y); cpu.cycles+=4; break;
        case 0x8C: cpu_write(ABS(), cpu.y); cpu.cycles+=4; break;

        case 0xAA: cpu.x = cpu.a; update_nz(cpu, cpu.x); cpu.cycles+=2; break;
        case 0xA8: cpu.y = cpu.a; update_nz(cpu, cpu.y); cpu.cycles+=2; break;
        case 0x8A: cpu.a = cpu.x; update_nz(cpu, cpu.a); cpu.cycles+=2; break;
        case 0x98: cpu.a = cpu.y; update_nz(cpu, cpu.a); cpu.cycles+=2; break;
        case 0x9A: cpu.s = cpu.x; cpu.cycles+=2; break;
        case 0xBA: cpu.x = cpu.s; update_nz(cpu, cpu.x); cpu.cycles+=2; break;

        case 0x48: PUSH(cpu.a); cpu.cycles+=3; break;
        case 0x08: PUSH(cpu.p | U_FLAG | B_FLAG); cpu.cycles+=3; break;
        case 0x68: cpu.a = POP(); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x28: cpu.p = POP() | U_FLAG; cpu.p &= ~B_FLAG; cpu.cycles+=4; break;

        case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71: {
            if(op==0x69) addr=IMM(); else if(op==0x65) addr=ZP(); else if(op==0x75) addr=ZPX();
            else if(op==0x6D) addr=ABS(); else if(op==0x7D) addr=ABX(); else if(op==0x79) addr=ABY();
            else if(op==0x61) addr=IX(); else addr=IY();
            uint8_t v = cpu_read(addr);
            uint16_t s = cpu.a + v + (get_flag(cpu, C_FLAG)?1:0);
            set_flag(cpu, C_FLAG, s > 0xFF);
            set_flag(cpu, V_FLAG, (~((uint16_t)cpu.a ^ (uint16_t)v) & ((uint16_t)cpu.a ^ (uint16_t)s)) & 0x0080);
            cpu.a = (uint8_t)s; update_nz(cpu, cpu.a); cpu.cycles+=2;
        } break;

        case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1: {
            if(op==0xE9) addr=IMM(); else if(op==0xE5) addr=ZP(); else if(op==0xF5) addr=ZPX();
            else if(op==0xED) addr=ABS(); else if(op==0xFD) addr=ABX(); else if(op==0xF9) addr=ABY();
            else if(op==0xE1) addr=IX(); else addr=IY();
            uint8_t v = cpu_read(addr) ^ 0xFF;
            uint16_t s = cpu.a + v + (get_flag(cpu, C_FLAG)?1:0);
            set_flag(cpu, C_FLAG, s > 0xFF);
            set_flag(cpu, V_FLAG, (~((uint16_t)cpu.a ^ (uint16_t)v) & ((uint16_t)cpu.a ^ (uint16_t)s)) & 0x0080);
            cpu.a = (uint8_t)s; update_nz(cpu, cpu.a); cpu.cycles+=2;
        } break;

        case 0x29: cpu.a &= cpu_read(IMM()); update_nz(cpu, cpu.a); cpu.cycles+=2; break;
        case 0x25: cpu.a &= cpu_read(ZP());  update_nz(cpu, cpu.a); cpu.cycles+=3; break;
        case 0x35: cpu.a &= cpu_read(ZPX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x2D: cpu.a &= cpu_read(ABS()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x3D: cpu.a &= cpu_read(ABX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x39: cpu.a &= cpu_read(ABY()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x21: cpu.a &= cpu_read(IX());  update_nz(cpu, cpu.a); cpu.cycles+=6; break;
        case 0x31: cpu.a &= cpu_read(IY());  update_nz(cpu, cpu.a); cpu.cycles+=5; break;

        case 0x49: cpu.a ^= cpu_read(IMM()); update_nz(cpu, cpu.a); cpu.cycles+=2; break;
        case 0x45: cpu.a ^= cpu_read(ZP());  update_nz(cpu, cpu.a); cpu.cycles+=3; break;
        case 0x55: cpu.a ^= cpu_read(ZPX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x4D: cpu.a ^= cpu_read(ABS()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x5D: cpu.a ^= cpu_read(ABX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x59: cpu.a ^= cpu_read(ABY()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x41: cpu.a ^= cpu_read(IX());  update_nz(cpu, cpu.a); cpu.cycles+=6; break;
        case 0x51: cpu.a ^= cpu_read(IY());  update_nz(cpu, cpu.a); cpu.cycles+=5; break;

        case 0x09: cpu.a |= cpu_read(IMM()); update_nz(cpu, cpu.a); cpu.cycles+=2; break;
        case 0x05: cpu.a |= cpu_read(ZP());  update_nz(cpu, cpu.a); cpu.cycles+=3; break;
        case 0x15: cpu.a |= cpu_read(ZPX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x0D: cpu.a |= cpu_read(ABS()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x1D: cpu.a |= cpu_read(ABX()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x19: cpu.a |= cpu_read(ABY()); update_nz(cpu, cpu.a); cpu.cycles+=4; break;
        case 0x01: cpu.a |= cpu_read(IX());  update_nz(cpu, cpu.a); cpu.cycles+=6; break;
        case 0x11: cpu.a |= cpu_read(IY());  update_nz(cpu, cpu.a); cpu.cycles+=5; break;

        case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1: {
            if(op==0xC9) addr=IMM(); else if(op==0xC5) addr=ZP(); else if(op==0xD5) addr=ZPX();
            else if(op==0xCD) addr=ABS(); else if(op==0xDD) addr=ABX(); else if(op==0xD9) addr=ABY();
            else if(op==0xC1) addr=IX(); else addr=IY();
            uint8_t m = cpu_read(addr); set_flag(cpu, C_FLAG, cpu.a >= m); update_nz(cpu, cpu.a - m); cpu.cycles+=2;
        } break;
        
        case 0xE0: { uint8_t m=cpu_read(IMM()); set_flag(cpu, C_FLAG,cpu.x>=m); update_nz(cpu, cpu.x-m); cpu.cycles+=2; } break;
        case 0xE4: { uint8_t m=cpu_read(ZP());  set_flag(cpu, C_FLAG,cpu.x>=m); update_nz(cpu, cpu.x-m); cpu.cycles+=3; } break;
        case 0xEC: { uint8_t m=cpu_read(ABS()); set_flag(cpu, C_FLAG,cpu.x>=m); update_nz(cpu, cpu.x-m); cpu.cycles+=4; } break;
        case 0xC0: { uint8_t m=cpu_read(IMM()); set_flag(cpu, C_FLAG,cpu.y>=m); update_nz(cpu, cpu.y-m); cpu.cycles+=2; } break;
        case 0xC4: { uint8_t m=cpu_read(ZP());  set_flag(cpu, C_FLAG,cpu.y>=m); update_nz(cpu, cpu.y-m); cpu.cycles+=3; } break;
        case 0xCC: { uint8_t m=cpu_read(ABS()); set_flag(cpu, C_FLAG,cpu.y>=m); update_nz(cpu, cpu.y-m); cpu.cycles+=4; } break;

        case 0xE6: case 0xF6: case 0xEE: case 0xFE: { 
            if(op==0xE6) addr=ZP(); else if(op==0xF6) addr=ZPX(); else if(op==0xEE) addr=ABS(); else addr=ABX();
            uint8_t v = cpu_read(addr)+1; cpu_write(addr,v); update_nz(cpu, v); cpu.cycles+=5; 
        } break;
        case 0xC6: case 0xD6: case 0xCE: case 0xDE: { 
            if(op==0xC6) addr=ZP(); else if(op==0xD6) addr=ZPX(); else if(op==0xCE) addr=ABS(); else addr=ABX();
            uint8_t v = cpu_read(addr)-1; cpu_write(addr,v); update_nz(cpu, v); cpu.cycles+=5;
        } break;
        case 0xE8: cpu.x++; update_nz(cpu, cpu.x); cpu.cycles+=2; break;
        case 0xC8: cpu.y++; update_nz(cpu, cpu.y); cpu.cycles+=2; break;
        case 0xCA: cpu.x--; update_nz(cpu, cpu.x); cpu.cycles+=2; break;
        case 0x88: cpu.y--; update_nz(cpu, cpu.y); cpu.cycles+=2; break;

        case 0x0A: { set_flag(cpu, C_FLAG, cpu.a & 0x80); cpu.a <<= 1; update_nz(cpu, cpu.a); cpu.cycles+=2; } break;
        case 0x06: case 0x16: case 0x0E: case 0x1E: { 
            if(op==0x06) addr=ZP(); else if(op==0x16) addr=ZPX(); else if(op==0x0E) addr=ABS(); else addr=ABX();
            uint8_t v=cpu_read(addr); set_flag(cpu, C_FLAG, v&0x80); v<<=1; cpu_write(addr,v); update_nz(cpu, v); cpu.cycles+=5;
        } break;
        case 0x4A: { set_flag(cpu, C_FLAG, cpu.a & 0x01); cpu.a >>= 1; update_nz(cpu, cpu.a); cpu.cycles+=2; } break;
        case 0x46: case 0x56: case 0x4E: case 0x5E: {
            if(op==0x46) addr=ZP(); else if(op==0x56) addr=ZPX(); else if(op==0x4E) addr=ABS(); else addr=ABX();
            uint8_t v=cpu_read(addr); set_flag(cpu, C_FLAG, v&0x01); v>>=1; cpu_write(addr,v); update_nz(cpu, v); cpu.cycles+=5;
        } break;
        case 0x2A: { uint8_t c=get_flag(cpu, C_FLAG); set_flag(cpu, C_FLAG,cpu.a&0x80); cpu.a=(cpu.a<<1)|c; update_nz(cpu, cpu.a); cpu.cycles+=2; } break;
        case 0x26: case 0x36: case 0x2E: case 0x3E: {
            if(op==0x26) addr=ZP(); else if(op==0x36) addr=ZPX(); else if(op==0x2E) addr=ABS(); else addr=ABX();
            uint8_t v=cpu_read(addr); uint8_t c=get_flag(cpu, C_FLAG); set_flag(cpu, C_FLAG,v&0x80); v=(v<<1)|c; cpu_write(addr,v); update_nz(cpu, v); cpu.cycles+=5;
        } break;
        case 0x6A: { uint8_t c=get_flag(cpu, C_FLAG)?0x80:0; set_flag(cpu, C_FLAG,cpu.a&0x01); cpu.a=(cpu.a>>1)|c; update_nz(cpu, cpu.a); cpu.cycles+=2; } break;
        case 0x66: case 0x76: case 0x6E: case 0x7E: {
            if(op==0x66) addr=ZP(); else if(op==0x76) addr=ZPX(); else if(op==0x6E) addr=ABS(); else addr=ABX();
            uint8_t v=cpu_read(addr); uint8_t c=get_flag(cpu, C_FLAG)?0x80:0; set_flag(cpu, C_FLAG,v&0x01); v=(v>>1)|c; cpu_write(addr,v); update_nz(cpu, v); cpu.cycles+=5;
        } break;

        case 0x24: { uint8_t m=cpu_read(ZP()); set_flag(cpu, Z_FLAG,(cpu.a&m)==0); set_flag(cpu, N_FLAG,m&0x80); set_flag(cpu, V_FLAG,m&0x40); cpu.cycles+=3; } break;
        case 0x2C: { uint8_t m=cpu_read(ABS()); set_flag(cpu, Z_FLAG,(cpu.a&m)==0); set_flag(cpu, N_FLAG,m&0x80); set_flag(cpu, V_FLAG,m&0x40); cpu.cycles+=4; } break;

        case 0x4C: cpu.pc = ABS(); cpu.cycles+=3; break;
        case 0x6C: cpu.pc = IND(); cpu.cycles+=5; break;
        case 0x20: { uint16_t t=ABS(); uint16_t r=cpu.pc-1; PUSH((r>>8)&0xFF); PUSH(r&0xFF); cpu.pc=t; cpu.cycles+=6; } break;
        case 0x60: { uint16_t l=POP(); uint16_t h=POP(); cpu.pc=((h<<8)|l)+1; cpu.cycles+=6; } break;
        case 0x40: { cpu.p=POP()|U_FLAG; cpu.p&=~B_FLAG; uint16_t l=POP(); uint16_t h=POP(); cpu.pc=(h<<8)|l; cpu.cycles+=6; } break;

        case 0x10: if(!get_flag(cpu, N_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0x30: if( get_flag(cpu, N_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0x50: if(!get_flag(cpu, V_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0x70: if( get_flag(cpu, V_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0x90: if(!get_flag(cpu, C_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0xB0: if( get_flag(cpu, C_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0xD0: if(!get_flag(cpu, Z_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;
        case 0xF0: if( get_flag(cpu, Z_FLAG)) { cpu.pc=REL(); cpu.cycles+=1; } else cpu.pc++; cpu.cycles+=2; break;

        case 0x18: cpu.p &= ~C_FLAG; cpu.cycles+=2; break;
        case 0x38: cpu.p |= C_FLAG;  cpu.cycles+=2; break;
        case 0x58: cpu.p &= ~I_FLAG; cpu.cycles+=2; break;
        case 0x78: cpu.p |= I_FLAG;  cpu.cycles+=2; break;
        case 0xB8: cpu.p &= ~V_FLAG; cpu.cycles+=2; break;
        case 0xD8: cpu.p &= ~D_FLAG; cpu.cycles+=2; break;
        case 0xF8: cpu.p |= D_FLAG;  cpu.cycles+=2; break;

        case 0x00: { cpu.pc++; PUSH((cpu.pc>>8)&0xFF); PUSH(cpu.pc&0xFF); PUSH(cpu.p|B_FLAG|U_FLAG); cpu.p|=I_FLAG; cpu.pc=cpu_read(0xFFFE)|(cpu_read(0xFFFF)<<8); cpu.cycles+=7; } break;
        case 0xEA: cpu.cycles+=2; break;
        default: cpu.cycles+=2; break;
    }
}

void NESApp::on_draw() {
    if (!ppu.frame_buffer) {
         my_window->renderer->drawString(10, 10, "FRAMEBUFFER ERROR", 0xFF0000, 2);
         return;
    }
    
    if (!cart.loaded) {
         my_window->renderer->drawString(10, 10, "INSERT CARTRIDGE", 0xFFFFFF, 2);
         return;
    }

    uint64_t now = rdtsc_serialized();
    if (now < next_frame_ticks) return; 

    if (now > next_frame_ticks + ticks_per_frame) {
        next_frame_ticks = now; 
    }
    next_frame_ticks += ticks_per_frame;

    uint8_t joy = 0;
    bool left = g_key_state[0x4B];
    bool right = g_key_state[0x4D];
    if (left && right) { left = false; right = false; } 

    if (g_key_state[0x21]) joy |= 0x01; 
    if (g_key_state[0x20]) joy |= 0x02; 
    if (g_key_state[0x36]) joy |= 0x04; 
    if (g_key_state[0x1C]) joy |= 0x08; 
    if (g_key_state[0x48]) joy |= 0x10; 
    if (g_key_state[0x50]) joy |= 0x20; 
    if (left) joy |= 0x40; 
    if (right) joy |= 0x80; 
    controller_state = joy;

    ppu.status &= ~0x80; ppu.status &= ~0x40; 
        
    int frame_cycles = 0;
    const int CYCLES_PER_FRAME = 29780;
    
    static int blink = 0;
    blink++;
    if(blink % 10 < 5) ppu.frame_buffer[0] = 0xFF0000;
    else ppu.frame_buffer[0] = 0x00FF00;
        
    while (frame_cycles < CYCLES_PER_FRAME) {
        uint64_t old_cyc = cpu.cycles;
        cpu_step();
        uint64_t delta = cpu.cycles - old_cyc;
        frame_cycles += delta;
        
        for (uint64_t i=0; i<delta*3; i++) {
            ppu.cycle++;
            if (ppu.cycle >= 341) {
                ppu.cycle = 0; ppu.scanline++;
                
                if (cart.mapper == 4 && ppu.scanline >= 0 && ppu.scanline <= 239) {
                    mapper4_irq_clock();
                }

                if (ppu.scanline == 241) {
                     ppu.status |= 0x80;
                     if (ppu.ctrl & 0x80) cpu.nmi_pending = true;
                }
                else if (ppu.scanline == 261) {
                     ppu.scanline = -1;
                     ppu.status &= ~0x80; ppu.status &= ~0x40;
                     if (ppu.mask & 0x18) {
                         ppu.v = (ppu.v & 0x041F) | (ppu.t & 0x7BE0);
                     }
                }
                
                if (ppu.scanline >= 0 && ppu.scanline < 240) {
                    if (ppu.mask & 0x18) {
                         ppu.v = (ppu.v & 0xFBE0) | (ppu.t & 0x041F);
                    }
                    else {
                        uint32_t* line_ptr = ppu.frame_buffer + (ppu.scanline * NES_WIDTH);
                         for(int x=0; x<NES_WIDTH; x++) {
                             line_ptr[x] = (rand() % 2 == 0) ? 0x202020 : 0x303030;
                         }
                    }

                    if (ppu.mask & 0x18) {
                        uint32_t* line_ptr = ppu.frame_buffer + (ppu.scanline * NES_WIDTH);
                        uint8_t bg_pixels[NES_WIDTH]; 
                        
                        uint16_t v = ppu.v;
                        uint8_t fine_x = ppu.fine_x; 
                        
                        for (int x = 0; x < NES_WIDTH; x++) {
                            int coarse_x = v & 0x1F;
                            int coarse_y = (v >> 5) & 0x1F;
                            int fine_y = (v >> 12) & 0x07;
                            int nt = (v >> 10) & 0x03;
                            
                            int physical_nt;
                            if (cart.mirroring == 0) physical_nt = (nt & 2) ? 1 : 0;
                            else physical_nt = (nt & 1) ? 1 : 0;
                            
                            uint8_t tile_id = ppu.name_tables[physical_nt][(coarse_y * 32) + coarse_x];
                            uint8_t attr_byte = ppu.name_tables[physical_nt][0x3C0 + ((coarse_y / 4) * 8) + (coarse_x / 4)];
                            int attr_shift = ((coarse_y & 2) << 1) | (coarse_x & 2);
                            uint8_t palette_high = (attr_byte >> attr_shift) & 3;
                            
                            uint16_t pattern_addr = ((ppu.ctrl & 0x10) ? 0x1000 : 0x0000) + (tile_id * 16) + fine_y;
                            
                            uint8_t p0 = get_chr_byte(pattern_addr);
                            uint8_t p1 = get_chr_byte(pattern_addr + 8);
                            
                            int bit_pos = 7 - fine_x; 
                            uint8_t pixel_value = ((p1 >> bit_pos) & 1) << 1 | ((p0 >> bit_pos) & 1);
                            
                            bg_pixels[x] = pixel_value;
                            if (pixel_value) {
                                line_ptr[x] = get_nes_color(ppu.palette_ram[(palette_high << 2) | pixel_value]);
                            } else {
                                line_ptr[x] = get_nes_color(ppu.palette_ram[0]);
                            }
                            
                            fine_x++;
                            if (fine_x == 8) {
                                fine_x = 0;
                                if ((v & 0x001F) == 31) {
                                    v &= ~0x001F;
                                    v ^= 0x0400; 
                                } else {
                                    v += 1;
                                }
                            }
                        }

                        int sprite_height = (ppu.ctrl & 0x20) ? 16 : 8; 
                        for (int i = 63; i >= 0; i--) { 
                            uint8_t y = ppu.oam[i*4];
                            uint8_t tile = ppu.oam[i*4+1];
                            uint8_t attr = ppu.oam[i*4+2];
                            uint8_t x = ppu.oam[i*4+3];
                            
                            if (ppu.scanline >= y && ppu.scanline < y + sprite_height) {
                                int row = ppu.scanline - y;
                                if (attr & 0x80) row = (sprite_height - 1) - row; 
                                
                                uint16_t base;
                                uint16_t tile_idx;
                                
                                if (sprite_height == 16) { 
                                    base = (tile & 1) ? 0x1000 : 0x0000;
                                    tile_idx = tile & 0xFE;
                                    if (row >= 8) { tile_idx++; row -= 8; }
                                } else { 
                                    base = (ppu.ctrl & 0x08) ? 0x1000 : 0x0000;
                                    tile_idx = tile;
                                }
                                
                                uint16_t ptrn = base | (tile_idx << 4) | row;
                                uint8_t p0 = get_chr_byte(ptrn);
                                uint8_t p1 = get_chr_byte(ptrn + 8);
                                
                                for (int bit = 0; bit < 8; bit++) {
                                    int px = x + bit;
                                    if (px >= NES_WIDTH) continue;
                                    int shift = (attr & 0x40) ? bit : (7 - bit); 
                                    uint8_t val = ((p1 >> shift) & 1) << 1 | ((p0 >> shift) & 1);
                                    
                                    if (val != 0) {
                                        if (i == 0 && bg_pixels[px] != 0 && px < 255) ppu.status |= 0x40; 
                                        if ((attr & 0x20) == 0 || bg_pixels[px] == 0) {
                                            uint8_t pal_idx = 0x10 + ((attr & 3) << 2) + val;
                                            line_ptr[px] = get_nes_color(ppu.palette_ram[pal_idx]);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    if (ppu.mask & 0x18) {
                        if ((ppu.v & 0x7000) != 0x7000) {
                            ppu.v += 0x1000;
                        } else {
                            ppu.v &= ~0x7000;
                            int y = (ppu.v & 0x03E0) >> 5;
                            if (y == 29) { y = 0; ppu.v ^= 0x0800; } 
                            else if (y == 31) { y = 0; } 
                            else { y += 1; }
                            ppu.v = (ppu.v & ~0x03E0) | (y << 5);
                        }
                    }
                }
            }
        }
    }
    
    uint32_t* win_buf = my_window->backing_buffer;
    uint32_t* nes_buf = ppu.frame_buffer;
    int win_w = my_window->width; 
    
    if (win_w != 512) return; 

    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 256; x++) {
            uint32_t col = nes_buf[y * 256 + x];
            int wy = y * 2;
            int wx = x * 2;
            int row1 = wy * win_w + wx;
            int row2 = (wy + 1) * win_w + wx;
            win_buf[row1] = col;
            win_buf[row1 + 1] = col;
            win_buf[row2] = col;
            win_buf[row2 + 1] = col;
        }
    }
}