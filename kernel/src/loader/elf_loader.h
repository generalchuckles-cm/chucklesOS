#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <cstdint>

class ElfLoader {
public:
    static void load_and_run(const char* filename, int argc, char** argv);
};

#endif