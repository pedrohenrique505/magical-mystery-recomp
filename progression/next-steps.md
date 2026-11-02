# Next steps

## >>> START HERE: top priority for the next session <<<

**More GPU headroom (KI-14, last part).** Pacing is solved (`--present_pacing`, FIFO) and the
FSI render-target path removed the EDRAM transfer draws; `scripts/run.sh` enables all three by
default. Songs now average ~59 fps, but the iGPU is still ~85% busy at ~1.4 GHz and saturates on
heavy camera shots and the pre-song venue fly-through (~49 fps), so frames occasionally miss
vsync. Plan, in order:

1. **New RenderDoc capture on the FSI path** of a slow frame, to see the new cost split (the
   user drives the qrenderdoc GUI: Performance Counter Viewer, export CSV; see KI-15 for how to
   capture). Before FSI: 55% transfer rectangles, 23% scene, 15% compute.
2. **Force the GPU clock**: `echo high | sudo tee /sys/class/drm/card1/device/power_dpm_force_performance_level`
   (back with `auto`). The average sclk in songs is ~1.4 GHz out of 1.5; DPM ramps late on
   sudden heavy frames.
3. **CPU power waste** shares the APU power budget (PPT ~35-40 W in songs): the SDK audio worker
   polls `WaitMultiple` at ~80% of a core; the guest main thread spins in the D3D wait loop.
4. Cheap flags still untested in gameplay on FSI: `--no-readback_memexport`,
   `--no-gamma_render_target_as_unorm16` (no effect on the intro on the old path).

**How to measure** (every change, on AC power, same song):
- `scripts/run.sh --perf_log=true --hitch_log_ms=35`: per 2 s guest fps, swap histogram,
  presenter refreshed/painted/new/dropped, pacing timeouts, GPU busy/clock/temperature/PPT.
  Compare song segments with the summary script approach used on 2026-11-02 (mean shown fps,
  share of windows < 55 fps). No `--show_fps`/F3/F4 while measuring.
- `amdgpu_top -J -s 250` in the background records per-block GPU counters (installed).
- The intro is **not** a valid GPU benchmark (a Bink movie; flags that matter in gameplay do
  nothing there). Gameplay runs need the user; announce automated runs ("don't touch").

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
