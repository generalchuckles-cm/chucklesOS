#ifndef TERMINAL_H
#define TERMINAL_H

#include "../gui/window.h"

class TerminalApp : public WindowApp {
public:
    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;
    
private:
    Window* my_window;
    
    char input_buffer[256];
    int input_index;
    
    // Helper for command processing
    void execute_command();
};

#endif