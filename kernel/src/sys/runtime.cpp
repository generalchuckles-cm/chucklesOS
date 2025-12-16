#include <cstdint>

namespace { void hcf() { for (;;) { asm ("hlt"); } } }

extern "C" {
    // Destructor registration (ignored in kernel)
    int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
    
    // Pure virtual function call handler
    void __cxa_pure_virtual() { hcf(); }
    
    // Global DSO handle
    void *__dso_handle;
    
    // Thread-safe static initialization guards
    // GCC expects 'long long' for 64-bit guards, not int64_t (which might be long)
    int __cxa_guard_acquire(long long *guard) { 
        volatile char *i = (volatile char *)guard; 
        return *i == 0; 
    }
    
    void __cxa_guard_release(long long *guard) { 
        volatile char *i = (volatile char *)guard; 
        *i = 1; 
    }
}