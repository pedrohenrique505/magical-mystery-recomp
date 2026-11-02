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

## 2026-11-01 — Gameplay audio fixed; measurement tooling

- Added measurement tools instead of judging by eye: `--show_fps` (ImGui overlay, also feeds the SDK's F3 overlay), `--perf_log` (guest fps from `VdSwap`, audio frames/s and submit pacing), `--wait_profile` (per-thread time in kernel waits/reads), `--audio_dump` + `scripts/analyze_audio_dump.py`, and `scripts/profile.sh` (perf record + GPU load sampling).
- First perf profile: guest main thread spins ~75% in the D3D wait-for-GPU loop; host GPU ~90% busy; the SDK audio worker burns a core polling. The fps counter then showed the guest at a steady 60 fps while the user saw ~10-15, so the problem was not guest CPU speed.
- User report: audio crackled and felt slowed down during songs. Ruled out one by one with data: audio submit rate (187.5/s, real-time), silence/starvation (none), clipping (none), pitch (normal spectrum), SDK output path (screen-capture audio matched the dump with zero drift), blocking waits and file reads (normal). The dump finally showed ~14% of frames were stale copies from two frames earlier: SDL pulled 4 frames per callback, so the game's XAudio callback ran in bursts its mixer could not follow (KI-13).
- Fix: `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256` in `scripts/run.sh`. Stale frames 14% → 0%; user: "the audio is perfect".
- A 60 fps screen capture shows only ~30 distinct frames per second in gameplay (60 in menus): next target (KI-14).

## 2026-11-01 — Gameplay frame pacing: presenter no longer drops frames

- Added host-side presentation counters (`project/src/present_hooks.cpp`): it preempts the SDK's non-virtual `Presenter::RefreshGuestOutput` and `Presenter::ConsumeGuestOutput` (reached through the GOT, so an executable definition wins) and adds refreshed/painted/new/dropped counts and interval histograms to each `--perf_log` line. The guest swap-interval histogram was added there too.
- Gameplay run (user): the guest refreshed ~60 frames/s into the mailbox, but the UI thread painted only 25-40 times per second, so 30-60 of every 120 frames were dropped: the ~30 distinct fps of KI-14. In menus the same thread painted 150-2000 times per second (IMMEDIATE present mode).
- Cause: the SDK always creates an ImGui achievement toast; any UI drawer makes the presenter paint from the UI thread, and those paints queue behind the game's GPU work with 3 submissions in flight. Fix: `CreateAchievementNotificationDialog` returns `nullptr`, so the GPU command thread presents each frame itself. Gameplay: painted = refreshed, 0 dropped; intro GPU busy 93% → 77%. User: "much better visually, still some frame drops".
- Remaining: guest-side hitches (one 40-150 ms swap every 2-6 s), not correlated with pipeline creation. Next session starts there.
- Gotcha found on the way: SDK boolean flags parse `--name false` as *true* (CLI11 flag plus a positional); use `--no-name`. Documented in `AGENTS.md`.

## 2026-11-02 — Frame pacing locked to vsync; FSI render targets

- New measurement tools: `--hitch_log_ms` (`project/src/hitch_tracer.cpp`: per-thread CPU time and kernel waits for every long guest frame), host GPU busy/clock/temperature/PPT in `--perf_log` (amdgpu sysfs), `amdgpu_top` block counters, and RenderDoc captures of slow frames (`--capture_slow_frames`, `project/src/gpu_capture.cpp`).
- The guest itself runs songs at a clean 60 fps; the stutter was host-side. Earlier tests had been on battery, where the iGPU is capped at ~400-650 MHz.
- Cause A: the guest vblank is a free-running SDK timer; with FIFO, frames drifted against the real vsync and were dropped/repeated, and late GPU frames halved the rate to 30. `--present_pacing` makes `VdSwap` wait for the previous frame's paint: 0 dropped frames in a full song, dips 40-57 fps instead of 30.
- Cause B: the GPU (pixel shading + texture fetch bound). A RenderDoc capture showed 55% of GPU time in render-target transfer rectangles of the host-render-target EDRAM emulation. `--render_target_path_vulkan=fsi` removes them: song windows below 55 fps 16% → 6.5%. User: drops reduced, still not a locked 60.
- FIFO, pacing and FSI are now defaults in `scripts/run.sh` (env vars, overridable on the command line).
- KI-15: the SDK `dlopen`s `librenderdoc.so` to "detect" RenderDoc, which injects it into every run once installed (no window on Wayland). Fixed with `project/src/renderdoc_guard.cpp`. Lesson: headless RenderDoc replays of gameplay captures saturate the iGPU and freeze the desktop; the user drove the GUI instead.

## 2026-11-02 (later) — Remaining slow scenes: what does not help, and why

- Forced GPU clock (+1-2 fps), `--no-readback_memexport`, triple-buffered pacing (`--present_pacing_frames=2`, kept as an option) and guest `--vsync=false`: no measurable gain in the two slow scenes (venue fly-through, sunflower field).
- RenderDoc on the FSI path: transfer rectangles 55% → 19% of GPU time; geometry now dominates, with many tiny draws (shadow-map passes) taking 0.7-1.5 ms each, i.e. stalls.
- MAILBOX (GPU thread presents) pushes the sunflower scene from GPU 84% / ~40 fps to GPU 99% / ~47-58 fps: under FIFO the SDK paints from the UI thread, behind the game's GPU work. The deciding functions are internal to `librexruntime`, so the next step is building the SDK from source (see `next-steps.md`). User: improvement "almost imperceptible"; session ended here.

## 2026-11-02 (afternoon) — SDK built from source; presentation ruled out

- Built ReXGlue v0.10.0 `librexruntime.so` from source (`scripts/build_sdk.sh`, ~15 min at `-j2`); the executable and the prebuilt xenos plugin need no symbol it lacks, and without the new flags it behaves like the prebuilt one. Patches are versioned in `patches/rexglue-sdk/` (`third_party/` is git-ignored).
- Patch 0001: `--host_present_vsync_from_guest_output_thread` (GPU command thread presents under FIFO). `present_hooks.cpp` defers the pacing paint notification when the paint happens inside `RefreshGuestOutput`, and counts GPU-thread paints. User A/B/C on "Sgt. Pepper": no change (sunflower 40 fps / GPU 84%, fly-through 37 fps / 99%).
- Patch 0002: paint-path wait counters (exported `rex_presenter_take_wait_stats`, shown by `--perf_log`) and `--vulkan_swapchain_min_images`. The GPU thread spends ~75% of its time in `vkAcquireNextImageKHR`; more images fix that in the intro but not in gameplay. User D/E (5/4 images): marginal, "nothing changed".
- `--anisotropic_override=-1/0` (SDK forces 4x by default): no change.
- `scripts/run.sh` takes `RUN_LOG=...` for one log per run (the SDK appends, so earlier A/B/C logs came out cumulative).
- Conclusion: the heavy scenes cost 21-27 ms of GPU per frame on a Vega 6 at top clock; 60 fps there needs less GPU work, not better presentation.

## 2026-11-02 (evening) — RGP on Vega, host render targets back, half-rate venue

- RGP: `scripts/rgp_capture.sh` (RADV `MESA_VK_TRACE=rgp` + trigger file; `--rgp_slow_frames N` in `gpu_capture.cpp` requests a trace when at least a third of the last 30 guest frames were slow). RGP 2.7 shows "no events" for GCN; RGP 1.14.1 (last with GCN support, `build/tools/rgp-1.14.1`) works. The queue-event chunk of the `.rgp` file gives per-submission GPU times without the GUI (light frames 11-16 ms, heavy 28 ms).
- Heavy frame on FSI: one render pass (main scene) 17.4 ms of pixel-shader work; FSI loses early-Z, so overdraw is expensive. Host render targets (`fbo`): windows below 55 fps 42% → 15%, fly-through 37 → 60 fps. `fbo` is the default again.
- User: "never really 60, feels like 40-45". Screen capture at 60 fps: 30-45 distinct frames/s. A per-region difference map showed alternating frames where only the note highway changes: the game renders the venue at half rate (very likely by design; RB2/RB3 Deluxe mods add "60 fps venues").
- New: `scripts/count_distinct_frames.py` (distinct frames per second in a CFR capture).
- User review of the capture: an apparent drop to 20-25 fps at 1:07-1:10 (venue every third frame there). Next session: force the venue to full rate (step 1) and profile the remaining dips on fbo (step 2); see `next-steps.md`.

## 2026-11-02 (night) — Why the venue runs at half rate

- Found the mechanism with the RB3 decomp (`system/rndobj/PostProc.cpp`, `ProcCounter`) and Ghidra (`Rnd::Handle` `proc_lock` → `0x8241BFB0` SetProcAndLock, `0x8241BFC0` ProcCommands): the world/post/char processing is split across frames according to the current post-processor's `emulate_fps` (30 in gameplay). It is a deliberate film-rate effect, not a frame budget.
- Added `--full_rate_world` (`project/src/world_rate_hooks.cpp`, first `[[entrypoint.midasm_hook]]` of the project) and `world_rate:` log lines; `debug_probe.cpp` exports `GuestBaseOrNull()`.
- A/B (user): full rate raises distinct frames on screen 37.4 → 42.7/s but drops the guest (and the note highway) 57 → 43.5 fps, GPU-bound at 97%. Off by default; see KI-14.

## 2026-11-02 (night, later) — GPU command counters; upload coalescing ruled out

- Patch 0003: per-command counters in the Vulkan backend (`gpu cmds:` in `--perf_log`) and `REXGLUE_GPU_PLUGIN_DIR`; `build_sdk.sh` rebuilds the xenos plugin too. Gameplay has ~195 shared-memory uploads per frame, ~106 of them breaking a render pass.
- RGP trace on fbo (user screenshots, `build/rgp/shots3/`): render-target transfers ~18% of a 15.6 ms frame; gaps in wavefront occupancy between barriers.
- Patch 0004: `--shared_memory_upload_coalesce_kb` (off by default). User runs D/E (256/1024 KB): 60-65% fewer barriers and render passes, no glitches, but no fps gain (more bytes copied on the GPU command thread). Kept as an option.

## 2026-11-02 (night, CPU profile) — Dips are GPU-bound; SDK busy-wait fixed

- `scripts/profile.sh` now logs to `build/run-profile.log` (no longer truncates `build/run.log`), drops `--show_fps` (KI-14) and adds `--hitch_log_ms=35`.
- Slow gameplay frames: the GPU command thread is ~19% busy, the main thread waits in `VdSwap`; the remaining dips are GPU work.
- perf showed the SDK `Audio Worker` spinning (43% of CPU samples): `WaitMultiple` truncated sub-millisecond sleeps to 0. Patch 0005 sleeps in microseconds: CPU −43%, package power −4.4 W, no fps change.

## 2026-11-02 (night, per-frame GPU log) — Tooling to see one camera cut

- Patch 0006: timestamp queries around every Vulkan submission and per-frame deltas of the GPU
  counters, plus new counters (guest draws, texture loads/bytes, resolves, EDRAM dumps,
  render-target transfer draws). Exported as `rex_gpu_frame_timing_enable` /
  `rex_gpu_take_frame_records`; `--gpu_frame_csv` writes one row per frame,
  `scripts/analyze_gpu_frames.py` summarises it. Intro check: 5-12 ms of GPU per frame.
- `scripts/build_sdk.sh` now rebuilds patched files from HEAD + all patches in a temporary copy
  and only copies files that differ (per-patch "already applied" checks broke once 0006 touched
  lines next to 0003).
- `present_hooks.cpp`: plugin symbol lookup shared as `GpuPluginSymbol()`.
- Searched online: no Xenia game patch for this title; xenia-edge has recent Vulkan
  render-target/resolve work worth porting if the data points there (see `next-steps.md`).

## 2026-11-02 (late night) — Render-target transfers: the cause of the camera-cut drops

- Per-category GPU timestamps (patch 0006, `--gpu_frame_categories`): in the heavy venue frames
  of "Sgt. Pepper" 43% of GPU time was render-target ownership transfers (~9 ms), not the
  resolve chain (~3 ms). A transfer profile per (mode, formats, MSAA, pitch, EDRAM base) showed
  ~50 kinds, some depth transfers covering all 2048 EDRAM tiles.
- Experiment `--rt_transfer_skip_modes`: skipping all transfers hit 60 fps but corrupted the
  intro logos, crowd and loading screen; skipping depth-only broke lighting, drum kit and crowd.
  The game needs the data, so skipping is not the way.
- `execute_unclipped_draw_vs_on_cpu=true` (upstream Xenia default, ReXGlue default false) bounds
  the EDRAM range of unclipped draws: frames over budget 7.9% → 0.02%, windows < 55 fps
  22% → 6.7% (loading only), no glitches. Now a `scripts/run.sh` default.
- Ghidra was not needed: the cost was in the emulator's EDRAM bookkeeping, not in game content.

## 2026-11-02 (late night, full-rate venue) — Re-measured after fix 4

- User tested other scenes with fix 4: performance held. `--full_rate_world`: 55.4 fps mean
  (was ~43), "much more fluid", still drops: 24% of frames over 16.7 ms of GPU (p90 18.9 ms).
  Stays off by default; it goes into a future in-game "PC settings" section (user's plan).
- Category run at full rate: game draws/post ~10.8 ms of an over-budget frame, transfers 3.3,
  EDRAM dump 1.5, resolve 1.4, texture reload 1.6. Next: port xenia-edge's Vulkan direct
  resolve (removes the dump), then game-side quality options via Ghidra.

## 2026-11-02 (late night) — Vulkan direct host resolve (patch 0007)

- The SDK had `TryResolveCopyDirectly` and pipeline layouts for a direct resolve on Vulkan, but
  the function always fell back to the EDRAM dump ("until source-image direct shaders land").
- Ported xenia-edge's direct resolve shader (`has207/xenia-edge` `aa2efeaf`, `e1ba7952`) into
  the SDK's dump shader generator as a `DumpPipelineKey::direct_resolve` variant, reusing the
  SDK's own per-format packing; eligibility follows edge (fast copies only, no resolution
  scaling, source fully owned by render targets with the resolve's layout).
- Intro A/B: GPU per frame 6.79 → 6.04 ms, EDRAM dumps 5 → 3 per frame. Gameplay validation
  pending. `scripts/build_sdk.sh` and the patch series now go up to 0007.
- Gameplay validation (user, "Sgt. Pepper", `--full_rate_world`): EDRAM dumps 39.6 → 3.8 per
  frame, GPU 15.8 → 14.9 ms per frame, guest 51.6 → 53.6 fps, no glitches. Validated.

## 2026-11-02 (evening) — In-game "PC Settings" and "Quit to Desktop"

- **Decision (user):** the PC options live inside the game's own menus, not in a host ImGui
  overlay. That meant changing the game's DTA scripts.
- **Archive format:** `gen/main_xbox.hdr` is ark version 5, little-endian, Rand2-encrypted
  (seed = first 4 bytes, cipher from the RB3 decomp `math/Rand2.cpp`); file offsets are global
  across the 10 `.ark` parts. `.dtb` scripts use the same cipher and the old DataArray layout
  (`u16 count, u32 line`; `#autorun` is a marker node). `scripts/harmonix.py` reads and writes
  both; all 443 scripts and the header round-trip byte for byte.
- **Menus are data:** `config/gen/main_menu.dtb` holds the main-menu tree (`proj9_menu`),
  button labels come from `ui/locale/*/gen/locale.dtb`, and `main.milo` has 7 buttons (stock
  top level uses 6, Options 5). New entries need no `.milo` editing.
- **Delivery without touching `game/`:** `scripts/build_game_overlay.py` writes
  `build/game_overlay/` (symlinks to `game/`, patched scripts in a new `gen/main_xbox_10.ark`,
  a rewritten header pointing at them); `scripts/run.sh` uses it as `--game_data_root`.
- **Disc Read Error:** any changed script made the game call `XamShowDirtyDiscErrorUI`. The
  executable keeps a SHA-1 of every script (RB3's `gFileChecksums`): 580 entries of
  `{char* path; u8 sha1[20]}` at `0x829F4C48..0x829F82A8` in `.data`. The overlay lists the new
  hashes in `checksums.txt` and `project/src/game_checksums.cpp` writes them into guest memory
  right after the XEX is mapped (KI-16).
- **Script → host bridge:** `DataArray::Execute` (`0x823A7768`) falls through to `0x823A78A4`
  for a command whose head is neither an object nor a registered DataFunc. A midasm hook there
  (`project/src/script_bridge.cpp`) handles `{pc_cfg ...}` and jumps to the epilogue. Registered
  DataFuncs (136 in the core table, `std::map` at `0x82A47CF4`) were mapped along the way.
- **Result (user-validated):** "Quit to Desktop" at the end of the main menu (yes/no dialog,
  closes like Alt+F4) and Options → "PC Settings" with Full-Rate Venue, Fullscreen, Keep Aspect
  Ratio, Internal Resolution 1x-3x (after restart), Texture Filtering Off-16x and Anti-Aliasing
  Off/FXAA/FXAA Extreme. Choices persist in `<user data>/pc_settings.toml`
  (`~/.local/share/beatles/`) and rank below `REX_*` variables and the command line.
- **Bug found while testing:** the first "2x" test was really 1x. `resolution_scale` and
  `anisotropic_override` belong to the GPU plugin, which registers them after startup, and
  `SetFlagByName` on an unknown cvar fails silently. The saved choices are now applied as a
  config file (`pc_settings.cvars.toml` via `rex::cvar::LoadConfig`), whose values the SDK keeps
  for cvars registered later.
- **Anti-aliasing:** anisotropic filtering does not touch geometry edges (the user's jaggies).
  The Vulkan backend already had an FXAA swap pass (`swap_post_effect`, as Xenia recommends for
  games without good AA); patch 0008 makes it switch live. User: 2x internal resolution and FXAA
  both improve the image clearly, but 2x costs a lot of frame rate on the Vega 6.

## 2026-11-02 (night) — Internal resolution measured; scaled direct resolve (patch 0009)

- Web search for similar problems: nothing on Rock Band or Vega iGPUs in Xenia; the useful
  source is the xenia-edge fork, which has commits matching our costs: scaled direct resolve
  (2d0f197, 4eb1115, c8ac925), per-surface resolution scale (74db632), duplicate texture loads
  (40cee11), render targets piling up on EDRAM base changes (fb96381), vblank pacing on a
  deadline (44f5b4a).
- Measured 1x / 2x2 / 2x1, each with FXAA off and on, in "Sgt. Pepper" (table in KI-17). 2x2 is
  ~34 ms of GPU per frame, 2x1 ~18 ms, 1x ~10 ms.
- Found that patch 0007 disabled the direct resolve under scaling (19.6 EDRAM dumps per frame
  at 2x vs 2.9 at 1x). xenia-edge's scaled version writes a group-packed layout; the SDK's
  precompiled scaled resolve/texture load shaders use the older Nx1 sub-unit layout (checked by
  disassembling `resolve_fast_32bpp_1x2xmsaa_scaled_cs`), so patch 0009 reimplements the
  destination addressing for that layout, plus edge's host-pixel tile geometry and half-pixel
  offset fill. Result: dumps 19.6 → 2.8, ~1 ms per frame (2x1: 48.3 → 50.4 fps), no glitches.

## 2026-11-02 (night) — Per-surface resolution scale (patch 0010)

- Ported xenia-edge 74db632 (`draw_resolution_scale_threshold`) to the SDK (Vulkan host render
  targets): `scale_native` render target key bit, native/scaled transfer shaders, 1x resolve
  copies into shared memory (separate unscaled pipelines), dump and direct resolve with native
  sources, per-draw scale in viewport/scissor/point size/polygon offset and PsParamGen /
  memexport (new shader modification bit, pipeline storage version 13), occlusion query
  normalization per query.
- 2x1: threshold 640 gives 57.6 fps (vs 49.7) but a softer image; threshold 320 keeps it sharp
  at 54.1 fps. `--draw_surface_profile` showed why: ~135 draws per frame go to a 640 px 2xMSAA
  surface without depth (2D/screen work), which threshold 640 renders at 1x (KI-17).
- Note: on Vulkan, `RenderTargetCache::BeginFrame` is never called; per-frame hooks there do
  nothing.
- Added `--draw_resolution_scale_threshold_msaa_mask`: threshold 640 with mask 5 (1x and 4x
  MSAA only) keeps the 2xMSAA screen surface scaled: sharp (user) at 56.2 fps, 21% of 2 s
  windows under 55 fps (threshold 320: 54.1 fps, 37%).

