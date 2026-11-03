This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Static recompilation of *Rock Band: The Beatles* (Xbox 360, PowerPC, XEX2) into native Linux C++, using the **prebuilt ReXGlue SDK v0.10.0**. There is no hand-written game code: `project/generated/` is machine output from `rexglue codegen`. Human-authored work is the manifest (`project/beatles_manifest.toml`), the tiny host app (`project/src/`), the helper scripts, and any runtime/kernel/GPU work that ends up needed.

Project status, roadmap and known issues live in `README.md` and `progression/`, not here. Update `progression/` (`status.md`, `next-steps.md`, `known-issues.md`, and a `log.md` entry) when you finish meaningful work.

**All documentation, comments, commit messages and script docstrings are written in English**, even if the user converses in Portuguese.

## Commands

Pipeline (run from repo root; each step feeds the next):

```bash
scripts/extract.sh                       # ISO -> game/ (needs extract-xiso); game/default.xex is the input
tools/rexglue/linux-amd64/bin/rexglue codegen project/beatles_manifest.toml   # ~2 min, ~3 GB peak; writes project/generated/default/
nice -n 10 ninja -C build/beatles -j2    # build; relink alone takes seconds, a full rebuild a few minutes
scripts/run.sh                           # runs the game; log in build/run.log
```

First-time CMake configure (presets in `project/CMakePresets.json` request `clang-20`, which is not installed — do not use them):

```bash
cmake -S project -B build/beatles -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold -DCMAKE_CXX_FLAGS="-march=x86-64-v2" \
  -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" -DREXGLUE_RECOMP_DEBUG_INFO=none \
  -DCMAKE_PREFIX_PATH=$PWD/tools/rexglue/linux-amd64
```

`ninja` re-runs `codegen` itself when its inputs change (stamp-based), so editing the manifest and running `ninja` is enough. There are no tests or linters.

Running the binary directly needs `LD_LIBRARY_PATH=tools/rexglue/linux-amd64/lib` and `--game_data_root game --gpu_plugin xenos`; `scripts/run.sh` sets all of that. Without `--gpu_plugin xenos` there is no graphics at all (log: `gpu_plugin not set`). Extra CLI args pass through `scripts/run.sh`. **Boolean flags:** `--name false` sets the flag to *true* (CLI11 treats it as a flag plus a positional `false`); use `--no-name` or `--name=false` to turn one off.

## Hard constraints

- **8 GB RAM machine.** Always build with `-j2` and `nice`; never run `ninja` with default parallelism. Keep Ghidra GUI closed during full rebuilds. PyGhidra headless is fine alongside the GUI.
- **Never commit or publish game data.** `game/`, `*.iso`, `*.xex` and generated output stay git-ignored. The user supplies their own dump.
- Use the session scratchpad, not `/tmp`, for temporary files.

## Architecture

```
game/default.xex ──rexglue codegen──▶ project/generated/default/*.cpp ──clang──▶ build/beatles/beatles
        ▲                                   (52k+ functions, ~130 files)              │ links
 beatles_manifest.toml (hints)                                                  librexruntime.so + librexgpu-xenos.so
                                                                                (kernel/XAM, Vulkan GPU, SDL audio/input)
```

- `tools/rexglue/linux-amd64/` is the downloaded SDK release (binary `rexglue`, `lib/`, `include/`). The full SDK source in `third_party/rexglue-sdk/` is mainly reference; the exceptions are `librexruntime.so` and the xenos GPU plugin `librexgpu-xenos.so` (the Vulkan backend lives in the plugin), which `scripts/build_sdk.sh` rebuilds with `patches/rexglue-sdk/*.patch` into `build/sdk-lib/` (preferred by `scripts/run.sh` when present, the plugin through `REXGLUE_GPU_PLUGIN_DIR`; `--remove` reverts). Keep SDK changes as patches there, since `third_party/` is git-ignored. `build_sdk.sh` rebuilds each patched file from `HEAD` plus all patches in order and copies only files that differ, so edits made directly in `third_party/rexglue-sdk` are overwritten by the next run unless they are in a patch: to add patch N, apply 0001..N-1 to a clean `git archive HEAD` copy of the touched files, copy the edited files over it and `git diff --src-prefix=i/ --dst-prefix=w/` into `patches/rexglue-sdk/000N-*.patch`. Read it to learn manifest options and runtime behavior (e.g. `src/codegen/config.cpp`, `src/codegen/phase_*.cpp`, `src/ui/rex_app.cpp`).
- `project/generated/rexglue.cmake` is generated boilerplate ("DO NOT EDIT"); customize via `project/CMakeLists.txt` (`rexglue_setup_target(beatles GPU_PLUGINS xenos)`) and `project/src/beatles_app.h` (virtual hooks on `rex::ReXApp`).
- `third_party/XenonRecomp`, `XenosRecomp`, `rb3`, `XEXLoaderWV` are references/tools only. `rb3` (Rock Band 3 Wii decomp) helps identify Harmonix engine structures and strings.
- Guest ↔ host: recompiled code runs against a big-endian guest memory arena (virtual base `0x100000000`); kernel imports (`xboxkrnl`, XAM, `Vd*`) are implemented by the SDK runtime. Missing/stubbed imports show up in `build/run.log` as `STUB` or `unimplemented` warnings.

## Manifest gotchas (non-obvious, cost hours to find)

1. **Sub-tables belong under `[entrypoint.*]`**. `[functions]` at the root is silently ignored (no error); use `[entrypoint.functions]`. Check `build/codegen.log` for `CONFIG functions` with `--log-level debug` to confirm entries loaded.
2. Function entries: `0xADDR = { name = "...", size = N }`. `size` and `end` are mutually exclusive; omit both to let the analyzer decide. Overlapping config ranges abort manifest loading.
3. `codegen` must pass validation completely: any `b`/`bl` to an address not inside a known function is a hard error (`UnresolvedCall`), even a single one.
4. **Why functions go missing:** the SDK's `VTableScanner` finds vtables only via MSVC RTTI. This game (Harmonix engine, STLport) has many classes without RTTI, so vtable-only targets (mostly small `this`-adjustor thunks) are never discovered. Symptom at runtime: `[FATAL] Call to invalid or unregistered function at guest address 0x...`.

5. **Patching guest code:** `[[entrypoint.midasm_hook]]` (`address`, `name`, `registers = ["r3"]`, `return_on_true`/`jump_address`...) makes codegen call a host function (`bool name(PPCRegister& r3)`) at that instruction; define it in `project/src/` (example: `world_rate_hooks.cpp`). Hooks get registers only; for guest memory use `GuestBaseOrNull()` from `debug_probe.cpp`.

## Fixing missing functions (the recurring loop)

- One-off: read the instructions at the address in Ghidra (see below), find where the function ends (first `blr`/`bctr`/unconditional `b` for thunks), add it to `[entrypoint.functions]` with `size`, rerun codegen + ninja.
- Bulk: `scripts/find_orphans.py` lists data words pointing into code that no function covers (input list of registered addresses comes from `project/generated/default/beatles_init.cpp`: `grep -hoE "\{ 0x[0-9A-F]{8}, "`). `scripts/resolve_orphans.py <workdir>` automates "codegen → parse `target not in any function` errors → size via Ghidra → append to manifest" until codegen passes. Both need `.venv` (PyGhidra) and run against a **copy** of the Ghidra project in `<workdir>/ghproj`.
- Computed `size` stops at the first terminator, so it is only trustworthy for thunks (≤32 bytes); larger orphans are declared without `size`. Candidates that fall inside another candidate's `[addr, addr+size)` are interior labels, not functions — drop them.
- **Not every "target not in any function" is a function.** The analyzer's view of a function ends early in two cases, and declaring the stranded code as a function splits the parent (codegen still passes, but the generated code gets `REX_FATAL("Unresolved branch ...")` on internal paths that fire at runtime):
  - **Switch case bodies.** MSVC jump tables (`lhzx`/`lbzx`/`lwzx` + `add` + `mtctr` + `bctr`). `scripts/find_switch_labels.py` decodes all of them; drop any candidate that falls between a `bctr` and its last case label.
  - **SEH blocks.** `__try/__except` resume code sits after an unconditional `b`, bracketed by `mr r8,r8` markers, and is referenced from scope-table data (so `find_orphans.py` sees it). The runtime never dispatches into it; do not declare it.
- **Validation is not enough.** After every codegen, check the output: `grep -h 'REX_FATAL("Unresolved' project/generated/default/beatles_recomp.*.cpp | wc -l` must be 0. `Analyze: N functions still PENDING` (debug log via `--log-level debug --log-file <path>`; add `-f` to force a rerun) is the other health metric.
- Function pointers built in code (`lis`/`addi`, e.g. qsort comparators) are invisible to `find_orphans.py`; `scripts/find_code_ptr_orphans.py` lists them (it already skips jump-table bases).

## Ghidra / PyGhidra

- Ghidra 12.1.2 at `/opt/ghidra`; XEX loader is the `XEXLoaderWV` extension (zip in `tools/xexloader/`, built for 12.0.4 and installed with "Install Anyway"; it also needs the Jython extension enabled). Project: `beatles-rock-band.gpr` in repo root (git-ignored), program `default.xex`, language `PowerPC:BE:64:A2ALT-32addr`, image base `0x82000000`.
- The `mcp__ghidra__*` tools only work while the Ghidra GUI has the program open in CodeBrowser with the GhidraMCP plugin enabled; otherwise they return `BACKEND_UNAVAILABLE`.
- Headless Python scripts: `GHIDRA_INSTALL_DIR=/opt/ghidra .venv/bin/python -I script.py ...` (PyGhidra 3.1.0 / JPype in `.venv`). Plain `analyzeHeadless` refuses Python scripts ("Ghidra was not started with PyGhidra"); use the PyGhidra route above. Use `space.getAddress(v)`, not `flat.toAddr(v)`, for addresses ≥ `0x80000000` (Java `int` overflow).
- Instruction-level facts used so far: MSVC vtable adjustor thunks look like `addi r3,r3,-N ; b target` or `cmpwi r3,0 ; addi ... ; bne +8 ; li ...`.

## Changing the game's scripts (menus, UI)

The UI is DTA script (`.dtb`) inside `game/gen/main_xbox_*.ark`. `scripts/harmonix.py` reads/writes the encrypted ark header (v5) and `.dtb` files byte-exactly and has a DTA text parser/printer (decompile a script: `hx.format_dta(hx.load_dtb(raw)[1])`). Changes go in `project/mods/*.dta` and are applied to the parsed trees by `scripts/build_game_overlay.py`, which writes `build/game_overlay/` (new `gen/main_xbox_10.ark` + rewritten header + `checksums.txt`); `scripts/run.sh` rebuilds it when inputs change. Non-obvious facts:

- **Every script is SHA-1-checked** against a table in the XEX `.data` (KI-16); a changed file without its hash in `checksums.txt` gives "Disc Read Error". `project/src/game_checksums.cpp` patches the table at boot; scripts missing from the table are logged.
- **Script → host:** `{pc_cfg <cmd> ...}` reaches `project/src/script_bridge.cpp` (midasm hook on `DataArray::Execute`'s unknown-command path); arguments must be literals, symbols, strings or `$variables` (no nested commands). New host commands go in `project/src/pc_settings.cpp`.
- A new panel can reuse an existing `.milo` layout (`(file "...")` is relative to the `.dtb`'s directory, so append the panel to a script in the same folder); widget names come from `strings` on the decompressed milo (`0xCBBEDEAF` header, zlib blocks).
- Menu entries are data (`config/gen/main_menu.dtb`), labels are `locale.dtb` tokens (add new tokens to every language). `main.milo` has 7 buttons.
- PC Settings choices are applied at startup as a config file, not with `SetFlagByName`: GPU-plugin cvars (`resolution_scale`, `anisotropic_override`, `swap_post_effect`) are registered after `OnConfigurePaths`, and setting an unregistered cvar fails silently.

## Debugging a run

`build/run.log` is the SDK log (`--log_file`), appended per run, so truncate it (`: > build/run.log`) before a test run. Useful filters: `grep -v "Loader Message\|Creating graphics pipeline"` removes Vulkan noise. Crashes produce a core dump (`coredumpctl list`). `timeout 60 scripts/run.sh` is the usual smoke test; exit 124 means it ran until the timeout (no crash), 134 means abort (look for `[FATAL]`).

The host app overrides some SDK imports by defining `__imp__<Name>` in the executable and forwarding with `dlsym(RTLD_NEXT)` (`project/src/xam_input_hooks.cpp`, `net_hooks.cpp`); that is the way to fix or extend SDK behavior without rebuilding the SDK. A `SIGBUS` anywhere (game or compositor) means a full `/dev/shm`: check `df -h /dev/shm` for leaked `xenia_memory_*` objects (KI-10).

Performance work: measure, don't judge by eye. `--show_fps`/`--perf_log` (`project/src/perf_hooks.cpp`, hooks `VdSwap` and `XAudioSubmitRenderDriverFrame`) give guest fps and audio rate/pacing; `--wait_profile` (`project/src/wait_profiler.cpp`) gives per-thread blocked time in kernel waits/reads, which `perf` cannot see; `--audio_dump` + `scripts/analyze_audio_dump.py` capture what the guest actually submits; `scripts/profile.sh` runs perf (user-space only, `perf_event_paranoid=2`, so the game must be perf's child) plus GPU load sampling. `--perf_log` also reports the host side (`project/src/present_hooks.cpp`, which preempts the SDK's `Presenter::RefreshGuestOutput`/`ConsumeGuestOutput`): frames refreshed into the presenter mailbox, painted, new and dropped, with interval histograms. Any ImGui dialog (`--show_fps`, F3/F4, the SDK achievement toast, which the app disables) switches the presenter to UI-thread painting, which dropped half the gameplay frames (KI-14); measure without overlays. `scripts/run.sh` enables FIFO, `--present_pacing`, host render targets (`fbo`; FSI was slower in heavy scenes) and `--execute_unclipped_draw_vs_on_cpu` (upstream Xenia's default; without it render-target ownership transfers copy the whole EDRAM, the main cause of the camera-cut drops, KI-14) by default through `REX_<CVAR>` environment variables (any `REX_<CVAR>` sets a cvar; the command line wins). `--gpu_frame_csv <path>` (`project/src/gpu_frame_log.cpp`, SDK patch 0006) logs every host frame's real GPU time (timestamp queries) and command/texture/resolve counters; `--gpu_frame_categories` adds GPU ms per kind of work (EDRAM dumps, resolves, texture loads, render-target transfers, uploads) and logs a `transfer profile:` per transfer kind every 600 frames; summarise with `scripts/analyze_gpu_frames.py`. `--rt_transfer_skip_modes` is a debug experiment only (skipping transfers corrupts the image). `--hitch_log_ms N` (`project/src/hitch_tracer.cpp`) logs per-thread CPU and kernel waits for each guest frame longer than N ms; `--perf_log` also reports host GPU busy %, clock, temperature and package power (measure on AC power: on battery the iGPU is capped). RenderDoc captures of slow frames: `SDL_VIDEODRIVER=x11 ENABLE_VULKAN_RENDERDOC_CAPTURE=1 scripts/run.sh --capture_slow_frames=N` (`project/src/gpu_capture.cpp`; RenderDoc needs XWayland); do not replay gameplay captures headless, it freezes the desktop (KI-15). A guest at 60 fps can still look slow: count distinct frames in a screen capture (`scripts/count_distinct_frames.py`, recipe in `progression/next-steps.md`); the game draws the venue at half rate. GPU profiling: `scripts/rgp_capture.sh` + RGP 1.14.1 (RGP 2.x cannot read GCN/Vega traces). Run long offline analyses (ffmpeg, numpy, `perf script`) in the background.

Gameplay benchmarks run unattended: `scripts/bench_song.sh <label> [game args]` boots the game, navigates the menus with a uinput virtual gamepad (`scripts/virtual_pad.py`) by replaying `build/bench_macro.txt` (recorded from the user's own controller with `.venv/bin/python -I scripts/record_pad.py`; without it a slow built-in sequence picks "Sgt. Pepper"), measures `BENCH_SECONDS` (60) of gameplay and closes the game; compare runs with `scripts/bench_summary.py <labels>`. The machine must be left alone meanwhile (the game window must stay visible and focused), so warn the user first.

To watch guest state at runtime, extend `project/src/debug_probe.cpp` (`--debug_probe true`): a host thread that reads guest memory every 2 s and logs it. It never calls guest code. Known guest addresses are listed in `progression/known-issues.md` (KI-12). The RB2 symbol dump in `third_party/rb3/doc/rb2_dump/` matches this engine closely and gives class/method names for what you find. Running PyGhidra headless rewrites Ghidra's `LastOpenedProject` preference; if the GUI opens an empty project, reopen `beatles-rock-band.gpr` from the repo root.
