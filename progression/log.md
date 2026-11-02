# Development log

## 2026-11-01 — From zero to a running game

- **Decision:** static recompilation (not a matching decomp). Use the **prebuilt ReXGlue SDK** instead of building the SDK (8 GB of RAM). XenonRecomp/XenosRecomp/rb3 are kept as reference only.
- Disc extracted with `extract-xiso`; `default.xex` (title `454108B1`).
- `rexglue init` + `codegen` failed on the first attempt with 1 error: `b 0x8264E3F0` to a 4-byte thunk (`b 0x8264E060`) that the analysis did not recognize.
  - Declaring the function **without `size`** did not help; the real reason was that `[functions]` at the manifest root is **silently ignored**. The correct table is `[entrypoint.functions]`.
- Installed the XEXLoaderWV loader in Ghidra (version warning; needed Jython enabled). Used Ghidra to disassemble the thunk.
- Build: `clang` + `mold` + `ccache`, `-O1`, `-j2`; the presets ask for `clang-20`, which is not installed on the machine.
- First run: no GPU (`gpu_plugin not set`). Fixed with `GPU_PLUGINS xenos` in CMake **and** `--gpu_plugin xenos` on the command line.
- Successive `Call to invalid or unregistered function` crashes (`0x82648980`, `0x828807E8`): vtable thunks without RTTI. Instead of fixing one per round, installed **PyGhidra** (already bundled with Ghidra, wheels in `Features/PyGhidra/pypkg/dist`) and wrote `scripts/find_orphans.py`, which lists data targets with no registered function (252 candidates).
  - Filter candidates that fall inside another candidate's range (interior labels) and follow `b` chains (thunks that jump to another stub).
  - Ghidra is **not** a good oracle for what rexglue covers (non-contiguous function bodies); the `codegen` validator is. Hence `scripts/resolve_orphans.py`.
- Result: 155 functions added, clean `codegen`, the game runs for 60 s without aborting.
- Created `CLAUDE.md`, `README.md` and this folder.

## 2026-11-01 — First visual check

- Running `scripts/run.sh` by hand, the window shows the game's **intro: an animated movie**. This is the first confirmation that rendering works end to end (Xenos → Vulkan, plus video playback).
- Not yet recorded: intro audio, behavior after the movie ends, menus. These are the first items in `next-steps.md`.

## 2026-11-01 — Intro freeze: input race in the SDK

- The intro froze at varying points (earlier when keys were pressed). Logs showed nothing.
- Diagnosis: ran under gdb (launching the process, since `ptrace_scope=1` forbids attaching) with per-thread CPU sampling from `/proc`, then a watcher that sends `SIGABRT` when the Main XThread stops using CPU, to get a core dump without ptrace overhead.
  - One snapshot: Main XThread blocked in `RtlEnterCriticalSection` (guest `sub_823DBBD8`, critical section at `0x82ACA738`) while the game's input polling thread was inside `XamInputGetState` → `InputSystem::RefreshDevices()`.
  - One run aborted on its own with `free(): invalid pointer` in `RefreshDevices()` (core dump).
  - The GPU command processor sitting in `WAIT_REG_MEM` (physical `0x1FC9B004`) and the main thread in the D3D wait loop `sub_826665D8` turned out to be a consequence, not the cause.
- Root cause: `InputSystem` in the prebuilt SDK has no locking (see KI-7). Fixed by interposing the three `XamInput*` imports the game uses in `project/src/input_serialization.cpp`.
- Result: two 120 s runs with no crash and no stall (automated check, nobody pressing keys). Visual confirmation by the user still pending.

## 2026-11-01 — Past the intro: code-built function pointers and bogus orphans

- With a gamepad connected, A skips the intro (the keyboard does nothing). Then `[FATAL] Call to invalid or unregistered function at guest address 0x829C9298`: a 24-byte qsort-style comparator whose address is built in code (`lis`/`addi`), invisible to `find_orphans.py`. New `scripts/find_code_ptr_orphans.py`: 160 hits at first, but 118 were switch jump-table bases (the label right after `bctr`). After filtering those out, 42 real entries (`codeptr_*`), declared without `size`.
- Next run: `[FATAL] Unresolved branch from 0x8232E788 to 0x8232E7B8`. Root cause: the previous orphan loop had declared switch case bodies and SEH resume blocks as functions (KI-8). New `scripts/find_switch_labels.py` decodes 129 jump tables; 98 manifest entries were case bodies of 5 switches, and 5 more were `__try/__except` resume blocks.
- Removing them: PENDING functions 66 → 4, `REX_FATAL("Unresolved` sites in generated code 115 → 0; codegen validation passes. 90 s smoke test with no FATAL. User test of skipping the intro still pending.
- Note: during the user's first run of this session Hyprland itself crashed (SIGBUS, core dump at 19:55). Cause unknown; watch whether it repeats.

## 2026-11-01 — Menus work; instrument emulation

- User report: intro skip works, menus navigate smoothly, frame rate stable. First-run prompts (song cache, console settings, save data, calibration) all show up.
- Blocker: starting a song asks for microphones. The game's controller classifier (guest `sub_823E5028`, after `XInputGetCapabilities`) maps SubType 6/11 → guitar path (`sub_823DBF20`), 8 → drums (`sub_823DBF98`), everything else → gamepad, i.e. vocalist.
- Added `--instruments` (KI-9) in `project/src/xam_input_hooks.cpp` (renamed from `input_serialization.cpp`; it also keeps the KI-7 mutex). The parsed setting is logged at startup (`Instrument emulation ...`). Waiting for the user to test a song.

## 2026-11-01 — Party screen, network spin, /dev/shm leak

- User: `--instruments guitar` works. New blocker: after creating a party the song list does not appear (only the background). The run log had rotated 20 times in 7 s, all `NetDll_WSAGetOverlappedResult STUB`: a network receive loop spinning on fake completions (KI-11). Implemented both imports in `project/src/net_hooks.cpp`.
- While testing, the game died with `SIGBUS` and Hyprland crashed (SIGBUS in its keymap mmap), forcing a reboot. Cause: leaked `xenia_memory_*` shm objects filling `/dev/shm` (KI-10). The 19:55 Hyprland crash earlier in the day was most likely the same. Fixed by unlinking at startup (`project/src/shm_cleanup.cpp`) plus a stale-object sweep in `scripts/run.sh`. Verified: `/dev/shm` stays empty during and after a run killed by `timeout`.
- Song list still to be retested by the user.

## 2026-11-01 — Song list fixed; first song played

- `--instruments` now defaults to `guitar` (a plain pad only counts as a vocalist).
- Empty song list after creating a party, tracked down with a new `--debug_probe` host thread that reads guest state every 2 s: `SongSyncPanel` waited for 3 pending users, which were the pre-allocated remote `BandUser`s. They looked like band members because every user GUID is built from `XNetRandom`, which the SDK stubs with a constant (KI-12). Implemented `XNetRandom` with `getrandom`.
- The Ghidra MCP (GUI with GhidraMCP) plus the RB2 symbol dump in `third_party/rb3/doc/rb2_dump` (`SongSyncPanel`, `NetGameMsgs`) made the reverse engineering fast; the RB2 engine matches this game closely.
- User test: song list shows, "Lucy in the Sky with Diamonds" played to the end with no black screen or crash. **New main problem: gameplay frame rate is very low** (menus were fine).
- Side note: running PyGhidra headless rewrites Ghidra's `LastOpenedProject`, so the GUI opened an empty scratch copy; reopen `beatles-rock-band.gpr` from the repo root if the program seems to be missing.
