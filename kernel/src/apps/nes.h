#ifndef NES_H
#define NES_H

#include "../gui/window.h"
#include <cstdint>

// --- Internal Struct Definitions ---
struct CPU6502 {
    uint8_t a, x, y, s, p;
    uint16_t pc;
    uint8_t ram[2048];
    uint64_t cycles;
    bool nmi_pending;
    bool irq_pending;
};

struct PPU2C02 {
    uint8_t name_tables[2][1024];
    uint8_t palette_ram[32];
    uint8_t oam[256];
    uint8_t sec_oam[32];
    uint8_t ctrl, mask, status, oam_addr;
    uint8_t fine_x;      
    uint16_t v;          
    uint16_t t;          
    uint8_t data_buf;
    bool w;              
    int scanline, cycle;
    uint32_t* frame_buffer; 
    uint64_t frame_count;
    bool odd_frame;
};

struct Cartridge {
    uint8_t* prg_rom;
    uint8_t* chr_rom;
    uint8_t* prg_ram; 
    int prg_banks, chr_banks, mapper, mirroring; 
    bool loaded;
};

struct Mapper4State {
    uint8_t regs[8];        
    uint8_t bank_select;    
    uint8_t prg_mode;       
    uint8_t chr_mode;       
    int prg_offsets[4];     
    int chr_offsets[8];     
    uint8_t irq_latch;
    uint8_t irq_counter;
    bool irq_enabled;
    bool irq_reload;
};

class NESApp : public WindowApp {
public:
    NESApp(const char* filename);
    ~NESApp(); 

    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;

private:
    char rom_file[64];
    Window* my_window;
    
    // Timing State
    uint64_t next_frame_ticks;
    uint64_t ticks_per_frame;
    
    // Emulation State (Now Members!)
    CPU6502 cpu;
    PPU2C02 ppu;
    Cartridge cart;
    Mapper4State m4;
    
    uint8_t controller_state;
    uint8_t controller_latch;
    bool controller_strobe;

    // Helpers
    void init_emulation();
    
    // Internal Emulation Methods
    uint8_t cpu_read(uint16_t addr);
    void cpu_write(uint16_t addr, uint8_t data);
    uint8_t ppu_read(uint16_t addr);
    void ppu_write(uint16_t addr, uint8_t data);
    
    void cpu_step();
    void mapper4_irq_clock();
    void mapper4_update_offsets();
    uint8_t get_chr_byte(uint16_t addr);
};

#endif