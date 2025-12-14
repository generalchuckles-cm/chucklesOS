#ifndef TEST_SONG_H
#define TEST_SONG_H

#include <cstdint>

// Note Offsets
#define _C  0
#define _Cs 1
#define _D  2
#define _Ds 3
#define _E  4
#define _F  5
#define _Fs 6
#define _G  7
#define _Gs 8
#define _A  9
#define _As 10
#define _B  11

struct SimpleNote {
    int note_offset;     // _C, _D, etc.
    int octave;          // 4, 5, etc.
    uint32_t start_ms;   // Start time in milliseconds (1s = 1000)
    uint32_t len_ms;     // Duration in milliseconds
    int channel;         // Instrument (0=Square, 1=Sine...)
};

// The Test Pattern
// Format: Note, Octave, Start(ms), Len(ms), Channel
static const SimpleNote g_test_song[] = {
    // 1. C5 at 1 second, lasts 1 second
    { _C, 5, 1000, 1000, 0 }, 
    
    // 2. E5 at 2 seconds
    { _E, 5, 2000, 500,  0 },

    // 3. G5 at 2.5 seconds
    { _G, 5, 2500, 500,  0 },

    // 4. C6 at 3 seconds (High C)
    { _C, 6, 3000, 1000, 0 },
    
    // 5. Chord test at 4.5s (C + E + G)
    { _C, 5, 4500, 1000, 1 },
    { _E, 5, 4500, 1000, 1 },
    { _G, 5, 4500, 1000, 1 }
};

static const int g_test_song_count = sizeof(g_test_song) / sizeof(SimpleNote);

#endif