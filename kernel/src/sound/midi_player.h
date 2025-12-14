#ifndef MIDI_PLAYER_H
#define MIDI_PLAYER_H

#include <cstdint>

// Fixed: Last argument is now void* to match the generated player
void play_midi(const uint8_t* data, uint32_t len, void* ignore, void* synth_ignored);

#endif