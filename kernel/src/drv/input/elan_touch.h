#ifndef ELAN_TOUCH_H
#define ELAN_TOUCH_H

#include <cstdint>

class ElanTouchpad {
public:
    static ElanTouchpad& getInstance();
    
    // Initializes driver and attempts to detect device at address 0x10
    bool init();
    
    // Polls the device. If data is available, updates global mouse state.
    void poll();

private:
    ElanTouchpad();
    
    static const uint8_t ELAN_I2C_ADDR = 0x10;
    bool valid;
    
    // Specs from Linux elan_i2c
    uint16_t fw_version;
    uint16_t max_x;
    uint16_t max_y;
    
    // For relative movement calculation
    int last_abs_x;
    int last_abs_y;
    bool finger_down;
};

#endif