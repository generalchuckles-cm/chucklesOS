#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <cstdint>

class Spinlock {
public:
    void lock() {
        // Atomic test-and-set. 
        // While the value was already true (locked), keep spinning.
        // __atomic_test_and_set returns the previous value.
        while (__atomic_test_and_set(&_locked, __ATOMIC_ACQUIRE)) {
            asm volatile("pause");
        }
    }

    void unlock() {
        // Release the lock
        __atomic_clear(&_locked, __ATOMIC_RELEASE);
    }

private:
    volatile bool _locked = false;
};

// A helper for RAII-style locking (locks on creation, unlocks on destruction)
struct ScopedLock {
    Spinlock& sl;
    ScopedLock(Spinlock& s) : sl(s) { sl.lock(); }
    ~ScopedLock() { sl.unlock(); }
};

#endif