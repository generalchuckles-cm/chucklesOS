#include "elan_touch.h"
#include "../i2c/intel_i2c.h"
#include "../../cppstd/stdio.h"
#include "../../input.h" // For g_mouse_x, etc.
#include "../../render.h"
#include "../../globals.h"

ElanTouchpad::ElanTouchpad() : valid(false), max_x(3200), max_y(2198), last_abs_x(-1), last_abs_y(-1), finger_down(false) {}

ElanTouchpad& ElanTouchpad::getInstance() {
    static ElanTouchpad instance;
    return instance;
}

bool ElanTouchpad::init() {
    if (!IntelI2C::getInstance().init()) {
        return false;
    }
    
    printf("ELAN: Probing 0x10...\n");
    
    // Try to read Hello Packet or FW ID
    // Common Elan command: Write 0x00 0x01 (Identify?)
    // Actually, just reading from 0x10 often returns state.
    
    // Let's try reading 4 bytes to check connectivity
    uint8_t buf[4];
    if (!IntelI2C::getInstance().i2c_read(ELAN_I2C_ADDR, buf, 4)) {
        printf("ELAN: Device not responding at 0x10.\n");
        return false;
    }
    
    printf("ELAN: Detected. Init Bytes: %02x %02x %02x %02x\n", buf[0], buf[1], buf[2], buf[3]);
    valid = true;
    return true;
}

void ElanTouchpad::poll() {
    if (!valid) return;
    
    // Elan I2C Report is typically ~30 bytes, but header is first 5-6 bytes
    // Format (Approximate based on open drivers):
    // Byte 0: Packet Type / Finger Count
    // Byte 1: Finger 1 X Low
    // Byte 2: Finger 1 X High (low nibble) | Y High (high nibble)
    // Byte 3: Finger 1 Y Low
    
    uint8_t packet[8];
    // Attempt read. If NAK (no data), it returns false usually or just empty
    if (!IntelI2C::getInstance().i2c_read(ELAN_I2C_ADDR, packet, 7)) {
        return;
    }
    
    // Basic validation (e.g. check for magic or reasonable values)
    // If packet is all 0 or all FF, probably idle
    if (packet[0] == 0 && packet[1] == 0) return;
    
    // Interpret packet
    // Note: This is a simplistic reverse-engineering assumption.
    // Elan header usually 0x55 or 0x04.
    // Let's assume absolute mode is active by default on power on.
    
    // Decode Finger 1
    // X = (Byte2 & 0x0F) << 8 | Byte1
    // Y = (Byte2 & 0xF0) << 4 | Byte3
    
    int raw_x = ((packet[2] & 0x0F) << 8) | packet[1];
    int raw_y = ((packet[2] & 0xF0) << 4) | packet[3];
    
    // Check contact bit (Byte 0 often contains contact info)
    // Bit 0 = Finger 1 valid?
    bool contact = (packet[0] & 0x01) || (packet[0] & 0x04); // Heuristic
    
    // Check click? (Byte 0 or Byte 4?)
    bool click = (packet[0] & 0x01); // Often bit 0 of first byte is button status in HID descriptors
    
    if (contact && raw_x > 0 && raw_y > 0) {
        if (finger_down) {
            // Calculate delta
            int dx = raw_x - last_abs_x;
            int dy = raw_y - last_abs_y;
            
            // Apply sensitivity
            dx /= 2;
            dy /= 2;
            
            g_mouse_x += dx;
            g_mouse_y += dy; // Touchpad Y is usually same orientation as screen
            
            // Clamp
            if (g_renderer) {
                if (g_mouse_x < 0) g_mouse_x = 0;
                if (g_mouse_x >= (int)g_renderer->getWidth()) g_mouse_x = g_renderer->getWidth() - 1;
                if (g_mouse_y < 0) g_mouse_y = 0;
                if (g_mouse_y >= (int)g_renderer->getHeight()) g_mouse_y = g_renderer->getHeight() - 1;
            }
        }
        
        last_abs_x = raw_x;
        last_abs_y = raw_y;
        finger_down = true;
        g_mouse_left = click;
    } else {
        finger_down = false;
        g_mouse_left = false;
    }
}