#include "midi_player.h"
#include "midi_data.h"
#include "../timer.h"
#include "../drv/pcspeaker/pcspeaker.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../input.h"
#include "../globals.h"

struct Oscillator { 
    bool active; 
    uint32_t freq; 
    uint32_t phase; 
    uint32_t step; 
    int32_t amp; 
    int note; 
};

static Oscillator g_voices[8];

void play_midi(const uint8_t* i1, uint32_t i2, void* i3, void* i4) {
    (void)i1; (void)i2; (void)i3; (void)i4;

    bool running = true;
    
    if (g_renderer) g_renderer->clear(0x000000);

    uint64_t cpu_freq = get_cpu_frequency();
    if (cpu_freq == 0) cpu_freq = 2000000000;
    
    uint64_t cycles_per_sample = cpu_freq / 44100;
    uint64_t next_sample = rdtsc_serialized();

    for(int i=0; i<8; i++) g_voices[i].active = false;
    pcspeaker_on();
    
    const uint8_t* ptr = g_music_data;
    uint32_t wait = 0;
    uint32_t total = 0;
    int32_t err = 0;
    const uint32_t PHASE_SCALE = 97391;

    printf("Playing... (Size: %u bytes)\n", sizeof(g_music_data));

    while(running) {
        // --- INPUT CHECK ---
        if ((total & 0xFFF) == 0) { // Check every ~4000 samples to save CPU
            char c = input_check_char();
            if (c == 27) running = false;
        }
        
        // Fetch Instructions
        while(wait == 0 && running) {
            uint8_t cmd = *ptr++;
            if(cmd == 0xFF) { running = false; break; }
            else if(cmd < 0xF0) wait = cmd * 44; 
            else if(cmd == 0xF0) { 
                wait = (ptr[0] | (ptr[1] << 8)) * 44; 
                ptr += 2; 
            }
            else if(cmd == 0xF1) {
                uint8_t n = *ptr++;
                uint8_t v = *ptr++;
                for(int i=0; i<8; i++) {
                    if(!g_voices[i].active) {
                        g_voices[i].active = true;
                        g_voices[i].freq = g_freq_table[n];
                        g_voices[i].step = g_voices[i].freq * PHASE_SCALE;
                        g_voices[i].phase = 0;
                        g_voices[i].amp = v * 250;
                        g_voices[i].note = n;
                        break;
                    }
                }
            } 
            else if(cmd == 0xF2) {
                uint8_t n = *ptr++;
                for(int i=0; i<8; i++) {
                    if(g_voices[i].active && g_voices[i].note == n) {
                        g_voices[i].active = false;
                    }
                }
            }
        }
        
        if(!running) break;

        // Wait
        while(rdtsc_serialized() < next_sample) asm("pause");
        
        // Synthesize
        int32_t mix = 0;
        int active_cnt = 0;
        for(int i=0; i<8; i++) {
            if(g_voices[i].active) {
                active_cnt++;
                int16_t val = (g_voices[i].phase & 0x80000000) ? g_voices[i].amp : -g_voices[i].amp;
                mix += val;
                g_voices[i].phase += g_voices[i].step;
            }
        }
        
        int32_t des = mix + err;
        bool bit = (des > 0);
        err = des - (bit ? 32000 : -32000);
        pcspeaker_set_data_bit(bit);
        
        if(wait > 0) wait--;
        next_sample += cycles_per_sample;
        total++;
        
        if((total % 4410) == 0 && g_console) {
             char buf[64];
             int sec = total / 44100;
             sprintf(buf, "Time: %02d:%02d | Voices: %d   ", sec/60, sec%60, active_cnt);
             g_console->putString(0, 0, buf, 0x00FF00, 0x000000);
        }
    }
    
    pcspeaker_off();
}