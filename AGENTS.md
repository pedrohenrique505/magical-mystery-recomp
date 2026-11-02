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

Running the binary directly needs `LD_LIBRARY_PATH=tools/rexglue/linux-amd64/lib` and `--game_data_root game --gpu_plugin xenos`; `scripts/run.sh` sets all of that. Without `--gpu_plugin xenos` there is no graphics at all (log: `gpu_plugin not set`). Extra CLI args pass through `scripts/run.sh`.

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

- `tools/rexglue/linux-amd64/` is the downloaded SDK release (binary `rexglue`, `lib/`, `include/`). The full SDK source in `third_party/rexglue-sdk/` is **reference only** — it is not built. Read it to learn manifest options and runtime behavior (e.g. `src/codegen/config.cpp`, `src/codegen/phase_*.cpp`, `src/ui/rex_app.cpp`).
- `project/generated/rexglue.cmake` is generated boilerplate ("DO NOT EDIT"); customize via `project/CMakeLists.txt` (`rexglue_setup_target(beatles GPU_PLUGINS xenos)`) and `project/src/beatles_app.h` (virtual hooks on `rex::ReXApp`).
- `third_party/XenonRecomp`, `XenosRecomp`, `rb3`, `XEXLoaderWV` are references/tools only. `rb3` (Rock Band 3 Wii decomp) helps identify Harmonix engine structures and strings.
- Guest ↔ host: recompiled code runs against a big-endian guest memory arena (virtual base `0x100000000`); kernel imports (`xboxkrnl`, XAM, `Vd*`) are implemented by the SDK runtime. Missing/stubbed imports show up in `build/run.log` as `STUB` or `unimplemented` warnings.

## Manifest gotchas (non-obvious, cost hours to find)

1. **Sub-tables belong under `[entrypoint.*]`**. `[functions]` at the root is silently ignored (no error); use `[entrypoint.functions]`. Check `build/codegen.log` for `CONFIG functions` with `--log-level debug` to confirm entries loaded.
2. Function entries: `0xADDR = { name = "...", size = N }`. `size` and `end` are mutually exclusive; omit both to let the analyzer decide. Overlapping config ranges abort manifest loading.
3. `codegen` must pass validation completely: any `b`/`bl` to an address not inside a known function is a hard error (`UnresolvedCall`), even a single one.
4. **Why functions go missing:** the SDK's `VTableScanner` finds vtables only via MSVC RTTI. This game (Harmonix engine, STLport) has many classes without RTTI, so vtable-only targets (mostly small `this`-adjustor thunks) are never discovered. Symptom at runtime: `[FATAL] Call to invalid or unregistered function at guest address 0x...`.

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

## Debugging a run

`build/run.log` is the SDK log (`--log_file`), appended per run, so truncate it (`: > build/run.log`) before a test run. Useful filters: `grep -v "Loader Message\|Creating graphics pipeline"` removes Vulkan noise. Crashes produce a core dump (`coredumpctl list`). `timeout 60 scripts/run.sh` is the usual smoke test; exit 124 means it ran until the timeout (no crash), 134 means abort (look for `[FATAL]`).

The host app overrides some SDK imports by defining `__imp__<Name>` in the executable and forwarding with `dlsym(RTLD_NEXT)` (`project/src/xam_input_hooks.cpp`, `net_hooks.cpp`); that is the way to fix or extend SDK behavior without rebuilding the SDK. A `SIGBUS` anywhere (game or compositor) means a full `/dev/shm`: check `df -h /dev/shm` for leaked `xenia_memory_*` objects (KI-10).

To watch guest state at runtime, extend `project/src/debug_probe.cpp` (`--debug_probe true`): a host thread that reads guest memory every 2 s and logs it. It never calls guest code. Known guest addresses are listed in `progression/known-issues.md` (KI-12). The RB2 symbol dump in `third_party/rb3/doc/rb2_dump/` matches this engine closely and gives class/method names for what you find. Running PyGhidra headless rewrites Ghidra's `LastOpenedProject` preference; if the GUI opens an empty project, reopen `beatles-rock-band.gpr` from the repo root.
