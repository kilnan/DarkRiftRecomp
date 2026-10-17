# Building

These steps are for macOS on Apple Silicon, the only platform tested so far.

## 1. Dependencies

```bash
brew install cmake ninja sdl2 sdl3 freetype llvm lld mips-linux-gnu-binutils
xcodebuild -downloadComponent MetalToolchain   # Metal shader compiler (Xcode 26+)
```

## 2. Clone

```bash
git clone --recurse-submodules https://github.com/kilnan/DarkRiftRecomp.git
cd DarkRiftRecomp
```

## 3. Inputs: ROM and ELF

You need the **Dark Rift (USA)** ROM in big-endian `.z64` format (sha1 `3254442626ca2f2ac74400dc1c6a306f5d1b6ceb`) and the ELF produced by the [decompilation](https://github.com/Unnunu/darkrift). Follow that project's instructions to build it. A matching build produces the same ELF.

```bash
cp /path/to/darkrift.z64 darkrift.z64
cp /path/to/darkrift-decomp/build/darkrift.elf darkrift.elf
```

## 4. Build the recompiler

```bash
git clone --recurse-submodules https://github.com/N64Recomp/N64Recomp.git tools/N64Recomp
cmake -S tools/N64Recomp -B tools/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tools/N64Recomp/build
ln -s tools/N64Recomp/build/N64Recomp N64Recomp
ln -s tools/N64Recomp/build/RSPRecomp RSPRecomp
```

## 5. Generate the C code

```bash
# symbol files used by the patches
./N64Recomp darkrift.toml --dump-context
mkdir -p syms && mv dump.toml syms/darkrift.syms.toml && mv data_dump.toml syms/darkrift.datasyms.toml

# game code and audio microcode
./N64Recomp darkrift.toml
./RSPRecomp aspMain.toml
```

The `manual_funcs` list in `darkrift.toml` was generated with `scripts/mdebug_static_funcs.py <decomp dir> syms/darkrift.syms.toml`. You only need to rerun it if the decompilation's layout changes.

## 6. Build

```bash
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DPATCHES_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
    -DPATCHES_LD=/opt/homebrew/opt/lld/bin/ld.lld
cmake --build build-cmake --target DarkRiftRecompiled
```

The app is `build-cmake/DarkRiftRecompiled.app`. On first launch, select your ROM in the launcher.
