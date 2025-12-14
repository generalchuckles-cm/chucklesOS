#ifndef NES_H
#define NES_H

#include "../render.h"

// Runs the NES emulator.
// rom_file: Path to the .nes file on the FAT32 filesystem.
void run_nes(const char* rom_file);

#endif