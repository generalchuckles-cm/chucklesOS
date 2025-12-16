#ifndef CCC_H
#define CCC_H

// Compiles a C source file into a flat executable binary
// in_file: Source path (e.g. "test.c")
// out_file: Output binary path (e.g. "test.bin")
void ccc_compile(const char* in_file, const char* out_file);

#endif