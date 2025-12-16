#ifndef HIGH_LOADER_H
#define HIGH_LOADER_H

class HighLoader {
public:
    // Loads a flat binary or CXE file to the top of physical RAM and executes it
    static void load_and_run(const char* filename);
};

#endif