# Dark Rift: Recompiled

A native port of **Dark Rift** (Nintendo 64, 1997) made with static recompilation.
The game's MIPS code is translated to C with [N64Recomp](https://github.com/N64Recomp/N64Recomp), runs on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) and is drawn by [RT64](https://github.com/rt64/rt64).

> **This repository does not contain the game.** No ROM, game code, recompiled output or assets are included.
> You need your own copy of the game (USA ROM) to build and run it.

## Playing

Prebuilt builds are on the [Releases](https://github.com/kilnan/DarkRiftRecomp/releases) page (macOS, Apple Silicon).

1. Download `DarkRiftRecompiled-macOS-arm64.zip`, unzip it and move `DarkRiftRecompiled.app` wherever you like.
2. The app is not signed by a registered Apple developer, so macOS blocks it on first launch. Either right-click it and choose **Open**, or run `xattr -dr com.apple.quarantine DarkRiftRecompiled.app` once.
3. In the launcher, select your ROM: **Dark Rift (USA)** in `.z64` format (SHA-1 `3254442626ca2f2ac74400dc1c6a306f5d1b6ceb`). Other regions are not supported.

Saves and settings are stored in `~/Library/Application Support/DarkRiftRecompiled`.

## Status

Work in progress, currently tested on macOS (Apple Silicon) only.

- [x] Game code recompiled from the [matching decompilation](https://github.com/Unnunu/darkrift)'s ELF
- [x] Audio microcode recompiled with RSPRecomp
- [x] Boots and is playable with glitches; stages and UI (Fast3D) render through RT64
- [x] Fighters render: Dark Rift draws its characters with SGI's *Turbo3D* microcode, which RT64 does not support, so Turbo3D tasks are translated to Fast3D display lists on the fly ([`src/main/turbo3d.cpp`](src/main/turbo3d.cpp))
- [x] Turbo3D screen-space objects (`GT_FLAG_NO_XFM`, used for 2D effects and menu elements) are drawn through a matrix that undoes the viewport transform
- [x] HUD and other 2D sprites: RT64 currently draws nothing for the game's 2D sprite rectangles in some scenes (health bars, names and timer in fights, labels on the character select screen). As a workaround the sprite list is rewritten at render time to draw each rectangle as two textured triangles ([`src/main/darkrift_hud.cpp`](src/main/darkrift_hud.cpp)); `DARKRIFT_NO_HUD_FIX=1` disables it
- [ ] Remaining glitches: the areas outside 4:3 are not filled correctly in widescreen
- [ ] Controls, settings and menus still carry over defaults from the frontend this is based on.

## Debug/test mode

For testing without a controller (and faster than real time):

```bash
DARKRIFT_SPEED=4 DarkRiftRecompiled.app/Contents/MacOS/DarkRiftRecompiled --autostart --script scripts/test/start_spam.txt
```

- `--autostart` skips the launcher, `--script` drives controller 1 from a text file (`wait`, `press`, `hold`, `stick`, `shot`, `quit`; see [`src/main/darkrift_debug.cpp`](src/main/darkrift_debug.cpp)) and `shot` saves a capture of the game window (`--shots <dir>`, default `/tmp/darkrift-shots`).
- `DARKRIFT_SPEED` (1-16) speeds up the VI and CPU timers through a small runtime patch in [`runtime-patches/`](runtime-patches), applied automatically at configure time.
- `DARKRIFT_NO_T3D=1` disables the Turbo3D translation, to tell which path draws what.

## Building

See [BUILDING.md](BUILDING.md).

## How it works

- **Recompilation input.** The game is compiled from the decompilation's ELF instead of a hand-made symbol file, so every function boundary is known. libultra `static` functions have no symbol in the ELF. They are recovered from the decompilation objects' IDO `.mdebug` debug info by [`scripts/mdebug_static_funcs.py`](scripts/mdebug_static_funcs.py) and listed as `manual_funcs` in [`darkrift.toml`](darkrift.toml).
- **Graphics.** Each frame the game submits a Fast3D task (stage), a Turbo3D task (fighters) and an optional Fast3D overlay task. Fast3D is rendered by RT64. Turbo3D tasks are intercepted in [`src/main/rt64_render_context.cpp`](src/main/rt64_render_context.cpp) and translated by [`src/main/turbo3d.cpp`](src/main/turbo3d.cpp): each object's state, transform, vertices and triangles become Fast3D commands, with Turbo3D's 64-entry vertex cache emulated (each cached vertex remembers the matrix it was loaded with).

## Credits

- [N64Recomp, N64ModernRuntime](https://github.com/N64Recomp) by Wiseguy and contributors
- [RT64](https://github.com/rt64/rt64) by Darío and contributors
- The frontend (launcher, menus, platform glue) is adapted from [Starfox64Recomp](https://github.com/sonicdcer/Starfox64Recomp), itself based on [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)
- The [Dark Rift decompilation](https://github.com/Unnunu/darkrift) project

## Legal

This is an unofficial fan project. It is not affiliated with or endorsed by Kronos Digital Entertainment, Vic Tokai or Nintendo. Dark Rift and all related names belong to their respective owners.

This project is licensed under the GNU General Public License v3.0 (see [COPYING](COPYING)), as is the frontend code it is based on.
