# Next steps

## >>> START HERE: top priority for the next session <<<

**Epic: stable 60 fps in gameplay — main cause fixed on 2026-11-02 (KI-14, fix 4).**
`execute_unclipped_draw_vs_on_cpu=true` (default in `scripts/run.sh`) removed nearly all
over-budget GPU frames in "Sgt. Pepper" (7.9% → 0.02%), no glitches. What is left:
- **Confirm on other songs/venues** (Cavern, Shea Stadium, Abbey Road dreamscapes, the pre-song
  venue fly-through) with `--gpu_frame_csv` + `scripts/analyze_gpu_frames.py`; look for frames
  with `busy_ms` > 16.7 and check the `transfer profile:` log lines (`--gpu_frame_categories`).
- **Headroom left per frame** (Sgt. Pepper, venue frames 13.1 ms): transfers 2.8 ms, dump +
  resolve + texture reload ~2.5 ms, game draws/post ~7.5 ms. If another scene is over budget:
  - xenia-edge (`has207/xenia-edge`) `aa2efeaf` "Resolve host render targets directly into
    shared memory" would remove the EDRAM dump before each resolve (~1 ms/frame here); check
    first what the SDK's existing `direct_host_resolve` cvar already covers.
  - Remaining transfers are real data (skipping them breaks the image, KI-14); a finer
    approach would need to know which EDRAM ranges the game overwrites before reading
    (Ghidra on the engine's surface setup) — only if still needed.
- `--full_rate_world` (venue every frame), re-measured after fix 4: 55.4 fps mean (was ~43),
  user: "much more fluid", but 24% of frames exceed 16.7 ms of GPU (p90 18.9 ms); heavy frames
  have 2.5x the resolve chain (55 resolves + EDRAM dumps, 31 MB texture reloads). Stays **off by
  default**: it belongs in the future in-game "PC settings" section (user's plan). Candidate fix:
  port xenia-edge's Vulkan direct resolve (`97ed99bb`, `aa2efeaf`, `e1ba7952`; the SDK's
  `direct_host_resolve` exists only for D3D12).
  Category run at full rate (timestamps add ~1.3 ms/frame, so compare proportions): over-budget
  frames 18.9 ms = game draws/post 10.8 + transfers 3.3 + EDRAM dump 1.5 + resolve 1.4 +
  texture reload 1.6 + uploads 0.3; real overshoot ~2 ms. Direct resolve removes the dump
  (~1.5 ms; 38% of over-budget frames would fit even with the overhead) — worth porting but not
  enough alone. The rest is game content (geometry + post at 4x MSAA): game-side quality options
  (MSAA, blur/bloom passes; find them with Ghidra) for the PC settings section.
- **Direct resolve ported (patch 0007, 2026-11-02), needs gameplay validation:** the SDK's
  `direct_host_resolve` was a stub on Vulkan; it now reads host render targets straight into
  shared memory for bitwise (fast) resolve copies whose source is fully owned by render
  targets with the same layout (xenia-edge's rules). Intro: 2 of 5 resolves per frame go
  direct, EDRAM dumps 5 → 3, GPU per frame 6.79 → 6.04 ms. Next: a "Sgt. Pepper" run (normal
  and `--full_rate_world`) with `--gpu_frame_categories`, checking for glitches in
  render-to-texture effects (crowd impostors, shadows, blur); `--direct_host_resolve=false`
  restores the old path.

**How to measure:** on AC power (check `/sys/class/power_supply/*/status`; on battery the iGPU
runs at 400 MHz), `RUN_LOG=build/runX.log scripts/run.sh --perf_log=true
--gpu_frame_csv=build/frames.csv [--gpu_frame_categories=true]`, then
`scripts/analyze_gpu_frames.py build/frames.csv --from 5`. Gameplay runs need the user.

### Done on 2026-11-02 (see KI-14 and log.md)
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
