# Next steps

## >>> START HERE: top priority for the next session <<<

**60 fps in the two heavy scenes is a GPU-cost problem, not a presentation problem (KI-14).**
The SDK is now built from source (`scripts/build_sdk.sh` applies `patches/rexglue-sdk/*.patch`
and drops `librexruntime.so` into `build/sdk-lib/`, which `scripts/run.sh` prefers). Tested on
2026-11-02 without gain: GPU-thread presentation under FIFO, 4-5 swapchain images, anisotropic
override off. The sunflower field needs ~21 ms of GPU per frame, the venue fly-through ~27 ms,
on a Vega 6 iGPU already at its 1500 MHz top clock.

Options, by expected payoff:
1. **Find what the GPU time is** with a real profiler: Radeon GPU Profiler traces from RADV
   (`MESA_VK_TRACE=rgp`, RGP GUI from AMD) show per-draw duration, barriers and idle gaps, which
   RenderDoc timestamps could not (small shadow-map draws at 0.7-1.5 ms each on the FSI path look
   like stalls). Then patch the Vulkan command processor / FSI path in the SDK.
2. **Fill the FIFO gap in the sunflower scene** (GPU 84% → ~99%, ~40 → ~47 fps at best): MAILBOX
   with pacing on real display timing (`VK_KHR_present_wait`), so the GPU thread never blocks
   in acquire. Does not help the fly-through (already 99%).
3. Accept the current state: ~59 fps in most of a song, ~37-40 fps in the two heavy scenes.

**How to measure** (every change, on AC power, same song): `RUN_LOG=build/runX.log
scripts/run.sh --perf_log=true --hitch_log_ms=35` (one log per run); `--perf_log` now also
reports paint-path waits (submission/acquire/present) when the patched runtime is loaded.
No `--show_fps`/F3/F4 while measuring. The intro is **not** a valid GPU benchmark. Gameplay runs
need the user; announce automated runs ("don't touch").

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
