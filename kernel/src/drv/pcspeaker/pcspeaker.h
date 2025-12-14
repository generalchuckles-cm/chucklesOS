#ifndef PCSPEAKER_H
#define PCSPEAKER_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Enables the speaker gate. Does NOT produce a tone.
void pcspeaker_on();

// Disables the speaker gate.
void pcspeaker_off();

// Toggles the raw data bit to produce 1-bit PCM sound.
void pcspeaker_set_data_bit(bool on);

#ifdef __cplusplus
}
#endif

#endif