# Next steps

## >>> START HERE: top priority for the next session <<<

**Epic "stable 60 fps with the PC Settings quality options".** The in-game PC Settings menu is
done (2026-11-02 evening, see log.md): 2x internal resolution and FXAA visibly improve the image
(user), but 2x drops the frame rate far below 60 on the Vega 6 (KI-17), and the full-rate venue
still runs at ~54 fps. Goal: keep 60 fps with the best quality this iGPU can hold, and make the
expensive options cheaper.
1. **Measure first** (nothing is measured yet at 2x): `--perf_log` + `--gpu_frame_csv
   [--gpu_frame_categories]` in "Sgt. Pepper" at 1x, 2x, and non-square
   `draw_resolution_scale_x=2`/`_y=1` (half the cost of 2x2 per the Xenia FXAA/FSR post), each
   with FXAA off/on. Summarise with `scripts/analyze_gpu_frames.py`; compare transfer and resolve
   costs, which should scale with the pixel count.
2. **Check what 2x scales:** the boot warning says the swap source is unscaled (logged once, on
   the Bink logos). Log per-frame swap sizes in gameplay to confirm the final image is scaled.
3. **Then optimise** where the numbers point: render-target transfers/resolves at scale
   (KI-14 tooling), the full-rate venue (game-side quality passes via Ghidra and midasm hooks),
   and consider a 1x2/2x1 "Internal Resolution" choice in the menu if it holds 60.
4. **Menu polish (small):** localized strings for the other languages; maybe a "restart now"
   action for restart-only settings.

**How to measure:** on AC power (check `/sys/class/power_supply/*/status`; on battery the iGPU
runs at 400 MHz), `RUN_LOG=build/runX.log scripts/run.sh --perf_log=true
--gpu_frame_csv=build/frames.csv [--gpu_frame_categories=true]`, then
`scripts/analyze_gpu_frames.py build/frames.csv --from 5`. Gameplay runs need the user. The PC
Settings choices in `~/.local/share/beatles/pc_settings.toml` apply to every run; the command
line overrides them.

### Done on 2026-11-02 evening (see log.md, KI-16, KI-17)
- Harmonix archive/DTA tooling (`scripts/harmonix.py`) and the game-data overlay
  (`scripts/build_game_overlay.py`, `project/mods/`), with the script SHA-1 table patched at boot.
- `{pc_cfg ...}` script → host bridge (midasm hook in `DataArray::Execute`).
- Main menu "Quit to Desktop"; Options → "PC Settings" (6 options, saved in `pc_settings.toml`).
- Patch 0008: FXAA (`swap_post_effect`) switchable at runtime.

### Done on 2026-11-02 (see KI-14 and log.md)
- Vulkan direct host resolve ported from xenia-edge (patch 0007), validated in gameplay.
- Per-frame GPU timing and per-category/transfer profiling (patch 0006).
- Render-target transfers identified as the main variable cost; `execute_unclipped_draw_vs_on_cpu`
  enabled.
- Venue half rate explained (`emulate_fps`), `--full_rate_world` added (off by default).
- GPU command counters in `--perf_log` (patch 0003), RGP traces on the fbo path.
- Shared-memory upload coalescing tried (patch 0004, off by default): no gain.
- CPU profile: dips are GPU-bound; SDK busy-wait fixed (patch 0005, −43% CPU, −4 W).

---

Suggested order, from cheapest/most informative to most expensive.

## 1. Validate what runs (immediate)
- [x] Run `scripts/run.sh` and record what appears. Result: the intro animated movie plays on screen.
- [x] Fix the random intro freeze/crash (KI-7, input race in the SDK).
- [x] The intro can be skipped (A) and leads to the title screen and menus.
- [ ] Confirm the intro movie has sound in sync (gameplay audio is confirmed, KI-13).

- [x] Song list after creating a party (KI-12, `XNetRandom`). A full song plays.
- [x] Gameplay audio crackling / slow-motion feel: SDL pulled 4 frames per callback, fixed with `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256` (KI-13).
- [x] Gameplay showed ~30 distinct fps: the presenter dropped half the frames because the SDK's achievement toast forced UI-thread painting (KI-14).
- [x] Gameplay frame pacing (KI-14): `--present_pacing` + FIFO, 0 dropped frames.
- [x] GPU: FSI render-target path tried (removes transfer draws but no early-Z; slower in heavy scenes, `fbo` is the default again, KI-14).
- [x] GPU headroom for 60 fps in gameplay (KI-14 fixes 4-5: `execute_unclipped_draw_vs_on_cpu`, Vulkan direct resolve).
- [ ] Full-rate venue (`--full_rate_world`) and 2x internal resolution at 60: see "START HERE".
- [x] In-game PC Settings menu and Quit to Desktop (log.md, 2026-11-02 evening).
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
- [x] XMA decoding: music and SFX play cleanly in songs (KI-13).
- [ ] Measure audio latency against the note highway (calibration).
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
- [x] Push the repository to GitHub (`pedrohenrique505/magical-mystery-recomp`).
- [ ] Pick a license (there is no LICENSE file yet).
- [ ] Document the manifest quirks discovered so far (see `AGENTS.md`) where other ReXGlue users can find them.
- [ ] Consider contributing fixes or reports back to ReXGlue: vtables without RTTI, the busy-waiting `WaitMultiple` (patch 0005), `execute_unclipped_draw_vs_on_cpu` defaulting to false (KI-14), and the Vulkan direct resolve stub (patch 0007).
