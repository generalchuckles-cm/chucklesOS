#ifndef RAW_LOADER_H
#define RAW_LOADER_H

class RawLoader {
public:
    static void load_and_run(const char* filename, int argc, char** argv);
};

#endif