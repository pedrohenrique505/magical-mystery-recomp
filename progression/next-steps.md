# Next steps

## >>> START HERE: top priority for the next session <<<

**Epic "stable 60 fps in gameplay": done (2026-11-02, KI-14).** `execute_unclipped_draw_vs_on_cpu`
(default in `scripts/run.sh`) removed nearly all over-budget GPU frames ("Sgt. Pepper": 7.9% →
0.02%; the user confirmed other scenes), and the Vulkan direct resolve (patch 0007) added
headroom. Candidates for the next session, pick with the user:
- **Full-rate venue at 60** (`--full_rate_world`, now ~54 fps). Over-budget frames ~18.9 ms:
  game draws/post ~8.8-10.8 ms, transfers ~3 ms, texture reloads after resolves ~1.3-1.6 ms.
  The remaining levers are game-side quality options (MSAA level, blur/bloom/depth-of-field
  passes): find where the engine sets them up with Ghidra and expose them through
  `[[entrypoint.midasm_hook]]`s. Transfers are real data (skipping them corrupts the image).
- **In-game "PC settings" section** (user's plan): a home for `--full_rate_world` and future
  quality options; enhancements stay off by default.
- **Real instruments** (section 5 below): USB guitar/drums/microphone.
- If a new scene drops frames: `--gpu_frame_csv` + `--gpu_frame_categories`, then
  `scripts/analyze_gpu_frames.py` and the `transfer profile:` log lines.

**How to measure:** on AC power (check `/sys/class/power_supply/*/status`; on battery the iGPU
runs at 400 MHz), `RUN_LOG=build/runX.log scripts/run.sh --perf_log=true
--gpu_frame_csv=build/frames.csv [--gpu_frame_categories=true]`, then
`scripts/analyze_gpu_frames.py build/frames.csv --from 5`. Gameplay runs need the user.

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
- [ ] Full-rate venue (`--full_rate_world`) at 60: see "START HERE".
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
