# Next steps

## >>> START HERE: top priority for the next session <<<

**Epic: stable 60 fps in gameplay.** State on 2026-11-02 (KI-14): songs average ~58-59 fps,
GPU-bound (Vega 6 iGPU at its 1500 MHz top clock). Drops are now rarer but still visible, and
the user sees the biggest ones **on camera cuts** (e.g. "Sgt. Pepper", the bandstand shot with
the crowd and sunflower field behind the band). What is already known about them:
- They are isolated ~34-36 ms frames (a frame that missed the 16.7 ms budget and one vsync),
  typically 2-6 per 2 s window, i.e. the GPU frame time sits close to 16.7 ms and heavier views
  push it over.
- Not CPU: the GPU command thread uses ~19% of a slow frame; the main thread waits in `VdSwap`.
- Not pipeline compilation: no pipeline is created during gameplay (all at load time).
- Slow windows coincide with more texture uploads (~25 per frame) — possibly texture
  loading/conversion on camera cuts (new LODs/streamed textures), worth confirming.
- Not barriers/uploads: coalescing cut barriers by 60% with no gain (patch 0004).
- The venue is drawn at half rate on purpose (`emulate_fps` 30); `--full_rate_world` exists
  but is GPU-bound at ~43 fps.

### Step 1: catch a camera-cut frame and see what is different
- Add per-frame (not per-2 s) logging around slow frames: extend `--hitch_log_ms`
  (`project/src/hitch_tracer.cpp`) to print the GPU command counters of the slow frame and of
  the frames before it (needs a per-frame snapshot of `rex_gpu_take_command_stats`, patch 0003;
  add counters for texture-cache work: textures loaded/converted, resolves, render-target
  transfers — `render_target_cache.cpp` transfer draws, `texture_cache` loads).
- Capture an RGP trace exactly on a camera cut: `scripts/rgp_capture.sh` with a trigger on a
  single slow frame (`--rgp_slow_frames` currently needs 10 of 30 slow; add a
  "single frame > N ms" mode in `project/src/gpu_capture.cpp`). Note an RGP trace is of the
  *next* frame; with a cut the following frames are usually still heavy.
- Compare that frame with a normal one: render-target transfer time (~18% of a normal frame,
  `vkCmdDraw(6,1,0,0)`), texture uploads/compute conversion (`vkCmdDispatch`), post passes.

### Step 2: cut the GPU work that is avoidable
- Render-target transfers of the host-render-target path (~2.9 ms of a 15.6 ms frame): find
  which EDRAM ranges/formats force them (log in `VulkanRenderTargetCache` which source/dest
  formats and sizes are transferred per frame) and whether a cheaper path exists (e.g. skip
  depth transfers that are cleared right after, `--depth_transfer_not_equal_test`, keeping
  formats compatible). Patches go in `patches/rexglue-sdk/`, built by `scripts/build_sdk.sh`
  (it now rebuilds the xenos plugin too).
- Texture uploads on camera cuts: if confirmed, look at pre-warming/caching in the texture
  cache, or spreading uploads over frames.
- Resolution scale is a last-resort lever (the game renders 1280x720; post passes are full
  screen).

**How to measure** (every change, on AC power, same song; gameplay runs need the user):
`RUN_LOG=build/runX.log scripts/run.sh --perf_log=true --hitch_log_ms=35`; summarize per 2 s
window (fps, windows < 55, GPU busy, `gpu cmds`). Screen captures and
`scripts/count_distinct_frames.py` for what the user sees. No overlays while measuring.

### Done on 2026-11-02 (see KI-14 and log.md)
- Venue half rate explained (`emulate_fps`), `--full_rate_world` added (off by default).
- GPU command counters in `--perf_log` (patch 0003), RGP traces on the fbo path.
- Shared-memory upload coalescing tried (patch 0004, off by default): no gain.
- CPU profile: dips are GPU-bound; SDK busy-wait fixed (patch 0005, −43% CPU, −4 W).

---

Suggested order, from cheapest/most informative to most expensive.

## 1. Validate what runs (immediate)
- [x] Run `scripts/run.sh` and record what appears. Result: the intro animated movie plays on screen.
- [x] Fix the random intro freeze/crash (KI-7, input race in the SDK).
- [ ] Record whether the intro has sound, whether it can be skipped, and what happens when it ends (title screen, black screen, crash). If it crashes, note the last `[FATAL]` line from `build/run.log`.

- [x] Song list after creating a party (KI-12, `XNetRandom`). A full song plays.
- [x] Gameplay audio crackling / slow-motion feel: SDL pulled 4 frames per callback, fixed with `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256` (KI-13).
- [x] Gameplay showed ~30 distinct fps: the presenter dropped half the frames because the SDK's achievement toast forced UI-thread painting (KI-14).
- [x] Gameplay frame pacing (KI-14): `--present_pacing` + FIFO, 0 dropped frames.
- [x] GPU: FSI render-target path removes EDRAM transfer draws (KI-14).
- [ ] **More GPU headroom (top priority, KI-14):** see "START HERE" at the top of this file.
- [ ] Remove or gate the `--debug_probe` watch list once it is no longer needed (it only reads memory and is off by default).

## 2. Stability of the recompiled code
- [x] Remove bogus orphan entries that split switches/SEH functions (KI-8): 115 unresolved sites → 0.
- [ ] Identify the 4 functions still PENDING and add `[[switch_tables]]` if needed.
- [ ] Re-check the 52 remaining `orphan_*` entries for SEH resume blocks (harmless today, but dead).
- [ ] Run longer scenarios (menus, loading a song) and repeat the missing-function loop (`scripts/resolve_orphans.py`) whenever `unregistered function` shows up.
- [ ] Decide whether to generate a complete vtable report to anticipate missing functions instead of waiting for each crash.

## 3. Title update
- [ ] The game looks for `update:\gen\patch_xbox.hdr`. Investigate whether the TU in `game/$SystemUpdate/su20076000_00000000` is an XEXP that should be applied to `default.xex` (XEXLoaderWV supports XEXP) and whether it changes the code.
- [ ] If so, recompile from the patched XEX and rebuild the manifest.

## 4. Audio and video
- [ ] Confirm XMA decoding (music and SFX) and latency.
- [ ] Check the `Skipping Vulkan frame presentation due to async placeholder draw usage` message and whether it affects the picture.
- [ ] Bink video playback: the intro movie already shows; verify audio sync and the other movies in the game (`BINK*` blocks in the XEX).

## 5. Instruments (the distinguishing feature of this game)
- [x] Let a gamepad act as guitar/bass/drums (`--instruments`, KI-9).
- [ ] Map how the game reads guitar/bass/drums/microphone (USB HID through XInput / `XamInput*` / `XUsb*`?) and what the ReXGlue runtime already exposes.
- [ ] Keyboard mapping: right now only a real gamepad reaches the game (check the SDK's `mnk` input driver / `input_backend`).
- [ ] Microphone capture for vocals (3-part harmonies).

## 6. Persistence and extras
- [ ] Saves (XContent/STFS), profile and achievements.
- [ ] DLC / extra songs (STFS under `Content/`).

## 7. Project quality
- [ ] Push the repository to GitHub and pick a license.
- [ ] Document the manifest quirks discovered so far (see `AGENTS.md`) where other ReXGlue users can find them.
- [ ] Consider contributing fixes or reports back to ReXGlue (vtables without RTTI).
