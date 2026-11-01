# I Am the Walrus — Rock Band: The Beatles recompilation for Linux

Static recompilation of *The Beatles: Rock Band* (Xbox 360) into native Linux C++. The PowerPC code in `default.xex` is converted to C++ by the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) and compiled against the SDK runtime (kernel/XAM, Xenos GPU on Vulkan, audio and input through SDL).

> **Bring your own disc.** This repository contains **no game code or assets**. You need your own legitimate copy of the game, dumped from a disc you own. Nothing from the game is distributed here, and `game/`, `*.iso` and `*.xex` are git-ignored.

> **Status:** the game boots, opens a window, brings up Vulkan and audio, and runs without aborting. Gameplay is not yet validated and USB instruments are not implemented. See [`progression/`](progression/) for details.

## Legal

Not affiliated with or endorsed by Microsoft, Harmonix, Apple Corps or any rights holder. This project is for education and preservation research. Do not use it to distribute copyrighted material.

## Requirements

- Linux x86-64 (developed on Arch/Hyprland) with a Vulkan-capable GPU.
- `clang`, `cmake`, `ninja`, `mold`, `ccache`, `git`, `python3`, `uv`, `gh`.
- `extract-xiso` (AUR: `extract-xiso-git`) to extract the disc image.
- Optional, for analysis only: Ghidra 12.1.2 with the [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV) extension (and the Jython extension enabled), plus PyGhidra.
- **8 GB of RAM is enough** as long as the build uses `-j2`. `codegen` peaks around 3 GB.
- About 10 GB of free disk (extracted game 4.4 GB, generated code 140 MB, build ~200 MB).

## Setup

```bash
# 0. Reference repositories (not built; used to read the SDK source and for research)
mkdir -p third_party && cd third_party
git clone --depth 1 https://github.com/rexglue/rexglue-sdk
git clone --depth 1 https://github.com/hedge-dev/XenonRecomp
git clone --depth 1 https://github.com/hedge-dev/XenosRecomp
git clone --depth 1 https://github.com/DarkRTA/rb3
git clone --depth 1 https://github.com/zeroKilo/XEXLoaderWV
cd ..

# 1. Prebuilt SDK (v0.10.0, linux-amd64) into tools/rexglue/
gh release download v0.10.0 -R rexglue/rexglue-sdk -p 'rexglue-sdk-0.10.0-linux-amd64.zip' -D tools/rexglue-dl
unzip tools/rexglue-dl/*.zip -d tools/rexglue

# 2. Extract your disc image (first argument is the path to your ISO)
scripts/extract.sh /path/to/your/game.iso      # produces game/default.xex and the assets

# 3. Recompile the XEX into C++ (~2 min)
tools/rexglue/linux-amd64/bin/rexglue codegen project/beatles_manifest.toml

# 4. Configure and build
cmake -S project -B build/beatles -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold -DCMAKE_CXX_FLAGS="-march=x86-64-v2" \
  -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" -DREXGLUE_RECOMP_DEBUG_INFO=none \
  -DCMAKE_PREFIX_PATH=$PWD/tools/rexglue/linux-amd64
nice -n 10 ninja -C build/beatles -j2

# 5. Run
scripts/run.sh
```

The run log is written to `build/run.log`.

## Layout

| Path | Contents |
|---|---|
| `project/` | ReXGlue project: manifest (`beatles_manifest.toml`), `CMakeLists.txt`, host app (`src/`). `generated/` is codegen output and is git-ignored. |
| `scripts/` | Disc extraction, run script, and PyGhidra tools that find functions the static analysis missed (`find_orphans.py`, `size_of.py`, `resolve_orphans.py`). |
| `tools/` | Prebuilt ReXGlue SDK and the Ghidra XEX loader zip (git-ignored, see Setup). |
| `third_party/` | Reference repositories, not built (git-ignored, see Setup). |
| `game/` | Your extracted game (git-ignored). |
| `progression/` | Current status, next steps, known issues and a development log. |
| `CLAUDE.md` | Guidance for AI coding agents working in this repository. |

## Credits

[ReXGlue](https://github.com/rexglue/rexglue-sdk), [Xenia](https://github.com/xenia-project/xenia), [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), [XenosRecomp](https://github.com/hedge-dev/XenosRecomp), [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV) (and the SaveEditors fork), [rb3](https://github.com/DarkRTA/rb3), Ghidra and GhidraMCP.
