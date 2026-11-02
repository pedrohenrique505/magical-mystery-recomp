# Known issues

## KI-1 — Functions with unresolved jumps (mostly fixed)
- **Was:** 66 functions PENDING and 115 `REX_FATAL("Unresolved branch ...")` sites in the generated code. Cause: 103 bogus manifest entries (see KI-8) that split switch statements and SEH-protected functions.
- **Now:** 0 unresolved sites; 4 functions still PENDING (not identified yet; `--log-level debug` codegen log). If a new `Unresolved branch` FATAL shows up at runtime, look for a manifest entry inside that function first.

## KI-2 — `update:\gen\patch_xbox.hdr` not found
- **Symptom:** `VFS: 'update:\gen' -> [no device]` and `NtCreateFile FAILED ... 0xc000000f` at the start of every run.
- **State:** has not blocked anything so far; the game continues.
- **Hypothesis:** the game looks for the title-update patch; the TU is in `game/$SystemUpdate/`.
- **See:** `next-steps.md` §3.

## KI-3 — Functions reachable only through vtables without RTTI
- **Symptom:** `[FATAL] Call to invalid or unregistered function at guest address 0x...` at runtime.
- **Cause:** ReXGlue's `VTableScanner` only finds vtables through MSVC RTTI; this game has many classes without RTTI.
- **Current mitigation:** `scripts/find_orphans.py` + `scripts/resolve_orphans.py` (155 functions already declared). New cases may appear in parts of the game not yet exercised.
- **Known limit:** the computed size runs to the first `blr`/`bctr`/`b`, so it is only reliable for thunks (≤ 32 bytes); larger functions are declared without `size` and rely on the SDK's analysis.

## KI-4 — Kernel/audio stubs
- **Symptom:** `__imp__XAudioOverrideSpeakerConfig STUB` in the log.
- **Impact:** unknown; verify that audio output is correct.

## KI-5 — Frame skipped because of `async placeholder draw`
- **Symptom:** `Skipping Vulkan frame presentation due to async placeholder draw usage in this frame` on the first frame.
- **Impact:** unknown; may just be the first frame while shaders load.

## KI-6 — XEXLoaderWV extension built for a different Ghidra version
- **State:** the fork's zip targets Ghidra 12.0.4; it was installed on 12.1.2 with "Install Anyway" and works, but required enabling the **Jython** extension (the loader references `org.python.jline`).
- **Plan B:** build from `third_party/XEXLoaderWV` with Gradle against the installed Ghidra.

## KI-7 — Input race in the SDK's `InputSystem` (fixed in the host app)
- **Symptom:** the intro froze at a random point (sooner when keys were pressed), or the process aborted with `free(): invalid pointer` inside `rex::input::InputSystem::RefreshDevices()`.
- **Cause:** `InputSystem` (prebuilt SDK v0.10.0) has no lock, but every `GetState`/`GetCapabilities`/`SetState` runs `RefreshDevices()`, which rebuilds the shared `devices_` vectors. The game polls `XInputGetState` from its own thread (guest `sub_823E4D20`, every 4 ms, while holding its input critical section) and calls `XInputGetCapabilities`/`XInputSetState` from other threads. Heap corruption either crashed or left the polling thread spinning with the critical section held, blocking the main thread.
- **Fix:** `project/src/xam_input_hooks.cpp` defines `__imp__XamInputGetState`, `__imp__XamInputGetCapabilities` and `__imp__XamInputSetState` in the executable (they take precedence over the `.so`), serializes them with one mutex and forwards to the SDK implementation via `dlsym(RTLD_NEXT)`. If the game starts calling other `XamInput*` imports that reach `InputSystem` (e.g. `GetKeystroke`), add them there too.
- **Upstream:** worth reporting to ReXGlue; remove the workaround once the SDK locks `InputSystem`.

## KI-8 — Bogus manifest entries split functions (fixed)
- **Symptom:** `[FATAL] Unresolved branch from 0x8232E788 to 0x8232E7B8` after skipping the intro.
- **Cause:** the earlier orphan loop declared code that is not a function: 98 switch case bodies (5 switches, e.g. `sub_8232DA28`, a 104-case `lhzx` table) and 5 `__try/__except` resume blocks. Each one cut its parent function short; codegen validation passed, but the parent's internal branches were emitted as `REX_FATAL`.
- **Fix:** removed those 103 entries (manifest: 155 → 52 `orphan_*`). Tooling: `scripts/find_switch_labels.py`, plus the post-codegen check documented in `AGENTS.md`.

## KI-9 — A gamepad only counts as a vocalist (workaround: `--instruments`)
- **Symptom:** starting a song asks to "connect between one to three microphones".
- **Cause:** the game classifies controllers by `XINPUT_CAPABILITIES.SubType` (guest `sub_823E5028`): 6 guitar, 11 bass, 8 drums, 7 and 9 other variants; anything else is a gamepad, which the game treats as a vocalist (the console headset plugs into the pad). The SDK reports every SDL pad as subtype 1.
- **Workaround:** `--instruments` (defined in `project/src/xam_input_hooks.cpp`, default `guitar`) rewrites the subtype per player. The game only reads the subtype plus the wireless flag; no vendor check.
- **Still open:** microphone/vocals (no capture implemented), keyboard input, drum velocity (the game reads it from the thumbsticks for subtype 8).

## KI-10 — Guest memory shm objects leaked into /dev/shm (fixed)
- **Symptom:** `SIGBUS` in the game at random points and, worse, the Hyprland compositor crashing with `SIGBUS` (twice on 2026-11-01, the second forced a reboot). `/dev/shm` was 80% full with 12 `xenia_memory_*` files (2.9 GB of RAM).
- **Cause:** the SDK backs the guest arena with a POSIX shm object and only `shm_unlink`s it in the `Memory` destructor, which crashes, aborts, `timeout` kills and the normal "hard-exiting process" path all skip. A full RAM-backed tmpfs turns page faults on shm mappings into `SIGBUS` for every process using it.
- **Fix:** `project/src/shm_cleanup.cpp` unlinks the object right after startup (`OnPostSetup`), so the kernel frees it when the process exits, however it exits. `scripts/run.sh` also removes leftovers not held by any process.

## KI-11 — Network receive loop spun forever once a party was created (fixed, follow-up pending)
- **Symptom:** after creating a party, the song list screen stayed empty, and the log grew by ~14 MB/s of `__imp__NetDll_WSAGetOverlappedResult STUB` (20 rotated 5 MB logs in 7 s, so the start of the run was lost).
- **Cause:** the game's UDP layer (guest `sub_8293EAA8`/`sub_8293ED70`) issues overlapped `WSARecvFrom` and polls `WSAGetOverlappedResult`. In the SDK the first returns `SOCKET_ERROR` without `WSA_IO_PENDING` and the second is a bare stub that leaves r3 non-zero ("completed"), so the game spun on fake completions.
- **Fix:** `project/src/net_hooks.cpp` models "no packet ever arrives": `WSARecvFrom` → `SOCKET_ERROR` + 997, `WSAGetOverlappedResult` → `FALSE` + 996.
- **Note:** this was not the cause of the empty song list; that was KI-12.

## KI-12 — Song list empty after creating a party: all users shared one GUID (fixed)
- **Symptom:** after creating a party the song select screen showed only the background, forever.
- **Diagnosis** (with `--debug_probe`, see `project/src/debug_probe.cpp`): `SongSyncPanel::IsLoaded` (guest `sub_822E64E0`) waited with `pending_users=3`. `SongSyncPanel::Load` (`sub_822E6D78`) queues every user returned by `BandUserMgr::GetUsers(8)` that is not local; the three pre-allocated *remote* `BandUser` objects passed the "in a band slot" filter (`sub_822609C0` → `BandUserMgr::FindSlot`, `sub_8225A550`, which matches 16-byte user GUIDs). Every GUID comes from `XNetRandom` (`sub_82517470`), and the SDK's `XNetRandom` fills the buffer with a constant `0xBB`, so all users had the same GUID and the remote ones matched the local player's slot. Their song lists never arrive, so the panel never synced.
- **Fix:** `__imp__NetDll_XNetRandom` in `project/src/net_hooks.cpp` returns real random bytes (`getrandom`). Verified by the user: the song list appears and a song (Lucy in the Sky with Diamonds) plays to the end without crashing.
- **Useful addresses** (title 454108B1, XEX 0.0.0.1): `SongSyncPanel::sInstance` `0x82A44F80` (`+72` sent, `+74` synced, `+76/+80` pending users), `TheContentMgr` ptr `0x829FEAB8` (`+40 == 1` ready), `TheBandUserMgr` ptr `0x82A42284` (remote users `+64/+68`, band slot GUIDs `+76`), net object ptr `0x82A47110` (players `+36/+40`, transport `+164`).

## KI-13 — Crackling, "slow-motion" audio during gameplay (fixed)
- **Symptom:** during songs the audio crackled and felt like it played at a fraction of the speed (in sync with the notes); together with uneven video it felt like 10-15 fps, although the guest swapped 60 times per second. Intro and menus sounded fine (the defect was there too, but quiet music hid it).
- **Diagnosis:** `--perf_log` showed 187.5 audio frames/s submitted (real-time; 256 samples at 48 kHz). `--audio_dump` captured the exact frames the guest submitted: no silence, no clipping, normal spectrum, and a screen capture's audio matched the dump with zero drift, so the SDK/SDL output path was faithful. The dump showed that ~14% of 256-sample frames were exact copies of the frame from two frames earlier (a stale buffer resubmitted), i.e. ~26 clicks per second.
- **Cause:** SDL pulled ~4 frames per device callback, so the SDK's audio worker ran the game's XAudio render callback (guest `sub_82651408` → `sub_82648278`, which hands mixing to another guest thread) in bursts, with gaps of up to 20 ms instead of one call every 5.3 ms. The game's mixer could not keep up with back-to-back callbacks and resubmitted its previous buffer.
- **Fix:** `scripts/run.sh` sets `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256`, so SDL pulls one guest frame at a time. Max gap between submits dropped to ~6 ms, stale frames to 0%, and the user confirmed perfect audio. If crackling comes back on another machine, `--audio_maxqframes 16` (SDK default 8) adds headroom.
- **Tooling left in place:** `--show_fps`, `--perf_log` (fps + audio rate/pacing), `--audio_dump` + `scripts/analyze_audio_dump.py`, `--wait_profile` (`project/src/wait_profiler.cpp`), `scripts/profile.sh` (perf + GPU load).

## KI-14 — Gameplay frame pacing and GPU load (fixed: steady 60 fps; full-rate venue still ~54 fps)
- **Symptom (original):** the guest swapped at a steady 60 fps, but a 60 fps screen capture of gameplay had ~30 distinct frames per second, plus drops on camera cuts.
- **Fix 1, presenter (2026-11-01):** the SDK always registers an ImGui `AchievementToastDialog`; any UI drawer makes the presenter paint from the UI thread, behind the game's GPU work, and half the frames were overwritten in the mailbox. `BeatlesApp::CreateAchievementNotificationDialog` returns `nullptr`. `--show_fps` and F3/F4 bring the UI-thread painting back while open; measure with `--perf_log` only.
- **Not a guest problem:** `--hitch_log_ms` (`project/src/hitch_tracer.cpp`) showed that during a song the guest swaps at a clean 60 fps; the main thread (which calls `VdSwap` in gameplay) is CPU-busy, never blocked, during the few stalls (song loading). What the user saw were host-side drops.
- **Cause A, vblank phase (fixed):** the guest paces itself on the SDK's `GPU VSync` thread, a free-running 60 Hz timer (1 ms sleeps) unrelated to the display. With IMMEDIATE the compositor silently drops frames; with FIFO, whenever two guest frames reached the mailbox within one refresh, one was dropped and one repeated (up to 60 of 120 frames per 2 s), and a late GPU frame halved the rate to exactly 30 fps. **Fix 2:** `--present_pacing` (`present_hooks.cpp`): the `VdSwap` hook blocks until the previous frame was painted, so the guest locks onto the real vsync. Song: 0 dropped frames, drops under GPU load become 40-57 fps instead of 30.
- **Cause B, GPU load:** amdgpu counters (`amdgpu_top`) in gameplay: GPU 75-90% busy at ~1.4 GHz, 99% on camera cuts; bound by pixel shading and texture fetch (SPI 70-89%, texture cache 75-96%), not by ROP/memory bandwidth (color block ~38%). A RenderDoc capture of a slow frame (~20 ms GPU per guest frame) showed 55% of GPU time in full-screen rectangle draws (render-target ownership transfers of the host-render-target EDRAM emulation, incl. 176 depth/stencil passes without pixel shading), 23% in actual scene geometry, 15% compute (resolves, texture conversion). **Fix 3:** `--render_target_path_vulkan=fsi` (EDRAM in a storage buffer with fragment shader interlock, supported by RADV on Renoir) removes the transfers. Intro GPU 48% → 41%; song: windows below 55 fps 16% → 6.5%, mean 57.9 → 59.2 fps. No visual glitches reported so far.
- **Defaults:** `scripts/run.sh` exports FIFO, `present_pacing` and FSI as `REX_<CVAR>` environment variables (the command line still overrides them).
- **Battery:** on battery the APU caps the iGPU at ~400-650 MHz (GPU 88% busy in the intro alone). Always measure on AC power.
- **Tested on 2026-11-02 without gain** (song, AC power, compared on the same song sections): forcing the GPU clock to 1500 MHz (`power_dpm_force_performance_level=high`; +1-2 fps, worst windows better, max 72 °C), `--no-readback_memexport` (no change, no glitches), `--present_pacing_frames=2` (triple buffering; no change), `--vsync=false` (guest vblank every 1 ms, pacing still caps at 60; no change). None is a default.
- **Two scenes remain slow:** the pre-song venue fly-through (GPU 99% busy, pure GPU limit, ~37 fps) and the sunflower field with the crowd at the start of "Sgt. Pepper" (~40 fps, guest swaps alternating 16.7/33 ms). The rest of the song runs at ~59 fps.
- **Key finding for the sunflower scene:** with FIFO the GPU stays at only ~84% busy there, even with triple buffering, and the main thread spends ~30 ms per frame waiting in `VdSwap`. With MAILBOX (`REX_VULKAN_ALLOW_PRESENT_MODE_MAILBOX=true REX_PRESENT_PACING=false`) the same scene reaches GPU 98-99% and ~47-58 fps. Reason: with FIFO the SDK paints from the UI thread (`Presenter::GetDesiredPaintModeFromUIThread` returns `kUIThreadOnRequest` whenever the swapchain has implicit vsync), and those paints queue on the same Vulkan queue behind the game's work, leaving the GPU idle between frames. That function and `SetPaintModeFromUIThread` are called directly inside `librexruntime` (no GOT entry), so the host app cannot override them. MAILBOX has no pacing, so it can drop frames silently in the compositor; the user felt it only "slightly" better.
- **RenderDoc on the FSI path** (sunflower scene, slow frame): rectangles fell from 55% to 19% of GPU time; geometry is now 69%. Many small draws (e.g. shadow-map passes, 512x512 R32F; a draw with 32 vertices and 45 pixels) take 0.7-1.5 ms each, so the time is GPU stalls, not shading work; per-draw timestamps overlap, so these numbers are only indicative.
- **SDK rebuilt from source (2026-11-02, `scripts/build_sdk.sh`, `patches/rexglue-sdk/`):** two runtime patches, both off by default. `--host_present_vsync_from_guest_output_thread` lets the GPU command thread present under FIFO (the user's A/B/C runs on the same song: no change, sunflower still 40 fps / GPU 84%). Paint-path wait counters in `--perf_log` showed the GPU command thread blocked in `vkAcquireNextImageKHR` ~1.5 s of every 2 s, even in the intro; `--vulkan_swapchain_min_images=5` removes that in the intro but not in gameplay (still ~1.5 s/2 s at 40 fps), so with FIFO on Wayland the swapchain behaves as double-buffered whatever the image count. 4 or 5 images: windows below 55 fps 42% → 33-36%, felt no different.
- **Not the cause:** forced anisotropic filtering. The SDK defaults to `--anisotropic_override=3` (4x on all eligible textures); `-1` (game's own) and `0` changed nothing in either slow scene.
- **Where this leaves it:** Ryzen 5 4600H, Vega 6 iGPU, always at its 1500 MHz top clock in these scenes. The sunflower field costs ~21 ms of GPU per frame (84% busy at 40 fps), the venue fly-through ~27 ms (99% at 37 fps); 60 fps needs ≤16.7 ms. Better presentation can at most fill the 84% → 99% gap in the sunflower scene (~47 fps, cf. MAILBOX); reaching 60 in either scene means cutting GPU work per frame by 25-40%.
- **RGP profiling (2026-11-02, `scripts/rgp_capture.sh`, RGP 1.14.1 in `build/tools/rgp-1.14.1`):** RGP 2.x only reads RDNA traces ("no events" for this GCN/Vega iGPU); 1.14.1 is the last release with GCN support and reads Mesa 26 traces. Slow stretches alternate light frames (11-16 ms of GPU work) with heavy ones (28 ms); in a heavy frame on the FSI path one render pass (the main scene) took 17.4 ms of pixel-shader work. FSI does depth/blend in the shader without early-Z, so overdraw (crowd, sunflower field) costs every fragment. Small draws in the light frames are also serialized by shared-memory uploads (barrier + `vkCmdCopyBuffer` per draw, `VulkanSharedMemory::UploadRanges`).
- **Host render targets beat FSI in the heavy scenes:** `--render_target_path_vulkan=fbo` on the same song: windows below 55 fps 42% → 15%, mean 52.6 → 58.3, fly-through 37 → 60 fps, no window with GPU ≥ 97%. `fbo` is the default in `scripts/run.sh` again (it was FSI since the earlier, different-section comparison).
- **The venue is drawn at half rate (game design, not emulation):** a 60 fps screen capture (`gpu-screen-recorder -fm cfr`, `scripts/count_distinct_frames.py`) of gameplay with the guest at ~58 fps shows 30-45 distinct frames/s, alternating full-frame changes with frames where only the note highway changes. That matches the heavy/light GPU frames: the game re-renders the 3D venue every other frame and the highway every frame. Mods for RB2/RB3 ("Deluxe") advertise 60 fps venues, which suggests the stock engine runs venues below 60 too. This is why gameplay "feels" like 40-45 fps even at a steady 60 swaps.
- **Worse stretch:** capture `build/capI.mp4` 1:07-1:10 shows the venue updating only every third frame (26-30 distinct frames/s); the user saw it as a drop to 20-25 fps. Not yet tied to a guest/GPU cause.
- **Half-rate venue explained (2026-11-02):** the engine emulates a film frame rate for the world. `ProcCounter::ProcCommands` (guest `0x8241BFC0`, counter at `TheRnd+0x118`) tells `Rnd::BeginDrawing` what to process each frame (1 world, 2 post, 4 chars, 7 all); here it is adaptive, with a cycle of `round(120 / emulate_fps)` half-frames, where `emulate_fps` is `RndPostProc::sCurrent+0x158` (`sCurrent` pointer at `0x82ACC198`). Gameplay uses 30.0 (venue every other frame; ~24 would alternate 2/3-frame cycles); menus and the intro use 0 (full rate). Values from about 34 up give full rate, so there is no intermediate setting. The cycle counts rendered frames, not time (only the constant `emulate_fps` is read, never the measured rate), so when the game drops frames the venue drops proportionally: 45 fps in gameplay means a ~22.5 fps venue, and a late frame that was due to bring a new venue image holds the old one for 3 vsyncs. World animation still advances by accumulated real time (`WorldDir::AccumulateDeltas`/`ExtendDeltas` in the RB3 decomp), so motion gets coarser, not slower. `--full_rate_world` (`project/src/world_rate_hooks.cpp`, a `[[entrypoint.midasm_hook]]`) returns 7 every frame; every change of `emulate_fps` is logged as `world_rate:`.
- **A/B on "Sgt. Pepper" (user, AC power):** original: guest 57.0 fps mean, 26% of windows < 55, GPU 84%, 37.4 distinct frames/s on screen. `--full_rate_world`: guest 43.5 fps, 94% of windows < 55, GPU 97% (≥ 95% in 96% of windows), 42.7 distinct frames/s. Full rate shows more distinct frames but drags the note highway down to ~43 fps; the GPU cannot draw the venue every frame on this iGPU. Off by default. User: both felt very similar; full rate looked smoother in light scenes but does not hold through the song.
- **GPU command counters (2026-11-02, patch 0003):** `scripts/build_sdk.sh` now also rebuilds the xenos plugin (the Vulkan backend lives there, not in `librexruntime`); `REXGLUE_GPU_PLUGIN_DIR` (set by `scripts/run.sh`) loads it from `build/sdk-lib`. `--perf_log` adds `gpu cmds:` totals per 2 s. Gameplay, per frame: ~385 draws, ~154 render passes, ~416 barriers, ~195 shared-memory uploads (2.9 MB), ~106 of which end an open render pass (each upload = barrier + `vkCmdCopyBuffer` + barrier). Intro: 136 draws, 3 uploads.
- **RGP on fbo (frame 3633, ~15.6 ms GPU, screenshots in `build/rgp/shots3/`):** render-target transfer draws (`vkCmdDraw(6,1,0,0)` from `render_target_cache.cpp`) take ~2.9 ms of real work (~18%); the game's full-screen post passes at 1280x720 are the other big items (one 1.26 ms); wavefront occupancy has near-empty stretches of ~1 ms between barriers.
- **Upload coalescing does not help (patch 0004, `--shared_memory_upload_coalesce_kb`, default 0):** extending each upload into adjacent CPU-written pages cut uploads 195 → 28/15 per frame, render passes 154 → 84/76 and barriers 416 → 184/160 (256/1024 KB), with no glitches, but fps did not improve (mean 59.1 → 58.2/57.7, windows < 55 fps 10% → 15%) because the uploaded bytes grew 2.9 → 11.6/22 MB per frame (memcpy on the GPU command thread). In the slow windows the GPU is 85-93% busy, not saturated. Conclusion: the barrier count is not the limiter.
- **CPU profile of the dips (2026-11-02, `scripts/profile.sh`):** not CPU-bound. In slow gameplay frames the GPU command thread uses ~19% of the frame (p90 22%) and the main thread waits ~29 ms in `VdSwap` (presentation pacing, i.e. the GPU). Most `--hitch_log_ms` hits were song loading (`NtReadFile`).
- **SDK busy-wait fixed (patch 0005):** `PosixConditionBase::WaitMultiple` slept `min(remaining, 1 ms)` with millisecond truncation, so any remainder under 1 ms became `sleep_for(0)` and it spun until the deadline; alertable waits are sliced into 1 ms waits, so every alertable multi-object wait (the SDK's `Audio Worker`, guest `KeWaitForMultipleObjects`) busy-waited. The Audio Worker was 43% of all CPU samples in gameplay. With microsecond sleeps: user-space CPU samples per second −43%, package power 29.9 → 25.5 W at the same GPU clock; fps unchanged (GPU-bound), audio rate unchanged in the intro; user confirmed gameplay audio sounds normal.
- **Per-frame GPU timing (2026-11-02, patch 0006, `--gpu_frame_csv`, `--gpu_frame_categories`):** timestamp queries per submission and per kind of work. "Sgt. Pepper": every frame pays ~10.5 ms even when only the note highway is redrawn (19 resolves, 19 EDRAM dumps, 20 texture reloads of 18.7 MB: the post-processing chain); venue frames 15.5 ms mean, 21.7 ms in the sunflower/crowd shots. Split of a venue frame over 20 ms: render-target transfers 9.3 ms, game draws/post 8.8 ms, dump+resolve+texture reload 3.2 ms. The resolve chain is not the main cost (porting xenia-edge's direct resolve would save ~1 ms).
- **Transfer profile** (log lines `transfer profile:`, every 600 frames with `--gpu_frame_categories`): ~50 kinds per window; the game reuses the same EDRAM bases (0, 608, 1328) with 1x/2x/4x MSAA, different pitches, and as both depth and color within a frame; several depth transfers covered ~2048 tiles (all of EDRAM).
- **Skipping transfers is not an option:** `--rt_transfer_skip_modes` (patch 0006, debug, default 0) brought frames over budget from 7.9% to 0.1%, but skipping all of them stacks the intro logos, turns the crowd impostors into garbage and half-blacks the loading screen; skipping only depth destinations (mask 1013) keeps the intro right but blacks out the band's lighting, the drum kit and the crowd. Both kinds carry data the game uses.
- **Fix 4: `execute_unclipped_draw_vs_on_cpu=true`** (Xenia's upstream default; ReXGlue defaults it to false). Unclipped draws (clears, full-screen quads) no longer count as touching the whole EDRAM, so ownership transfers only copy the range actually drawn. "Sgt. Pepper", AC power: frames over 16.7 ms of GPU work 7.9% → 0.02%, venue frames 15.5 → 13.1 ms (p90 21.0 → 15.5), transfers 5.0 → 2.8 ms (venue) and 3.1 → 1.4 ms (highway), guest 56.4 → 58.2 fps mean, 2 s windows below 55 fps 22% → 6.7% (all in the first seconds of loading). No visual glitches (user). Default in `scripts/run.sh`.
- **Fix 5: Vulkan direct host resolve (patch 0007).** The SDK's `direct_host_resolve` was a stub on Vulkan; the port of xenia-edge's direct resolve shader skips the EDRAM dump for ~90% of resolves in gameplay. Full-rate venue: GPU per frame 15.8 → 14.9 ms, guest 51.6 → 53.6 fps; no glitches (user). Default mode keeps its 60 fps with more headroom.

## KI-15 — The SDK injects RenderDoc into every run once it is installed (fixed in the host app)
- **Symptom:** after installing the `renderdoc` package, the game played audio but no window appeared, at ~18 fps, and the log said `RenderDoc API initialized` without RenderDoc being requested.
- **Cause:** `rex::ui::RenderDocAPI::CreateIfConnected()` "detects" RenderDoc with `dlopen("librenderdoc.so")` without `RTLD_NOLOAD`, which loads it. RenderDoc does not handle Wayland windows, so nothing is presented.
- **Fix:** `project/src/renderdoc_guard.cpp` overrides the function (exported, reached through the GOT) and only forwards when the library is already loaded.
- **Capturing on purpose:** `SDL_VIDEODRIVER=x11 ENABLE_VULKAN_RENDERDOC_CAPTURE=1 scripts/run.sh --capture_slow_frames=N` (`project/src/gpu_capture.cpp`; XWayland is required for RenderDoc to see the window). Replaying a gameplay capture saturates the iGPU and makes the desktop unusable for minutes: let the user drive the qrenderdoc GUI (Performance Counter Viewer → GPU Duration, PS/CS Invocations, Rasterized Primitives → export CSV) instead of headless replays.

## KI-16 — Modified game scripts trigger "Disc Read Error" (fixed in the host app)
- **Symptom:** with any changed `.dtb` in the archive the game stops at the boot logo with the
  SDK's "Disc Read Error" dialog (`XamShowDirtyDiscErrorUI called!` in the log); the same files
  relocated unchanged load fine.
- **Cause:** the executable checks each script against a built-in SHA-1 table (RB3 decomp:
  `gFileChecksums`, `system/math/FileChecksum.h`): 580 entries of `{char* path; u8 sha1[20]}` at
  `0x829F4C48..0x829F82A8` (`.data`), paths like `./config/gen/main_menu.dtb` and
  `../../system/run/config/gen/...`. The hash covers the encrypted file as stored.
- **Fix:** `scripts/build_game_overlay.py` writes `build/game_overlay/checksums.txt` and
  `project/src/game_checksums.cpp` (`OnPostLoadXexImage`) replaces those entries in guest memory
  before the game runs. A path missing from the table is logged as a warning.

## KI-17 — Higher internal resolution is expensive (measured: 2x2 cannot hold 60 on the Vega 6)
- **Symptom:** "Internal Resolution 2x" (PC Settings, `resolution_scale`) clearly sharpens the
  image but drops the frame rate far below 60 on the Vega 6. At boot the SDK warns
  `draw resolution scaling is enabled, but the swap source is unscaled (1280x720)`; it logs once,
  on the first swap (the Bink intro logos), so it does not prove that gameplay is unscaled
  (FXAA costing more at 2x than at 1x suggests the swap source is scaled in gameplay).
- **Measured (2026-11-02, "Sgt. Pepper", AC power, `--gpu_frame_csv --gpu_frame_categories`,
  means per frame from 5 s on; `build/frames_<cfg>.csv`):**

  | config | guest fps | GPU busy ms (p90) | dump | resolve | texload | transfer | other | EDRAM dumps |
  |---|---|---|---|---|---|---|---|---|
  | 1x | 58.9 | 9.8 (13.0) | 0.44 | 0.85 | 0.81 | 1.87 | 5.7 | 2.9 |
  | 1x + FXAA extreme | 59.1 | 10.4 (13.5) | 0.46 | 0.84 | 0.80 | 1.80 | 6.4 | 2.9 |
  | 2x2 | 28.0 | 33.9 (42.7) | 3.62 | 3.47 | 4.72 | 6.30 | 15.7 | 19.6 |
  | 2x2 + patch 0009 | 28.7 | 32.7 (41.0) | 1.64 | 3.32 | 4.64 | 6.27 | 16.7 | 2.8 |
  | 2x1 (`draw_resolution_scale_x=2`) | 48.3 | 18.4 (23.4) | 1.80 | 1.66 | 2.26 | 3.28 | 9.2 | 21.2 |
  | 2x1 + patch 0009 | 50.4 | 17.4 (21.9) | 0.84 | 1.65 | 2.28 | 3.29 | 9.2 | 2.9 |

  FXAA costs ~0.6 ms at 1x and 1-2.5 ms at 2x. Texture loads grow with the pixel count
  (15 → 29 → 57 MB per frame: the post-processing chain reloads its resolved textures every
  frame). "other" (the game's draws) alone is ~16 ms at 2x2, so 2x2 at 60 fps is out of reach
  on this iGPU whatever the emulation overhead; 2x1 needs ~4-5 ms less per frame.
- **Patch 0009 (`--direct_host_resolve_scaled`, on):** patch 0007 fell back to the EDRAM dump
  for every resolve with scaling. It now writes the scaled resolve buffer directly, in the
  SDK's Nx1 sub-unit scaled layout (`XeTextureScaledTiledOffset`; the SDK's precompiled
  shaders predate xenia-canary 0f23f0568, so xenia-edge's group-packed scaled direct resolve,
  2d0f197, could not be ported as is). EDRAM dumps 19.6 → 2.8 per frame, about −1 ms of GPU per
  frame; no glitches (user, 2x2 and 2x1).
- **Patch 0010 (`--draw_resolution_scale_threshold=N`, default 0 = off):** per-surface scale
  after xenia-edge 74db632 (surfaces whose tile-aligned pitch is ≤ N pixels render at 1x;
  native/scaled transfers, 1x resolves into shared memory, per-draw viewport/scissor/point
  size/polygon offset/PsParamGen, occlusion counts normalized per query). Ported to the SDK's
  Vulkan host-render-target path only. `--draw_surface_profile` logs draws per surface every
  10 s. Results in "Sgt. Pepper" at 2x1 (perf-log windows of 2 s):

  | 2x1 | mean fps | windows < 55 fps | GPU ms/frame (p90) | sharpness (user) |
  |---|---|---|---|---|
  | no threshold (patch 0009) | 49.7 | 71% | 17.4 (21.9) | sharp |
  | threshold 640 | 57.6 | 13% | 12.8 (16.7) | softer |
  | threshold 320 | 54.1 | 37% | 15.9 (18.7) | sharp |
  | threshold 640, `--draw_resolution_scale_threshold_msaa_mask=5` | 56.2 | 21% | 15.0 (17.9) | sharp |

  2x2 with threshold 640: 28.8 → 48.3 fps. Surfaces per frame in gameplay: 1280 px 1x with
  depth ~150 draws (the 3D scene), **640 px 2xMSAA without depth ~135 draws** (2D/screen work,
  probably the note highway, HUD or crowd impostors), 640 px 4xMSAA ~33 (full-screen post
  passes), 560 px 1x with depth 7-20 (shadow map or reflection), ≤320 px ~40 (bloom chain,
  shadows). Threshold 640 makes the 640 px 2xMSAA surface native, which is both where most of
  the gain comes from and why the image is softer. `--draw_resolution_scale_threshold_msaa_mask`
  (bit 0 1x, bit 1 2x, bit 2 4x; default 7) limits the threshold to some MSAA modes: mask 5
  keeps the 640 px 2xMSAA surface scaled and the 640 px 4xMSAA post passes, the 560 px surface
  and everything ≤320 px native - the best sharp 2x1 so far (56.2 fps), still short of a
  stable 60.
