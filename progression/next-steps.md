# Next steps

## >>> START HERE: top priority for the next session <<<

**Goal: make gameplay feel smooth.** State on 2026-11-02 (KI-14): with host render targets
(default) the guest swaps at ~58 fps, but the game redraws the 3D venue only every other frame
(highway every frame), so the screen shows 30-45 distinct frames/s. In a short stretch the
venue drops to every third frame (capture `build/capI.mp4` 1:07-1:10: 26-30 distinct/s,
repeat runs of 2; user: "looks like 20-25 fps"). The user chose to do both items below.

### Step 1 (done 2026-11-02): venue half-rate rendering — found, `--full_rate_world`; see KI-14. Full rate is GPU-bound on this iGPU (43 fps). Open: an adaptive mode, or GPU savings (step 2) first
- Hypothesis: the engine skips the world/venue draw on alternate frames (RB2/RB3 "Deluxe"
  mods advertise "60 fps venues", so the stock engine runs venues below 60). Confirm before
  patching: it could also be a guest-side frame budget that skips the venue when a frame runs
  late (the 1:07-1:10 stretch with every-third-frame venue hints at an adaptive skip).
- Where to look: RB2 symbol dump `third_party/rb3/doc/rb2_dump/` (class/method names; search
  for the world/venue draw, `TheWorld`, `WorldDir`, `RndDir::DrawShowing`, `Rnd::` frame
  logic, anything with "skip", "every", "rate", "frame" in the world/char/venue classes), the
  RB3 decomp in `third_party/rb3/src`, and Ghidra (MCP needs the GUI open; PyGhidra
  headless otherwise). Compare with the gwarb3dx (RB3 Deluxe) project for how it unlocks
  venue frame rate: https://github.com/gwarb/gwarb3dx
- Runtime check: `--debug_probe` (`project/src/debug_probe.cpp`) can watch a candidate flag or
  frame counter; the heavy/light alternation is visible in `scripts/rgp_capture.sh` traces
  (queue-event GPU times) and in a screen capture.
- Patch from the host app (a guest memory write after boot, like the other hooks), behind a
  cvar so it can be A/B tested. Then measure: guest fps (`--perf_log`) and distinct frames
  (`scripts/count_distinct_frames.py`). Expect the GPU to become the limit (fbo averaged ~80%
  busy at 58 fps with the half-rate venue).

### Step 2: profile the remaining dips on the fbo path (in progress 2026-11-02: counters + RGP done, upload coalescing ruled out; see KI-14. Open: render-target transfers ~18% of GPU time, and CPU time of the GPU command thread in the dips — `scripts/profile.sh`)
- `scripts/rgp_capture.sh 3` while playing the start of "Sgt. Pepper" (the earlier traces were
  all FSI). Open with RGP 1.14.1 (`build/tools/rgp-1.14.1/x/RadeonDeveloperToolSuite-2023-02-15-1051/RadeonGPUProfiler`);
  useful views: Events → Event timing with "Group by pass" + collapsed tree + zoom to fit
  (per-pass durations), Wavefront occupancy (empty stretches = serialization), Overview → Most
  expensive events sorted by Duration. The user drives the GUI and saves screenshots; RGP has
  no text export.
- Known candidate from the FSI traces: small draws serialized by shared-memory uploads
  (barrier → `vkCmdCopyBuffer` → barrier before each draw; `VulkanSharedMemory::UploadRanges`
  and `VulkanCommandProcessor::IssueDraw` in `third_party/rexglue-sdk/src/graphics/vulkan/`).
  Fixes go into `patches/rexglue-sdk/` and are built with `scripts/build_sdk.sh`.
- Also check the host-render-target transfer draws again on fbo (they were 55% of a slow frame
  in an older RenderDoc capture, before FIFO pacing and other fixes).

**How to measure** (every change, on AC power, same song; gameplay runs need the user):
- Guest fps / GPU: `RUN_LOG=build/runX.log scripts/run.sh --perf_log=true --hitch_log_ms=35`
  (summarize the `perf:` lines per 2 s window: shown fps, GPU busy).
- What the screen shows: `gpu-screen-recorder -w eDP-1 -f 60 -fm cfr -k h264 -q ultra -cursor no -s 960x540 -o build/capX.mp4`
  in a second terminal, then `scripts/count_distinct_frames.py build/capX.mp4` in the
  background. A "longest repeat run" of 1 means the venue updates every other frame, 2 every
  third. To see *what* changes, compute the per-region difference map (done ad hoc on
  2026-11-02; worth adding to the script).
- No `--show_fps`/F3/F4 while measuring. The intro is not a valid benchmark.

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
