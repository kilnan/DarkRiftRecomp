// Debug/test mode for Dark Rift: Recompiled.
//
// Command line options (all optional):
//   --autostart          skip the launcher and start the game immediately
//   --script <file>      drive controller 1 from a script (see below)
//   --shots <dir>        where `shot` saves window captures (default /tmp/darkrift-shots)
//   --background         run as a background app (no Dock icon, does not take focus)
//   --mute               no audio output
// The game speed can be raised with the DARKRIFT_SPEED environment variable (1-16).
//
// Script format, one command per line, executed on the VI clock (60 per game second):
//   wait <frames>                    do nothing
//   press <BTN[+BTN...]> [frames]    hold buttons (default 4 frames), then release for 4
//   hold <BTN[+BTN...]> <frames>     hold buttons without the release gap
//   stick <x> <y> <frames>           hold the analog stick (-1..1)
//   shot <name>                      save a capture of the game window as <name>.png
//   start_until_battle <frames>      press START once a second until a fight starts (gives up after <frames>)
//   start_until_mode <mode> <frames> press START once a second until gCurrentGameMode == <mode> (5 = character select)
//   key <name>                       press and release a keyboard key (SDL scancode name, e.g. F1)
//   mouse <x> <y>                    move the mouse to window coordinates (points)
//   click <x> <y> [right]            click at window coordinates
//   wheel <x> <y> <amount>           scroll the mouse wheel at window coordinates (negative scrolls down)
//   Keyboard and mouse events are pushed into the game's own SDL queue: no OS permissions, no focus needed.
//   quit                             exit the game
// Buttons: A B Z START L R DU DD DL DR CU CD CL CR. Lines starting with # are comments.

#include <atomic>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "darkrift_debug.h"
#include "SDL.h"
#include "recomp_input.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

extern SDL_Window* window;

namespace recompui {
    void hide_all_contexts();
}

namespace {

struct Step {
    enum class Kind { Wait, Hold, Stick, Shot, Quit, StartUntilBattle, Key, Mouse, Click, Wheel } kind;
    uint32_t frames = 0;
    uint16_t buttons = 0;
    float x = 0, y = 0;
    std::string name;
};

std::vector<Step> script;
std::string shots_dir = "/tmp/darkrift-shots";
bool autostart = false;

std::mutex state_mutex;
size_t step_index = 0;
uint32_t step_frames_left = 0;
bool step_started = false;
uint16_t scripted_buttons = 0;
float scripted_x = 0, scripted_y = 0;
std::atomic<uint64_t> vi_count = 0;
std::atomic<uint8_t*> game_rdram = nullptr;

// gCurrentGameMode (s16) in the decompilation; fights are GAME_MODE_BATTLE_AARON (6) to _ZENMURON (16).
constexpr uint32_t GAME_MODE_ADDR = 0x8005BED0;
constexpr int GAME_MODE_BATTLE_FIRST = 6;
constexpr int GAME_MODE_BATTLE_LAST = 16;

int game_mode() {
    uint8_t* rdram = game_rdram.load();
    if (rdram == nullptr) {
        return -1;
    }
    // RDRAM is stored as host-order 32-bit words, so halfwords are swizzled.
    return *reinterpret_cast<int16_t*>(rdram + ((GAME_MODE_ADDR & 0x3FFFFFF) ^ 2));
}

bool in_battle() {
    const int mode = game_mode();
    return mode >= GAME_MODE_BATTLE_FIRST && mode <= GAME_MODE_BATTLE_LAST;
}

uint16_t parse_buttons(const std::string& spec) {
    static const std::pair<const char*, uint16_t> names[] = {
        { "A", 0x8000 }, { "B", 0x4000 }, { "Z", 0x2000 }, { "START", 0x1000 },
        { "DU", 0x0800 }, { "DD", 0x0400 }, { "DL", 0x0200 }, { "DR", 0x0100 },
        { "L", 0x0020 }, { "R", 0x0010 }, { "CU", 0x0008 }, { "CD", 0x0004 }, { "CL", 0x0002 }, { "CR", 0x0001 },
    };
    uint16_t mask = 0;
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, '+')) {
        bool found = false;
        for (const auto& [n, bit] : names) {
            if (part == n) {
                mask |= bit;
                found = true;
            }
        }
        if (!found) {
            fprintf(stderr, "[debug] unknown button '%s'\n", part.c_str());
        }
    }
    return mask;
}

void load_script(const char* path) {
    std::ifstream in(path);
    if (!in) {
        fprintf(stderr, "[debug] cannot open script %s\n", path);
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string op;
        if (!(ss >> op) || op[0] == '#') {
            continue;
        }
        Step s{};
        if (op == "wait") {
            s.kind = Step::Kind::Wait;
            ss >> s.frames;
        } else if (op == "press" || op == "hold") {
            std::string btn;
            ss >> btn;
            s.kind = Step::Kind::Hold;
            s.buttons = parse_buttons(btn);
            s.frames = 4;
            ss >> s.frames;
            script.push_back(s);
            if (op == "press") {
                script.push_back(Step{ Step::Kind::Wait, 4 });
            }
            continue;
        } else if (op == "stick") {
            s.kind = Step::Kind::Stick;
            ss >> s.x >> s.y >> s.frames;
        } else if (op == "shot") {
            s.kind = Step::Kind::Shot;
            ss >> s.name;
        } else if (op == "start_until_battle") {
            s.kind = Step::Kind::StartUntilBattle;
            s.buttons = 0xFFFF;
            ss >> s.frames;
        } else if (op == "start_until_mode") {
            s.kind = Step::Kind::StartUntilBattle;
            ss >> s.buttons >> s.frames;
        } else if (op == "key") {
            s.kind = Step::Kind::Key;
            ss >> s.name;
            s.frames = 2;
        } else if (op == "mouse" || op == "click") {
            s.kind = (op == "mouse") ? Step::Kind::Mouse : Step::Kind::Click;
            ss >> s.x >> s.y;
            std::string button;
            ss >> button;
            s.buttons = (button == "right") ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            s.frames = 2;
        } else if (op == "wheel") {
            s.kind = Step::Kind::Wheel;
            ss >> s.x >> s.y >> s.frames;
            s.buttons = uint16_t(int16_t(s.frames));
            s.frames = 2;
        } else if (op == "quit") {
            s.kind = Step::Kind::Quit;
        } else {
            fprintf(stderr, "[debug] unknown command '%s'\n", op.c_str());
            continue;
        }
        script.push_back(s);
    }
    fprintf(stderr, "[debug] loaded %zu script steps from %s\n", script.size(), path);
}

// Pushes keyboard/mouse events for the game window into SDL's queue (SDL_PushEvent is thread-safe).
uint32_t game_window_id() {
    return window != nullptr ? SDL_GetWindowID(window) : 0;
}

void push_key(const std::string& name) {
    const SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        fprintf(stderr, "[debug] unknown key '%s'\n", name.c_str());
        return;
    }
    for (Uint32 type : { SDL_KEYDOWN, SDL_KEYUP }) {
        SDL_Event e{};
        e.type = type;
        e.key.windowID = game_window_id();
        e.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.scancode = scancode;
        e.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
        const int pushed = SDL_PushEvent(&e);
        SDL_EventFilter filter = nullptr;
        void* filter_data = nullptr;
        const bool has_filter = SDL_GetEventFilter(&filter, &filter_data);
        fprintf(stderr, "[debug] key %s %s: window id %u, pushed %d, event filter %s\n", name.c_str(), type == SDL_KEYDOWN ? "down" : "up",
                game_window_id(), pushed, has_filter ? "installed" : "none");
    }
}

void push_mouse(float x, float y, uint8_t button) {
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.windowID = game_window_id();
    motion.motion.x = int(x);
    motion.motion.y = int(y);
    SDL_PushEvent(&motion);
    if (button != 0) {
        for (Uint32 type : { SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP }) {
            SDL_Event e{};
            e.type = type;
            e.button.windowID = game_window_id();
            e.button.button = button;
            e.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
            e.button.clicks = 1;
            e.button.x = int(x);
            e.button.y = int(y);
            SDL_PushEvent(&e);
        }
    }
}

// Runs on the VI thread once per VI.
void advance_script() {
    std::lock_guard lock{ state_mutex };
    while (step_index < script.size()) {
        Step& s = script[step_index];
        if (!step_started) {
            step_started = true;
            step_frames_left = s.frames;
            scripted_buttons = 0;
            scripted_x = scripted_y = 0;
            switch (s.kind) {
            case Step::Kind::Hold:
                scripted_buttons = s.buttons;
                break;
            case Step::Kind::Stick:
                scripted_x = s.x;
                scripted_y = s.y;
                break;
            case Step::Kind::Shot: {
                const std::string path = shots_dir + "/" + s.name + ".png";
                const bool ok = darkrift::debug::capture_window(path);
                fprintf(stderr, "[debug] vi %llu: shot %s %s\n", (unsigned long long)vi_count.load(), path.c_str(), ok ? "saved" : "FAILED");
                break;
            }
            case Step::Kind::Key:
                push_key(s.name);
                break;
            case Step::Kind::Mouse:
                push_mouse(s.x, s.y, 0);
                break;
            case Step::Kind::Click:
                push_mouse(s.x, s.y, uint8_t(s.buttons));
                break;
            case Step::Kind::Wheel: {
                push_mouse(s.x, s.y, 0);
                SDL_Event e{};
                e.type = SDL_MOUSEWHEEL;
                e.wheel.windowID = game_window_id();
                e.wheel.y = int16_t(s.buttons);
                e.wheel.preciseY = float(int16_t(s.buttons));
                SDL_PushEvent(&e);
                break;
            }
            case Step::Kind::Quit:
                fprintf(stderr, "[debug] vi %llu: quit\n", (unsigned long long)vi_count.load());
                ultramodern::quit();
                break;
            default:
                break;
            }
        }
        if (s.kind == Step::Kind::StartUntilBattle) {
            const bool reached = (s.buttons == 0xFFFF) ? in_battle() : (game_mode() == int(s.buttons));
            if (reached || step_frames_left == 0) {
                fprintf(stderr, "[debug] vi %llu: start_until_battle done, mode %d\n", (unsigned long long)vi_count.load(), game_mode());
            } else {
                // Hold START for 5 frames out of every 60.
                scripted_buttons = ((step_frames_left % 60) < 5) ? 0x1000 : 0;
                step_frames_left--;
                return;
            }
        }
        else if (step_frames_left > 0) {
            step_frames_left--;
            return;
        }
        step_index++;
        step_started = false;
        scripted_buttons = 0;
        scripted_x = scripted_y = 0;
    }
}

// Prints the faulting thread's stack to stderr, so crashes during scripted runs leave a trace in the log.
void crash_handler(int sig) {
    void* frames[64];
    const int count = backtrace(frames, 64);
    const char header[] = "[debug] fatal signal, backtrace:\n";
    write(STDERR_FILENO, header, sizeof(header) - 1);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

#ifndef __APPLE__
void darkrift::debug::disable_window_restoration() {}
bool darkrift::debug::capture_window(const std::string&) { return false; }
#endif

void darkrift::debug::init(int argc, char** argv) {
    disable_window_restoration();
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--autostart") == 0) {
            autostart = true;
        } else if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            load_script(argv[++i]);
        } else if (std::strcmp(argv[i], "--shots") == 0 && i + 1 < argc) {
            shots_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--background") == 0) {
            // Must be set before SDL initializes video.
            SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        } else if (std::strcmp(argv[i], "--mute") == 0) {
            // The dummy driver accepts and discards samples without opening a device.
            SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        }
    }
    if (!script.empty()) {
        for (int sig : { SIGSEGV, SIGBUS, SIGABRT, SIGILL }) {
            signal(sig, crash_handler);
        }
        std::string cmd = "mkdir -p '" + shots_dir + "'";
        std::system(cmd.c_str());
    }
    if (autostart) {
        // Wait for the frontend to come up, then start the game as the launcher would.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            fprintf(stderr, "[debug] autostart\n");
            recomp::start_game(u8"darkrift.n64.us");
            recompui::hide_all_contexts();
        }).detach();
    }
}

void darkrift::debug::on_game_init(uint8_t* rdram, recomp_context*) {
    game_rdram = rdram;
}

void darkrift::debug::on_vi() {
    vi_count++;
    if (!script.empty()) {
        static int last_mode = -2;
        const int mode = game_mode();
        if (mode != last_mode) {
            fprintf(stderr, "[debug] vi %llu: game mode %d\n", (unsigned long long)vi_count.load(), mode);
            last_mode = mode;
        }
    }
    // With --autostart the script waits for the game; without it, it drives the launcher and menus.
    if (!script.empty() && (ultramodern::is_game_started() || !autostart)) {
        advance_script();
    }
    recomp::update_rumble();
}

bool darkrift::debug::get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    if (!script.empty() && controller_num == 0) {
        std::lock_guard lock{ state_mutex };
        if (step_index < script.size()) {
            *buttons = scripted_buttons;
            *x = scripted_x;
            *y = scripted_y;
            return true;
        }
    }
    return recomp::get_n64_input(controller_num, buttons, x, y);
}
