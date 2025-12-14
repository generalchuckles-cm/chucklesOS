#include "pcspeaker.h"
#include "../../io.h"

#define SPEAKER_PORT 0x61

extern "C" {

void pcspeaker_on() {
    // Enable the PIT Channel 2 Gate (bit 0) to allow the speaker to be controlled.
    uint8_t status = inb(SPEAKER_PORT);
    if (!(status & 0x01)) {
        outb(SPEAKER_PORT, status | 0x01);
    }
}

void pcspeaker_off() {
    // Disable both the gate and the data bit.
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & 0xFC);
}

void pcspeaker_set_data_bit(bool on) {
    uint8_t status = inb(SPEAKER_PORT);
    if (on) {
        outb(SPEAKER_PORT, status | 0x02); // Set bit 1
    } else {
        outb(SPEAKER_PORT, status & ~0x02); // Clear bit 1
    }
}

} // end extern "C"