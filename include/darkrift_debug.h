#ifndef DARKRIFT_DEBUG_H
#define DARKRIFT_DEBUG_H

#include <cstdint>
#include <string>

namespace darkrift::debug {
    // Parses --autostart / --script / --shots (see darkrift_debug.cpp).
    void init(int argc, char** argv);
    // VI callback: advances the input script, then forwards to recomp::update_rumble.
    void on_vi();
    // Input callback: returns scripted input for controller 1 while a script runs.
    bool get_input(int controller_num, uint16_t* buttons, float* x, float* y);
    // Saves a PNG capture of the game window (implemented per platform).
    bool capture_window(const std::string& path);
    // Stops macOS from offering to restore windows after a crash (no-op elsewhere).
    void disable_window_restoration();
}

#endif
