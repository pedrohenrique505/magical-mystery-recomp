# Magical Mystery Recomp — Rock Band: The Beatles recompilation for Linux

Static recompilation of *The Beatles: Rock Band* (Xbox 360) into native Linux C++. The PowerPC code in `default.xex` is converted to C++ by the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) and compiled against the SDK runtime (kernel/XAM, Xenos GPU on Vulkan, audio and input through SDL).

> **Bring your own disc.** This repository contains **no game code or assets**. You need your own legitimate copy of the game, dumped from a disc you own. Nothing from the game is distributed here, and `game/`, `*.iso` and `*.xex` are git-ignored.

> **Status:** the game is playable from start to finish with a gamepad: intro, menus, party creation, song list and full songs with clean audio, at a steady 60 fps on a Ryzen 5 4600H with its Vega 6 iGPU. Real USB instruments (guitar, drums, microphone), saves and DLC are not implemented yet. See [`progression/`](progression/) for details.

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

# 5. Recommended: rebuild the SDK runtime and Vulkan GPU plugin with this project's patches
#    (patches/rexglue-sdk/: presentation and busy-wait fixes, Vulkan direct resolve, profiling,
#    runtime FXAA switching).
#    ~15 min at -j2 the first time; scripts/run.sh picks up build/sdk-lib automatically.
scripts/build_sdk.sh            # scripts/build_sdk.sh --remove goes back to the prebuilt SDK

# 6. Run
scripts/run.sh
```

The run log is written to `build/run.log` (`RUN_LOG=path` picks another file).

`scripts/run.sh` runs the game from `build/game_overlay/`, which `scripts/build_game_overlay.py`
(re)builds from your `game/` folder: symlinks to your files plus a few changed game scripts
(`project/mods/`) that add **Options → PC Settings** and **Quit to Desktop** to the game's own
menus. Your dump is never modified; `GAME_DATA_ROOT=game scripts/run.sh` runs the stock scripts.

### PC Settings (in the game: Options → PC Settings)

| Option | Values | Notes |
|---|---|---|
| Venue | 30 FPS / 60 FPS / 60 FPS Performance | 30 FPS is the engine's own venue rate. 60 FPS draws the venue every frame while the GPU keeps up and falls back to 30 in heavy shots. Performance does the same with a lower-resolution crowd texture (impostor LOD 1) and no depth of field, which keeps the venue at 60 most of the time on a Vega 6 (KI-18) |
| Fullscreen | on/off | |
| Keep Aspect Ratio | on/off | off stretches the image to the window |
| Internal Resolution | 1x, 2x, 3x | takes effect after a restart; much sharper, much slower |
| Texture Filtering | Off, 2x-16x | anisotropic filtering (sharp textures at oblique angles; not edges) |
| Anti-Aliasing | Off, FXAA, FXAA Extreme | smooths polygon edges |

Choices are saved in `~/.local/share/beatles/pc_settings.toml`; command-line options and
`REX_<CVAR>` variables override them.

`scripts/run.sh` sets the defaults this game needs, as `REX_<CVAR>` environment variables (anything on the command line still wins): vsync with frame pacing locked to the display (`--present_pacing`), host render targets (`fbo`), and `--execute_unclipped_draw_vs_on_cpu` (upstream Xenia's default; without it the camera cuts drop frames). Measure on AC power: on battery the iGPU is capped.

### Performance and debugging options

```bash
scripts/run.sh --show_fps true      # guest frame rate in the top-left corner (F3: SDK debug overlay); overlays cost frames, see KI-14
scripts/run.sh --perf_log true      # every 2 s: guest fps, audio pacing, host frames painted/dropped
scripts/run.sh --gpu_frame_csv=build/frames.csv   # per-frame GPU time; scripts/analyze_gpu_frames.py summarises it
scripts/run.sh --gpu_frame_csv=build/frames.csv --gpu_frame_categories true   # plus GPU ms per kind of work and a transfer profile
scripts/run.sh --wait_profile true  # every 2 s: per-thread time spent in kernel waits and file reads
scripts/run.sh --audio_dump build/audio.raw   # raw audio frames; see scripts/analyze_audio_dump.py
scripts/profile.sh                  # perf record + host GPU load, writes build/perf.data
```

### Controllers and instruments

Only a real gamepad reaches the game for now (the keyboard does nothing). The game decides
what a controller is from its XInput subtype, and a plain gamepad only counts as a vocalist
(it then asks for a microphone), so by default every pad is presented as a guitar. To pick
another instrument per player:

```bash
scripts/run.sh --instruments drums           # every pad is a drum kit
scripts/run.sh --instruments "guitar,drums"  # player 1 guitar, player 2 drums
```

Values: `guitar` (default), `bass`, `drums`, `gamepad` (real subtype: vocalist, needs a mic). Rock Band layout:
frets A/B/Y/X/LB (green/red/yellow/blue/orange), strum on D-pad up/down, overdrive on Back;
drums use A/B/Y/X for the pads and LB for the kick.

## Layout

| Path | Contents |
|---|---|
| `project/` | ReXGlue project: manifest (`beatles_manifest.toml`), `CMakeLists.txt`, host app (`src/`). `generated/` is codegen output and is git-ignored. |
| `scripts/` | Disc extraction, run and profiling scripts (`run.sh`, `profile.sh`, `rgp_capture.sh`), the patched SDK build (`build_sdk.sh`), the game-data overlay with our script changes (`build_game_overlay.py`, `harmonix.py`: Harmonix ark/DTA formats), analysis of audio dumps, GPU frame logs and screen captures, and PyGhidra tools that find functions the static analysis missed (`find_orphans.py`, `size_of.py`, `resolve_orphans.py`). |
| `project/mods/` | DTA snippets added to the game's scripts (PC Settings screen, menu entries, strings), applied by `scripts/build_game_overlay.py`. |
| `patches/rexglue-sdk/` | Patches to the ReXGlue SDK runtime and Vulkan plugin, applied in order by `scripts/build_sdk.sh`. |
| `tools/` | Prebuilt ReXGlue SDK and the Ghidra XEX loader zip (git-ignored, see Setup). |
| `third_party/` | Reference repositories, not built (git-ignored, see Setup). |
| `game/` | Your extracted game (git-ignored). |
| `progression/` | Current status, next steps, known issues and a development log. |
| `AGENTS.md` | Guidance for AI coding agents working in this repository (`CLAUDE.md` is a symlink to it). |

## Credits

[ReXGlue](https://github.com/rexglue/rexglue-sdk), [Xenia](https://github.com/xenia-project/xenia), [xenia-edge](https://github.com/has207/xenia-edge) (Vulkan direct resolve), [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), [XenosRecomp](https://github.com/hedge-dev/XenosRecomp), [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV) (and the SaveEditors fork), [rb3](https://github.com/DarkRTA/rb3), Ghidra and GhidraMCP.
